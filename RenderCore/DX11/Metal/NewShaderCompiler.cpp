// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../ShaderService.h"
#include "../../ShaderLangUtil.h"
#include "../../StateDesc.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/Conversion.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Utility/StringFormat.h"

#include "../../../OSServices/WinAPI/WinAPIWrapper.h"
#include "../../../OSServices/WinAPI/IncludeWindows.h"
#include <winapifamily.h>
#include <unknwnbase.h>
#include <dxcapi.h>
#include <vector>

namespace RenderCore { namespace Metal_DX11
{
	enum class ShaderFeatureLevel
	{
		Level_11_0	= 0xb000,
		Level_11_1	= 0xb100,
		Level_12_0	= 0xc000,
		Level_12_1	= 0xc100
	};

	class DXCompilerLibrary
	{
	public:
		template<typename InterfaceType>
			intrusive_ptr<InterfaceType> CreateDXCompilerInterface(GUID clsId)
		{
			InterfaceType *result = nullptr;
			auto hresult = _dxcCreateInstance(clsId, IID_PPV_ARGS(&result));
			if (hresult != S_OK)
				Throw(std::runtime_error("Failure while attempting to create dxcompiler type"));
			return moveptr(result);
		}

		DXCompilerLibrary()
		{
			_dxilModule = (*OSServices::Windows::Fn_LoadLibrary)("dxil.dll");
			_dxcModule = (*OSServices::Windows::Fn_LoadLibrary)("dxcompiler.dll");
			if (!_dxilModule || _dxilModule == INVALID_HANDLE_VALUE || !_dxcModule || _dxcModule == INVALID_HANDLE_VALUE)
				Throw(std::runtime_error("dxcompiler.dll and/or dxil.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path"));
			_dxcCreateInstance = (DxcCreateInstanceProc)(*OSServices::Windows::Fn_GetProcAddress)(_dxcModule, "DxcCreateInstance");
			if (!_dxcCreateInstance)
				Throw(std::runtime_error("DxcCreateInstance was not found in dxcompiler.dll. This suggests either a corrupted or incompatible version"));
		}

		~DXCompilerLibrary()
		{
			(*OSServices::Windows::Fn_FreeLibrary)(_dxcModule);
			(*OSServices::Windows::Fn_FreeLibrary)(_dxilModule);
		}

		HMODULE _dxilModule, _dxcModule;
		DxcCreateInstanceProc _dxcCreateInstance;
	};

	static DXCompilerLibrary& GetDXCompilerLibrary()
	{
		static DXCompilerLibrary result;
		return result;
	}

	class NewCompilerIncludeHandler : public IDxcIncludeHandler
	{
	public:
		HRESULT LoadSource(LPCWSTR pFileName, IDxcBlob **ppIncludeSource) override
		{
			size_t size = 0;
			utf8 path[MaxPath];
			std::string buffer;
			buffer.reserve(MaxPath);

			auto inputFilenameAsUtf8 = Conversion::Convert<std::string>(MakeStringSection(pFileName));

			// We need to do some processing on the filenames here in order for dxcompiler to write reasonable filenames in the debugging info
			// (ie, so that frame capture tools can do something with them)
			// We always want to do our filename lookups using the XLE filesystem. But we've fed in the os filesystem (natural) name for
			// the initial shader file into dxcompiler. It will then use that name as prefix on all requests to this function
			// So -- the filename manipulation here replaces the os filename prefix with the xle filesystem prefix to try to make sure
			// we're always using xle filenames with MainFileSystem::TryOpen(). Tools will then be able to find the file in the os
			// filesystem, so long as the filename can be made to be relative to the original file.
			auto postPrefixFilename = inputFilenameAsUtf8.begin();
			if (!_expectedSearchPrefix.empty() && XlBeginsWith(MakeStringSection(inputFilenameAsUtf8), MakeStringSection(_expectedSearchPrefix))) {
				if (_replacementSearchPrefix.size() <= _expectedSearchPrefix.size()) {
					inputFilenameAsUtf8.erase(inputFilenameAsUtf8.begin()+_replacementSearchPrefix.size(), inputFilenameAsUtf8.begin()+_expectedSearchPrefix.size());
					std::copy(_replacementSearchPrefix.begin(), _replacementSearchPrefix.end(), inputFilenameAsUtf8.begin());
				} else {
					std::copy(_replacementSearchPrefix.begin(), _replacementSearchPrefix.begin()+_expectedSearchPrefix.size(), inputFilenameAsUtf8.begin());
					inputFilenameAsUtf8.insert(inputFilenameAsUtf8.begin()+_expectedSearchPrefix.size(), _replacementSearchPrefix.begin()+_expectedSearchPrefix.size(), _replacementSearchPrefix.end());
				}
				postPrefixFilename = inputFilenameAsUtf8.begin()+_replacementSearchPrefix.size();
			}

			for (auto i2=_searchDirectories.cbegin(); i2!=_searchDirectories.cend(); ++i2) {
				buffer.clear();
				buffer.insert(buffer.end(), MakeStringSection(*i2).begin(), MakeStringSection(*i2).end());
				if (!i2->empty()) buffer += "/";
				buffer += inputFilenameAsUtf8;
				MakeSplitPath(buffer).Simplify().Rebuild(path);

				std::unique_ptr<uint8[]> file;
				::Assets::DependentFileState timeMarker;
				{
					std::unique_ptr<::Assets::IFileInterface> fileInterface;
					auto ioResult = ::Assets::MainFileSystem::TryOpen(fileInterface, path, "rb", OSServices::FileShareMode::Read | OSServices::FileShareMode::Write);
					if (ioResult == ::Assets::IFileSystem::IOReason::Success && fileInterface) {
						size = fileInterface->GetSize();
						timeMarker._snapshot = fileInterface->GetSnapshot();

						file = std::make_unique<uint8[]>(size);
						if (size) {
							auto blocksRead = fileInterface->Read(file.get(), size);
							assert(blocksRead == 1); (void)blocksRead;
						}
					}
				}

				if (file) {
						// only add this to the list of include file, if it doesn't
						// already exist there. We will get repeats and when headers
						// are included multiple times (#pragma once isn't supported by
						// the HLSL compiler)
					auto existing = std::find_if(_includeFiles.cbegin(), _includeFiles.cend(),
						[path](const ::Assets::DependentFileState& depState)
						{
							return !XlCompareStringI(MakeStringSection(depState._filename), MakeStringSection(path));
						});

					if (existing == _includeFiles.cend()) {
						timeMarker._filename = path;
						_includeFiles.push_back(timeMarker);
						
						auto newDirectory = MakeFileNameSplitter(path).StemAndPath().AsString();
						auto i = std::find(_searchDirectories.cbegin(), _searchDirectories.cend(), newDirectory);
						if (i==_searchDirectories.cend()) {
							_searchDirectories.push_back(newDirectory);
						}
					}

					IDxcBlobEncoding *pSource;
					assert(size > 0 && file[0] != 0xff);
					auto hresult = _library->CreateBlobFromPinned((LPBYTE)file.get(), (UINT32)size, CP_UTF8, &pSource);
					if (hresult != S_OK) {
						return hresult;	
					}			
					
					// we must retain the file memory, CreateBlobFromPinned assumes we're going to manage the lifetime 
					_readFiles.push_back(std::move(file));
					*ppIncludeSource = pSource;
					return S_OK;
				}
			}

			// dxcompiler will prepend the base directory name on every lookup, as if all lookups are relative. 
			// We ideally want absolute includes to work (as in xleres/...). We can try to handle this by just
			// removing the expected search prefix, if it exists
			if (postPrefixFilename != inputFilenameAsUtf8.begin())
				return LoadSource(Conversion::Convert<std::wstring>(MakeStringSection(postPrefixFilename, inputFilenameAsUtf8.end())).c_str(), ppIncludeSource);

			*ppIncludeSource = nullptr;
			return ERROR_FILE_NOT_FOUND;
		}

		HRESULT QueryInterface(const IID &, void **) override { return S_OK; }
		ULONG AddRef() override { return 0; }
		ULONG Release() override { return 0; }

		IDxcUtils* _library = nullptr;
		std::vector<::Assets::DependentFileState> _includeFiles;
		std::vector<std::string> _searchDirectories;
		std::vector<std::unique_ptr<uint8[]>> _readFiles;
		std::string _expectedSearchPrefix;
		std::string _replacementSearchPrefix;
	};

	template<typename OutputType, typename InputType>
		static intrusive_ptr<OutputType> QueryInterfaceCast(InputType* input)
	{
		OutputType* result = nullptr;
		auto hresult = input->QueryInterface(IID_PPV_ARGS(&result));
		if (hresult == S_OK && result)
			return moveptr(result);
		return {};
	}

	class DXShaderCompiler : public ILowLevelCompiler
	{
	public:
		virtual void AdaptResId(ResId& resId) const override
		{
			assert(resId._shaderModel[0] != '\0');
			if (resId._shaderModel[0] != '\0') {
				size_t length = XlStringSize(resId._shaderModel);

					//
					//      Some shaders end with vs_*, gs_*, etc..
					//      Change this to the highest shader model we can support
					//      with the current device
					//
				if (resId._shaderModel[length-1] == '*') {
					resId._shaderModel[length-1] = '\0';
					XlCatString(resId._shaderModel, dimof(resId._shaderModel)-length, _defaultShaderModel.c_str());
				}
			}
		}

		static Payload AsPayload(IDxcBlob* input)
		{
			auto byteCount = input->GetBufferSize();
			if (!byteCount) return {};

			bool isText = false;
			IDxcBlobEncoding* blobEncoding = nullptr;
			input->QueryInterface(IID_PPV_ARGS(&blobEncoding));
			if (blobEncoding) {
				BOOL knownEncoding = false;
				UINT32 codepage = 0;
				auto hres = blobEncoding->GetEncoding(&knownEncoding, &codepage);
				isText = hres == S_OK && knownEncoding && (codepage == CP_UTF8 || codepage == CP_ACP || codepage == CP_OEMCP || codepage == CP_MACCP || codepage == CP_THREAD_ACP);
			}

			Payload result = std::make_shared<std::vector<uint8_t>>();
			result->resize(byteCount);
			std::memcpy(result->data(), input->GetBufferPointer(), byteCount);

			if (isText) {
				// strip off zeroes from the end
				while (!result->empty() && !*(result->end()-1))
					result->erase(result->end()-1);
			}
			return result;
		}

		static Payload AsCodePayload(IDxcBlob* input, const CompiledShaderByteCode::ShaderHeader& hdr)
		{
			auto byteCount = input->GetBufferSize();
			if (!byteCount) return {};

			Payload result = std::make_shared<std::vector<uint8_t>>();
			result->resize(sizeof(hdr) + byteCount);
			*(CompiledShaderByteCode::ShaderHeader*)result->data() = hdr;
			std::memcpy(PtrAdd(result->data(), sizeof(hdr)), input->GetBufferPointer(), byteCount);
			return result;
		}

		virtual bool DoLowLevelCompile(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPathInit,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const override
		{
			ScopedLock(_lock);

			NewCompilerIncludeHandler includeHandler;
			includeHandler._searchDirectories.push_back({});
			includeHandler._library = _utils.get();

			/*IDxcOperationResult *pResultPre = nullptr;
			hresult = compiler->Preprocess(
				pSource, 
				(LPCWSTR)shaderFN.c_str(), 
				NULL, 0,		// arguments to the compiler 
				_fixedDefines.data(), _fixedDefines.size(), 
				&includeHandler, 
				&pResultPre);
			if (hresult == S_OK)
				return true;*/

			ResId shaderPath = shaderPathInit;
			AdaptResId(shaderPath);

			StringMeld<dimof(CompiledShaderByteCode::ShaderHeader::_identifier)> identifier;
			identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";

			// -O3 should the default (some more information on optimization: https://github.com/Microsoft/DirectXShaderCompiler/blob/master/docs/SPIR-V.rst#optimization)
			// -fspv-reflect adds some extra reflection info?
			// -fvk-invert-y, 	-fvk-use-dx-layout, -fvk-use-dx-position-w provide some compatibility with DX
			// -WX warnings as errors DXC_ARG_WARNINGS_ARE_ERRORS
			// -Zi debug information
			// -P preprocess only
			// -enable-16bit-types			see https://github.com/Microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types
			// 								may require shader model 6_2 & SPV_AMD_gpu_shader_half_float to actually get half floats emitted
			// (DXC_ARG_AVOID_FLOW_CONTROL, DXC_ARG_PREFER_FLOW_CONTROL)
			// more here: https://simoncoenen.com/blog/programming/graphics/DxcCompiling
			// also just call "dxc.exe --help"

			std::vector<LPCWSTR> fixedArguments;
			fixedArguments.push_back(L"-spirv");
			fixedArguments.push_back(L"-fspv-target-env=vulkan1.1");
			fixedArguments.push_back(L"-fvk-use-dx-layout");			// XLE associates the DirectX alignment rules with HLSL source;
			if (_capabilities & CompilerCapability::Float16)
				fixedArguments.push_back(L"-enable-16bit-types");

			if (shaderPath._compilationFlags & CompilationFlags::DebugSymbols) {
				fixedArguments.push_back(L"-Qembed_debug");
				fixedArguments.push_back(DXC_ARG_DEBUG);
				fixedArguments.push_back(L"-fspv-debug=line");
				fixedArguments.push_back(L"-fspv-debug=source");		// emits the preprocessed source code into the shader bundle
				// fixedArguments.push_back(L"-fspv-debug=rich");		// "rich" and "rich-with-source" are undocumented options here, but appear unfinished, since they can crash
			} else {
				fixedArguments.push_back(L"-Qstrip_debug");
			}

			if (shaderPath._compilationFlags & CompilationFlags::DisableOptimizations) {
				// we always need to eliminate dead code, otherwise we'll end up with a massive uniforms interface for every shader
				fixedArguments.push_back(L"-Oconfig=--eliminate-dead-branches,--eliminate-dead-code-aggressive,--eliminate-dead-functions");
			} else {
				fixedArguments.push_back(DXC_ARG_OPTIMIZATION_LEVEL3);
			}

			auto defines = MakeDefinesTable(definesTable, shaderPath._shaderModel, _fixedDefines);

			::Assets::FileDesc fileDesc;
			if (shaderPath._filename[0])
				fileDesc = ::Assets::MainFileSystem::TryGetDesc(shaderPath._filename);
			std::string filenameForCompiler;
			if (!fileDesc._naturalName.empty()) {
				filenameForCompiler = fileDesc._naturalName;
			} else {
				filenameForCompiler = shaderPath._filename;
			}

			auto naturalNameSplit = MakeFileNameSplitter(filenameForCompiler);
			includeHandler._expectedSearchPrefix = naturalNameSplit.StemAndPath().AsString();
			if (naturalNameSplit.Stem().IsEmpty()) {
				// the compiler appears to prepend "./" in all cases, except if there's a drive specified (even if string begins with a / or \)
				includeHandler._expectedSearchPrefix = "./" + includeHandler._expectedSearchPrefix;
			}
			includeHandler._replacementSearchPrefix = MakeFileNameSplitter(shaderPath._filename).StemAndPath().AsString();

			IDxcCompilerArgs* rawArgs = nullptr;
			auto res2 = _utils->BuildArguments(
				Conversion::Convert<std::wstring>(MakeStringSection(filenameForCompiler)).c_str(),
				Conversion::Convert<std::wstring>(MakeStringSection(shaderPath._entryPoint)).c_str(),
				Conversion::Convert<std::wstring>(MakeStringSection(shaderPath._shaderModel)).c_str(),
				fixedArguments.data(), (UINT32)fixedArguments.size(),
				defines._defines.data(), (UINT32)defines._defines.size(),
				&rawArgs);
			if (res2 != S_OK) {
				assert(!rawArgs);
				return false;
			}
			intrusive_ptr<IDxcCompilerArgs> args = moveptr(rawArgs);

			IDxcOperationResult* compileResultRaw = nullptr;

			DxcBuffer inputBuffer;
			inputBuffer.Ptr = sourceCode;
			inputBuffer.Size = sourceCodeLength;
			inputBuffer.Encoding = CP_UTF8;
			auto res = _compiler->Compile(
				&inputBuffer, 
				args->GetArguments(), args->GetCount(), 
				&includeHandler, 
				IID_PPV_ARGS(&compileResultRaw));

			if (res != S_OK) {
				assert(!compileResultRaw);
				return false;
			}
			intrusive_ptr<IDxcOperationResult> compileResult = moveptr(compileResultRaw);

			for (auto&i:includeHandler._includeFiles)
				if (std::find(dependencies.begin(), dependencies.end(), i) == dependencies.end())
					dependencies.push_back(i);

			auto compileResult2 = QueryInterfaceCast<IDxcResult>(compileResult.get());
			if (compileResult2) {
				if (compileResult2->HasOutput(DXC_OUT_OBJECT)) {
					IDxcBlob* payloadBlobRaw = nullptr;
					auto hresult = compileResult2->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&payloadBlobRaw), nullptr);
					intrusive_ptr<IDxcBlob> payloadBlob = moveptr(payloadBlobRaw);
					if (hresult == S_OK && payloadBlob)
						payload = AsCodePayload(payloadBlob, CompiledShaderByteCode::ShaderHeader { identifier, shaderPath._shaderModel, shaderPath._entryPoint, false });
				}

				if (compileResult2->HasOutput(DXC_OUT_ERRORS)) {
					IDxcBlob* payloadBlobRaw = nullptr;
					auto hresult = compileResult2->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&payloadBlobRaw), nullptr);
					intrusive_ptr<IDxcBlob> payloadBlob = moveptr(payloadBlobRaw);
					if (hresult == S_OK && payloadBlob)
						errors = AsPayload(payloadBlob);
				}

				return payload != nullptr;
			} else {
				// IDxcOperationResult interface (older interface, replaced by IDxcResult)
				HRESULT compileStatus = -1;
				compileResult->GetStatus(&compileStatus);
				if (compileStatus == S_OK) {
					IDxcBlob* payloadBlobRaw = nullptr;
					res = compileResult->GetResult(&payloadBlobRaw);
					intrusive_ptr<IDxcBlob> payloadResult = moveptr(payloadBlobRaw);
					if (res == S_OK && payloadResult)
						payload = AsCodePayload(payloadResult, CompiledShaderByteCode::ShaderHeader { identifier, shaderPath._shaderModel, shaderPath._entryPoint, false });

					IDxcBlobEncoding* errorBlobRaw = nullptr;
					res = compileResult->GetErrorBuffer(&errorBlobRaw);
					intrusive_ptr<IDxcBlobEncoding> errorBlob = moveptr(errorBlobRaw);
					if (res == S_OK && errorBlob)
						errors = AsPayload(errorBlob);

					return payload != nullptr;
				}

				return false;
			}
		}

		virtual std::string MakeShaderMetricsString(
			const void* byteCode, size_t byteCodeSize) const override
		{
			return "Shader metrics not yet implemented for dxcompiler";
		}

		virtual CompilerCapability::BitField GetCapabilities() const override { return _capabilities; }
		virtual ShaderLanguage GetShaderLanguage() const override { return ShaderLanguage::HLSL; }

		using FixedDefined = std::pair<std::basic_string<wchar_t>, std::basic_string<wchar_t>>;

		struct DefinesTable
		{
			std::vector<wchar_t> _buffer;
			std::vector<DxcDefine> _defines;

			void Add(StringSection<wchar_t> name, StringSection<wchar_t> value);
			void Add(StringSection<> name, StringSection<> value);
		};
		static DefinesTable MakeDefinesTable(
			StringSection<char> definesTable, const char shaderModel[],
			IteratorRange<const FixedDefined*> fixedDefines);

		DXShaderCompiler(std::vector<FixedDefined>&& fixedDefines, ShaderFeatureLevel featureLevel, std::string defaultShaderModel, CompilerCapability::BitField capabilities)
		: _fixedDefines(std::move(fixedDefines))
		, _featureLevel(featureLevel)
		, _defaultShaderModel(std::move(defaultShaderModel))
		{
			auto& library = GetDXCompilerLibrary();
			_utils = library.CreateDXCompilerInterface<IDxcUtils>(CLSID_DxcUtils);
			_compiler = library.CreateDXCompilerInterface<IDxcCompiler3>(CLSID_DxcCompiler);
			_capabilities = capabilities & CompilerCapability::Float16;
		}

		~DXShaderCompiler()
		{}

	protected:
		std::vector<FixedDefined> _fixedDefines;
		ShaderFeatureLevel _featureLevel;

		intrusive_ptr<IDxcUtils> _utils;
		intrusive_ptr<IDxcCompiler3> _compiler;

		std::string _defaultShaderModel;
		CompilerCapability::BitField _capabilities = 0;

		mutable Threading::Mutex _lock;
	};

	void DXShaderCompiler::DefinesTable::Add(StringSection<wchar_t> name, StringSection<wchar_t> value)
	{
		DxcDefine define;
		define.Name = LPCWSTR(1+_buffer.size());
		_buffer.insert(_buffer.end(), name.begin(), name.end());
		_buffer.push_back(0);
		if (!value.IsEmpty()) {
			define.Value = LPCWSTR(1+_buffer.size());
			_buffer.insert(_buffer.end(), value.begin(), value.end());
			_buffer.push_back(0);
		} else
			define.Value = 0;
		_defines.push_back(define);
	}

	void DXShaderCompiler::DefinesTable::Add(StringSection<> name, StringSection<> value)
	{
		DxcDefine define;
		define.Name = LPCWSTR(1+_buffer.size());
		auto converted = Conversion::Convert<std::wstring>(name);
		_buffer.insert(_buffer.end(), converted.begin(), converted.end());
		_buffer.push_back(0);
		if (!value.IsEmpty()) {
			define.Value = LPCWSTR(1+_buffer.size());
			converted = Conversion::Convert<std::wstring>(value);
			_buffer.insert(_buffer.end(), converted.begin(), converted.end());
			_buffer.push_back(0);
		} else
			define.Value = 0;
		_defines.push_back(define);
	}

	auto DXShaderCompiler::MakeDefinesTable(
		StringSection<char> definesTable, const char shaderModel[],
		IteratorRange<const FixedDefined*> fixedDefines) -> DefinesTable
	{
		DefinesTable result;
		for (const auto& fixed:fixedDefines)
			result.Add(fixed.first, fixed.second);

		auto iterator = definesTable.begin();
		while (iterator != definesTable.end()) {
			auto defineEnd = std::find(iterator, definesTable.end(), ';');
			auto equals = std::find(iterator, defineEnd, '=');

			if (equals != defineEnd) {
				auto namePart = MakeStringSection(iterator, equals);
				auto valuePart = MakeStringSection(equals+1, defineEnd);
				result.Add(namePart, valuePart);
			} else {
				auto namePart = MakeStringSection(iterator, defineEnd);
				result.Add(namePart, {});
			}

			iterator = defineEnd;
			if (iterator != definesTable.end())
				++iterator;
		}

		// offset by the buffer starting point
		for (auto&d:result._defines) {
			assert(d.Name);
			d.Name = size_t(d.Name)-1+result._buffer.data();
			if (d.Value)
				d.Value = size_t(d.Value)-1+result._buffer.data();
		}

		return result;
	}

	std::shared_ptr<ILowLevelCompiler> CreateHLSLToSPIRVCompiler(ILowLevelCompiler::CompilerCapability::BitField capabilities)
	{
		std::vector<DXShaderCompiler::FixedDefined> fixedDefines {
			std::make_pair(L"VULKAN", L"1")
		};

		std::string defaultShaderModel = "6_2";
		return std::make_shared<DXShaderCompiler>(std::move(fixedDefines), ShaderFeatureLevel::Level_11_0, std::move(defaultShaderModel), capabilities);
	}
}}

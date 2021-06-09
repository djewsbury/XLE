// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../ShaderService.h"
#include "../../ShaderLangUtil.h"
#include "../../StateDesc.h"
#include "../../../Assets/IntermediatesStore.h"		// (for GetDependentFileState)
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
			(*OSServices::Windows::FreeLibrary)(_dxcModule);
			(*OSServices::Windows::FreeLibrary)(_dxilModule);
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
			auto inputFilenameAsUtf8 = Conversion::Convert<std::string>(std::basic_string<wchar_t>(pFileName));
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
						auto desc = fileInterface->GetDesc();
						size = (size_t)desc._size;
						timeMarker._timeMarker = desc._modificationTime;

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
						
						auto newDirectory = MakeFileNameSplitter(path).DriveAndPath().AsString();
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

			// dxcompiler will prepend the base directory name on every lookup 
			// as if all lookups are relative. We don't really know where the base
			// starts (since it's just the directory of the file that included this one, and we have no idea what that is...)
			// so we have to try removing each path in turn
			auto splitPath = MakeSplitPath((const utf16*)pFileName).Simplify();
			if (splitPath.GetSectionCount() > 1) {
				// we inherit the null terminator from the original pFileName string here
				auto test = splitPath.GetSection(1).begin();
				return LoadSource((LPCWSTR)test, ppIncludeSource);
			}

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
		virtual void AdaptShaderModel(
			ResChar destination[], 
			const size_t destinationCount,
			StringSection<ResChar> inputShaderModel) const override
		{
			assert(inputShaderModel.size() >= 1);
			if (inputShaderModel[0] != '\0') {
				size_t length = inputShaderModel.size();

					//
					//      Some shaders end with vs_*, gs_*, etc..
					//      Change this to the highest shader model we can support
					//      with the current device
					//
				if (inputShaderModel[length-1] == '*') {
					const char* bestShaderModel = "5_0";
				
					if (destination != inputShaderModel.begin()) 
						XlCopyString(destination, destinationCount, inputShaderModel);

					destination[std::min(length-1, destinationCount-1)] = '\0';
					XlCatString(destination, destinationCount, bestShaderModel);
					return;
				}
			}

			if (destination != inputShaderModel.begin())
				XlCopyString(destination, destinationCount, inputShaderModel);
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
				isText = hres == S_OK && knownEncoding == true && (codepage == CP_UTF8 || codepage == CP_ACP || codepage == CP_OEMCP || codepage == CP_MACCP || codepage == CP_THREAD_ACP);
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

		static Payload AsCodePayload(IDxcBlob* input, const ShaderService::ShaderHeader& hdr)
		{
			auto byteCount = input->GetBufferSize();
			if (!byteCount) return {};

			Payload result = std::make_shared<std::vector<uint8_t>>();
			result->resize(sizeof(hdr) + byteCount);
			*(ShaderService::ShaderHeader*)result->data() = hdr;
			std::memcpy(PtrAdd(result->data(), sizeof(hdr)), input->GetBufferPointer(), byteCount);
			return result;
		}

		virtual bool DoLowLevelCompile(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPath,
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

			// -O3 should the default (some more information on optimization: https://github.com/Microsoft/DirectXShaderCompiler/blob/master/docs/SPIR-V.rst#optimization)
			// -fspv-reflect adds some extra reflection info?
			// -fvk-invert-y, 	-fvk-use-dx-layout, -fvk-use-dx-position-w provide some compatibility with DX
			// -WX warnings as errors
			// -Zi debug information
			// -P preprocess only
			// more here: https://simoncoenen.com/blog/programming/graphics/DxcCompiling
			LPCWSTR fixedArguments[] {
				L"-spirv",
				L"-fspv-target-env=vulkan1.0",
				L"-Qstrip_debug",
				L"-Qstrip_reflect",
			};

			ResChar shaderModel[64];
			AdaptShaderModel(shaderModel, dimof(shaderModel), shaderPath._shaderModel);

			StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
			identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";

			auto arguments = MakeDefinesTable(definesTable, shaderPath._shaderModel, _fixedDefines);

			arguments.push_back(L"-E " + Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderPath._entryPoint)));
			arguments.push_back(L"-T " + Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderModel)));
			if (shaderPath._filename[0])
				arguments.push_back(Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderPath._filename)));

			auto argumentCount = dimof(fixedArguments) + arguments.size();
			LPCWSTR finalArguments[argumentCount];
			LPCWSTR* i = finalArguments;
			for (auto a:fixedArguments) *i++ = a;
			for (const auto&a:arguments) *i++ = a.c_str();

			IDxcOperationResult* compileResultRaw = nullptr;
			/*auto res = _compiler->Compile(
				sourceBlob.get(),
				shaderFN.c_str(), 
				Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderPath._entryPoint)).c_str(), 
				Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderModel)).c_str(), 
				finalArguments, argumentCount, 
				_fixedDefines.data(), _fixedDefines.size(),
				&includeHandler, &compileResultRaw);*/

			DxcBuffer inputBuffer;
			inputBuffer.Ptr = sourceCode;
			inputBuffer.Size = sourceCodeLength;
			inputBuffer.Encoding = CP_UTF8;
			auto res = _compiler->Compile(
				&inputBuffer, 
				finalArguments, argumentCount, 
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
						payload = AsCodePayload(payloadBlob, ShaderService::ShaderHeader { identifier, shaderModel, shaderPath._entryPoint, false });
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
						payload = AsCodePayload(payloadResult, ShaderService::ShaderHeader { identifier, shaderModel, shaderPath._entryPoint, false });

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

		virtual ShaderLanguage GetShaderLanguage() const override { return ShaderLanguage::HLSL; }

		using FixedDefined = std::pair<std::basic_string<wchar_t>, std::basic_string<wchar_t>>;

		static std::vector<std::basic_string<wchar_t>> MakeDefinesTable(
			StringSection<char> definesTable, const char shaderModel[],
			IteratorRange<const FixedDefined*> fixedDefines);

		DXShaderCompiler(std::vector<FixedDefined>&& fixedDefines, ShaderFeatureLevel featureLevel)
		: _fixedDefines(std::move(fixedDefines))
		, _featureLevel(featureLevel)
		{
			auto& library = GetDXCompilerLibrary();
			_utils = library.CreateDXCompilerInterface<IDxcUtils>(CLSID_DxcUtils);
			_compiler = library.CreateDXCompilerInterface<IDxcCompiler3>(CLSID_DxcCompiler);
		}

		~DXShaderCompiler()
		{}

	protected:
		std::vector<FixedDefined> _fixedDefines;
		ShaderFeatureLevel _featureLevel;

		intrusive_ptr<IDxcUtils> _utils;
		intrusive_ptr<IDxcCompiler3> _compiler;

		mutable Threading::Mutex _lock;
	};

	static const wchar_t s_shaderModelDef_V[] = L"-DVSH=1";
    static const wchar_t s_shaderModelDef_P[] = L"-DPSH=1";
    static const wchar_t s_shaderModelDef_G[] = L"-DGSH=1";
    static const wchar_t s_shaderModelDef_H[] = L"-DHSH=1";
    static const wchar_t s_shaderModelDef_D[] = L"-DDSH=1";
    static const wchar_t s_shaderModelDef_C[] = L"-DCSH=1";

	std::vector<std::basic_string<wchar_t>> DXShaderCompiler::MakeDefinesTable(
		StringSection<char> definesTable, const char shaderModel[],
		IteratorRange<const FixedDefined*> fixedDefines)
	{
		unsigned definesCount = 1;
		auto iterator = definesTable.end();
		while ((iterator = std::find(iterator, definesTable.end(), ';')) != definesTable.end()) {
			++definesCount; ++iterator;
		}
			
		std::vector<std::basic_string<wchar_t>> arguments;
		arguments.reserve(2+fixedDefines.size()+definesCount);

		for (const auto& fixed:fixedDefines)
			arguments.push_back(L"-D" + fixed.first + L"=" + fixed.second);

		const wchar_t* shaderModelStr = nullptr;
		switch (tolower(shaderModel[0])) {
		case 'v': shaderModelStr = s_shaderModelDef_V; break;
		case 'p': shaderModelStr = s_shaderModelDef_P; break;
		case 'g': shaderModelStr = s_shaderModelDef_G; break;
		case 'h': shaderModelStr = s_shaderModelDef_H; break;
		case 'd': shaderModelStr = s_shaderModelDef_D; break;
		case 'c': shaderModelStr = s_shaderModelDef_C; break;
		}
		if (shaderModelStr)
			arguments.push_back(shaderModelStr);

		iterator = definesTable.begin();
		while (iterator != definesTable.end()) {
			auto defineEnd = std::find(iterator, definesTable.end(), ';');

			StringMeld<256> meld;
			meld << "-D" << MakeStringSection(iterator, defineEnd);
			arguments.push_back(Conversion::Convert<std::basic_string<wchar_t>>(meld.AsStringSection()));

			iterator = defineEnd;
			if (iterator != definesTable.end())
				++iterator;
		}

		return arguments;
	}

	std::shared_ptr<ILowLevelCompiler> CreateHLSLToSPIRVCompiler()
	{
		std::vector<DXShaderCompiler::FixedDefined> fixedDefines {
			std::make_pair(L"VULKAN", L"1")
			#if defined(_DEBUG)
				, std::make_pair(L"_DEBUG", L"1")
			#endif
		};

		return std::make_shared<DXShaderCompiler>(std::move(fixedDefines), ShaderFeatureLevel::Level_11_0);
	}
}}

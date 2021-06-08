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
			utf16 path[MaxPath];
			std::basic_string<utf16> buffer;
			buffer.reserve(MaxPath);
			for (auto i2=_searchDirectories.cbegin(); i2!=_searchDirectories.cend(); ++i2) {
				buffer.clear();
				buffer.insert(buffer.end(), MakeStringSection(*i2).begin(), MakeStringSection(*i2).end());
				if (!i2->empty()) buffer += u"/";
				buffer += (const utf16*)pFileName;
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
						[&path](const ::Assets::DependentFileState& depState)
						{
							return !XlCompareStringI(depState._filename.c_str(), (const char*)path);
						});

					if (existing == _includeFiles.cend()) {
						timeMarker._filename = (const char*)path;
						_includeFiles.push_back(timeMarker);
						
						auto newDirectory = MakeFileNameSplitter(path).DriveAndPath().AsString();
						auto i = std::find(_searchDirectories.cbegin(), _searchDirectories.cend(), newDirectory);
						if (i==_searchDirectories.cend()) {
							_searchDirectories.push_back(newDirectory);
						}
					}

					IDxcBlobEncoding *pSource;
					auto hresult = _library->CreateBlobFromPinned((LPBYTE)file.get(), (UINT32)size, CP_UTF8, &pSource);
					if (hresult != S_OK)
						return hresult;				
					
					*ppIncludeSource = pSource;
					return S_OK;
				}
			}
			return -1;

			
			return S_OK;
		}

		HRESULT QueryInterface(const IID &, void **) override { return S_OK; }
		ULONG AddRef() override { return 0; }
		ULONG Release() override { return 0; }

		IDxcUtils* _library = nullptr;
        std::vector<::Assets::DependentFileState> _includeFiles;
        std::vector<std::basic_string<utf16>> _searchDirectories;
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

			Payload result = std::make_shared<std::vector<uint8_t>>();
			result->resize(byteCount);
			std::memcpy(result->data(), input->GetBufferPointer(), byteCount);
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
			NewCompilerIncludeHandler includeHandler;
			auto shaderDriveAndPath = MakeFileNameSplitter(shaderPath._filename).DriveAndPath();
			if (!shaderDriveAndPath.IsEmpty())
				includeHandler._searchDirectories.push_back(Conversion::Convert<std::basic_string<utf16>>(shaderDriveAndPath));
			includeHandler._library = _utils.get();

			IDxcBlobEncoding *sourceBlobRaw = nullptr;
			auto hresult = _utils->CreateBlobFromPinned((LPBYTE)sourceCode, (UINT32)sourceCodeLength, CP_UTF8, &sourceBlobRaw);
			if (hresult != S_OK)
				return false;
			intrusive_ptr<IDxcBlobEncoding> sourceBlob = moveptr(sourceBlobRaw);

			auto shaderFN = Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderPath._filename));

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

			LPCWSTR arguments[] {
				L"-spirv",
				L"-fspv-target-env=vulkan1.0"
			};

			ResChar shaderModel[64];
			AdaptShaderModel(shaderModel, dimof(shaderModel), shaderPath._shaderModel);

			StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
			identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";

			IDxcOperationResult* compileResultRaw = nullptr;
			auto res = _compiler->Compile(
				sourceBlob.get(),
				shaderFN.c_str(), 
				Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderPath._entryPoint)).c_str(), 
				Conversion::Convert<std::basic_string<wchar_t>>(MakeStringSection(shaderModel)).c_str(), 
				arguments, dimof(arguments), 
				_fixedDefines.data(), _fixedDefines.size(),
				&includeHandler, &compileResultRaw);
			if (res != S_OK) {
				assert(!compileResultRaw);
				return false;
			}
			intrusive_ptr<IDxcOperationResult> compileResult = moveptr(compileResultRaw);

			auto compileResult2 = QueryInterfaceCast<IDxcResult>(compileResult.get());
			if (compileResult2) {
				if (compileResult2->HasOutput(DXC_OUT_OBJECT)) {
					IDxcBlob* payloadBlobRaw = nullptr;
					auto hresult = compileResult2->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&payloadBlobRaw), nullptr);
					intrusive_ptr<IDxcBlob> payloadBlob = moveptr(payloadBlobRaw);
					if (hresult == S_OK && payloadBlob)
						payload = AsCodePayload(payloadBlob, ShaderService::ShaderHeader { identifier, shaderModel, false });
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
						payload = AsCodePayload(payloadResult, ShaderService::ShaderHeader { identifier, shaderModel, false });

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
			assert(0);
			return {};
		}

		virtual ShaderLanguage GetShaderLanguage() const override { return ShaderLanguage::HLSL; }

		DXShaderCompiler(IteratorRange<const DxcDefine*> fixedDefines, ShaderFeatureLevel featureLevel)
		: _fixedDefines(fixedDefines.begin(), fixedDefines.end())
		, _featureLevel(featureLevel)
		{
			auto& library = GetDXCompilerLibrary();
			_utils = library.CreateDXCompilerInterface<IDxcUtils>(CLSID_DxcUtils);
			_compiler = library.CreateDXCompilerInterface<IDxcCompiler2>(CLSID_DxcCompiler);
		}

		~DXShaderCompiler()
		{}

	protected:
		std::vector<DxcDefine> _fixedDefines;
		ShaderFeatureLevel _featureLevel;

		intrusive_ptr<IDxcUtils> _utils;
		intrusive_ptr<IDxcCompiler2> _compiler;
	};


	std::shared_ptr<ILowLevelCompiler> CreateHLSLToSPIRVCompiler()
	{
		return std::make_shared<DXShaderCompiler>(IteratorRange<const DxcDefine*>{}, ShaderFeatureLevel::Level_11_0);
	}
}}

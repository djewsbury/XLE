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

	class IncludeHandler : public IDxcIncludeHandler
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
		std::basic_string<utf16> _baseDirectory;
        std::vector<::Assets::DependentFileState> _includeFiles;
        std::vector<std::basic_string<utf16>> _searchDirectories;
	};

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

		virtual bool DoLowLevelCompile(
			/*out*/ Payload& payload,
			/*out*/ Payload& errors,
			/*out*/ std::vector<::Assets::DependentFileState>& dependencies,
			const void* sourceCode, size_t sourceCodeLength,
			const ResId& shaderPath,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const override
		{
			auto& library = GetDXCompilerLibrary();

			auto utils = library.CreateDXCompilerInterface<IDxcUtils>(CLSID_DxcUtils);

			IDxcBlobEncoding *pSource;
			auto hresult = utils->CreateBlobFromPinned((LPBYTE)sourceCode, (UINT32)sourceCodeLength, CP_UTF8, &pSource);
			if (hresult != S_OK)
				return false;

			auto compiler = library.CreateDXCompilerInterface<IDxcCompiler2>(CLSID_DxcCompiler);

			IncludeHandler includeHandler;

			IDxcOperationResult *pResultPre = nullptr;
			auto shaderFN = Conversion::Convert<std::u16string>(MakeStringSection(shaderPath._filename));
			hresult = compiler->Preprocess(
				pSource, 
				(LPCWSTR)shaderFN.c_str(), 
				NULL, 0,		// arguments to the compiler 
				_fixedDefines.data(), _fixedDefines.size(), 
				&includeHandler, 
				&pResultPre);
			if (hresult == S_OK)
				return true;
			return false;
		}

		virtual std::string MakeShaderMetricsString(
			const void* byteCode, size_t byteCodeSize) const override
		{
			assert(0);
			return {};
		}

		virtual ShaderLanguage GetShaderLanguage() const override { return ShaderLanguage::HLSL; }

		DXShaderCompiler(IteratorRange<const DxcDefine*> fixedDefines, ShaderFeatureLevel featureLevel);
		~DXShaderCompiler();

	protected:
		mutable Threading::Mutex _moduleLock;
		mutable HMODULE _module;
		HMODULE GetShaderCompileModule() const;

		std::vector<DxcDefine> _fixedDefines;
		ShaderFeatureLevel _featureLevel;
	};


}}

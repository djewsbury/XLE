// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "DeviceContext.h"
#include "FunctionLinkingGraph.h"
#include "../../ShaderService.h"
#include "../../ShaderLangUtil.h"
#include "../../StateDesc.h"
#include "../../../Assets/ConfigFileContainer.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/FastParseValue.h"
#include "../../../OSServices/Log.h"
#include "../../../Foreign/plustache/template.hpp"

#include <set>
#include <sstream>

#include "../../../OSServices/WinAPI/WinAPIWrapper.h"
#include "IncludeDX11.h"
#include <D3D11Shader.h>
#include <D3Dcompiler.h>

#pragma GCC diagnostic ignored "-Wmicrosoft-exception-spec"         // warning: exception specification of overriding function is more lax than base version

namespace RenderCore { namespace Metal_DX11
{
    using ::Assets::ResChar;

    static const auto s_shaderReflectionInterfaceGuid = IID_ID3D11ShaderReflection; // __uuidof(ID3D::ShaderReflection); // 

    class D3DShaderCompiler : public ILowLevelCompiler
    {
    public:
        virtual void AdaptShaderModel(
            ResChar destination[], 
            const size_t destinationCount,
			StringSection<ResChar> inputShaderModel) const override;

        virtual bool DoLowLevelCompile(
            /*out*/ Payload& payload,
            /*out*/ Payload& errors,
            /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
            const void* sourceCode, size_t sourceCodeLength,
            const ResId& shaderPath,
			StringSection<::Assets::ResChar> definesTable,
			IteratorRange<const SourceLineMarker*> sourceLineMarkers) const override;

        virtual std::string MakeShaderMetricsString(
            const void* byteCode, size_t byteCodeSize) const override;

        virtual ShaderLanguage GetShaderLanguage() const override { return ShaderLanguage::HLSL; }

        HRESULT D3DReflect_Wrapper(
            const void* pSrcData, size_t SrcDataSize, 
            const IID& pInterface, void** ppReflector) const;

        HRESULT D3DCompile_Wrapper(
            LPCVOID pSrcData,
            SIZE_T SrcDataSize,
            LPCSTR pSourceName,
            const D3D_SHADER_MACRO* pDefines,
            ID3DInclude* pInclude,
            LPCSTR pEntrypoint,
            LPCSTR pTarget,
            UINT Flags1,
            UINT Flags2,
            ID3DBlob** ppCode,
            ID3DBlob** ppErrorMsgs) const;

        HRESULT D3DCreateFunctionLinkingGraph_Wrapper(
            UINT uFlags,
            ID3D11FunctionLinkingGraph **ppFunctionLinkingGraph) const;

        HRESULT D3DCreateLinker_Wrapper(ID3D11Linker **ppLinker) const;

        HRESULT D3DLoadModule_Wrapper(
            LPCVOID pSrcData, SIZE_T cbSrcDataSize, 
            ID3D11Module **ppModule) const;

        HRESULT D3DReflectLibrary_Wrapper(
            LPCVOID pSrcData,
            SIZE_T  SrcDataSize,
            REFIID  riid,
            LPVOID  *ppReflector) const;

        D3DShaderCompiler(IteratorRange<D3D10_SHADER_MACRO*> fixedDefines, D3D_FEATURE_LEVEL featureLevel);
        ~D3DShaderCompiler();

        static std::shared_ptr<D3DShaderCompiler> GetInstance() { return s_instance.lock(); }

    protected:
        mutable Threading::Mutex _moduleLock;
        mutable HMODULE _module;
        HMODULE GetShaderCompileModule() const;

        static std::weak_ptr<D3DShaderCompiler> s_instance;
        friend std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice&, D3D_FEATURE_LEVEL);

        std::vector<D3D10_SHADER_MACRO> _fixedDefines;
        D3D_FEATURE_LEVEL _featureLevel;
    };

        ////////////////////////////////////////////////////////////

    void D3DShaderCompiler::AdaptShaderModel(
        ResChar destination[], 
        const size_t destinationCount,
		StringSection<ResChar> inputShaderModel) const
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
                const char* bestShaderModel;
                if (_featureLevel >= D3D_FEATURE_LEVEL_11_0)         { bestShaderModel = "5_0"; } 
                else if (_featureLevel >= D3D_FEATURE_LEVEL_10_0)    { bestShaderModel = "4_0"; } 
                else if (_featureLevel >= D3D_FEATURE_LEVEL_9_3)     { bestShaderModel = "4_0_level_9_3"; } 
                else if (_featureLevel >= D3D_FEATURE_LEVEL_9_2)     { bestShaderModel = "4_0_level_9_2"; } 
                else                                                { bestShaderModel = "4_0_level_9_1"; }
            
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

    static D3D10_SHADER_MACRO MakeShaderMacro(const char name[], const char definition[])
    {
        D3D10_SHADER_MACRO result;
        result.Name = name;
        result.Definition = definition;
        return result;
    }

    static UINT GetShaderCompilationFlags()
    {
        #if defined(_DEBUG)
            return D3D10_SHADER_ENABLE_STRICTNESS | D3D10_SHADER_DEBUG | D3D10_SHADER_SKIP_OPTIMIZATION; //| D3D10_SHADER_WARNINGS_ARE_ERRORS;
        #else
            return 
                  D3D10_SHADER_ENABLE_STRICTNESS 
                | D3D10_SHADER_OPTIMIZATION_LEVEL3 // | D3D10_SHADER_NO_PRESHADER;
                // | D3D10_SHADER_IEEE_STRICTNESS
                // | D3D10_SHADER_WARNINGS_ARE_ERRORS
                ;
        #endif
    }

    class IncludeHandler : public ID3D10Include 
    {
    public:
        HRESULT __stdcall   Open(D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes);
        HRESULT __stdcall   Close(LPCVOID pData);
        
        const std::vector<::Assets::DependentFileState>& GetIncludeFiles() const { return _includeFiles; }
        const std::basic_string<utf8>& GetBaseDirectory() const { return _baseDirectory; }

        IncludeHandler(StringSection<> baseDirectory) : _baseDirectory(baseDirectory.AsString()) 
        {
            _searchDirectories.push_back(_baseDirectory);
			_searchDirectories.push_back("");
        }
        ~IncludeHandler() {}
    private:
        std::basic_string<utf8> _baseDirectory;
        std::vector<::Assets::DependentFileState> _includeFiles;
        std::vector<std::basic_string<utf8>> _searchDirectories;
    };

    HRESULT     IncludeHandler::Open(D3D10_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID *ppData, UINT *pBytes)
    {
        size_t size = 0;
        utf8 path[MaxPath], buffer[MaxPath];
        for (auto i2=_searchDirectories.cbegin(); i2!=_searchDirectories.cend(); ++i2) {
            XlCopyString(buffer, MakeStringSection(*i2));
            if (!i2->empty()) XlCatString(buffer, "/");
            XlCatString(buffer, (const utf8*)pFileName);
            SplitPath<utf8>(buffer).Simplify().Rebuild(path);

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
                    [&path](const ::Assets::DependentFileState& depState)
                    {
                        return !XlCompareStringI(depState._filename.c_str(), (const char*)path);
                    });

                if (existing == _includeFiles.cend()) {
                    timeMarker._filename = (const char*)path;
                    _includeFiles.push_back(timeMarker);
                    
                    auto newDirectory = FileNameSplitter<utf8>(path).DriveAndPath().AsString();
                    auto i = std::find(_searchDirectories.cbegin(), _searchDirectories.cend(), newDirectory);
                    if (i==_searchDirectories.cend()) {
                        _searchDirectories.push_back(newDirectory);
                    }
                }

                if (ppData) { *ppData = file.release(); }
                if (pBytes) { *pBytes = (UINT)size; }

                return S_OK;
            }
        }
        return -1;
    }

    HRESULT     IncludeHandler::Close(LPCVOID pData)
    {
        delete[] (uint8*)pData;
        return S_OK;
    }

    static const char s_shaderModelDef_V[] = "VSH";
    static const char s_shaderModelDef_P[] = "PSH";
    static const char s_shaderModelDef_G[] = "GSH";
    static const char s_shaderModelDef_H[] = "HSH";
    static const char s_shaderModelDef_D[] = "DSH";
    static const char s_shaderModelDef_C[] = "CSH";

    static std::vector<D3D10_SHADER_MACRO> MakeDefinesTable(
        StringSection<char> definesTable, const char shaderModel[], std::string& definesCopy,
        IteratorRange<const D3D10_SHADER_MACRO*> fixedDefines)
    {
        definesCopy = definesTable.AsString();
        unsigned definesCount = 1;
        size_t offset = 0;
        while ((offset = definesCopy.find_first_of(';', offset)) != std::string::npos) {
            ++definesCount; ++offset;
        }
            
        std::vector<D3D10_SHADER_MACRO> arrayOfDefines;
        arrayOfDefines.reserve(2+fixedDefines.size()+definesCount);
        arrayOfDefines.insert(arrayOfDefines.begin(), fixedDefines.begin(), fixedDefines.end());

        const char* shaderModelStr = nullptr;
        switch (tolower(shaderModel[0])) {
        case 'v': shaderModelStr = s_shaderModelDef_V; break;
        case 'p': shaderModelStr = s_shaderModelDef_P; break;
        case 'g': shaderModelStr = s_shaderModelDef_G; break;
        case 'h': shaderModelStr = s_shaderModelDef_H; break;
        case 'd': shaderModelStr = s_shaderModelDef_D; break;
        case 'c': shaderModelStr = s_shaderModelDef_C; break;
        }
        if (shaderModelStr)
            arrayOfDefines.push_back(MakeShaderMacro(shaderModelStr, "1"));

        offset = 0;
        if (!definesCopy.empty()) {
            for (;;) {
                size_t definition = definesCopy.find_first_of('=', offset);
                size_t defineEnd = definesCopy.find_first_of(';', offset);
                if (defineEnd == std::string::npos) {
                    defineEnd = definesCopy.size();
                }

                if (definition < defineEnd) {
                    definesCopy[definition] = '\0';
                    if (defineEnd < definesCopy.size()) {
                        definesCopy[defineEnd] = '\0';
                    }
                    arrayOfDefines.push_back(MakeShaderMacro(&definesCopy[offset], &definesCopy[definition+1]));
                } else {
                    arrayOfDefines.push_back(MakeShaderMacro(&definesCopy[offset], nullptr));
                }

                if (defineEnd+1 >= definesCopy.size()) {
                    break;
                }
                offset = defineEnd+1;
            }
        }
        arrayOfDefines.push_back(MakeShaderMacro(nullptr, nullptr));
        return arrayOfDefines;
    }

        ////////////////////////////////////////////////////////////

    /*
	static const char* AsString(HRESULT reason)
    {
        switch (reason) {
        case D3D11_ERROR_FILE_NOT_FOUND: return "File not found";
        case E_NOINTERFACE: return "Could not find d3dcompiler_47.dll";
        default: return "General failure";
        }
    }
	*/

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void CreatePayloadFromBlobs(
        /*out*/ ::Assets::Blob& payload,
        /*out*/ ::Assets::Blob& errors,
        ID3DBlob* payloadBlob,
        ID3DBlob* errorsBlob,
        const ShaderService::ShaderHeader& hdr)
    {
        payload.reset();
        if (payloadBlob && payloadBlob->GetBufferPointer() && payloadBlob->GetBufferSize()) {
            payload = std::make_shared<std::vector<uint8>>(payloadBlob->GetBufferSize() + sizeof(ShaderService::ShaderHeader));
            auto* dsthdr = (ShaderService::ShaderHeader*)AsPointer(payload->begin());
            *dsthdr = hdr;
            XlCopyMemory(
                PtrAdd(AsPointer(payload->begin()), sizeof(ShaderService::ShaderHeader)),
                (uint8*)payloadBlob->GetBufferPointer(), payloadBlob->GetBufferSize());
        }

        errors.reset();
        if (errorsBlob && errorsBlob->GetBufferPointer() && errorsBlob->GetBufferSize()) {
			auto str = MakeStringSection(
                (char*)errorsBlob->GetBufferPointer(), 
                PtrAdd((char*)errorsBlob->GetBufferPointer(), errorsBlob->GetBufferSize()));
			while (!str.IsEmpty() && *(str.end()-1) == '\0') str._end--;	// strip off trailing zeroes, because we don't need them in the blob
            errors = Assets::AsBlob(str);
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static PlustacheTypes::ObjectType CreateTemplateContext(IteratorRange<const D3D10_SHADER_MACRO*> macros)
    {
        PlustacheTypes::ObjectType result;
        for (auto i=macros.cbegin(); i!=macros.cend(); ++i)
            if (i->Name && i->Definition)
                result[i->Name] = i->Definition;
        return result;
    }

    bool D3DShaderCompiler::DoLowLevelCompile(
        /*out*/ ::Assets::Blob& payload,
        /*out*/ ::Assets::Blob& errors,
        /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
        const void* sourceCode, size_t sourceCodeLength,
        const ResId& shaderPath,
		StringSection<::Assets::ResChar> definesTable,
		IteratorRange<const SourceLineMarker*> sourceLineMarkers) const
    {
            // This is called (typically in a background thread)
            // after the shader data has been loaded from disk.
            // Here we will invoke the D3D compiler. It will block
            // in this thread (so we should normally only call this from
            // a background thread (often one in a thread pool)
        ID3DBlob* codeResult = nullptr, *errorResult = nullptr;

        std::string definesCopy;
        auto arrayOfDefines = MakeDefinesTable(definesTable, shaderPath._shaderModel, definesCopy, MakeIteratorRange(_fixedDefines));

        ResChar shaderModel[64];
        AdaptShaderModel(shaderModel, dimof(shaderModel), shaderPath._shaderModel);

		StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
		identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "[" << definesTable << "]";

            // If this is a compound text document, look for a chunk that contains a 
            // function linking graph with the right name. We can embedded different
            // forms of text data in a single file by using a compound document like this.
        StringSection<char> asStringSection((const char*)sourceCode, (const char*)PtrAdd(sourceCode, sourceCodeLength));
        auto compoundChunks = ::Assets::ReadCompoundTextDocument(asStringSection);
        auto chunk = std::find_if(compoundChunks.begin(), compoundChunks.end(),
            [&shaderPath](const ::Assets::TextChunk<char>& chunk) 
            {
                return XlEqString(chunk._type, "FunctionLinkingGraph")
                    && XlEqString(chunk._name, shaderPath._entryPoint);
            });
        if (chunk != compoundChunks.end()) {

                // This a FunctionLinkingGraph definition
                // look for the version number, and then parse this file as
                // a simple script language.
            const char* i = chunk->_content.begin();
            const char* e = chunk->_content.end();

                // A version number may follow -- but it's optional
            if (i < e && *i == ':') {
                ++i;
                auto versionStart = i;
                while (i < e && *i != '\n' && *i != '\r') ++i;
                uint32_t versionNumber = 0;
                if (!FastParseValue(MakeStringSection(versionStart, i), versionNumber) || versionNumber != 1)
                    Throw(::Exceptions::BasicLabel("Function linking graph script version unsupported (%s)", MakeStringSection(versionStart, i).AsString().c_str()));
            }

            // we need to remove the initial part of the shader model (eg, ps_, vs_)
            ResChar shortenedModel[64];
            XlCopyString(shortenedModel, shaderModel);
            const char* firstUnderscore = shortenedModel;
            while (*firstUnderscore != '\0' && *firstUnderscore != '_') ++firstUnderscore;
            if (firstUnderscore != 0)
                XlMoveMemory(shortenedModel, firstUnderscore+1, XlStringEnd(shortenedModel) - firstUnderscore);
                
            // We must first process the string using a string templating/interpolation library
            // this adds a kind of pre-processing step that allows us to customize the shader graph
            // that will be generated.
            // Unfortunately the string processing step is a little inefficient. Other than using a 
            // C preprocessor, there doesn't seem to be a highly efficient string templating
            // library for C++ (without dependencies on other libraries such as boost). Maybe google ctemplates?
            auto finalSection = MakeStringSection(i, e);
            std::string customizedString;
            const bool doStringTemplating = true;
            if (constant_expression<doStringTemplating>::result()) {
                Plustache::template_t templ;
                auto obj = CreateTemplateContext(MakeIteratorRange(arrayOfDefines));
                customizedString = templ.render(std::string(i, e), obj);
                finalSection = MakeStringSection(customizedString);
            }
            
            FunctionLinkingGraph flg(finalSection, shortenedModel, definesTable, ::Assets::DefaultDirectorySearchRules(shaderPath._filename));
            return flg.TryLink(payload, errors, dependencies, identifier.AsStringSection(), shaderModel);

        } else {

            IncludeHandler includeHandler(MakeFileNameSplitter(shaderPath._filename).DriveAndPath());

            auto hresult = D3DCompile_Wrapper(
                sourceCode, sourceCodeLength,
                shaderPath._filename,

                AsPointer(arrayOfDefines.cbegin()), &includeHandler, 
                XlEqString(shaderPath._entryPoint, "null") ? nullptr : shaderPath._entryPoint,       // (libraries have a null entry point)
                shaderModel,

                GetShaderCompilationFlags(), 0, 
                &codeResult, &errorResult);

                // we get a "blob" from D3D. But we need to copy it into
                // a shared_ptr<vector> so we can pass to it our clients
            
            CreatePayloadFromBlobs(
                payload, errors, codeResult, errorResult, 
                ShaderService::ShaderHeader {
					identifier.AsStringSection(),
                    shaderPath._shaderModel,
                    shaderPath._entryPoint,
                    shaderPath._dynamicLinkageEnabled
                });

            for (auto&i:includeHandler.GetIncludeFiles())
				if (std::find(dependencies.begin(), dependencies.end(), i) == dependencies.end())
					dependencies.push_back(i);

            return SUCCEEDED(hresult);

        }
    }
    
        ////////////////////////////////////////////////////////////

    HMODULE D3DShaderCompiler::GetShaderCompileModule() const
    {
        ScopedLock(_moduleLock);
        if (_module == INVALID_HANDLE_VALUE)
            _module = (*OSServices::Windows::Fn_LoadLibrary)("d3dcompiler_47.dll");
        return _module;
    }

    HRESULT D3DShaderCompiler::D3DReflect_Wrapper(
        const void* pSrcData, size_t SrcDataSize, 
        const IID& pInterface, void** ppReflector) const
    {
            // This is a wrapper for the D3DReflect(). See D3D11CreateDevice_Wrapper in Device.cpp
            // for a similar function.

        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            return E_NOINTERFACE;
        }

        typedef HRESULT WINAPI D3DReflect_Fn(LPCVOID, SIZE_T, REFIID, void**);

        auto fn = (D3DReflect_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DReflect");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(pSrcData, SrcDataSize, pInterface, ppReflector);
    }

    HRESULT D3DShaderCompiler::D3DCompile_Wrapper(
        LPCVOID pSrcData,
        SIZE_T SrcDataSize,
        LPCSTR pSourceName,
        const D3D_SHADER_MACRO* pDefines,
        ID3DInclude* pInclude,
        LPCSTR pEntrypoint,
        LPCSTR pTarget,
        UINT Flags1,
        UINT Flags2,
        ID3DBlob** ppCode,
        ID3DBlob** ppErrorMsgs) const
    {
            // This is a wrapper for the D3DCompile(). See D3D11CreateDevice_Wrapper in Device.cpp
            // for a similar function.

        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            Log(Error) << "Could not load d3dcompiler_47.dll. This is required to compile shaders. Please make sure this dll is in the same directory as your executable, or reachable path" << std::endl;
            return E_NOINTERFACE;
        }

        Log(Verbose) << "Performing D3D shader compile on: " << (pSourceName ? pSourceName : "<<unnamed>>") << ":" << (pEntrypoint?pEntrypoint:"<<unknown entry point>>") << "(" << (pTarget?pTarget:"<<unknown shader model>>") << ")" << std::endl;

        typedef HRESULT WINAPI D3DCompile_Fn(
            LPCVOID, SIZE_T, LPCSTR,
            const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
            UINT, UINT, ID3DBlob**, ID3DBlob**);

        auto fn = (D3DCompile_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DCompile");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs);
    }

    HRESULT D3DShaderCompiler::D3DCreateFunctionLinkingGraph_Wrapper(
        UINT uFlags,
        ID3D11FunctionLinkingGraph **ppFunctionLinkingGraph) const
    {
        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            return E_NOINTERFACE;
        }

        typedef HRESULT WINAPI D3DCreateFunctionLinkingGraph_Fn(UINT, ID3D11FunctionLinkingGraph**);

        auto fn = (D3DCreateFunctionLinkingGraph_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DCreateFunctionLinkingGraph");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(uFlags, ppFunctionLinkingGraph);
    }

    HRESULT D3DShaderCompiler::D3DCreateLinker_Wrapper(ID3D11Linker **ppLinker) const
    {
        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            return E_NOINTERFACE;
        }

        typedef HRESULT WINAPI D3DCreateLinker_Fn(ID3D11Linker**);

        auto fn = (D3DCreateLinker_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DCreateLinker");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(ppLinker);
    }

    HRESULT D3DShaderCompiler::D3DLoadModule_Wrapper(
        LPCVOID pSrcData, SIZE_T cbSrcDataSize, 
        ID3D11Module **ppModule) const
    {
        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            return E_NOINTERFACE;
        }

        typedef HRESULT WINAPI D3DLoadModule_Fn(LPCVOID, SIZE_T, ID3D11Module**);

        auto fn = (D3DLoadModule_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DLoadModule");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(pSrcData, cbSrcDataSize, ppModule);
    }

    HRESULT D3DShaderCompiler::D3DReflectLibrary_Wrapper(
        LPCVOID pSrcData,
        SIZE_T  SrcDataSize,
        REFIID  riid,
        LPVOID  *ppReflector) const
    {
        auto compiler = GetShaderCompileModule();
        if (!compiler || compiler == INVALID_HANDLE_VALUE) {
			assert(0 && "d3dcompiler_47.dll is missing. Please make sure this dll is in the same directory as your executable, or reachable path");
            return E_NOINTERFACE;
        }

        typedef HRESULT WINAPI D3DReflectLibrary_Fn(LPCVOID, SIZE_T, REFIID, LPVOID);

        auto fn = (D3DReflectLibrary_Fn*)(*OSServices::Windows::Fn_GetProcAddress)(compiler, "D3DReflectLibrary");
        if (!fn) {
            (*OSServices::Windows::FreeLibrary)(compiler);
            compiler = (HMODULE)INVALID_HANDLE_VALUE;
            return E_NOINTERFACE;
        }

        return (*fn)(pSrcData, SrcDataSize, riid, ppReflector);
    }

    std::string D3DShaderCompiler::MakeShaderMetricsString(const void* data, size_t dataSize) const
    {
            // Build some metrics information about the given shader, using the D3D
            // reflections interface.
		auto* hdr = (ShaderService::ShaderHeader*)data;
		if (dataSize <= sizeof(ShaderService::ShaderHeader)
			|| hdr->_version != ShaderService::ShaderHeader::Version) 
			return "<Shader header corrupted, or wrong version>";

        ID3D::ShaderReflection* reflTemp = nullptr;
        auto hresult = D3DReflect_Wrapper(
			PtrAdd(data, sizeof(ShaderService::ShaderHeader)), dataSize - sizeof(ShaderService::ShaderHeader),
			s_shaderReflectionInterfaceGuid, (void**)&reflTemp);
        intrusive_ptr<ID3D::ShaderReflection> refl = moveptr(reflTemp);
        if (!SUCCEEDED(hresult) || !refl) {
            return "<Failure in D3DReflect>";
        }

        D3D11_SHADER_DESC desc;
        XlZeroMemory(desc);
        hresult = refl->GetDesc(&desc);
		if (!SUCCEEDED(hresult)) {
			return "<Failure in D3DReflect>";
		}

        std::stringstream str;
        str << "Instruction Count: " << desc.InstructionCount << "; ";
        str << "Temp Reg Count: " << desc.TempRegisterCount << "; ";
        str << "Temp Array Count: " << desc.TempArrayCount << "; ";
        str << "CB Count: " << desc.ConstantBuffers << "; ";
        str << "Res Count: " << desc.BoundResources << "; ";

        str << "Texture Instruction -- N:" << desc.TextureNormalInstructions 
            << " L:" << desc.TextureLoadInstructions 
            << " C:" << desc.TextureCompInstructions 
            << " B:" << desc.TextureBiasInstructions
            << " G:" << desc.TextureGradientInstructions
            << "; ";

        str << "Arith Instruction -- float:" << desc.FloatInstructionCount 
            << " i:" << desc.IntInstructionCount 
            << " uint:" << desc.FloatInstructionCount
            << "; ";

        str << "Flow control -- static:" << desc.StaticFlowControlCount
            << " dyn:" << desc.DynamicFlowControlCount
            << "; ";

        str << "Macro instructions:" << desc.MacroInstructionCount << "; ";

        str << "Compute shader instructions -- barrier:" << desc.cBarrierInstructions
            << " interlocked: " << desc.cInterlockedInstructions
            << " store: " << desc.cTextureStoreInstructions
            << "; ";

        str << "Bitwise instructions: " << refl->GetBitwiseInstructionCount() << "; ";
        str << "Conversion instructions: " << refl->GetConversionInstructionCount() << "; ";
        str << "Sample frequency: " << refl->IsSampleFrequencyShader();

        return str.str();
    }

    std::weak_ptr<D3DShaderCompiler> D3DShaderCompiler::s_instance;

    D3DShaderCompiler::D3DShaderCompiler(IteratorRange<D3D10_SHADER_MACRO*> fixedDefines, D3D_FEATURE_LEVEL featureLevel)
    : _fixedDefines(fixedDefines.begin(), fixedDefines.end())
    , _featureLevel(featureLevel)
    {
        _module = (HMODULE)INVALID_HANDLE_VALUE;
    }

    D3DShaderCompiler::~D3DShaderCompiler()
    {
            // note --  we have to be careful when unloading this DLL!
            //          We may have created ID3D11Reflection objects using
            //          this dll. If any of them are still alive when we unload
            //          the DLL, then they will cause a crash if we attempt to
            //          use them, or call the destructor. The only way to be
            //          safe is to make sure all reflection objects are destroyed
            //          before unloading the dll
        if (_module != INVALID_HANDLE_VALUE) {
            (*OSServices::Windows::FreeLibrary)(_module);
            _module = (HMODULE)INVALID_HANDLE_VALUE;
        }
    }

    intrusive_ptr<ID3D::ShaderReflection> CreateReflection(const CompiledShaderByteCode& shaderCode)
    {
        auto stage = shaderCode.GetStage();
        if (stage == ShaderStage::Null)
            return intrusive_ptr<ID3D::ShaderReflection>();

            // awkward -- we have to use a singleton pattern to get access to the compiler here...
            //          Otherwise, we could potentially have multiple instances of D3DShaderCompiler.
        auto compiler = D3DShaderCompiler::GetInstance();

        auto byteCode = shaderCode.GetByteCode();

        ID3D::ShaderReflection* reflectionTemp = nullptr;
        HRESULT hresult = compiler->D3DReflect_Wrapper(byteCode.begin(), byteCode.size(), s_shaderReflectionInterfaceGuid, (void**)&reflectionTemp);
        if (!SUCCEEDED(hresult) || !reflectionTemp)
            Throw(::Exceptions::BasicLabel("Error while invoking low-level shader reflection"));
        return moveptr(reflectionTemp);
    }

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device, D3D_FEATURE_LEVEL featureLevel)
    {
        auto result = D3DShaderCompiler::s_instance.lock();
        if (result) return std::move(result);

        D3D10_SHADER_MACRO fixedDefines[] = { 
            MakeShaderMacro("D3D11", "1")
            #if defined(_DEBUG)
                , MakeShaderMacro("_DEBUG", "1")
            #endif
        };
        result = std::make_shared<D3DShaderCompiler>(MakeIteratorRange(fixedDefines), featureLevel);
        D3DShaderCompiler::s_instance = result;
        return std::move(result);
    }

    std::shared_ptr<ILowLevelCompiler> CreateVulkanPrecompiler()
    {
        D3D10_SHADER_MACRO fixedDefines[] = { 
            MakeShaderMacro("VULKAN", "1"),
            MakeShaderMacro("HLSLCC", "1")
            #if defined(_DEBUG)
                , MakeShaderMacro("_DEBUG", "1")
            #endif
        };
        return std::make_shared<D3DShaderCompiler>(MakeIteratorRange(fixedDefines), D3D_FEATURE_LEVEL_11_0);
    }

}}



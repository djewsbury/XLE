// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "DeviceContext.h"
#include "../../ShaderService.h"
#include "../../ShaderLangUtil.h"
#include "../../StateDesc.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../Assets/ConfigFileContainer.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/IntermediatesStore.h"		// (for GetDependentFileState)
#include "../../../Assets/IFileSystem.h"
#include "../../../Utility/Streams/PathUtils.h"
#include "../../../Utility/Threading/Mutex.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/PtrUtils.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/Conversion.h"
#include "../../../OSServices/Log.h"
#include "../../../Foreign/plustache/template.hpp"

#include <regex> // used for parsing parameter definition
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

    class FLGFormatter
    {
    public:
        enum class Blob
        {
            Call, PassValue, Module, Alias,
            ParameterBlock, Assignment,
            DeclareInput, DeclareOutput, 
            Token,
            End
        };
        std::pair<Blob, StringSection<char>> PeekNext();
        void SetPosition(const char* newPosition);

        StreamLocation GetStreamLocation() const { return { unsigned(_iterator - _lineStart + 1), _lineIndex }; }

        FLGFormatter(StringSection<char> script);
        ~FLGFormatter();
    private:
        StringSection<char> _script;
        const char* _iterator;

        unsigned    _lineIndex;
        const char* _lineStart;
    };

    static bool IsWhitespace(char chr)          { return chr == ' ' || chr == '\t'; }
    static bool IsNewlineWhitespace(char chr)   { return chr == '\r' || chr == '\n'; }
    static bool IsIgnoreable(char chr)          { return chr == ')'; }

    auto FLGFormatter::PeekNext() -> std::pair<Blob, StringSection<char>>
    {
    restartParse:

        while (_iterator < _script.end()) {
            if (IsWhitespace(*_iterator) || IsIgnoreable(*_iterator)) {
                ++_iterator;
            } else if (*_iterator == '\n' || *_iterator == '\r') {
                if (*_iterator == '\r' && (_iterator+1) < _script.end() && *(_iterator+1) == '\n')
                    ++_iterator;
                ++_iterator;
                ++_lineIndex;
                _lineStart = _iterator;
            } else 
                break;
        }

        if (_iterator == _script.end())
            return std::make_pair(Blob::End, StringSection<char>());

        // check for known tokens -- 
        if (*_iterator == '/' && (_iterator + 1) < _script.end() && *(_iterator+1) == '/') {
            // just scan to the end of the line...
            _iterator += 2;
            while (_iterator < _script.end() && *_iterator != '\r' && *_iterator != '\n') ++_iterator;

            // ok, I could use a loop to do this (or a recursive call)
            // but sometimes goto is actually the best solution...!
            // A loop would make all of the rest of the function confusing and a recursive
            // call isn't ideal if there are many sequential comment lines
            goto restartParse;
        } else if (*_iterator == '=') {
            return std::make_pair(Blob::Assignment, StringSection<char>(_iterator, _iterator+1));
        } else if (*_iterator == '(') {
                // This is a parameter block. We need to scan forward over everything until
                // we reach the end bracket. The end bracket is the only thing we care about. Everything
                // else gets collapsed into the parameter block.
            const auto* i = _iterator+1;
            for (;;) {
                if (i == _script.end())
                    Throw(FormatException(
                        "Missing closing ')' on parameter block",
                        GetStreamLocation()));
                if (*i == ')') break;
                ++i;
            }
            return std::make_pair(Blob::ParameterBlock, StringSection<char>(_iterator+1, i));
        } else {
            static const std::pair<Blob, StringSection<char>> KnownTokens[] = 
            {
                std::make_pair(Blob::Module,        "Module"),
                std::make_pair(Blob::DeclareInput,  "DeclareInput"),
                std::make_pair(Blob::DeclareOutput, "DeclareOutput"),
                std::make_pair(Blob::Call,          "Call"),
                std::make_pair(Blob::PassValue,     "PassValue"),
                std::make_pair(Blob::Alias,         "Alias")
            };
            // read forward to any token terminator
            const char* i = _iterator;
            while (i < _script.end() && !IsWhitespace(*i) && *i != '\r' && *i != '\n' && *i != '(' && *i != ')')
                ++i;

            auto token = MakeStringSection(_iterator, i);
            for (unsigned c=0; c<dimof(KnownTokens); ++c)
                if (XlEqString(token, KnownTokens[c].second))
                    return std::make_pair(KnownTokens[c].first, token);
                
            return std::make_pair(Blob::Token, token);
        }
    }

    void FLGFormatter::SetPosition(const char* newPosition)
    {
        // typically called after PeekNext(), we should advance to the given
        // position. While advancing, we have to look for new lines!
        assert(newPosition >= _iterator && newPosition <= _script.end());
        while (_iterator < newPosition) {
            if (*_iterator == '\n' || *_iterator == '\r') {
                    // note that if we attempt to "SetPosition" to a point in the middle of "\r\n"
                    // we will automatically get adjusted to after the \n
                if (*_iterator == '\r' && (_iterator+1) < _script.end() && *(_iterator+1) == '\n')
                    ++_iterator;
                ++_lineIndex;
                _lineStart = _iterator;
            }
            ++_iterator;
        }
    }

    FLGFormatter::FLGFormatter(StringSection<char> script)
    : _script(script)
    {
        _iterator = _script.begin();
        _lineIndex = 1;
        _lineStart = _script.begin();
    }

    FLGFormatter::~FLGFormatter() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class FunctionLinkingModule
    {
    public:
        ID3D11Module* GetUnderlying() { return _module.get(); }
        ID3D11LibraryReflection* GetReflection() { return _reflection.get(); }
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _dependencyValidation; }

        FunctionLinkingModule(StringSection<::Assets::ResChar> initializer, StringSection<::Assets::ResChar> defines);
        ~FunctionLinkingModule();
    private:
        intrusive_ptr<ID3D11Module> _module;
        intrusive_ptr<ID3D11LibraryReflection> _reflection;
		::Assets::DependencyValidation _dependencyValidation;
    };

    FunctionLinkingModule::FunctionLinkingModule(StringSection<::Assets::ResChar> initializer, StringSection<::Assets::ResChar> defines)
    {
        // note --  we have to be a little bit careful here. If all of the compilation threads hit this point
        //          and start waiting for other pending assets, there may be no threads left to compile the other assets!
        //          this might happen if we attempt to compile a lot of different variations of a single shader graph at
        //          the same time.
        //      Also, there is a potential chance that the source shader code could change twice in rapid succession, which
        //      could cause the CompilerShaderByteCode object to be destroyed while we still have a pointer to it. Actually,
        //      this case of one compiled asset being dependent on another compile asset introduces a lot of complications!
        const auto& byteCode = ::Assets::ActualizeAsset<CompiledShaderByteCode>(initializer, defines);
        auto code = byteCode.GetByteCode();

        ID3D11Module* rawModule = nullptr;
        auto compiler = D3DShaderCompiler::GetInstance(); 
        auto hresult = compiler->D3DLoadModule_Wrapper(code.begin(), code.size(), &rawModule);
        _module = moveptr(rawModule);
        if (!SUCCEEDED(hresult))
            Throw(::Exceptions::BasicLabel("Failure while creating shader module from compiled shader byte code (%s)", initializer));

        ID3D11LibraryReflection* reflectionRaw = nullptr;
        compiler->D3DReflectLibrary_Wrapper(code.begin(), code.size(), IID_ID3D11LibraryReflection, (void**)&reflectionRaw);
        _reflection = moveptr(reflectionRaw);
		_dependencyValidation = byteCode.GetDependencyValidation();
    }

    FunctionLinkingModule::~FunctionLinkingModule() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename First, typename Second>
        class StringCompareFirst
        {
        public:
            inline bool operator()(const std::pair<First, Second>& lhs, const std::pair<First, Second>& rhs) const   { return XlCompareString(lhs.first, rhs.first) < 0; }
            inline bool operator()(const std::pair<First, Second>& lhs, const First& rhs) const                      { return XlCompareString(lhs.first, rhs) < 0; }
            inline bool operator()(const First& lhs, const std::pair<First, Second>& rhs) const                      { return XlCompareString(lhs, rhs.first) < 0; }
        };

    template<typename StringComparable, typename Object>
        typename std::vector<std::pair<StringComparable, Object>>::iterator
            LowerBoundT(
                std::vector<std::pair<StringComparable, Object>>& vector,
                StringComparable comparison)
        {
            return std::lower_bound(
                vector.begin(), vector.end(), comparison,
                StringCompareFirst<StringComparable, Object>());
        }

    class FunctionLinkingGraph
    {
    public:
        ID3D11FunctionLinkingGraph* GetUnderlying() { return _graph.get(); }

        bool TryLink(
			::Assets::Blob& payload,
			::Assets::Blob& errors,
            std::vector<::Assets::DependentFileState>& dependencies,
			StringSection<> identifier,
            const char shaderModel[]);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _dependencyValidation; }

        using Section = StringSection<char>;
        FunctionLinkingGraph(Section script, Section shaderProfile, Section defines, const ::Assets::DirectorySearchRules& searchRules);
        ~FunctionLinkingGraph();
    private:
        void ParseAssignmentExpression(FLGFormatter& formatter, Section variableName, const ::Assets::DirectorySearchRules& searchRules);
        void ParsePassValue(Section src, Section dst, StreamLocation loc);

        using NodePtr = intrusive_ptr<ID3D11LinkingNode>;
        using AliasTarget = std::pair<NodePtr, int>;
        AliasTarget ResolveParameter(Section src, StreamLocation loc);
        NodePtr ParseCallExpression(Section fnName, Section paramsShortHand, StreamLocation loc);
        FunctionLinkingModule ParseModuleExpression(Section params, const ::Assets::DirectorySearchRules& searchRules, StreamLocation loc);

        intrusive_ptr<ID3D11FunctionLinkingGraph> _graph;
        std::vector<std::pair<Section, FunctionLinkingModule>> _modules;
        std::vector<std::pair<Section, NodePtr>> _nodes;

        std::vector<std::pair<std::string, AliasTarget>> _aliases;

        std::vector<::Assets::DependentFileState> _depFiles;
		::Assets::DependencyValidation _dependencyValidation;
        std::vector<std::pair<Section, Section>> _referencedFunctions;

        std::string _shaderProfile, _defines;
    };

    static std::regex PassValueParametersParse(R"--(\s*([\w.]+)\s*,\s*([\w.]+)\s*)--");

    FunctionLinkingGraph::FunctionLinkingGraph(StringSection<char> script, Section shaderProfile, Section defines, const ::Assets::DirectorySearchRules& searchRules)
    : _shaderProfile(shaderProfile.AsString())
    , _defines(defines.AsString())
    {
        ID3D11FunctionLinkingGraph* graphRaw = nullptr;
        auto compiler = D3DShaderCompiler::GetInstance();
        auto hresult = compiler->D3DCreateFunctionLinkingGraph_Wrapper(0, &graphRaw);
        if (!SUCCEEDED(hresult))
            Throw(::Exceptions::BasicLabel("Failure while creating D3D function linking graph"));
        _graph = moveptr(graphRaw);

        _dependencyValidation = ::Assets::GetDepValSys().Make();

		TRY
		{
			using Blob = FLGFormatter::Blob;
			FLGFormatter formatter(script);
			for (;;) {
				auto next = formatter.PeekNext();
				if (next.first == Blob::End) break;
            
					// Will we parse a statement at a time.
					// There are only 2 types of statements.
					// Assignments  -- <<variable>> = <<Module/Input/Output/Call>
					// Bindings     -- PassValue(<<node>>.<<parameter>>, <<node>>.<<parameter>>))
				switch (next.first) {
				case Blob::Token:
					{
							// expecting an '=' token after this
						formatter.SetPosition(next.second.end());
						auto expectingAssignment = formatter.PeekNext();
						if (expectingAssignment.first != Blob::Assignment)
							Throw(FormatException("Expecting assignment after variable name", formatter.GetStreamLocation()));
						formatter.SetPosition(expectingAssignment.second.end());

						ParseAssignmentExpression(formatter, next.second, searchRules);
						break;
					}

				case Blob::PassValue:
					{
						auto startLocation = formatter.GetStreamLocation();
						formatter.SetPosition(next.second.end());
						auto expectingParameters = formatter.PeekNext();
						if (expectingParameters.first != Blob::ParameterBlock)
							Throw(FormatException("Expecting parameters block for PassValue statement", formatter.GetStreamLocation()));
						formatter.SetPosition(expectingParameters.second.end());

						std::match_results<const char*> match;
						bool a = std::regex_match(
							expectingParameters.second.begin(), expectingParameters.second.end(), 
							match, PassValueParametersParse);
						if (a && match.size() >= 3) {
							ParsePassValue(
								MakeStringSection(match[1].first, match[1].second),
								MakeStringSection(match[2].first, match[2].second),
								startLocation);
						} else {
							Throw(FormatException("Couldn't parser parameters block for PassValue statement", formatter.GetStreamLocation()));
						}

						break;
					}

				default:
					Throw(FormatException("Unexpected token. Statements should start with either an assignment or PassValue instruction", formatter.GetStreamLocation()));
				}
			}
		} CATCH (const ::Assets::Exceptions::ConstructionError& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _dependencyValidation));
		} CATCH (const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _dependencyValidation));
		} CATCH_END
    }

    FunctionLinkingGraph::~FunctionLinkingGraph() {}

    bool FunctionLinkingGraph::TryLink(
		::Assets::Blob& payload,
		::Assets::Blob& errors,
        std::vector<::Assets::DependentFileState>& dependencies,
		StringSection<> identifier,
        const char shaderModel[])
    {
        ID3D11Linker* linkerRaw = nullptr;
        auto compiler = D3DShaderCompiler::GetInstance();
        auto hresult = compiler->D3DCreateLinker_Wrapper(&linkerRaw);
        intrusive_ptr<ID3D11Linker> linker = moveptr(linkerRaw);
        if (!SUCCEEDED(hresult)) {
            errors = ::Assets::AsBlob("Could not create D3D shader linker object");
            return false;
        }

        ID3DBlob* errorsBlobRaw = nullptr;
        ID3D11ModuleInstance* baseModuleInstanceRaw = nullptr;
        hresult = _graph->CreateModuleInstance(&baseModuleInstanceRaw, &errorsBlobRaw);
        intrusive_ptr<ID3DBlob> errorsBlob0 = moveptr(errorsBlobRaw);
        intrusive_ptr<ID3D11ModuleInstance> baseModuleInstance = moveptr(baseModuleInstanceRaw);
        if (!SUCCEEDED(hresult)) {
            StringMeld<1024> meld;
            meld << "Failure while creating a module instance from the function linking graph (" << (const char*)errorsBlob0->GetBufferPointer() << ")";
            errors = ::Assets::AsBlob(meld.AsStringSection());
            return false;
        }

        std::vector<intrusive_ptr<ID3D11ModuleInstance>> instances;
        instances.reserve(_modules.size());
        for (auto& i2:_modules) {
            ID3D11ModuleInstance* rawInstance = nullptr;
            hresult = i2.second.GetUnderlying()->CreateInstance("", &rawInstance);
            intrusive_ptr<ID3D11ModuleInstance> instance = moveptr(rawInstance);
            if (!SUCCEEDED(hresult)) {
                errors = ::Assets::AsBlob("Failure while creating a module instance from a module while linking");
                return false;
            }

            // We need to call BindResource / BindSampler / BindConstantBuffer for each
            // of these used by the called functions in the module instance. If we don't bind them, it
            // seems we get link errors below.
            // We can setup a default binding by just binding to the original slots -- 
            {
                auto* reflection = i2.second.GetReflection();

                auto refFns = std::equal_range(_referencedFunctions.cbegin(), _referencedFunctions.cend(), i2.first, StringCompareFirst<Section, Section>());
                if (refFns.first == refFns.second) continue;

                D3D11_LIBRARY_DESC libDesc;
                reflection->GetDesc(&libDesc);

                for (unsigned c2=0; c2<libDesc.FunctionCount; ++c2) {
                    auto* fn = reflection->GetFunctionByIndex(c2);

                    D3D11_FUNCTION_DESC desc;
                    fn->GetDesc(&desc);

                    // if the function is referenced, we can apply the default bindings...
                    auto i = std::find_if(refFns.first, refFns.second, 
                        [&desc](const std::pair<Section, Section>& p) { return XlEqString(p.second, desc.Name);});
                    if (i != refFns.second) {
                        for (unsigned c=0; c<desc.BoundResources; ++c) {
                            D3D11_SHADER_INPUT_BIND_DESC bindDesc;
                            fn->GetResourceBindingDesc(c, &bindDesc);
                            if (bindDesc.Type == D3D_SIT_CBUFFER) {
                                instance->BindConstantBuffer(bindDesc.BindPoint, bindDesc.BindPoint, 0);
                            } else if (bindDesc.Type == D3D_SIT_TEXTURE) {
                                instance->BindResource(bindDesc.BindPoint, bindDesc.BindPoint, bindDesc.BindCount);
                            } else if (bindDesc.Type == D3D_SIT_SAMPLER) {
                                instance->BindSampler(bindDesc.BindPoint, bindDesc.BindPoint, bindDesc.BindCount);
                            }
                        }
                    }
                }
            }

            instances.emplace_back(instance);
        }

        for (auto& i:instances) linker->UseLibrary(i.get());

        ID3DBlob* resultBlobRaw = nullptr;
        errorsBlobRaw = nullptr;
        hresult = linker->Link(
            baseModuleInstance.get(), "main", shaderModel, 0,
            &resultBlobRaw, &errorsBlobRaw);
        intrusive_ptr<ID3DBlob> errorsBlob1 = moveptr(errorsBlobRaw);
        intrusive_ptr<ID3DBlob> resultBlob = moveptr(resultBlobRaw);

        if (!SUCCEEDED(hresult)) {
            StringMeld<1024> meld;
            meld << "Failure during final linking process for dynamic shader (" << (const char*)errorsBlob1->GetBufferPointer() << ")";
            errors = ::Assets::AsBlob(meld.AsStringSection());
            return false;
        }

        CreatePayloadFromBlobs(
            payload, errors, resultBlob.get(), errorsBlob1.get(), 
            ShaderService::ShaderHeader { identifier, shaderModel, "main", false });

        dependencies.insert(dependencies.end(), _depFiles.begin(), _depFiles.end());
        return true;
    }

    class ShaderParameter
    {
    public:
        std::string _name, _semanticName;
        D3D_SHADER_VARIABLE_TYPE _type;
        D3D_SHADER_VARIABLE_CLASS _class;
        unsigned _rows, _columns;

        D3D11_PARAMETER_DESC AsParameterDesc(D3D_PARAMETER_FLAGS defaultFlags = D3D_PF_NONE)
        {
            return 
              { _name.c_str(), _semanticName.c_str(),
                _type, _class, _rows, _columns,
                D3D_INTERPOLATION_UNDEFINED,
                defaultFlags,
                0, 0, 0, 0 };
        }

        ShaderParameter(StringSection<char> param);
    };

    static std::regex ShaderParameterParse(R"--((\w+)\s+(\w+)\s*(?:\:\s*(\w+))\s*)--");
    static std::regex CommaSeparatedList(R"--([^,\s]+)--");

    template<typename DestType = unsigned, typename CharType = char>
        DestType StringToUnsigned(const StringSection<CharType> source)
    {
        auto* start = source.begin();
        auto* end = source.end();
        if (start >= end) return 0;

        auto result = DestType(0);
        for (;;) {
            if (start >= end) break;
            if (*start < '0' || *start > '9') break;
            result = (result * 10) + DestType((*start) - '0');
            ++start;
        }
        return result;
    }

    ShaderParameter::ShaderParameter(StringSection<char> param)
    {
        // Our parameters are always of the format "type name [: semantic]"
        // We ignore some other HLSL syntax elements (in/out/inout and interpolation modes)
        // The parse is simple, but let's use regex anwyay

        _type = D3D_SVT_VOID;
        _class = D3D_SVC_SCALAR;
        _rows = _columns = 1;

        std::match_results<const char*> match;
        bool a = std::regex_match(param.begin(), param.end(), match, ShaderParameterParse);
        if (a && match.size() >= 3) {
            _name = std::string(match[2].first, match[2].second);
            if (match.size() >= 4)
                _semanticName = std::string(match[3].first, match[3].second);

            auto typeName = MakeStringSection(match[1].first, match[1].second);
            auto typeDesc = ShaderLangTypeNameAsTypeDesc(typeName);
            
                // Convert the "typeDesc" values into the types used by the HLSL library
            switch (typeDesc._type) {
            case ImpliedTyping::TypeCat::Float:     _type = D3D_SVT_FLOAT; break;
            case ImpliedTyping::TypeCat::UInt32:    _type = D3D_SVT_UINT; break;
            case ImpliedTyping::TypeCat::Int32:     _type = D3D_SVT_INT; break;
            case ImpliedTyping::TypeCat::UInt8:     _type = D3D_SVT_UINT8; break;
            default:
                Throw(::Exceptions::BasicLabel("Unknown parameter type encountered in ShaderParameter (%s)", typeName.AsString().c_str()));
                break;
            }

            _class = (typeDesc._arrayCount <= 1) ? D3D_SVC_SCALAR : D3D_SVC_VECTOR;
            _rows = 1;
            _columns = typeDesc._arrayCount;
        }
    }

    static std::vector<ShaderParameter> ParseParameters(StringSection<char> input)
    {
        // This should be a comma separated list of parameterss
        // just have to do the separation by comma here...
        std::vector<ShaderParameter> result;
        const auto* i = input.begin();
        for (;;) {
            while (i < input.end() && (IsWhitespace(*i) || IsNewlineWhitespace(*i))) ++i;
            if (i == input.end()) break;

            const auto* start = i;
            while (i < input.end() && *i != ',') ++i;
            result.emplace_back(ShaderParameter(MakeStringSection(start, i)));
            if (i == input.end()) break;
            ++i;
        }

        return std::move(result);
    }

    static intrusive_ptr<ID3DBlob> GetLastError(ID3D11FunctionLinkingGraph& graph)
    {
        ID3DBlob* rawBlob = nullptr;
        graph.GetLastError(&rawBlob);
        return moveptr(rawBlob);
    }

    void FunctionLinkingGraph::ParseAssignmentExpression(FLGFormatter& formatter, Section variableName, const ::Assets::DirectorySearchRules& searchRules)
    {
        auto startLoc = formatter.GetStreamLocation();
        
        using Blob = FLGFormatter::Blob;
        auto next = formatter.PeekNext();
        if (   next.first != Blob::Module && next.first != Blob::DeclareInput
            && next.first != Blob::DeclareOutput && next.first != Blob::Call
            && next.first != Blob::Token && next.first != Blob::Alias)
            Throw(FormatException("Unexpected token after assignment operation", formatter.GetStreamLocation()));
        formatter.SetPosition(next.second.end());

        if (next.first == Blob::Token) {
            // This can be one of 2 things:
            // 1)  a "PassValue" expression written in short-hand
            //    eg: output.0 = fn.0
            // 2)  a "Call" expression in short-hand
            //    eg: output.0 = m0.Resolve(position)
            // We assume it's a function call if there is a parameter block
            auto maybeParams = formatter.PeekNext();
            if (maybeParams.first == Blob::ParameterBlock) {
                formatter.SetPosition(maybeParams.second.end());

                auto linkingNode = ParseCallExpression(next.second, maybeParams.second, startLoc);
                auto n = LowerBoundT(_nodes, variableName);
                if (n != _nodes.end() && XlEqString(n->first, variableName))
                    Throw(FormatException("Attempting to reassign node that is already assigned. Check for naming conflicts.", startLoc));

                _nodes.insert(n, std::make_pair(variableName, std::move(linkingNode)));
            } else {
                ParsePassValue(next.second, variableName, startLoc);
            }
            return;
        }

        // parse the parameter block that comes next
        auto paramBlockLoc = formatter.GetStreamLocation();
        auto p = formatter.PeekNext();
        if (p.first != Blob::ParameterBlock)
            Throw(FormatException("Expecting parameter block", formatter.GetStreamLocation()));
        formatter.SetPosition(p.second.end());
        auto parameterBlock = p.second;

        // we must perform some operations based on the token we got...
        switch (next.first) {
        case Blob::Module:
            {
                auto i = LowerBoundT(_modules, variableName);
                if (i != _modules.end() && XlEqString(i->first, variableName))
                    Throw(FormatException("Attempting to reassign module that is already assigned. Check for naming conflicts.", startLoc));

                auto module = ParseModuleExpression(parameterBlock, searchRules, startLoc);
				_dependencyValidation.RegisterDependency(module.GetDependencyValidation());
                _modules.insert(i, std::make_pair(variableName, std::move(module)));
            }
            break;

        case Blob::DeclareInput:
        case Blob::DeclareOutput:
            {
                // For both input and output, our parameter block should be a list of 
                // parameters (in a HLSL-like syntax). We use this to create a linking
                // node (which can be bound using the Bind instruction).
                auto params = ParseParameters(parameterBlock);

                std::vector<D3D11_PARAMETER_DESC> finalParams;
                finalParams.reserve(params.size());
                for (auto&i:params)
                    finalParams.push_back(
                        i.AsParameterDesc((next.first == Blob::DeclareInput) ? D3D_PF_IN : D3D_PF_OUT));

                ID3D11LinkingNode* linkingNodeRaw = nullptr;
                HRESULT hresult;
                if (next.first == Blob::DeclareInput) {
                    hresult = _graph->SetInputSignature(AsPointer(finalParams.cbegin()), (unsigned)finalParams.size(), &linkingNodeRaw);
                } else {
                    hresult = _graph->SetOutputSignature(AsPointer(finalParams.cbegin()), (unsigned)finalParams.size(), &linkingNodeRaw);
                }
                intrusive_ptr<ID3D11LinkingNode> linkingNode = moveptr(linkingNodeRaw);
                if (!SUCCEEDED(hresult)) {
                    auto e = GetLastError(*_graph);
                    StringMeld<1024> buffer;
                    buffer << "D3D error while creating input or output linking node (" << (const char*)e->GetBufferPointer() << ")";
                    Throw(FormatException(buffer.get(), startLoc));
                }

                auto i2 = LowerBoundT(_nodes, variableName);
                if (i2 != _nodes.end() && XlEqString(i2->first, variableName))
                    Throw(FormatException("Attempting to reassign node that is already assigned. Check for naming conflicts.", startLoc));

                // we can use the parameter names to create aliases...
                for (unsigned c=0; c<params.size(); ++c) {
                    auto i = LowerBoundT(_aliases, params[c]._name);
                    if (i != _aliases.end() && i->first == params[c]._name)
                        Throw(FormatException("Duplicate parameter name found", startLoc));
                    AliasTarget target = std::make_pair(linkingNode, c);
                    _aliases.insert(i, std::make_pair(params[c]._name, target));
                }

                _nodes.insert(i2, std::make_pair(variableName, std::move(linkingNode)));
            }
            break;

        case Blob::Call:
            {
                // Our parameters should be a function name with the form:
                //      <<ModuleName>>.<<FunctionName>>
                // The module name should correspond to a module previously loaded
                // and assigned with the Module instruction. This will create a new
                // linking node.

                auto linkingNode = ParseCallExpression(parameterBlock, Section(), paramBlockLoc);
                auto n = LowerBoundT(_nodes, variableName);
                if (n != _nodes.end() && XlEqString(n->first, variableName))
                    Throw(FormatException("Attempting to reassign node that is already assigned. Check for naming conflicts.", startLoc));

                _nodes.insert(n, std::make_pair(variableName, std::move(linkingNode)));
            }
            break;

        case Blob::Alias:
            {
                // Our parameters should be an alias or node parameter reference
                // we're just creating a new name for something that already exists
                auto target = ResolveParameter(parameterBlock, paramBlockLoc);
                auto varNameStr = variableName.AsString();
                auto i = LowerBoundT(_aliases, varNameStr);
                if (i != _aliases.end() && i->first == varNameStr)
                    Throw(FormatException("Duplicate alias name found", startLoc));
                _aliases.insert(i, std::make_pair(varNameStr, target));
            }
            break;

        default:
            break;
        }
    }

    auto FunctionLinkingGraph::ParseCallExpression(Section fnName, Section paramsShortHand, StreamLocation loc) -> NodePtr
    {
        auto* i = fnName.begin();
        while (i != fnName.end() && *i != '.') ++i;

        if (i == fnName.end())
            Throw(FormatException("Expected a module and function name in Call instruction.", loc));

        auto modulePart = MakeStringSection(fnName.begin(), i);
        auto fnPart = MakeStringSection(i+1, fnName.end());

        auto m = LowerBoundT(_modules, modulePart);
        if (m == _modules.end() || !XlEqString(m->first, modulePart))
            Throw(FormatException("Unknown module variable in Call instruction. Modules should be registered with Module instruction before using.", loc));

        auto module = m->second.GetUnderlying();

        ID3D11LinkingNode* linkingNodeRaw = nullptr;
        auto hresult = _graph->CallFunction(
            "", module, 
            fnPart.AsString().c_str(), &linkingNodeRaw);
        intrusive_ptr<ID3D11LinkingNode> linkingNode = moveptr(linkingNodeRaw);
        if (!SUCCEEDED(hresult)) {
            auto e = GetLastError(*_graph);
            Throw(FormatException(StringMeld<1024>() << "D3D error while creating linking node for function call (" << (const char*)e->GetBufferPointer() << ")", loc));
        }

        _referencedFunctions.insert(LowerBoundT(_referencedFunctions, modulePart), std::make_pair(modulePart, fnPart));

        // paramsShortHand should be a comma separated list. Parameters can either be aliases, or they can be
        // in node.index format
        auto param = std::cregex_iterator(paramsShortHand.begin(), paramsShortHand.end(), CommaSeparatedList);
        unsigned index = 0;
        for (; param != std::cregex_iterator(); ++param) {
            auto resolvedParam = ResolveParameter(Section(param->begin()->first, param->begin()->second), loc);
            hresult = _graph->PassValue(resolvedParam.first.get(), resolvedParam.second, linkingNode.get(), index++);
            if (!SUCCEEDED(hresult)) {
                auto e = GetLastError(*_graph);
                Throw(FormatException(StringMeld<1024>() << "D3D failure in PassValue statement (" << (const char*)e->GetBufferPointer() << ")", loc));
            }
        }

        return std::move(linkingNode);
    }

    FunctionLinkingModule FunctionLinkingGraph::ParseModuleExpression(Section params, const ::Assets::DirectorySearchRules& searchRules, StreamLocation loc)
    {
            // This is an operation to construct a new module instance. We must load the
            // module from some other source file, and then construct a module instance from it.
            // Note that we can do relative paths for this module name reference here -- 
            //      it must be an full asset path
            // Also, we should explicitly specify the shader version number and use the "lib" type
            // And we want to pass through our defines as well -- so that the linked module inherits
            // the same defines.

        auto param = std::cregex_iterator(params.begin(), params.end(), CommaSeparatedList);
        if (param == std::cregex_iterator())
            Throw(FormatException("Expecting module name in Module expression", loc));

            // First parameter is just the module name -- 
        ::Assets::ResChar resolvedName[MaxPath];
        XlCopyString(resolvedName, MakeStringSection(param->begin()->first, param->begin()->second));
        searchRules.ResolveFile(resolvedName, resolvedName);

            // Register a dependent file (even if it doesn't exist)
            // Note that this isn't really enough -- we need dependencies on
            // this file, and any dependencies it has! Really, our dependency
            // is on the product asset, not the source asset.
        _depFiles.push_back(::Assets::IntermediatesStore::GetDependentFileState(resolvedName));

        XlCatString(resolvedName, ":null:lib_");
        XlCatString(resolvedName, _shaderProfile.c_str());

            // Second parameter is a filter that we can use to filter the list of defines
            // typically many input defines should be filtered out in this step. This prevents
            // us from creating a separate asset for a define that is ignored.
        ++param;
        if (param != std::cregex_iterator()) {
            auto filter = MakeStringSection(param->begin()->first, param->begin()->second);
            std::set<uint64> filteredIn;
            auto* i=filter.begin();
            for (;;) {
                while (i!=filter.end() && *i==';') ++i;
                auto start=i;
                while (i!=filter.end() && *i!=';') ++i;
                if (i == start) break;
                filteredIn.insert(Hash64(start, i));
            }

            auto filteredDefines = _defines;
            auto si=filteredDefines.rbegin();
            for (;;) {
                while (si!=filteredDefines.rend() && *si==';') ++si;
                auto start=si;
                while (si!=filteredDefines.rend() && *si!=';') ++si;
                if (si == start) break;

                auto equs = start;
                while (equs!=si && *equs!='=') ++equs;

                auto hash = Hash64(&(*si.base()), &(*equs));
                if (filteredIn.find(hash) == filteredIn.end()) {
                    // reverse over the ';' deliminator
                    if (si!=filteredDefines.rend()) ++si;
                    si = decltype(si)(filteredDefines.erase(si.base(), start.base()));
                }
            }

            return FunctionLinkingModule(resolvedName, Conversion::Convert<::Assets::rstring>(filteredDefines).c_str());
        } else {
            return FunctionLinkingModule(resolvedName, Conversion::Convert<::Assets::rstring>(_defines).c_str());
        }
    }

    auto FunctionLinkingGraph::ResolveParameter(Section src, StreamLocation loc) -> AliasTarget
    {
        // Could be an alias, or could be in <node>.<parameter> format
        auto a = LowerBoundT(_aliases, src.AsString());
        if (a != _aliases.end() && XlEqString(MakeStringSection(a->first), src))
            return a->second;

        // split into <node>.<parameter> and resolve
        auto* dot = src.begin();
        while (dot != src.end() && *dot != '.') ++dot;
        if (dot == src.end())
            Throw(FormatException(StringMeld<256>() << "Unknown alias (" << src.AsString().c_str() << ")", loc));

        auto nodeSection = MakeStringSection(src.begin(), dot);
        auto indexSection = MakeStringSection(dot+1, src.end());

        auto n = LowerBoundT(_nodes, nodeSection);
        if (n == _nodes.end() || !XlEqString(n->first, nodeSection))
            Throw(FormatException(StringMeld<256>() << "Could not find node (" << nodeSection.AsString().c_str() << ")", loc));

        // Parameters are refered to by index. We could potentially
        // do a lookup to convert string names to their correct indices. We
        // can use ID3D11LibraryReflection to get the reflection information
        // for a shader module.

        int index;
        if (XlEqString(indexSection, "result")) index = D3D_RETURN_PARAMETER_INDEX;
        else index = (int)StringToUnsigned(indexSection);

        return AliasTarget(n->second, index);
    }

    void FunctionLinkingGraph::ParsePassValue(
        Section srcName, Section dstName, StreamLocation loc)
    {
        AliasTarget src = ResolveParameter(srcName, loc);
        AliasTarget dst = ResolveParameter(dstName, loc);

        auto hresult = _graph->PassValue(src.first.get(), src.second, dst.first.get(), dst.second);
        if (!SUCCEEDED(hresult)) {
            auto e = GetLastError(*_graph);
            Throw(FormatException(StringMeld<1024>() << "D3D failure in PassValue statement (" << (const char*)e->GetBufferPointer() << ")", loc));
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static PlustacheTypes::ObjectType CreateTemplateContext(IteratorRange<const D3D10_SHADER_MACRO*> macros)
    {
        PlustacheTypes::ObjectType result;
        for (auto i=macros.cbegin(); i!=macros.cend(); ++i)
            if (i->Name && i->Definition)
                result[i->Name] = i->Definition;
        return std::move(result);
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
                if (StringToUnsigned(MakeStringSection(versionStart, i)) != 1)
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



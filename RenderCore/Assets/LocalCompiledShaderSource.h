// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ShaderService.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetUtils.h"
#include <vector>
#include <memory>
#include <mutex>

namespace RenderCore { class DeviceDesc; }

namespace RenderCore { namespace Assets 
{
    class ShaderCacheSet;
    class ShaderCompileMarker;

    class ISourceCodePreprocessor
    {
    public:
        struct SourceCodeWithRemapping
        {
        public:
            std::string _processedSource;
			unsigned _processedSourceLineCount = 0;
            std::vector<ILowLevelCompiler::SourceLineMarker> _lineMarkers;
            std::vector<::Assets::DependentFileState> _dependencies;
        };

        virtual SourceCodeWithRemapping RunPreprocessor(StringSection<> filename, StringSection<> definesTable) = 0;
    };

    class LocalCompiledShaderSource 
        : public ::Assets::IAssetCompiler
        , public ShaderService::IShaderSource
        , public std::enable_shared_from_this<LocalCompiledShaderSource>
    {
    public:
        std::shared_ptr<::Assets::IIntermediateCompileMarker> Prepare(
            uint64 typeCode, const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount);
		std::vector<uint64_t> GetTypesForAsset(const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount);
		std::vector<std::pair<std::string, std::string>> GetExtensionsForType(uint64_t typeCode);

        std::shared_ptr<::Assets::ArtifactCollectionFuture> CompileFromFile(
            StringSection<::Assets::ResChar> resId, 
            StringSection<::Assets::ResChar> definesTable) const;
            
        std::shared_ptr<::Assets::ArtifactCollectionFuture> CompileFromMemory(
            StringSection<char> shaderInMemory, StringSection<char> entryPoint, 
            StringSection<char> shaderModel, StringSection<::Assets::ResChar> definesTable) const;

        void ClearCaches();
        
        void StallOnPendingOperations(bool cancelAll);

        ShaderCacheSet& GetCacheSet() { return *_shaderCacheSet; }

        // enable "WriteErrorLogFiles" to write out a little text file in the directory "shader_error"
        // for each shader compile error
        void SetWriteErrorLogFiles(bool newValue) { _writeErrorLogFiles = newValue; }

        LocalCompiledShaderSource(
            std::shared_ptr<ILowLevelCompiler> compiler,
            std::shared_ptr<ISourceCodePreprocessor> preprocessor,
            const DeviceDesc& devDesc,
			uint64_t associatedCompileProcessType = GetCompileProcessType((CompiledShaderByteCode*)nullptr));
        ~LocalCompiledShaderSource();
    protected:
        std::shared_ptr<ShaderCacheSet> _shaderCacheSet;
        std::shared_ptr<ILowLevelCompiler> _compiler;
        std::shared_ptr<ISourceCodePreprocessor> _preprocessor;
		uint64_t _associatedCompileProcessType;
        bool _writeErrorLogFiles = false;

        class Marker;
    };
}}


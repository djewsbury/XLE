// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Types.h"
#include "ShaderLangUtil.h"
#include "../Assets/AssetsCore.h"
#include "../Assets/DepVal.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Core/Prefix.h"
#include "../Core/Types.h"
#include <memory>
#include <vector>
#include <utility>
#include <assert.h>
#include <functional>

namespace Assets { class DependencyValidation; struct DependentFileState; }

namespace RenderCore
{
	class ILowLevelCompiler
    {
    public:
        using Payload = ::Assets::Blob;
		using ResChar = ::Assets::ResChar;

        struct CompilationFlags
        {
            enum Flags { DebugSymbols = 1<<0, DisableOptimizations = 1<<1, DynamicLinkageEnabled = 1<<2 };
            using BitField = unsigned;
        };

        struct CompilerCapability
        {
            enum Flags { Float16 = 1<<0, CompletionFunctionCompile = 1<<1 };
            using BitField = unsigned;
        };

		class ResId
        {
        public:
            ResChar     _filename[MaxPath];
            ResChar     _entryPoint[64];
            ResChar     _shaderModel[32];
            CompilationFlags::BitField _compilationFlags;

            ResId(StringSection<ResChar> filename, StringSection<ResChar> entryPoint, StringSection<ResChar> shaderModel);
            ResId();

            ShaderStage AsShaderStage() const;

        protected:
            ResId(StringSection<ResChar> initializer);
        };

		/* Represents source line number remapping (eg, during some preprocessing step) */
        struct SourceLineMarker
        {
            std::string _sourceName;
            unsigned    _sourceLine;
            unsigned    _processedSourceLine;
        };

        virtual bool DoLowLevelCompile(
            /*out*/ Payload& payload,
            /*out*/ Payload& errors,
            /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
            const void* sourceCode, size_t sourceCodeLength,
            const ResId& shaderPath,
            StringSection<::Assets::ResChar> definesTable,
            IteratorRange<const SourceLineMarker*> sourceLineMarkers = {}) const = 0;

        using CompletionFunction = std::function<void(bool success, const ::Assets::Blob& payload, const ::Assets::Blob& errors, const std::vector<::Assets::DependentFileState>& dependencies)>;
        virtual bool DoLowLevelCompile(
            CompletionFunction&& completionFunction,
            const void* sourceCode, size_t sourceCodeLength,
            const ResId& shaderPath,
            StringSection<::Assets::ResChar> definesTable,
            IteratorRange<const SourceLineMarker*> sourceLineMarkers = {}) const { return false; }

        virtual void AdaptResId(ResId&) const = 0;

        virtual std::string MakeShaderMetricsString(
            const void* byteCode, size_t byteCodeSize) const = 0;

        virtual CompilerCapability::BitField GetCapabilities() const { return 0; }
        virtual ShaderLanguage GetShaderLanguage() const = 0;

        virtual ~ILowLevelCompiler();
    };

    class IShaderSource
    {
    public:
        struct ShaderByteCodeBlob
        {
            ::Assets::Blob _payload, _errors;
            std::vector<::Assets::DependentFileState> _deps;
        };

        virtual ShaderByteCodeBlob CompileFromFile(
            const ILowLevelCompiler::ResId& resId, 
            StringSection<> definesTable) const = 0;
        
        virtual ShaderByteCodeBlob CompileFromMemory(
            StringSection<> shaderInMemory, StringSection<> entryPoint, 
            StringSection<> shaderModel, StringSection<> definesTable) const = 0;

        virtual ILowLevelCompiler::ResId MakeResId(
            StringSection<> initializer) const = 0;

        virtual std::string GenerateMetrics(
            IteratorRange<const void*> byteCodeBlob) const = 0;

        virtual ILowLevelCompiler::CompilerCapability::BitField GetCompilerCapabilities() const = 0;

        virtual ~IShaderSource();
    };

    /// <summary>Represents a chunk of compiled shader code</summary>
    /// Typically we construct CompiledShaderByteCode with either a reference
    /// to a file or a string containing high-level shader code.
    ///
    /// When loading a shader from a file, there is a special syntax for the "initializer":
    ///  * {filename}:{entry point}:{shader model}
    ///
    /// Most clients will want to use the default shader model for a given stage. To use the default
    /// shader model, use ":ps_*". This will always use a shader model that is valid for the current
    /// hardware. Normally use of an explicit shader model is only required when pre-compiling many
    /// shaders for the final game image.
    ///
    /// Also, you can disable optimization and enable debug symbols for a specific shader by appending
    /// "$" to the shader model (eg, "$ps_*"). While other methods allow controlling compilation flags
    /// universally, this allows for applying this flags to particular shaders.
    class CompiledShaderByteCode
    {
    public:
        IteratorRange<const void*>  GetByteCode() const;
        StringSection<>             GetIdentifier() const;
        
		ShaderStage		GetStage() const;
        bool            DynamicLinkingEnabled() const;
        StringSection<> GetEntryPoint() const;

        class ShaderHeader
        {
        public:
            static const unsigned Version;
            unsigned _version = Version;
            char _identifier[128];
			char _shaderModel[8];
            char _entryPoint[64];
            unsigned _dynamicLinkageEnabled = false;

			ShaderHeader() { _identifier[0] = '\0'; _shaderModel[0] = '\0'; _entryPoint[0] = '\0'; }
			ShaderHeader(StringSection<char> identifier, StringSection<char> shaderModel, StringSection<char> entryPoint, bool dynamicLinkageEnabled = false);
        };

		CompiledShaderByteCode(const ::Assets::Blob&, const ::Assets::DependencyValidation&, StringSection<::Assets::ResChar>);
		CompiledShaderByteCode();
        ~CompiledShaderByteCode();

		CompiledShaderByteCode(const CompiledShaderByteCode&) = default;
        CompiledShaderByteCode& operator=(const CompiledShaderByteCode&) = default;
        CompiledShaderByteCode(CompiledShaderByteCode&&) = default;
        CompiledShaderByteCode& operator=(CompiledShaderByteCode&&) = default;

        auto        GetDependencyValidation() const -> const ::Assets::DependencyValidation& { return _depVal; }

    private:
		::Assets::Blob			_shader;
		::Assets::DependencyValidation		_depVal;
    };

    constexpr auto GetCompileProcessType(CompiledShaderByteCode*) { return ConstHash64Legacy<'Shdr', 'Byte', 'Code'>::Value; }
}


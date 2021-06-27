// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../../Assets/AssetsCore.h"
#include "../../ShaderService.h"
#include "../../../Utility/OCUtils.h"

namespace RenderCore { class CompiledShaderByteCode; class IDevice; class ICompiledPipelineLayout; }

namespace RenderCore { namespace Metal_AppleMetal
{
    class ObjectFactory;

    class ShaderProgram
    {
    public:
        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
        uint32_t GetGUID() const { return _guid; }
        #if defined(_DEBUG)
            std::string SourceIdentifiers() const { return _sourceIdentifiers; };
        #endif

        ShaderProgram(
            ObjectFactory& factory, 
            std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
			const CompiledShaderByteCode& vs, const CompiledShaderByteCode& fs);
        ~ShaderProgram();

        /* KenD -- Metal TODO -- shader construction will need to account for shader variants and conditional compilation, possibly with function constants */
        ShaderProgram(const std::string& vertexFunctionName, const std::string& fragmentFunctionName);
        IdPtr _vf; // MTLFunction
        IdPtr _ff; // MTLFunction

    private:
        ::Assets::DependencyValidation                     _depVal;
        uint32_t                                _guid;

        #if defined(_DEBUG)
            std::string _sourceIdentifiers;
        #endif
    };

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device);
}}

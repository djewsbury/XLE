// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../../Assets/AssetsCore.h"
#include "../../ShaderService.h"
#include "ObjectFactory.h"
#include <ostream>

namespace RenderCore { class CompiledShaderByteCode; class IDevice; }

namespace RenderCore { namespace Metal_OpenGLES
{
    class ShaderProgramCapturedState;

    class ShaderProgram
    {
    public:
        using UnderlyingType = OpenGL::ShaderProgram*;
        UnderlyingType GetUnderlying() const { return _underlying.get(); }
        const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
        uint32_t GetGUID() const { return _guid; }
        #if defined(_DEBUG)
            std::string SourceIdentifiers() const { return _sourceIdentifiers; };
        #endif

        ShaderProgram(
            ObjectFactory& factory,
            const CompiledShaderByteCode& vs,
            const CompiledShaderByteCode& fs);

        ShaderProgram(	
            ObjectFactory& factory, 
            const CompiledShaderByteCode& vs,
            const CompiledShaderByteCode& gs,
            const CompiledShaderByteCode& ps,
            StreamOutputInitializers so = {});

        ~ShaderProgram();

        mutable std::shared_ptr<ShaderProgramCapturedState> _capturedState;

        friend std::ostream& operator<<(std::ostream&, const ShaderProgram&);

        // Legacy asset based API --
		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&&,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable = {});

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&&,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&&,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> hsName,
			StringSection<::Assets::ResChar> dsName,
			StringSection<::Assets::ResChar> definesTable);
    private:
        intrusive_ptr<OpenGL::ShaderProgram>    _underlying;
        ::Assets::DependencyValidation                     _depVal;
        uint32_t                                _guid;

        #if defined(_DEBUG)
            std::string _sourceIdentifiers;
        #endif
    };

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device);
}}

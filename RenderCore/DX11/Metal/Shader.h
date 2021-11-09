// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DX11.h"
#include "../../Types.h"
#include "../../ShaderService.h"
#include "../../Assets/AssetsCore.h"
#include "../../../Utility/IntrusivePtr.h"
#include <memory>

namespace Assets { class DependencyValidation; }
namespace RenderCore { class InputElementDesc; class CompiledShaderByteCode; class IDevice; }

namespace RenderCore { namespace Metal_DX11
{
	class ObjectFactory;
	class DeviceContext;
	class BoundClassInterfaces;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ///
    /// <summary>A set of shaders for each stage of the shader pipeline</summary>
    ///
    /// A ShaderProgram contains a set of shaders, each of which will be bound to the pipeline
    /// at the same time. Most of the time, it will contain just a vertex shader and a pixel shader.
    /// 
    /// In DirectX, it's easy to manage shaders for different stages of the pipeline separately. But
    /// OpenGL prefers to combine all of the shaders together into one shader program object.
    ///
    ///     The DirectX11 shader pipeline looks like this:
    ///         <list>
    ///             <item>Vertex Shader</item>
    ///             <item>Hull Shader</item>
    ///             <item>(fixed function tessellator)</item>
    ///             <item>Domain shader</item>
    ///             <item>Geometry shader</item>
    ///             <item>Pixel Shader</item>
    ///         </list>
    ///
    /// Normally ShaderProgram objects are created and managed using the "Assets::MakeAsset"
    /// functions.
    /// For example:
    /// <code>\code
    ///     auto shader = Assets::MakeAsset<ShaderProgram>("shaders/basic.vertex.hlsl:main:vs_*", "shaders/basic.pixel.hlsl:main:ps_*");
    /// \endcode</code>
    ///
    /// But it's also possible to construct a ShaderProgram as a basic object. 
    /// <code>\code
    ///     ShaderProgram myShaderProgram("shaders/basic.vertex.hlsl:main:vs_*", "shaders/basic.pixel.hlsl:main:ps_*");
    /// \endcode</code>
    ///
    /// Note that the parameters ":main:vs_*" specify the entry point and shader compilation model.
    ///     <list>
    ///         <item>":main" -- this is the entry point function. That is, there is a function in
    ///                 the shader file called "main." It will take the shader input parameters and 
    ///                 return the shader output values.</item>
    ///         <item>":vs_*" -- this is the compilation model. "vs" means vertex shader. The "_*"
    ///                 means to use the default compilation model for the current device. But it can 
    ///                 be replaced with an appropriate compilation model 
    ///                     (eg, vs_5_0, vs_4_0_level_9_1, ps_3_0, etc)
    ///                 Normally it's best to use the default compilation model.</item>
    ///     </list>
    ///
    class ShaderProgram
    {
    public:
        ShaderProgram(	ObjectFactory& factory,
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& ps);
        
        ShaderProgram(	ObjectFactory& factory, 
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& gs,
						const CompiledShaderByteCode& ps,
						StreamOutputInitializers so = {});

		ShaderProgram(	ObjectFactory& factory, 
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& gs,
						const CompiledShaderByteCode& ps,
						const CompiledShaderByteCode& hs,
						const CompiledShaderByteCode& ds,
						StreamOutputInitializers so = {});

		ShaderProgram();
        ~ShaderProgram();

		void Apply(DeviceContext&) const;
		void Apply(DeviceContext&, const BoundClassInterfaces&) const;

		const CompiledShaderByteCode&       GetCompiledCode(ShaderStage stage) const;
		ID3D::ClassLinkage*					GetClassLinkage(ShaderStage stage) const;

        bool DynamicLinkingEnabled() const;

		uint64_t GetGUID() const { return _guid; }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _validationCallback; }

		ShaderProgram(ShaderProgram&&) never_throws = default;
        ShaderProgram& operator=(ShaderProgram&&) never_throws = default;

		ShaderProgram(const ShaderProgram&) = default;
        ShaderProgram& operator=(const ShaderProgram&) = default;

		// Legacy asset based API --
		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&& promise,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable = {});

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&& promise,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ShaderProgram>>&& promise,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> hsName,
			StringSection<::Assets::ResChar> dsName,
			StringSection<::Assets::ResChar> definesTable);

    protected:
		intrusive_ptr<ID3D::VertexShader>		_vertexShader;
		intrusive_ptr<ID3D::PixelShader>		_pixelShader;
		intrusive_ptr<ID3D::GeometryShader>		_geometryShader;
		intrusive_ptr<ID3D::HullShader>			_hullShader;
		intrusive_ptr<ID3D::DomainShader>		_domainShader;

		CompiledShaderByteCode _compiledCode[(unsigned)ShaderStage::Max];
		intrusive_ptr<ID3D::ClassLinkage> _classLinkage[(unsigned)ShaderStage::Max];
		::Assets::DependencyValidation   _validationCallback;

		uint64_t _guid;
    };

    ///
    /// <summary>A shader program with tessellation support</summary>
    ///
    /// A DeepShaderProgram is the same as ShaderProgram, but with tessellation support. Most
    /// ShaderPrograms don't need tessellation shaders, so for simplicity they are excluded
    /// from the default ShaderProgram object.
    ///
	using DeepShaderProgram = ShaderProgram;

        ////////////////////////////////////////////////////////////////////////////////////////////////

	class ComputeShader
	{
	public:
		ComputeShader(ObjectFactory& factory, const CompiledShaderByteCode& byteCode);
		ComputeShader();
		~ComputeShader();

		typedef ID3D::ComputeShader*    UnderlyingType;
		UnderlyingType                  GetUnderlying() const { return _underlying.get(); }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _validationCallback; }

		const CompiledShaderByteCode&       GetCompiledCode() const { return _compiledCode; }

		// Legacy asset based API --
		static void ConstructToPromise(
			std::promise<std::shared_ptr<ComputeShader>>&&,
			StringSection<::Assets::ResChar> codeName,
			StringSection<::Assets::ResChar> definesTable = {});

	private:
		intrusive_ptr<ID3D::ComputeShader>      _underlying;
		::Assets::DependencyValidation			_validationCallback;
		CompiledShaderByteCode					_compiledCode;
	};

		////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device, D3D_FEATURE_LEVEL featureLevel);
    intrusive_ptr<ID3D::ShaderReflection>  CreateReflection(const CompiledShaderByteCode& shaderCode);
}}


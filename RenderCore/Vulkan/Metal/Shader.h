// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../Types.h"
#include "../../ShaderService.h"
#include "../../../Assets/AssetsCore.h"

namespace RenderCore { 
	class CompiledShaderByteCode; class IDevice; 
	class ICompiledPipelineLayout; class LegacyRegisterBindingDesc; 
	class VulkanCompilerConfiguration;
}

namespace RenderCore { namespace Metal_Vulkan
{
	class ObjectFactory;
	class BoundClassInterfaces;
	class GraphicsPipelineBuilder;
	class DescriptorSetSignatureFile;
	class CompiledPipelineLayout;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class ShaderProgram
    {
    public:
        ShaderProgram(	ObjectFactory& factory,
						const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& ps);
        
        ShaderProgram(	ObjectFactory& factory, 
						const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& gs,
						const CompiledShaderByteCode& ps,
						StreamOutputInitializers so = {});

		ShaderProgram(	ObjectFactory& factory,
						const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
						const CompiledShaderByteCode& vs,
						const CompiledShaderByteCode& gs,
						const CompiledShaderByteCode& ps,
						const CompiledShaderByteCode& hs,
						const CompiledShaderByteCode& ds,
						StreamOutputInitializers so = {});

		ShaderProgram();
        ~ShaderProgram();

		const CompiledShaderByteCode&				GetCompiledCode(ShaderStage stage) const	{ assert(unsigned(stage) < dimof(_compiledCode)); return _compiledCode[(unsigned)stage]; }
		const VulkanSharedPtr<VkShaderModule>&		GetModule(ShaderStage stage) const			{ assert(unsigned(stage) < dimof(_modules)); return _modules[(unsigned)stage]; }
		const CompiledPipelineLayout&				GetPipelineLayout() const					{ return *_pipelineLayout; }
		static const unsigned s_maxShaderStages = 5;

        bool DynamicLinkingEnabled() const;

		// void Apply(GraphicsPipelineBuilder& pipeline) const;
		// void Apply(GraphicsPipelineBuilder& pipeline, const BoundClassInterfaces&) const;

		const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const { return _validationCallback; }

		ShaderProgram(ShaderProgram&&) = default;
        ShaderProgram& operator=(ShaderProgram&&) = default;
		ShaderProgram(const ShaderProgram&) = default;
        ShaderProgram& operator=(const ShaderProgram&) = default;

		// Legacy asset based API --
		static void ConstructToFuture(
			::Assets::AssetFuture<ShaderProgram>&,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable = {});

		static void ConstructToFuture(
			::Assets::AssetFuture<ShaderProgram>&,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> definesTable);

		static void ConstructToFuture(
			::Assets::AssetFuture<ShaderProgram>&,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<::Assets::ResChar> vsName,
			StringSection<::Assets::ResChar> gsName,
			StringSection<::Assets::ResChar> psName,
			StringSection<::Assets::ResChar> hsName,
			StringSection<::Assets::ResChar> dsName,
			StringSection<::Assets::ResChar> definesTable);

		// std::shared_ptr<PartialPipelineDescriptorsLayout> _pipelineLayoutConfig;

    protected:
		CompiledShaderByteCode _compiledCode[s_maxShaderStages];
		VulkanSharedPtr<VkShaderModule> _modules[s_maxShaderStages];
        std::shared_ptr<::Assets::DependencyValidation>   _validationCallback;
		std::shared_ptr<CompiledPipelineLayout> _pipelineLayout;
    };

	using DeepShaderProgram = ShaderProgram;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class ComputeShader
    {
    public:
		const VulkanSharedPtr<VkShaderModule>&	GetModule() const { return _module; }
		const CompiledShaderByteCode& GetCompiledShaderByteCode() const { return _compiledCode; }
		const CompiledPipelineLayout& GetPipelineLayout() const { return *_pipelineLayout; }

        ComputeShader(
			ObjectFactory& factory,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const CompiledShaderByteCode& byteCode);
        ComputeShader();
        ~ComputeShader();

		ComputeShader& operator=(ComputeShader&& moveFrom) = default;
		ComputeShader(ComputeShader&& moveFrom) = default;
		ComputeShader& operator=(const ComputeShader&) = default;
		ComputeShader(const ComputeShader&) = default;

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const     { return _validationCallback; }

		// Legacy asset based API --
		static void ConstructToFuture(
			::Assets::AssetFuture<ComputeShader>&,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			StringSection<::Assets::ResChar> codeName,
			StringSection<::Assets::ResChar> definesTable = {});

		// std::shared_ptr<PartialPipelineDescriptorsLayout> _pipelineLayoutConfig;

    private:
        std::shared_ptr<::Assets::DependencyValidation>		_validationCallback;
		VulkanSharedPtr<VkShaderModule>						_module;
		CompiledShaderByteCode								_compiledCode;
		std::shared_ptr<CompiledPipelineLayout> 			_pipelineLayout;
    };

	namespace Internal
	{
		using VkShaderStageFlags_ = unsigned;
		VkShaderStageFlags_ AsVkShaderStageFlags(ShaderStage input);
	}

        ////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device, const VulkanCompilerConfiguration& cfg);
}}
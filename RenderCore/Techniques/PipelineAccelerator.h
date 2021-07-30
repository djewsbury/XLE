// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Types.h"
#include "../StateDesc.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include <memory>

namespace RenderCore 
{
	class FrameBufferDesc;
	class InputElementDesc;
	class MiniInputElementDesc;
}

namespace RenderCore { namespace Assets { class RenderStateSet; class ShaderPatchCollection; class PredefinedDescriptorSetLayout; } }
namespace RenderCore { class IDevice; class ICompiledPipelineLayout; class IDescriptorSet; }

namespace RenderCore { namespace Techniques
{
	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class DescriptorSetBindingInfo;
	class SequencerConfig;
	class ITechniqueDelegate;
	class DescriptorSetLayoutAndBinding;
	class ActualizedDescriptorSet;
	class CompiledPipelineLayoutAsset;

	// Switching this to a virtual interface style class in order to better support multiple DLLs/modules
	// For many objects like the SimpleModelRenderer, the pipeline accelerator pools is one of the main
	// interfaces for interacting with render states and shaders. By making this an interface class, it
	// allows us to keep the implementation for the pool in the main host module, even if DLLs have their
	// own SimpleModelRenderer, etc
	class IPipelineAcceleratorPool
	{
	public:
		virtual std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const InputElementDesc*> inputAssembly,
			RenderCore::Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) = 0;

		virtual std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			RenderCore::Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) = 0;

		virtual std::shared_ptr<DescriptorSetAccelerator> CreateDescriptorSetAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			const Utility::ParameterBox& constantBindings,
			const Utility::ParameterBox& resourceBindings,
			IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings = {}) = 0;

		virtual std::shared_ptr<SequencerConfig> CreateSequencerConfig(
			std::shared_ptr<ITechniqueDelegate> delegate,
			::Assets::PtrToFuturePtr<CompiledPipelineLayoutAsset> pipelineLayout,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex = 0) = 0;

		class Pipeline;

		virtual const ::Assets::PtrToFuturePtr<Pipeline>& GetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const = 0;
		virtual Pipeline* TryGetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const = 0;

		virtual const std::shared_ptr<::Assets::Future<ActualizedDescriptorSet>>& GetDescriptorSet(DescriptorSetAccelerator& accelerator) const = 0;
		virtual const ActualizedDescriptorSet* TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const = 0;

		virtual void	SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) = 0;
		T1(Type) void   SetGlobalSelector(StringSection<> name, Type value);
		virtual void	RemoveGlobalSelector(StringSection<> name) = 0;

		virtual void	RebuildAllOutOfDatePipelines() = 0;
		virtual const std::shared_ptr<IDevice>& GetDevice() const = 0;

		// virtual const DescriptorSetLayoutAndBinding& GetMaterialDescriptorSetLayout() const = 0;
		// virtual const DescriptorSetLayoutAndBinding& GetSequencerDescriptorSetLayout() const = 0;
		// virtual const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() const = 0;

		virtual ~IPipelineAcceleratorPool();

		unsigned GetGUID() const { return _guid; }
	protected:
		unsigned _guid;
		std::shared_ptr<IDevice> _device;
	};

	T1(Type) inline void   IPipelineAcceleratorPool::SetGlobalSelector(StringSection<> name, Type value)
	{
		const auto insertType = ImpliedTyping::TypeOf<Type>();
        auto size = insertType.GetSize();
        assert(size == sizeof(Type)); (void)size;
        SetGlobalSelector(name, MakeOpaqueIteratorRange(value), insertType);
	}

	// namespace Internal { const DescriptorSetLayoutAndBinding& GetDefaultDescriptorSetLayoutAndBinding(); }

	namespace PipelineAcceleratorPoolFlags
	{
		enum Flags { RecordDescriptorSetBindingInfo = 1<<0 };
		using BitField = unsigned;
	}

	std::shared_ptr<IPipelineAcceleratorPool> CreatePipelineAcceleratorPool(
		const std::shared_ptr<IDevice>&,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		PipelineAcceleratorPoolFlags::BitField flags = 0);

		// const DescriptorSetLayoutAndBinding& matDescSetLayout = Internal::GetDefaultDescriptorSetLayoutAndBinding(),
		// const DescriptorSetLayoutAndBinding& sequencerDescSetLayout = Internal::GetDefaultDescriptorSetLayoutAndBinding());
}}


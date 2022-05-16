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
	class ICompiledPipelineLayout;
}

namespace RenderCore { namespace Assets { class RenderStateSet; class ShaderPatchCollection; class PredefinedDescriptorSetLayout; class ScaffoldCmdIterator; } }
namespace RenderCore { class IDevice; class IDescriptorSet; class UniformsStreamInterface; class IResourceView; }

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
	class DeformerToDescriptorSetBinding;
	class ICompiledLayoutPool;
	class IDrawablesPool;

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
			IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
			std::shared_ptr<void> memoryHolder,								// retained while we need access to materialMachine
			const std::shared_ptr<DeformerToDescriptorSetBinding>& deformBinding = {}) = 0;

		virtual std::shared_ptr<SequencerConfig> CreateSequencerConfig(
			const std::string& name,
			std::shared_ptr<ITechniqueDelegate> delegate,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex = 0) = 0;

		class Pipeline;

		virtual const Pipeline* TryGetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const = 0;
		virtual const ActualizedDescriptorSet* TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const = 0;
		virtual std::shared_ptr<ICompiledPipelineLayout> TryGetCompiledPipelineLayout(const SequencerConfig& sequencerConfig) const = 0;

		// These "Get...Marker" functions are less optimal than the "TryGet..." ones, and should not be called frequently
		// They are find to use during construction tasks (eg, stalling until a pipeline is ready, etc), but during the render
		// loop use the "TryGet..." versions
		virtual std::shared_ptr<::Assets::Marker<Pipeline>> GetPipelineMarker(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const = 0;
		virtual std::shared_ptr<::Assets::Marker<ActualizedDescriptorSet>> GetDescriptorSetMarker(DescriptorSetAccelerator& accelerator) const = 0;
		virtual ::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> GetCompiledPipelineLayoutMarker(const SequencerConfig& sequencerConfig) const = 0;

		virtual void	SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) = 0;
		T1(Type) void   SetGlobalSelector(StringSection<> name, Type value);
		virtual void	RemoveGlobalSelector(StringSection<> name) = 0;

		virtual void	RebuildAllOutOfDatePipelines() = 0;
		virtual const std::shared_ptr<IDevice>& GetDevice() const = 0;
		virtual const std::shared_ptr<ICompiledLayoutPool>& GetCompiledLayoutPool() const = 0;

		virtual void 	LockForReading() const = 0;
		virtual void 	UnlockForReading() const = 0;

		struct Records;
		virtual Records LogRecords() const = 0;

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

	struct PipelineAcceleratorRecord
	{
		uint64_t _shaderPatchesHash = 0ull;
		std::string _materialSelectors, _geoSelectors;
		uint64_t _inputAssemblyHash = 0ull;
		Topology _topology;
		uint64_t _stateSetHash = 0ull;
	};

	struct SequencerConfigRecord
	{
		std::string _name;
		std::string _sequencerSelectors;
		uint64_t _fbRelevanceValue = 0ull;
	};

	struct IPipelineAcceleratorPool::Records
	{
		std::vector<PipelineAcceleratorRecord> _pipelineAccelerators;
		std::vector<SequencerConfigRecord> _sequencerConfigs;
		size_t _descriptorSetAcceleratorCount = 0;
		size_t _metalPipelineCount = 0;
	};

	namespace PipelineAcceleratorPoolFlags
	{
		enum Flags { RecordDescriptorSetBindingInfo = 1<<0 };
		using BitField = unsigned;
	}

	std::shared_ptr<IPipelineAcceleratorPool> CreatePipelineAcceleratorPool(
		const std::shared_ptr<IDevice>&,
		const std::shared_ptr<IDrawablesPool>&,
		const std::shared_ptr<ICompiledLayoutPool>&,
		PipelineAcceleratorPoolFlags::BitField flags = 0);
}}


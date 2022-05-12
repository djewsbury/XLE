// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineCollection.h"
#include "TechniqueDelegates.h"
#include "TechniqueUtils.h"		// (for GetDefaultShaderLanguage())
#include "../../Assets/AssetsCore.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore 
{
	class IThreadContext;
	class UniformsStream;
	class UniformsStreamInterface;
	class IDescriptorSet;

	namespace Assets { class PredefinedPipelineLayout; }
}

namespace RenderCore { namespace Techniques
{
	class ParsingContext;

	class IShaderOperator
	{
	public:
		virtual void Draw(ParsingContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual void Draw(IThreadContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const = 0;
		virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;
		virtual ~IShaderOperator();
	};
	
	class IComputeShaderOperator
	{
	public:
		virtual void Dispatch(ParsingContext&, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual void Dispatch(IThreadContext&, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;

		virtual void BeginDispatches(ParsingContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}, uint64_t pushConstantsBinding = 0) = 0;
		virtual void BeginDispatches(IThreadContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}, uint64_t pushConstantsBinding = 0) = 0;
		virtual void EndDispatches() = 0;
		virtual void Dispatch(unsigned countX, unsigned countY, unsigned countZ, IteratorRange<const void*> pushConstants = {}) = 0;
		virtual void DispatchIndirect(const IResource& indirectArgsBuffer, unsigned offset = 0, IteratorRange<const void*> pushConstants = {}) = 0;
		
		virtual const Assets::PredefinedPipelineLayout& GetPredefinedPipelineLayout() const = 0;
		
		virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;
		virtual ~IComputeShaderOperator();
	};

	class RenderPassInstance;

	enum class FullViewportOperatorSubType { DisableDepth, MaxDepth };

	struct PixelOutputStates
	{
		const FrameBufferDesc* _fbDesc;
		unsigned _subpassIdx = ~0u;
		DepthStencilDesc _depthStencilState;
		RasterizationDesc _rasterizationState;
		IteratorRange<const AttachmentBlendDesc*> _attachmentBlendStates;

		uint64_t GetHash() const;

		void Bind(const FrameBufferDesc& fbDesc, unsigned subpassIdx);
		void Bind(const RenderPassInstance&);
		void Bind(const DepthStencilDesc& depthStencilState);
		void Bind(const RasterizationDesc& rasterizationState);
		void Bind(IteratorRange<const AttachmentBlendDesc*> blendStates);
	};

	::Assets::PtrToMarkerPtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const PixelOutputStates& fbTarget,
		const UniformsStreamInterface& usi);

	::Assets::PtrToMarkerPtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		FullViewportOperatorSubType subType,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAsset,
		const PixelOutputStates& fbTarget,
		const UniformsStreamInterface& usi);

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi);

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAsset,
		const UniformsStreamInterface& usi);

	::Assets::PtrToMarkerPtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelineCollection>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi);

	class DescriptorSetLayoutAndBinding;

	class CompiledPipelineLayoutAsset
	{
	public:
		const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() const { return _pipelineLayout; }
		const std::shared_ptr<Assets::PredefinedPipelineLayout>& GetPredefinedPipelineLayout() const { return _predefinedLayout; }
		const ::Assets::DependencyValidation GetDependencyValidation() const;

		CompiledPipelineLayoutAsset(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<Assets::PredefinedPipelineLayout> predefinedLayout,
			std::shared_ptr<DescriptorSetLayoutAndBinding> patchInDescSet = nullptr,
			ShaderLanguage shaderLanguage = Techniques::GetDefaultShaderLanguage());
		CompiledPipelineLayoutAsset() = default;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<CompiledPipelineLayoutAsset>>&& promise,
			const std::shared_ptr<IDevice>& device,
			StringSection<> srcFile,
			const std::shared_ptr<DescriptorSetLayoutAndBinding>& patchInDescSet = nullptr,
			ShaderLanguage shaderLanguage = GetDefaultShaderLanguage());

	protected:
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedLayout;
	};

	class DescriptorSetLayoutAndBinding
	{
	public:
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& GetLayout() const { return _layout; }
		unsigned GetSlotIndex() const { return _slotIdx; }
		PipelineType GetPipelineType() const { return _pipelineType; }
		const std::string& GetName() const { return _name; }

		uint64_t GetHash() const { return _hash; }
		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		DescriptorSetLayoutAndBinding(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& layout,
			unsigned slotIdx,
			const std::string& name,
			PipelineType pipelineType,
			::Assets::DependencyValidation depVal);
		DescriptorSetLayoutAndBinding();
		~DescriptorSetLayoutAndBinding();

	private:
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _layout;
		unsigned _slotIdx;
		uint64_t _hash;
		std::string _name;
		PipelineType _pipelineType = PipelineType::Graphics;
		::Assets::DependencyValidation _depVal;
	};

	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayoutFile&, const std::string& pipelineLayoutName, const std::string& descSetName, PipelineType pipelineType);
	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayout& file, const std::string& descriptorSetName, PipelineType pipelineType);
}}

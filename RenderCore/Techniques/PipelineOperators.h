// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineCollection.h"
#include "TechniqueDelegates.h"
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
}}

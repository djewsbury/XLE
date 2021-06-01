// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Forward.h"
#include "State.h"
#include "PipelineLayout.h"		// for PipelineDescriptorsLayoutBuilder
#include "Shader.h"
#include "VulkanCore.h"
#include "../../ResourceList.h"
#include "../../ResourceDesc.h"
#include "../../FrameBufferDesc.h"
#include "../../IDevice_Forward.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <sstream>

namespace RenderCore { class VertexBufferView; class IndexBufferView; class ICompiledPipelineLayout; }

namespace RenderCore { namespace Metal_Vulkan
{
	static const unsigned s_maxBoundVBs = 4;

	namespace Internal { class CaptureForBindRecords; }

	class GlobalPools;
	class FrameBuffer;
	class PipelineLayout;
	class CommandPool;
	class DescriptorPool;
	class DummyResources;
	enum class CommandBufferType;
	class TemporaryBufferSpace;
	class Resource;

	class GraphicsPipeline : public VulkanUniquePtr<VkPipeline>
	{
	public:
		uint64_t GetInterfaceBindingGUID() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _shader.GetDependencyValidation(); }

		// --------------- Vulkan specific interface --------------- 
		ShaderProgram _shader;

		GraphicsPipeline(VulkanUniquePtr<VkPipeline>&&);
		~GraphicsPipeline();
	};

	class ComputePipeline : public VulkanUniquePtr<VkPipeline>
	{
	public:
		uint64_t GetInterfaceBindingGUID() const;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _shader.GetDependencyValidation(); }

		// --------------- Vulkan specific interface --------------- 
		ComputeShader _shader;

		ComputePipeline(VulkanUniquePtr<VkPipeline>&&);
		~ComputePipeline();
	};

	class GraphicsPipelineBuilder
	{
	public:
		// --------------- Cross-GFX-API interface --------------- 
		void        Bind(const ShaderProgram& shaderProgram);
		
		void        Bind(IteratorRange<const AttachmentBlendDesc*> blendStates);
		void        Bind(const DepthStencilDesc& depthStencilState);
		void        Bind(const RasterizationDesc& rasterizer);

		void 		Bind(const BoundInputLayout& inputLayout, Topology topology);
		void		UnbindInputLayout();

		void 		SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass);
		uint64_t 	GetRenderPassConfigurationHash() const { return _renderPassConfigurationHash; }

		static uint64_t CalculateFrameBufferRelevance(const FrameBufferDesc& fbDesc, unsigned subPass = 0);

		std::shared_ptr<GraphicsPipeline> CreatePipeline(ObjectFactory& factory);

		GraphicsPipelineBuilder();
		~GraphicsPipelineBuilder();

		// --------------- Vulkan specific interface --------------- 

		std::shared_ptr<GraphicsPipeline> CreatePipeline(
			ObjectFactory& factory, VkPipelineCache pipelineCache,
			VkRenderPass renderPass, unsigned subpass, 
			TextureSamples samples);
		bool IsPipelineStale() const { return _pipelineStale; }

	private:
		Internal::VulkanRasterizerState		_rasterizerState;
		Internal::VulkanBlendState			_blendState;
		Internal::VulkanDepthStencilState	_depthStencilState;
		VkPrimitiveTopology     			_topology;

		std::vector<VkVertexInputAttributeDescription> _iaAttributes;
		std::vector<VkVertexInputBindingDescription> _iaVBBindings;
		uint64_t _iaHash;

		const ShaderProgram*    _shaderProgram;

		bool                    _pipelineStale;

		uint64_t 				_renderPassConfigurationHash = 0;
		VulkanSharedPtr<VkRenderPass> 	_currentRenderPass;
		unsigned						_currentSubpassIndex = ~0u;
		TextureSamples					_currentTextureSamples = TextureSamples::Create(0);

	protected:
		GraphicsPipelineBuilder(const GraphicsPipelineBuilder&);
		GraphicsPipelineBuilder& operator=(const GraphicsPipelineBuilder&);

		const ShaderProgram* GetBoundShaderProgram() const { return _shaderProgram; }
	};

	class ComputePipelineBuilder
	{
	public:
		// --------------- Cross-GFX-API interface --------------- 
		void        Bind(const ComputeShader& shader);

		ComputePipelineBuilder();
		~ComputePipelineBuilder();

		// --------------- Vulkan specific interface --------------- 
		std::shared_ptr<ComputePipeline> CreatePipeline(
			ObjectFactory& factory, VkPipelineCache pipelineCache);
		bool IsPipelineStale() const { return _pipelineStale; }

		ComputePipelineBuilder(const ComputePipelineBuilder&);
		ComputePipelineBuilder& operator=(const ComputePipelineBuilder&);

	private:
		const ComputeShader*    _shader;
		bool                    _pipelineStale;

	protected:
		const ComputeShader* GetBoundComputeShader() const { return _shader; }
	};

	class CommandList
	{
	public:
		// --------------- Vulkan specific interface --------------- 
		void UpdateBuffer(
			VkBuffer buffer, VkDeviceSize offset, 
			VkDeviceSize byteCount, const void* data);
		void BindDescriptorSets(
			VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout layout,
			uint32_t firstSet,
			uint32_t descriptorSetCount,
			const VkDescriptorSet* pDescriptorSets,
			uint32_t dynamicOffsetCount,
			const uint32_t* pDynamicOffsets);
		void CopyBuffer(
			VkBuffer srcBuffer,
			VkBuffer dstBuffer,
			uint32_t regionCount,
			const VkBufferCopy* pRegions);
		void CopyImage(
			VkImage srcImage,
			VkImageLayout srcImageLayout,
			VkImage dstImage,
			VkImageLayout dstImageLayout,
			uint32_t regionCount,
			const VkImageCopy* pRegions);
		void CopyBufferToImage(
			VkBuffer srcBuffer,
			VkImage dstImage,
			VkImageLayout dstImageLayout,
			uint32_t regionCount,
			const VkBufferImageCopy* pRegions);
		void CopyImageToBuffer(
			VkImage srcImage,
			VkImageLayout srcImageLayout,
			VkBuffer dstBuffer,
			uint32_t regionCount,
			const VkBufferImageCopy* pRegions);
		void ClearColorImage(
			VkImage image,
			VkImageLayout imageLayout,
			const VkClearColorValue* pColor,
			uint32_t rangeCount,
			const VkImageSubresourceRange* pRanges);
		void ClearDepthStencilImage(
			VkImage image,
			VkImageLayout imageLayout,
			const VkClearDepthStencilValue* pDepthStencil,
			uint32_t rangeCount,
			const VkImageSubresourceRange* pRanges);
		void PipelineBarrier(
			VkPipelineStageFlags            srcStageMask,
			VkPipelineStageFlags            dstStageMask,
			VkDependencyFlags               dependencyFlags,
			uint32_t                        memoryBarrierCount,
			const VkMemoryBarrier*          pMemoryBarriers,
			uint32_t                        bufferMemoryBarrierCount,
			const VkBufferMemoryBarrier*    pBufferMemoryBarriers,
			uint32_t                        imageMemoryBarrierCount,
			const VkImageMemoryBarrier*     pImageMemoryBarriers);
		void PushConstants(
			VkPipelineLayout layout,
			VkShaderStageFlags stageFlags,
			uint32_t offset,
			uint32_t size,
			const void* pValues);
		void WriteTimestamp(
			VkPipelineStageFlagBits pipelineStage, 
			VkQueryPool queryPool, uint32_t query);
		void BeginQuery(VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags = 0);
		void EndQuery(VkQueryPool queryPool, uint32_t query);
		void ResetQueryPool(
			VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
		void SetEvent(VkEvent evnt, VkPipelineStageFlags stageMask);
		void ResetEvent(VkEvent evnt, VkPipelineStageFlags stageMask);

		const VulkanSharedPtr<VkCommandBuffer>& GetUnderlying() const { return _underlying; }

		CommandList();
		explicit CommandList(const VulkanSharedPtr<VkCommandBuffer>& underlying);
		~CommandList();

		CommandList(CommandList&&) = default;
		CommandList& operator=(CommandList&&) = default;
	private:
		VulkanSharedPtr<VkCommandBuffer> _underlying;

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			std::vector<uint64_t> _resourcesBecomingVisible;
			std::vector<uint64_t> _resourcesThatMustBeVisible;
		#endif

		friend class DeviceContext;
	};

	class VulkanEncoderSharedState;
	class NumericUniformsInterface;

	class GraphicsEncoder
	{
	public:
		//	------ Non-pipeline states (that can be changed mid-render pass) -------
		void        Bind(IteratorRange<const VertexBufferView*> vbViews, const IndexBufferView& ibView);
		void		SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef);
		void		SetDepthBounds(float minDepthValue, float maxDepthValue);		// the 0-1 value stored in the depth buffer is compared directly to these bounds
		void 		Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects);

		const std::shared_ptr<CompiledPipelineLayout>& GetPipelineLayout() { return _pipelineLayout; }

		NumericUniformsInterface	BeginNumericUniformsInterface();

		// --------------- Vulkan specific interface --------------- 
		void		BindDescriptorSet(unsigned index, VkDescriptorSet set VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description));
		void		PushConstants(VkShaderStageFlags stageFlags, unsigned offset, IteratorRange<const void*> data);

	protected:
		enum class Type { Normal, StreamOutput };
		GraphicsEncoder(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout = nullptr,
			std::shared_ptr<VulkanEncoderSharedState> sharedState = nullptr,
			Type type = Type::Normal);
		~GraphicsEncoder();
		GraphicsEncoder(GraphicsEncoder&&);		// (hide these to avoid slicing in derived types)
		GraphicsEncoder& operator=(GraphicsEncoder&&);

		VkPipelineLayout GetUnderlyingPipelineLayout();
		unsigned GetDescriptorSetCount();

		std::shared_ptr<CompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<VulkanEncoderSharedState> _sharedState;
		Type _type;

		friend class DeviceContext;
	};
	
	class GraphicsEncoder_ProgressivePipeline : public GraphicsEncoder, public GraphicsPipelineBuilder
	{
	public:
		//	------ Draw & Clear -------
		void        Draw(unsigned vertexCount, unsigned startVertexLocation=0);
		void        DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0);
		void    	DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
		void    	DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0);
		void        DrawAuto();

		using GraphicsEncoder::Bind;
		using GraphicsPipelineBuilder::Bind;
		void        Bind(const ShaderProgram& shaderProgram);

		GraphicsEncoder_ProgressivePipeline(GraphicsEncoder_ProgressivePipeline&&);
		GraphicsEncoder_ProgressivePipeline& operator=(GraphicsEncoder_ProgressivePipeline&&);
		GraphicsEncoder_ProgressivePipeline();
		~GraphicsEncoder_ProgressivePipeline();
	protected:
		GraphicsEncoder_ProgressivePipeline(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
			std::shared_ptr<VulkanEncoderSharedState> sharedState,
			ObjectFactory& objectFactory,
			GlobalPools& globalPools,
			Type type = Type::Normal);
	
		bool 		BindGraphicsPipeline();
		std::shared_ptr<GraphicsPipeline>	_currentGraphicsPipeline;
		ObjectFactory*						_factory;
		GlobalPools*                        _globalPools;
		void LogPipeline();

		friend class DeviceContext;
	};

	class GraphicsEncoder_Optimized : public GraphicsEncoder
	{
	public:
		//	------ Draw & Clear -------
		void        Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation=0);
		void        DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation=0);
		void    	DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0);
		void    	DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0);
		void        DrawAuto(const GraphicsPipeline& pipeline);

		GraphicsEncoder_Optimized(GraphicsEncoder_Optimized&&);
		GraphicsEncoder_Optimized& operator=(GraphicsEncoder_Optimized&&);
		GraphicsEncoder_Optimized();
		~GraphicsEncoder_Optimized();
	protected:
		GraphicsEncoder_Optimized(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
			std::shared_ptr<VulkanEncoderSharedState> sharedState,
			Type type = Type::Normal);

		void LogPipeline(const GraphicsPipeline& pipeline);

		friend class DeviceContext;
	};

	class ComputeEncoder_ProgressivePipeline : public ComputePipelineBuilder
	{
	public:
		void        Dispatch(unsigned countX, unsigned countY=1, unsigned countZ=1);

		// --------------- Vulkan specific interface --------------- 
		void		BindDescriptorSet(unsigned index, VkDescriptorSet set VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description));

		~ComputeEncoder_ProgressivePipeline();
	protected:
		bool 		BindComputePipeline();
		std::shared_ptr<CompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<VulkanEncoderSharedState> _sharedState;
		std::shared_ptr<ComputePipeline>	_currentComputePipeline;
		ObjectFactory*						_factory;
		GlobalPools*                        _globalPools;

		VkPipelineLayout GetUnderlyingPipelineLayout();
		unsigned GetDescriptorSetCount();
		void LogPipeline();

		ComputeEncoder_ProgressivePipeline(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
			std::shared_ptr<VulkanEncoderSharedState> sharedState,
			ObjectFactory& objectFactory,
			GlobalPools& globalPools);

		friend class DeviceContext;
	};

	class BlitEncoder;

	class DeviceContext
	{
	public:
		// --------------- Cross-GFX-API interface --------------- 

		void BeginRenderPass(
			FrameBuffer& frameBuffer,
			IteratorRange<const ClearValue*> clearValues = {});
		void BeginNextSubpass(FrameBuffer& frameBuffer);
		void EndRenderPass();
		unsigned GetCurrentSubpassIndex() const;

		GraphicsEncoder_Optimized BeginGraphicsEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		GraphicsEncoder_ProgressivePipeline BeginGraphicsEncoder_ProgressivePipeline(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		ComputeEncoder_ProgressivePipeline BeginComputeEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		GraphicsEncoder_Optimized BeginStreamOutputEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout, IteratorRange<const VertexBufferView*> outputBuffers);
		BlitEncoder BeginBlitEncoder();

		void Clear(const IResourceView& renderTarget, const VectorPattern<float,4>& clearColour);
		void Clear(const IResourceView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil);
		void ClearUInt(const IResourceView& unorderedAccess, const VectorPattern<unsigned,4>& clearColour);
		void ClearFloat(const IResourceView& unorderedAccess, const VectorPattern<float,4>& clearColour);
		void ClearStencil(const IResourceView& depthStencil, unsigned stencil);

		static std::shared_ptr<DeviceContext> Get(IThreadContext& threadContext);

		// --------------- Vulkan specific interface --------------- 

		void		BeginCommandList();
		void		BeginCommandList(const VulkanSharedPtr<VkCommandBuffer>& cmdList);
		void		ExecuteCommandList(CommandList&, bool);
		auto        ResolveCommandList() -> std::shared_ptr<CommandList>;

		CommandList& GetActiveCommandList();
		bool HasActiveCommandList();

		GlobalPools&    GetGlobalPools();
		VkDevice        GetUnderlyingDevice();
		ObjectFactory&	GetFactory() const				{ return *_factory; }
		TemporaryBufferSpace& GetTemporaryBufferSpace();

		void BeginRenderPass(
			const FrameBuffer& fb,
			TextureSamples samples,
			VectorPattern<int, 2> offset, VectorPattern<unsigned, 2> extent,
			IteratorRange<const ClearValue*> clearValues);
		bool IsInRenderPass() const;
		void NextSubpass(VkSubpassContents);

		DeviceContext(
			ObjectFactory& factory, 
			GlobalPools& globalPools,
			CommandPool& cmdPool, 
			CommandBufferType cmdBufferType,
			TemporaryBufferSpace& tempBufferSpace);
		~DeviceContext();
		DeviceContext(const DeviceContext&) = delete;
		DeviceContext& operator=(const DeviceContext&) = delete;

		void RequireResourceVisbility(IteratorRange<const uint64_t*> resourceGuids);
		void MakeResourcesVisible(IteratorRange<const uint64_t*> resourceGuids);
		void ValidateCommitToQueue();

		std::shared_ptr<Internal::CaptureForBindRecords> _captureForBindRecords;

		// --------------- Legacy interface --------------- 
		void			InvalidateCachedState() {}
		static void		PrepareForDestruction(IDevice*, IPresentationChain*);
		bool			IsImmediate() { return false; }

	private:
		ObjectFactory*						_factory;
		GlobalPools*                        _globalPools;

		std::shared_ptr<VulkanEncoderSharedState> _sharedState;

		CommandPool*                        _cmdPool;
		CommandBufferType					_cmdBufferType;

		friend class BlitEncoder;
		void EndBlitEncoder();
		void ResetDescriptorSetState();
	};

}}


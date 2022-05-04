// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Forward.h"
#include "State.h"
#include "PipelineLayout.h"		// for PipelineDescriptorsLayoutBuilder
#include "CmdListAttachedStorage.h"
#include "CommandList.h"
#include "Shader.h"
#include "VulkanCore.h"
#include "../../ResourceList.h"
#include "../../ResourceDesc.h"
#include "../../FrameBufferDesc.h"
#include "../../IDevice_Forward.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <sstream>
#include <vector>

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
	class Resource;

	class GraphicsPipeline : public VulkanUniquePtr<VkPipeline>
	{
	public:
		// --------------- Cross-GFX-API interface ---------------
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
		// --------------- Cross-GFX-API interface ---------------
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
			ObjectFactory& factory);
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

	class CommandList;
	class VulkanEncoderSharedState;
	class NumericUniformsInterface;

	class CapturedStates
    {
    public:
		const void* _currentPipeline = nullptr;
		const void* _currentDescSet[s_maxBoundDescriptorSetCount] = {nullptr};
	};

	class SharedEncoder
	{
	public:
		NumericUniformsInterface	BeginNumericUniformsInterface();
		const std::shared_ptr<CompiledPipelineLayout>& GetPipelineLayout() { return _pipelineLayout; }

		void BeginStateCapture(CapturedStates& capturedStates);
		void EndStateCapture();
		const CapturedStates* GetCapturedStates() const { return _capturedStates; }

		// --------------- Vulkan specific interface --------------- 
		void		BindDescriptorSet(unsigned index, VkDescriptorSet set, IteratorRange<const unsigned*> dynamicOffsets VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description));
		void		PushConstants(VkShaderStageFlags stageFlags, unsigned offset, IteratorRange<const void*> data);

		enum class EncoderType { None, Graphics, ProgressiveGraphics, Compute };
		EncoderType GetEncoderType();
	protected:
		SharedEncoder(
			EncoderType encoderType = EncoderType::None,
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout = nullptr,
			std::shared_ptr<VulkanEncoderSharedState> sharedState = nullptr);
		~SharedEncoder();
		SharedEncoder(SharedEncoder&&);		// (hide these to avoid slicing in derived types)
		SharedEncoder& operator=(SharedEncoder&&);

		VkPipelineLayout GetUnderlyingPipelineLayout();
		unsigned GetDescriptorSetCount();

		std::shared_ptr<CompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<VulkanEncoderSharedState> _sharedState;
		CapturedStates* _capturedStates;

		friend class VulkanEncoderSharedState;
	};

	class GraphicsEncoder : public SharedEncoder
	{
	public:
		//	------ Non-pipeline states (that can be changed mid-render pass) -------
		void        Bind(IteratorRange<const VertexBufferView*> vbViews, const IndexBufferView& ibView);
		void		SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef);
		void		SetDepthBounds(float minDepthValue, float maxDepthValue);		// the 0-1 value stored in the depth buffer is compared directly to these bounds
		void 		Bind(IteratorRange<const ViewportDesc*> viewports, IteratorRange<const ScissorRect*> scissorRects);

	protected:
		enum class Type { Normal, StreamOutput };
		GraphicsEncoder(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout = nullptr,
			std::shared_ptr<VulkanEncoderSharedState> sharedState = nullptr,
			Type type = Type::Normal);
		~GraphicsEncoder();
		GraphicsEncoder(GraphicsEncoder&&);		// (hide these to avoid slicing in derived types)
		GraphicsEncoder& operator=(GraphicsEncoder&&);

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

		void BindPipeline(const GraphicsPipeline& pipeline);
		void LogPipeline(const GraphicsPipeline& pipeline);

		friend class DeviceContext;
	};

	class ComputeEncoder : public SharedEncoder
	{
	public:
		void        Dispatch(const ComputePipeline& pipeline, unsigned countX, unsigned countY=1, unsigned countZ=1);
		void        DispatchIndirect(const ComputePipeline& pipeline, const IResource& res, unsigned offset=0);

		ComputeEncoder(ComputeEncoder&&);
		ComputeEncoder& operator=(ComputeEncoder&&);
		ComputeEncoder();
		~ComputeEncoder();
	protected:
		ComputeEncoder(
			std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
			std::shared_ptr<VulkanEncoderSharedState> sharedState);

		void BindPipeline(const ComputePipeline& pipeline);
		void LogPipeline(const ComputePipeline& pipeline);

		friend class DeviceContext;
	};

	class BlitEncoder;
	class TemporaryStorageResourceMap;

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
		ComputeEncoder BeginComputeEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout);
		GraphicsEncoder_Optimized BeginStreamOutputEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout, IteratorRange<const VertexBufferView*> outputBuffers);
		BlitEncoder BeginBlitEncoder();

		void Clear(const IResourceView& renderTarget, const VectorPattern<float,4>& clearColour);
		void Clear(const IResourceView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil);
		void ClearUInt(const IResourceView& unorderedAccess, const VectorPattern<unsigned,4>& clearColour);
		void ClearFloat(const IResourceView& unorderedAccess, const VectorPattern<float,4>& clearColour);

		void BeginLabel(const char*, const VectorPattern<float,4>& color = {1,1,1,1});
		void EndLabel();

		TemporaryStorageResourceMap MapTemporaryStorage(size_t byteCount, BindFlag::Enum type);

		static std::shared_ptr<DeviceContext> Get(IThreadContext& threadContext);

		// --------------- Vulkan specific interface --------------- 

		void		BeginCommandList(std::shared_ptr<IAsyncTracker> asyncTracker);
		void		BeginCommandList(const VulkanSharedPtr<VkCommandBuffer>& cmdList, std::shared_ptr<IAsyncTracker> asyncTracker);
		void		ExecuteCommandList(CommandList&&);
		auto        ResolveCommandList() -> std::shared_ptr<CommandList>;

		CommandList& GetActiveCommandList();
		bool HasActiveCommandList();

		GlobalPools&    GetGlobalPools();
		VkDevice        GetUnderlyingDevice();
		ObjectFactory&	GetFactory() const				{ return *_factory; }

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
			CommandBufferType cmdBufferType);
		~DeviceContext();
		DeviceContext(const DeviceContext&) = delete;
		DeviceContext& operator=(const DeviceContext&) = delete;

		std::shared_ptr<Internal::CaptureForBindRecords> _captureForBindRecords;

		// --------------- Legacy interface --------------- 
		void			InvalidateCachedState() {}
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


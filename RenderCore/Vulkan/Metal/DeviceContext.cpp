// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "InputLayout.h"
#include "Shader.h"
#include "Buffer.h"
#include "State.h"
#include "TextureView.h"
#include "FrameBuffer.h"
#include "Pools.h"
#include "PipelineLayout.h"
#include "ShaderReflection.h"
#include "ExtensionFunctions.h"
#include "../IDeviceVulkan.h"
#include "../../Format.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ArithmeticUtils.h"

namespace RenderCore { namespace Metal_Vulkan
{

///////////////////////////////////////////////////////////////////////////////////////////////////

	static VkViewport AsVkViewport(const ViewportDesc& viewport, const float renderTargetHeight)
	{
		VkViewport vp;
		vp.x = viewport._x;
		vp.y = viewport._y;
		vp.width = viewport._width;
		vp.height = viewport._height;
		vp.minDepth = viewport._minDepth;
		vp.maxDepth = viewport._maxDepth;
		if (!viewport._originIsUpperLeft) {
			// Vulkan window coordinate space has origin in upper-left, so we must account for that in the viewport
			vp.y = renderTargetHeight - viewport._y - viewport._height;
		}
		return vp;
	}

	static VkRect2D AsVkRect2D(const ScissorRect& input, const float renderTargetHeight)
	{
		VkRect2D scissor;
		scissor.offset.x = input._x;
		scissor.offset.y = input._y;
		scissor.extent.width = input._width;
		scissor.extent.height = input._height;
		if (!input._originIsUpperLeft) {
			// Vulkan window coordinate space has origin in upper-left, so we must account for that in the viewport
			scissor.offset.y = renderTargetHeight - input._y - input._height;
		}
		return scissor;
	}

	class VulkanEncoderSharedState
	{
	public:
		CommandList 	_commandList;

		VkRenderPass	_renderPass = 0;
		TextureSamples	_renderPassSamples = TextureSamples::Create(0);
		unsigned		_renderPassSubpass = 0;

		float			_renderTargetWidth = 0.f;
		float			_renderTargetHeight = 0.f;

		bool			_inBltPass = false;

		class DescriptorCollection
		{
		public:
			std::vector<VkDescriptorSet>		_descriptorSets;			// (can't use a smart pointer here because it's often bound to the descriptor set in NumericUniformsInterface, which we must share)
			bool                                _hasSetsAwaitingFlush = false;

			#if defined(VULKAN_VERBOSE_DEBUG)
				std::vector<DescriptorSetDebugInfo> _currentlyBoundDesc;
			#endif

			void ResetState(const CompiledPipelineLayout&);
			const ObjectFactory& GetObjectFactory() { return *_factory; }

			DescriptorCollection(
				const ObjectFactory&    factory, 
				GlobalPools&            globalPools,
				unsigned				descriptorSetCount);

		private:
			const ObjectFactory*    _factory;
			GlobalPools*			_globalPools;
		};

		DescriptorCollection	_graphicsDescriptors;
		DescriptorCollection	_computeDescriptors;
		TemporaryBufferSpace*	_tempBufferSpace = nullptr;

		void* _currentEncoder = nullptr;
		enum class EncoderType { None, Graphics, ProgressiveGraphics, ProgressiveCompute };
		EncoderType _currentEncoderType = EncoderType::None;

		bool _ibBound = false;		// (for debugging, validates that an index buffer actually is bound when calling DrawIndexed & alternatives)

		VulkanEncoderSharedState(
			const ObjectFactory&    factory, 
			GlobalPools&            globalPools,
			TemporaryBufferSpace&	tempBufferSpace);
		~VulkanEncoderSharedState();
	};

	void        GraphicsEncoder::Bind(
		IteratorRange<const ViewportDesc*> viewports,
		IteratorRange<const ScissorRect*> scissorRects)
	{
		// maxviewports: VkPhysicalDeviceLimits::maxViewports
		// VkPhysicalDeviceFeatures::multiViewport must be enabled
		// need VK_DYNAMIC_STATE_VIEWPORT & VK_DYNAMIC_STATE_SCISSOR set
		assert(viewports.size() >= 1);
		assert(scissorRects.size() >= 1);
		assert(viewports.size() == scissorRects.size());
		assert(viewports.size() <= GetObjectFactory().GetPhysicalDeviceProperties().limits.maxViewports);

		assert(_sharedState->_commandList.GetUnderlying());
		VkViewport vkViewports[viewports.size()];
		for (unsigned c=0; c<viewports.size(); ++c)
			vkViewports[c] = AsVkViewport(viewports[c], _sharedState->_renderTargetHeight);

		VkRect2D vkScissors[scissorRects.size()];
		for (unsigned c=0; c<scissorRects.size(); ++c)
			vkScissors[c] = AsVkRect2D(scissorRects[c], _sharedState->_renderTargetHeight);

		vkCmdSetViewport(_sharedState->_commandList.GetUnderlying().get(), 0, viewports.size(), vkViewports);
		vkCmdSetScissor(_sharedState->_commandList.GetUnderlying().get(), 0, scissorRects.size(), vkScissors);
	}

	void        GraphicsEncoder::Bind(IteratorRange<const VertexBufferView*> vbViews, const IndexBufferView& ibView)
	{
		assert(_sharedState->_commandList.GetUnderlying());

		VkBuffer buffers[s_maxBoundVBs];
		VkDeviceSize offsets[s_maxBoundVBs];
		// auto count = (unsigned)std::min(std::min(vertexBuffers.size(), dimof(buffers)), _vbBindingDescriptions.size());
		assert(vbViews.size() < s_maxBoundVBs);
		for (unsigned c=0; c<vbViews.size(); ++c) {
			offsets[c] = vbViews[c]._offset;
			assert(const_cast<IResource*>(vbViews[c]._resource)->QueryInterface(typeid(Resource).hash_code()));
			buffers[c] = checked_cast<const Resource*>(vbViews[c]._resource)->GetBuffer();
		}
		vkCmdBindVertexBuffers(
			_sharedState->_commandList.GetUnderlying().get(), 
			0, vbViews.size(),
			buffers, offsets);

		if (ibView._resource) {
			assert(ibView._resource);
			vkCmdBindIndexBuffer(
				_sharedState->_commandList.GetUnderlying().get(),
				checked_cast<const Resource*>(ibView._resource)->GetBuffer(),
				ibView._offset,
				ibView._indexFormat == Format::R32_UINT ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
			_sharedState->_ibBound = true;
		} else {
			_sharedState->_ibBound = false;
		}
	}

	void		GraphicsEncoder::SetStencilRef(unsigned frontFaceStencilRef, unsigned backFaceStencilRef)
	{
		if (frontFaceStencilRef == backFaceStencilRef) {
			vkCmdSetStencilReference(_sharedState->_commandList.GetUnderlying().get(), VK_STENCIL_FACE_FRONT_AND_BACK, frontFaceStencilRef);
		} else {
			vkCmdSetStencilReference(_sharedState->_commandList.GetUnderlying().get(), VK_STENCIL_FACE_FRONT_BIT, frontFaceStencilRef);
			vkCmdSetStencilReference(_sharedState->_commandList.GetUnderlying().get(), VK_STENCIL_FACE_BACK_BIT, backFaceStencilRef);
		}
	}

	void		GraphicsEncoder::SetDepthBounds(float minDepthValue, float maxDepthValue)
	{
		// See 26.5. Depth Bounds Test
		// The depth bounds test compares the depth value za in the depth/stencil attachment at each sample’s
		// framebuffer coordinates (xf,yf) and sample index i against a set of depth bounds.
		// (the interval is inclusive, so minDepthValue <= za <= maxDepthValue)
		vkCmdSetDepthBounds(_sharedState->_commandList.GetUnderlying().get(), minDepthValue, maxDepthValue);
	}

	void        GraphicsEncoder::BindDescriptorSet(
		unsigned index, VkDescriptorSet set
		VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description))
	{
		auto& collection = _sharedState->_graphicsDescriptors;
		if (index < (unsigned)collection._descriptorSets.size() && collection._descriptorSets[index] != set) {
			collection._descriptorSets[index] = set;
			assert(index < GetDescriptorSetCount());
			_sharedState->_commandList.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				GetUnderlyingPipelineLayout(),
				index, 1, &collection._descriptorSets[index], 
				0, nullptr);

			#if defined(VULKAN_VERBOSE_DEBUG)
				collection._currentlyBoundDesc[index] = description;
			#endif
		}
	}

	void        ComputeEncoder_ProgressivePipeline::BindDescriptorSet(
		unsigned index, VkDescriptorSet set
		VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description))
	{
		auto& collection = _sharedState->_computeDescriptors;
		if (index < (unsigned)collection._descriptorSets.size() && collection._descriptorSets[index] != set) {
			collection._descriptorSets[index] = set;
			assert(index < GetDescriptorSetCount());
			_sharedState->_commandList.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_COMPUTE,
				GetUnderlyingPipelineLayout(),
				index, 1, &collection._descriptorSets[index], 
				0, nullptr);

			#if defined(VULKAN_VERBOSE_DEBUG)
				collection._currentlyBoundDesc[index] = description;
			#endif
		}
	}

	NumericUniformsInterface	GraphicsEncoder::BeginNumericUniformsInterface()
	{
		return NumericUniformsInterface { 
			_sharedState->_graphicsDescriptors.GetObjectFactory(),
			*_pipelineLayout,
			*_sharedState->_tempBufferSpace,
			Internal::VulkanGlobalsTemp::GetInstance()._legacyRegisterBindings};
	}

	void			GraphicsEncoder::PushConstants(VkShaderStageFlags stageFlags, unsigned offset, IteratorRange<const void*> data)
	{
		assert(!(stageFlags & VK_SHADER_STAGE_COMPUTE_BIT));
		_sharedState->_commandList.PushConstants(
			GetUnderlyingPipelineLayout(),
			stageFlags, offset, (uint32_t)data.size(), data.begin());
	}

	unsigned GraphicsEncoder::GetDescriptorSetCount()
	{
		return _pipelineLayout->GetDescriptorSetCount();
	}

	VkPipelineLayout GraphicsEncoder::GetUnderlyingPipelineLayout()
	{
		return _pipelineLayout->GetUnderlying();
	}

	unsigned ComputeEncoder_ProgressivePipeline::GetDescriptorSetCount()
	{
		return _pipelineLayout->GetDescriptorSetCount();
	}

	VkPipelineLayout ComputeEncoder_ProgressivePipeline::GetUnderlyingPipelineLayout()
	{
		return _pipelineLayout->GetUnderlying();
	}

	bool GraphicsEncoder_ProgressivePipeline::BindGraphicsPipeline()
	{
		assert(_sharedState->_commandList.GetUnderlying());

		if (_currentGraphicsPipeline && !GraphicsPipelineBuilder::IsPipelineStale()) return true;

		_currentGraphicsPipeline = GraphicsPipelineBuilder::CreatePipeline(
			*_factory, _globalPools->_mainPipelineCache.get(),
			_sharedState->_renderPass, _sharedState->_renderPassSubpass, _sharedState->_renderPassSamples);
		assert(_currentGraphicsPipeline);
		LogPipeline();

		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			_currentGraphicsPipeline->get());
		// Bind(_boundViewport);
		return true;
	}

	bool ComputeEncoder_ProgressivePipeline::BindComputePipeline()
	{
		assert(_sharedState->_commandList.GetUnderlying());

		if (_currentComputePipeline && !ComputePipelineBuilder::IsPipelineStale()) return true;

		_currentComputePipeline = ComputePipelineBuilder::CreatePipeline(
			*_factory, _globalPools->_mainPipelineCache.get());
		assert(_currentComputePipeline);
		LogPipeline();

		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_COMPUTE,
			_currentComputePipeline->get());

		return true;
	}

	void GraphicsEncoder_ProgressivePipeline::LogPipeline()
	{
		return;
		#if defined(_DEBUG)
			if (!Verbose.IsEnabled()) return;

			const CompiledShaderByteCode* shaders[(unsigned)ShaderStage::Max] = {};
			Log(Verbose) << "-------------VertexShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(GetBoundShaderProgram()->GetCompiledCode(ShaderStage::Vertex).GetByteCode()) << std::endl;
			Log(Verbose) << "-------------PixelShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(GetBoundShaderProgram()->GetCompiledCode(ShaderStage::Pixel).GetByteCode()) << std::endl;
			static_assert(ShaderProgram::s_maxShaderStages <= dimof(shaders));
			for (unsigned c=0; c<ShaderProgram::s_maxShaderStages; ++c)
				shaders[c] = &GetBoundShaderProgram()->GetCompiledCode((ShaderStage)c);

			#if defined(VULKAN_VERBOSE_DEBUG)
				const auto& descriptors = _sharedState->_graphicsDescriptors;
				_pipelineLayout->WriteDebugInfo(
					Log(Verbose),
					MakeIteratorRange(shaders),
					MakeIteratorRange(descriptors._currentlyBoundDesc));
			#endif
		#endif
	}

	void GraphicsEncoder_Optimized::LogPipeline(const GraphicsPipeline& pipeline)
	{
		return;
		#if defined(_DEBUG)
			if (!Verbose.IsEnabled()) return;

			const CompiledShaderByteCode* shaders[(unsigned)ShaderStage::Max] = {};
			Log(Verbose) << "-------------VertexShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(pipeline._shader.GetCompiledCode(ShaderStage::Vertex).GetByteCode()) << std::endl;
			Log(Verbose) << "-------------PixelShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(pipeline._shader.GetCompiledCode(ShaderStage::Pixel).GetByteCode()) << std::endl;
			static_assert(ShaderProgram::s_maxShaderStages <= dimof(shaders));
			for (unsigned c=0; c<ShaderProgram::s_maxShaderStages; ++c)
				shaders[c] = &pipeline._shader.GetCompiledCode((ShaderStage)c);

			#if defined(VULKAN_VERBOSE_DEBUG)
				const auto& descriptors = _sharedState->_graphicsDescriptors;
				_pipelineLayout->WriteDebugInfo(
					Log(Verbose),
					MakeIteratorRange(shaders),
					MakeIteratorRange(descriptors._currentlyBoundDesc));
			#endif
		#endif
	}

	void ComputeEncoder_ProgressivePipeline::LogPipeline()
	{
		#if defined(_DEBUG)
			if (!Verbose.IsEnabled()) return;

			const CompiledShaderByteCode* shaders[(unsigned)ShaderStage::Max] = {};
			Log(Verbose) << "-------------ComputeShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(GetBoundComputeShader()->GetCompiledShaderByteCode().GetByteCode()) << std::endl;
			shaders[(unsigned)ShaderStage::Compute] = &GetBoundComputeShader()->GetCompiledShaderByteCode();

			#if defined(VULKAN_VERBOSE_DEBUG)
				const auto& descriptors = _sharedState->_computeDescriptors;
				_pipelineLayout->WriteDebugInfo(
					Log(Verbose),
					MakeIteratorRange(shaders),
					MakeIteratorRange(descriptors._currentlyBoundDesc));
			#endif
		#endif
	}

	void GraphicsEncoder_ProgressivePipeline::Bind(const ShaderProgram& shaderProgram)
	{
		assert(shaderProgram.GetPipelineLayout().get() == _pipelineLayout.get());
		GraphicsPipelineBuilder::Bind(shaderProgram);
	}

	void GraphicsEncoder_ProgressivePipeline::Draw(unsigned vertexCount, unsigned startVertexLocation)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		if (BindGraphicsPipeline()) {
			assert(vertexCount);
			vkCmdDraw(
				_sharedState->_commandList.GetUnderlying().get(),
				vertexCount, 1,
				startVertexLocation, 0);
		}
	}
	
	void GraphicsEncoder_ProgressivePipeline::DrawIndexed(unsigned indexCount, unsigned startIndexLocation)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		assert(_sharedState->_ibBound);
		if (BindGraphicsPipeline()) {
			assert(indexCount);
			vkCmdDrawIndexed(
				_sharedState->_commandList.GetUnderlying().get(),
				indexCount, 1,
				startIndexLocation, 0,
				0);
		}
	}

	void GraphicsEncoder_ProgressivePipeline::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
	{
		// Vulkan does have a per-instance data rate concept, but to access it we need to use
		// the draw indirect commands. That allows us to put instance count and offset information
		// into the VkDrawIndirectCommand, VkDrawIndexedIndirectCommand data structures, which
		// are read from VkBuffer.
		//
		// We can emulate that functionality here by creating a buffer and just calling 
		// the vkCmdDrawIndirect. Or alternatively having some large buffer that we just
		// stream commands to over time. But neither of those really ideal. 
		// We should try to avoid creating and uploading buffer data during render passes,
		// and where possible move that to construction time.
		Log(Verbose) << "DrawInstances is very inefficient on Vulkan. Prefer pre-building buffers and vkCmdDrawIndirect" << std::endl;
		assert(_sharedState->_commandList.GetUnderlying());
		if (BindGraphicsPipeline()) {
			VkDrawIndirectCommand indirectCommands[] {
				VkDrawIndirectCommand { vertexCount, instanceCount, startVertexLocation, 0 }
			};
			Resource temporaryBuffer(
				*_factory,
				CreateDesc(
					BindFlag::DrawIndirectArgs, 0, GPUAccess::Read,
					LinearBufferDesc::Create(sizeof(indirectCommands)),
					"temp-DrawInstances-buffer"),
				SubResourceInitData{MakeIteratorRange(indirectCommands)});
			vkCmdDrawIndirect(
				_sharedState->_commandList.GetUnderlying().get(),
				temporaryBuffer.GetBuffer(),
				0, 1, sizeof(VkDrawIndirectCommand));
		}
	}

	void GraphicsEncoder_ProgressivePipeline::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
	{
		Log(Verbose) << "DrawIndexedInstances is very inefficient on Vulkan. Prefer pre-building buffers and vkCmdDrawIndirect" << std::endl;
		assert(_sharedState->_commandList.GetUnderlying());
		if (BindGraphicsPipeline()) {
			VkDrawIndexedIndirectCommand indirectCommands[] {
				VkDrawIndexedIndirectCommand { indexCount, instanceCount, startIndexLocation, 0, 0 }
			};
			Resource temporaryBuffer(
				*_factory,
				CreateDesc(
					BindFlag::DrawIndirectArgs, 0, GPUAccess::Read,
					LinearBufferDesc::Create(sizeof(indirectCommands)),
					"temp-DrawInstances-buffer"),
				SubResourceInitData{MakeIteratorRange(indirectCommands)});
			vkCmdDrawIndexedIndirect(
				_sharedState->_commandList.GetUnderlying().get(),
				temporaryBuffer.GetBuffer(),
				0, 1, sizeof(VkDrawIndexedIndirectCommand));
		}
	}

	void GraphicsEncoder_ProgressivePipeline::DrawAuto() 
	{
		assert(0);      // not implemented
	}

	void GraphicsEncoder_Optimized::Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.get());
		DEBUG_ONLY(LogPipeline(pipeline));
		
		assert(vertexCount);
		vkCmdDraw(
			_sharedState->_commandList.GetUnderlying().get(),
			vertexCount, 1,
			startVertexLocation, 0);
	}
	
	void GraphicsEncoder_Optimized::DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		assert(_sharedState->_ibBound);
		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.get());
		DEBUG_ONLY(LogPipeline(pipeline));

		assert(indexCount);
		vkCmdDrawIndexed(
			_sharedState->_commandList.GetUnderlying().get(),
			indexCount, 1,
			startIndexLocation, 0,
			0);
	}

	void GraphicsEncoder_Optimized::DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
	{
		Log(Verbose) << "DrawInstances is very inefficient on Vulkan. Prefer pre-building buffers and vkCmdDrawIndirect" << std::endl;
		assert(_sharedState->_commandList.GetUnderlying());
		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.get());
		DEBUG_ONLY(LogPipeline(pipeline));

		VkDrawIndirectCommand indirectCommands[] {
			VkDrawIndirectCommand { vertexCount, instanceCount, startVertexLocation, 0 }
		};
		Resource temporaryBuffer(
			GetObjectFactory(),
			CreateDesc(
				BindFlag::DrawIndirectArgs, 0, GPUAccess::Read,
				LinearBufferDesc::Create(sizeof(indirectCommands)),
				"temp-DrawInstances-buffer"),
			SubResourceInitData{MakeIteratorRange(indirectCommands)});
		vkCmdDrawIndirect(
			_sharedState->_commandList.GetUnderlying().get(),
			temporaryBuffer.GetBuffer(),
			0, 1, sizeof(VkDrawIndirectCommand));
	}

	void GraphicsEncoder_Optimized::DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
	{
		Log(Verbose) << "DrawIndexedInstances is very inefficient on Vulkan. Prefer pre-building buffers and vkCmdDrawIndirect" << std::endl;
		assert(_sharedState->_commandList.GetUnderlying());
		vkCmdBindPipeline(
			_sharedState->_commandList.GetUnderlying().get(),
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline.get());
		DEBUG_ONLY(LogPipeline(pipeline));

		VkDrawIndexedIndirectCommand indirectCommands[] {
			VkDrawIndexedIndirectCommand { indexCount, instanceCount, startIndexLocation, 0, 0 }
		};
		Resource temporaryBuffer(
			GetObjectFactory(),
			CreateDesc(
				BindFlag::DrawIndirectArgs, 0, GPUAccess::Read,
				LinearBufferDesc::Create(sizeof(indirectCommands)),
				"temp-DrawInstances-buffer"),
			SubResourceInitData{MakeIteratorRange(indirectCommands)});
		vkCmdDrawIndexedIndirect(
			_sharedState->_commandList.GetUnderlying().get(),
			temporaryBuffer.GetBuffer(),
			0, 1, sizeof(VkDrawIndexedIndirectCommand));
	}

	void GraphicsEncoder_Optimized::DrawAuto(const GraphicsPipeline& pipeline)
	{
		assert(0);      // not implemented
	}

	void ComputeEncoder_ProgressivePipeline::Dispatch(unsigned countX, unsigned countY, unsigned countZ)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		if (BindComputePipeline()) {
			vkCmdDispatch(
				_sharedState->_commandList.GetUnderlying().get(),
				countX, countY, countZ);
		}
	}

	GraphicsEncoder::GraphicsEncoder(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState,
		Type type)
	: _pipelineLayout(std::move(pipelineLayout))
	, _sharedState(std::move(sharedState))
	, _type(type)
	{
		if (_sharedState) {
			assert(_sharedState->_currentEncoder == nullptr && _sharedState->_currentEncoderType == VulkanEncoderSharedState::EncoderType::None);
			assert(_sharedState->_renderPass != nullptr);
			assert(_pipelineLayout);
			_sharedState->_currentEncoder = this;
			_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::Graphics;

			_sharedState->_graphicsDescriptors.ResetState(*_pipelineLayout);

			// bind descriptor sets that are pending
			// If we've been using the pipeline layout builder directly, then we
			// must flush those changes down to the GraphicsPipelineBuilder
			if (_sharedState->_graphicsDescriptors._hasSetsAwaitingFlush) {
				_sharedState->_commandList.BindDescriptorSets(
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					GetUnderlyingPipelineLayout(),
					0, GetDescriptorSetCount(), AsPointer(_sharedState->_graphicsDescriptors._descriptorSets.begin()), 
					0, nullptr);
				_sharedState->_graphicsDescriptors._hasSetsAwaitingFlush = false;
			}
		}
	}

	GraphicsEncoder::~GraphicsEncoder()
	{
		if (_type == Type::StreamOutput && _sharedState) {
			assert(GetObjectFactory().GetExtensionFunctions()._endTransformFeedback);
			(*GetObjectFactory().GetExtensionFunctions()._endTransformFeedback)(
				_sharedState->_commandList.GetUnderlying().get(),
				0, 0, nullptr, nullptr);
		}

		if (_sharedState) {
			assert(_sharedState->_currentEncoder == this);
			_sharedState->_currentEncoder = nullptr;
			_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::None;
		}
	}

	GraphicsEncoder::GraphicsEncoder(GraphicsEncoder&& moveFrom)
	{
		if (moveFrom._sharedState) {
			assert(moveFrom._sharedState->_currentEncoder == &moveFrom);
			_sharedState = std::move(moveFrom._sharedState);
			_sharedState->_currentEncoder = this;
		}
		_pipelineLayout = std::move(moveFrom._pipelineLayout);
		_type = moveFrom._type;
		moveFrom._type = Type::Normal;
	}

	GraphicsEncoder& GraphicsEncoder::operator=(GraphicsEncoder&& moveFrom)
	{
		if (_sharedState) {
			if (_type == Type::StreamOutput) {
				assert(GetObjectFactory().GetExtensionFunctions()._endTransformFeedback);
				(*GetObjectFactory().GetExtensionFunctions()._endTransformFeedback)(
					_sharedState->_commandList.GetUnderlying().get(),
					0, 0, nullptr, nullptr);
			}
			
			assert(_sharedState->_currentEncoder == this);
			_sharedState->_currentEncoder = nullptr;
			_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::None;
			_sharedState.reset();
		}

		if (moveFrom._sharedState) {
			assert(moveFrom._sharedState->_currentEncoder == &moveFrom);
			_sharedState = std::move(moveFrom._sharedState);
			_sharedState->_currentEncoder = this;
		}
		_pipelineLayout = std::move(moveFrom._pipelineLayout);
		_type = moveFrom._type;
		moveFrom._type = Type::Normal;
		return *this;
	}

	GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline(GraphicsEncoder_ProgressivePipeline&& moveFrom)
	: GraphicsEncoder(std::move(moveFrom))
	, GraphicsPipelineBuilder(moveFrom)
	{
		_currentGraphicsPipeline = std::move(moveFrom._currentGraphicsPipeline);
		_factory = moveFrom._factory;
		_globalPools = moveFrom._globalPools;
		moveFrom._factory = nullptr;
		moveFrom._globalPools = nullptr;
	}

	GraphicsEncoder_ProgressivePipeline& GraphicsEncoder_ProgressivePipeline::operator=(GraphicsEncoder_ProgressivePipeline&& moveFrom)
	{
		GraphicsEncoder::operator=(std::move(moveFrom));
		GraphicsPipelineBuilder::operator=(std::move(moveFrom));
		_currentGraphicsPipeline = std::move(moveFrom._currentGraphicsPipeline);
		_factory = moveFrom._factory;
		_globalPools = moveFrom._globalPools;
		moveFrom._factory = nullptr;
		moveFrom._globalPools = nullptr;
		return *this;
	}

	GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState,
		ObjectFactory& objectFactory,
		GlobalPools& globalPools,
		Type type)
	: GraphicsEncoder(std::move(pipelineLayout), std::move(sharedState), type)
	, _factory(&objectFactory)
	, _globalPools(&globalPools)
	{
		assert(_sharedState);
		_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::ProgressiveGraphics;
	}

	GraphicsEncoder_ProgressivePipeline::GraphicsEncoder_ProgressivePipeline()
	{
		_factory = nullptr;
		_globalPools = nullptr;
		_type = Type::Normal;
	}

	GraphicsEncoder_ProgressivePipeline::~GraphicsEncoder_ProgressivePipeline()
	{
	}

	GraphicsEncoder_Optimized::GraphicsEncoder_Optimized(GraphicsEncoder_Optimized&& moveFrom)
	: GraphicsEncoder(std::move(moveFrom))
	{
	}

	GraphicsEncoder_Optimized::GraphicsEncoder_Optimized() {}

	GraphicsEncoder_Optimized& GraphicsEncoder_Optimized::operator=(GraphicsEncoder_Optimized&& moveFrom)
	{
		GraphicsEncoder::operator=(std::move(moveFrom));
		return *this;
	}

	GraphicsEncoder_Optimized::GraphicsEncoder_Optimized(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState,
		Type type)
	: GraphicsEncoder(std::move(pipelineLayout), std::move(sharedState), type)
	{}
	GraphicsEncoder_Optimized::~GraphicsEncoder_Optimized()
	{}

	ComputeEncoder_ProgressivePipeline::ComputeEncoder_ProgressivePipeline(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState,
		ObjectFactory& objectFactory,
		GlobalPools& globalPools)
	: _pipelineLayout(std::move(pipelineLayout))
	, _sharedState(std::move(sharedState))
	, _factory(&objectFactory)
	, _globalPools(&globalPools)
	{
		assert(_sharedState->_currentEncoder == nullptr && _sharedState->_currentEncoderType == VulkanEncoderSharedState::EncoderType::None);
		assert(_sharedState->_renderPass == nullptr);	// don't start compute encoding during a render pass
		_sharedState->_currentEncoder = this;
		_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::ProgressiveCompute;

		_sharedState->_computeDescriptors.ResetState(*_pipelineLayout);

		// bind descriptor sets that are pending
		if (_sharedState->_computeDescriptors._hasSetsAwaitingFlush) {
			_sharedState->_commandList.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_COMPUTE,
				GetUnderlyingPipelineLayout(),
				0, GetDescriptorSetCount(), AsPointer(_sharedState->_computeDescriptors._descriptorSets.begin()), 
				0, nullptr);
			_sharedState->_computeDescriptors._hasSetsAwaitingFlush = false;
		}
	}

	ComputeEncoder_ProgressivePipeline::~ComputeEncoder_ProgressivePipeline()
	{
		if (_sharedState) {
			assert(_sharedState->_currentEncoder == this);
			_sharedState->_currentEncoder = nullptr;
			_sharedState->_currentEncoderType = VulkanEncoderSharedState::EncoderType::None;
		}
	}

	GraphicsEncoder_Optimized DeviceContext::BeginGraphicsEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a blt pass is in progress"));
		return GraphicsEncoder_Optimized { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState };
	}

	GraphicsEncoder_ProgressivePipeline DeviceContext::BeginGraphicsEncoder_ProgressivePipeline(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a blt pass is in progress"));
		return GraphicsEncoder_ProgressivePipeline { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState, *_factory, *_globalPools };
	}

	GraphicsEncoder_Optimized DeviceContext::BeginStreamOutputEncoder(
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		IteratorRange<const VertexBufferView*> outputBuffers)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a stream output encoder while a blt pass is in progress"));
		if (outputBuffers.empty())
			Throw(::Exceptions::BasicLabel("No stream output buffers provided to BeginStreamOutputEncoder"));

		if (!_factory->GetExtensionFunctions()._beginTransformFeedback)
			Throw(::Exceptions::BasicLabel("Stream output extension not supported on this platform"));

		assert(_factory->GetExtensionFunctions()._beginTransformFeedback);
		assert(_factory->GetExtensionFunctions()._bindTransformFeedbackBuffers);

		VkDeviceSize offsets[outputBuffers.size()];
		VkBuffer buffers[outputBuffers.size()];
		for (unsigned c=0; c<outputBuffers.size(); ++c) {
			offsets[c] = outputBuffers[c]._offset;
			assert(const_cast<IResource*>(outputBuffers[c]._resource)->QueryInterface(typeid(Resource).hash_code()));
			buffers[c] = checked_cast<const Resource*>(outputBuffers[c]._resource)->GetBuffer();
		}

		(*_factory->GetExtensionFunctions()._bindTransformFeedbackBuffers)(
			GetActiveCommandList().GetUnderlying().get(),
			0, outputBuffers.size(), 
			buffers, offsets, nullptr);

		(*_factory->GetExtensionFunctions()._beginTransformFeedback)(
			GetActiveCommandList().GetUnderlying().get(),
			0, 0, nullptr, nullptr);

		return GraphicsEncoder_Optimized { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState, GraphicsEncoder::Type::StreamOutput };
	}

	ComputeEncoder_ProgressivePipeline DeviceContext::BeginComputeEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_renderPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a render pass is in progress"));
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a blt pass is in progress"));
		return ComputeEncoder_ProgressivePipeline { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState, *_factory, *_globalPools };
	}

	std::shared_ptr<DeviceContext> DeviceContext::Get(IThreadContext& threadContext)
	{
		IThreadContextVulkan* vulkanContext = 
			(IThreadContextVulkan*)threadContext.QueryInterface(
				typeid(IThreadContextVulkan).hash_code());
		if (vulkanContext) {
			auto res = vulkanContext->GetMetalContext();
			if (!res->HasActiveCommandList())
				res->BeginCommandList();
			return res;
		}
		return nullptr;
	}

	GlobalPools&    DeviceContext::GetGlobalPools()
	{
		return *_globalPools;
	}

	VkDevice        DeviceContext::GetUnderlyingDevice()
	{
		return _factory->GetDevice().get();
	}

	TemporaryBufferSpace& DeviceContext::GetTemporaryBufferSpace()
	{
		assert(_sharedState->_tempBufferSpace);
		return *_sharedState->_tempBufferSpace;
	}

	void		DeviceContext::BeginCommandList()
	{
		if (!_cmdPool) return;
		BeginCommandList(_cmdPool->Allocate(_cmdBufferType));
	}

	void		DeviceContext::BeginCommandList(const VulkanSharedPtr<VkCommandBuffer>& cmdList)
	{
		assert(!_sharedState->_commandList.GetUnderlying());
		_sharedState->_commandList = CommandList(cmdList);
		_sharedState->_ibBound = false;

		VkCommandBufferInheritanceInfo inheritInfo = {};
		inheritInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		inheritInfo.pNext = nullptr;
		inheritInfo.renderPass = VK_NULL_HANDLE;
		inheritInfo.subpass = 0;
		inheritInfo.framebuffer = VK_NULL_HANDLE;
		inheritInfo.occlusionQueryEnable = false;
		inheritInfo.queryFlags = 0;
		inheritInfo.pipelineStatistics = 0;

		VkCommandBufferBeginInfo cmd_buf_info = {};
		cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmd_buf_info.pNext = nullptr;
		cmd_buf_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		cmd_buf_info.pInheritanceInfo = &inheritInfo;
		auto res = vkBeginCommandBuffer(_sharedState->_commandList.GetUnderlying().get(), &cmd_buf_info);
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while beginning command buffer"));
	}

	void		DeviceContext::ExecuteCommandList(CommandList& cmdList, bool preserveState)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		(void)preserveState;		// we can't handle this properly in Vulkan

		const VkCommandBuffer buffers[] = { cmdList.GetUnderlying().get() };
		vkCmdExecuteCommands(
			_sharedState->_commandList.GetUnderlying().get(),
			dimof(buffers), buffers);

		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)

			// Merge in the list of "must be visible" resources and "becoming visible resources
			// However, note:
			//		- input and output arrays should be sorted -- so we can use merge sort approach for this
			//		- any new "must be visible" resources that are already present in our "becoming visible" list 
			//			be filtered out (ie, we're merging in use of a resource that was made visible previously on this cmd list) 
			RequireResourceVisbility(cmdList._resourcesThatMustBeVisible);
			MakeResourcesVisible(cmdList._resourcesBecomingVisible);
		#endif
	}

	auto        DeviceContext::ResolveCommandList() -> std::shared_ptr<CommandList>
	{
		assert(_sharedState->_commandList.GetUnderlying());
		if (_captureForBindRecords)
			Internal::ValidateIsEmpty(*_captureForBindRecords);		// always complete these captures before completing a command list
		auto res = vkEndCommandBuffer(_sharedState->_commandList.GetUnderlying().get());
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while ending command buffer"));

		// We will release our reference on _command list here.
		auto result = std::make_shared<CommandList>(std::move(_sharedState->_commandList));
		assert(!_sharedState->_commandList.GetUnderlying());
		return result;
	}

	void        DeviceContext::BeginRenderPass(
		const FrameBuffer& fb,
		TextureSamples samples,
		VectorPattern<int, 2> offset, VectorPattern<unsigned, 2> extent,
		IteratorRange<const ClearValue*> clearValues)
	{
		if (_sharedState->_renderPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a render pass while another render pass is already in progress"));
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a render pass while a blt pass is in progress"));
		assert(!_sharedState->_currentEncoder);
		assert(_sharedState->_commandList.GetUnderlying() != nullptr);

		VkRenderPassBeginInfo rp_begin;
		rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rp_begin.pNext = nullptr;
		rp_begin.renderPass = fb.GetLayout();
		rp_begin.framebuffer = fb.GetUnderlying();
		rp_begin.renderArea.offset.x = offset[0];
		rp_begin.renderArea.offset.y = offset[1];
		rp_begin.renderArea.extent.width = extent[0];
		rp_begin.renderArea.extent.height = extent[1];
		
		VkClearValue vkClearValues[fb._clearValuesOrdering.size()];
		for (unsigned c=0; c<fb._clearValuesOrdering.size(); ++c) {
			if (fb._clearValuesOrdering[c]._originalAttachmentIndex < clearValues.size()) {
				vkClearValues[c] = *(const VkClearValue*)&clearValues[fb._clearValuesOrdering[c]._originalAttachmentIndex];
			} else {
				vkClearValues[c] = *(const VkClearValue*)&fb._clearValuesOrdering[c]._defaultClearValue;
			}
		}
		rp_begin.pClearValues = vkClearValues;
		rp_begin.clearValueCount = (uint32_t)fb._clearValuesOrdering.size();

		vkCmdBeginRenderPass(_sharedState->_commandList.GetUnderlying().get(), &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
		_sharedState->_renderPass = fb.GetLayout();
		_sharedState->_renderPassSamples = samples;
		_sharedState->_renderPassSubpass = 0u;
		_sharedState->_renderTargetHeight = extent[1];

		// Set the default viewport & scissor
		VkViewport defaultViewport = AsVkViewport(fb.GetDefaultViewport(), _sharedState->_renderTargetHeight);
		VkRect2D defaultScissor { offset[0], offset[1], extent[0], extent[1] };
		vkCmdSetViewport(_sharedState->_commandList.GetUnderlying().get(), 0, 1, &defaultViewport);
		vkCmdSetScissor(_sharedState->_commandList.GetUnderlying().get(), 0, 1, &defaultScissor);
		vkCmdSetStencilReference(_sharedState->_commandList.GetUnderlying().get(), VK_STENCIL_FACE_FRONT_AND_BACK, 0);		// we must set this to something, because all the pipelines we use have this marked as a dynamic state
		vkCmdSetDepthBounds(_sharedState->_commandList.GetUnderlying().get(), 0.0f, 1.0f);
	}

	void DeviceContext::EndRenderPass()
	{
		assert(!_sharedState->_currentEncoder);
		vkCmdEndRenderPass(_sharedState->_commandList.GetUnderlying().get());
		_sharedState->_renderPass = nullptr;
		_sharedState->_renderPassSamples = TextureSamples::Create();
		_sharedState->_renderPassSubpass = 0u;
	}

	bool DeviceContext::IsInRenderPass() const
	{
		return _sharedState->_renderPass != nullptr;
	}

	void DeviceContext::NextSubpass(VkSubpassContents contents)
	{
		assert(!_sharedState->_currentEncoder);
		vkCmdNextSubpass(_sharedState->_commandList.GetUnderlying().get(), contents);
		++_sharedState->_renderPassSubpass;
	}

	unsigned DeviceContext::GetCurrentSubpassIndex() const
	{
		return _sharedState->_renderPassSubpass;
	}

	BlitEncoder DeviceContext::BeginBlitEncoder()
	{
		if (_sharedState->_renderPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a blt pass while a render pass is in progress"));
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a blt pass while another blt pass is already in progress"));
		if (_sharedState->_currentEncoder)
			Throw(::Exceptions::BasicLabel("Attempting to begin a blt pass while an encoder is in progress"));
		_sharedState->_inBltPass = true;
		return BlitEncoder(*this);
	}

	void DeviceContext::EndBlitEncoder()
	{
		assert(_sharedState->_inBltPass);
		_sharedState->_inBltPass = false;
	}

	void        DeviceContext::Clear(const IResourceView& renderTarget, const VectorPattern<float,4>& clearColour)
	{
		// 
		auto& resView = *checked_cast<const ResourceView*>(&renderTarget);
		auto& res = *resView.GetVulkanResource();
		if (res.GetImage()) {
			VkClearColorValue clearValue;
			clearValue.float32[0] = clearColour[0];
			clearValue.float32[1] = clearColour[1];
			clearValue.float32[2] = clearColour[2];
			clearValue.float32[3] = clearColour[3];
			VkImageSubresourceRange subResRange = resView.GetImageSubresourceRange();
			_sharedState->_commandList.ClearColorImage(
				res.GetImage(),
				(VkImageLayout)Internal::AsVkImageLayout(res._steadyStateLayout),
				&clearValue, 1, &subResRange);
		} else {
			Throw(std::runtime_error("Attempting to clear non-image resource with GraphicsEncoder::Clear"));
		}
	}

	void        DeviceContext::Clear(const IResourceView& depthStencil, ClearFilter::BitField clearFilter, float depth, unsigned stencil)
	{
		auto& resView = *checked_cast<const ResourceView*>(&depthStencil);
		auto& res = *resView.GetVulkanResource();
		if (res.GetImage()) {
			VkClearDepthStencilValue clearValue;
			clearValue.depth = depth;
			clearValue.stencil = stencil;
			VkImageSubresourceRange subResRange = resView.GetImageSubresourceRange();
			if (!(clearFilter & ClearFilter::Depth))
				subResRange.aspectMask &= ~VK_IMAGE_ASPECT_DEPTH_BIT;
			if (!(clearFilter & ClearFilter::Stencil))
				subResRange.aspectMask &= ~VK_IMAGE_ASPECT_STENCIL_BIT;
			if (!subResRange.aspectMask) return;
			_sharedState->_commandList.ClearDepthStencilImage(
				res.GetImage(),
				(VkImageLayout)Internal::AsVkImageLayout(res._steadyStateLayout),
				&clearValue, 1, &subResRange);
		} else {
			Throw(std::runtime_error("Attempting to clear non-image resource with GraphicsEncoder::Clear"));
		}
	}

	CommandList& DeviceContext::GetActiveCommandList()
	{
		assert(_sharedState->_commandList.GetUnderlying());
		return _sharedState->_commandList;
	}

	bool DeviceContext::HasActiveCommandList()
	{
		return _sharedState->_commandList.GetUnderlying() != nullptr;
	}

	void DeviceContext::RequireResourceVisbility(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		return;
		
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			auto& cmdList = _sharedState->_commandList;

			uint64_t resourceGuids[resourceGuidsInit.size()];
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);

				// Don't record the guid for any resources that are already marked as becoming visible 
				// during this command list (this is the only way we can check relative ordering of 
				// initialization and use within the same command list)
			size_t mustBeVisibleInitialSize = cmdList._resourcesThatMustBeVisible.size();
			auto becomingI = cmdList._resourcesBecomingVisible.begin();
			cmdList._resourcesThatMustBeVisible.reserve(cmdList._resourcesBecomingVisible.size() + resourceGuidsInit.size());
			auto mustBeVisibleI = resourceGuids;
			while (mustBeVisibleI != &resourceGuids[resourceGuidsInit.size()]) {
				while (becomingI != cmdList._resourcesBecomingVisible.end() && *becomingI < *mustBeVisibleI) ++becomingI;
				if (becomingI == cmdList._resourcesBecomingVisible.end() || *becomingI != *mustBeVisibleI)
					cmdList._resourcesThatMustBeVisible.push_back(*mustBeVisibleI);		// we sort using std::inplace_merge just below
				++mustBeVisibleI;
			}
			std::inplace_merge(cmdList._resourcesThatMustBeVisible.begin(), cmdList._resourcesThatMustBeVisible.begin() + mustBeVisibleInitialSize, cmdList._resourcesThatMustBeVisible.end());
		#endif
	}

	void DeviceContext::MakeResourcesVisible(IteratorRange<const uint64_t*> resourceGuidsInit)
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			auto& cmdList = _sharedState->_commandList;

			uint64_t resourceGuids[resourceGuidsInit.size()];
			std::copy(resourceGuidsInit.begin(), resourceGuidsInit.end(), resourceGuids);
			std::sort(resourceGuids, &resourceGuids[resourceGuidsInit.size()]);

			auto mid = cmdList._resourcesBecomingVisible.insert(cmdList._resourcesBecomingVisible.end(), resourceGuids, &resourceGuids[resourceGuidsInit.size()]);
			std::inplace_merge(cmdList._resourcesBecomingVisible.begin(), mid, cmdList._resourcesBecomingVisible.end());
		#endif
	}

	void DeviceContext::ValidateCommitToQueue()
	{
		#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
			// We're going to commit the current command list to the queue. Let's validate resource visibility
			// All resources in _resourcesBecomingVisible must be on the "_resourcesVisibleToQueue" list in ObjectFactory
			// If they are not, it means one of the following:
			//   - that the resource was never made visible on a command list
			//   - the command list in which it was made visible hasn't yet been commited to the queue
			//   - it's made visible after it was used on this command list
			auto& cmdList = _sharedState->_commandList;

			auto factoryi = _factory->_resourcesVisibleToQueue.begin();
			auto searchi = cmdList._resourcesThatMustBeVisible.begin();
			while (searchi != cmdList._resourcesThatMustBeVisible.end()) {
				while (factoryi != _factory->_resourcesVisibleToQueue.end() && *factoryi < *searchi)
					++factoryi;

				if (factoryi == _factory->_resourcesVisibleToQueue.end() || *factoryi != *searchi)
					Throw(std::runtime_error("Attempting to use resource that hasn't been made visible. Ensure that all used resources have had Metal::CompleteInitialization() called on them"));

				++searchi;
			}
			cmdList._resourcesThatMustBeVisible.clear();

			// Now register the resources in _resourcesBecomingVisible as visible to the queue
			auto becomingVisibleEnd = std::unique(cmdList._resourcesBecomingVisible.begin(), cmdList._resourcesBecomingVisible.end());
			if (cmdList._resourcesBecomingVisible.begin() != becomingVisibleEnd) {
				std::vector<uint64_t> newVisibleToQueue;
				newVisibleToQueue.reserve(becomingVisibleEnd - cmdList._resourcesBecomingVisible.begin() + _factory->_resourcesVisibleToQueue.size());
				std::set_union(
					_factory->_resourcesVisibleToQueue.begin(), _factory->_resourcesVisibleToQueue.end(),
					cmdList._resourcesBecomingVisible.begin(), becomingVisibleEnd,
					std::back_inserter(newVisibleToQueue));

				std::swap(newVisibleToQueue, _factory->_resourcesVisibleToQueue);
			}
		#endif
	}

	DeviceContext::DeviceContext(
		ObjectFactory&			factory, 
		GlobalPools&            globalPools,
		CommandPool&            cmdPool, 
		CommandBufferType		cmdBufferType,
		TemporaryBufferSpace&	tempBufferSpace)
	: _cmdPool(&cmdPool), _cmdBufferType(cmdBufferType)
	, _factory(&factory)
	, _globalPools(&globalPools)
	{
		_sharedState = std::make_shared<VulkanEncoderSharedState>(*_factory, *_globalPools, tempBufferSpace);

		auto& globals = Internal::VulkanGlobalsTemp::GetInstance();
		globals._globalPools = &globalPools;
	}

	DeviceContext::~DeviceContext()
	{
		if (_captureForBindRecords)
			Internal::ValidateIsEmpty(*_captureForBindRecords);
	}

	void DeviceContext::PrepareForDestruction(IDevice*, IPresentationChain*) {}

	VulkanEncoderSharedState::VulkanEncoderSharedState(
		const ObjectFactory&    factory, 
		GlobalPools&            globalPools,
		TemporaryBufferSpace&	tempBufferSpace)
	: _graphicsDescriptors(factory, globalPools, 4)
	, _computeDescriptors(factory, globalPools, 4)
	, _tempBufferSpace(&tempBufferSpace)	
	{
		_renderPass = nullptr;
		_renderPassSubpass = 0u;
		_renderPassSamples = TextureSamples::Create();
		_currentEncoder = nullptr;
		_currentEncoderType = EncoderType::None;
		_ibBound = false;
		_inBltPass = false;
	}
	VulkanEncoderSharedState::~VulkanEncoderSharedState() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void CommandList::UpdateBuffer(
		VkBuffer buffer, VkDeviceSize offset, 
		VkDeviceSize byteCount, const void* data)
	{
		assert(byteCount <= 65536); // this restriction is imposed by Vulkan
		assert((byteCount & (4 - 1)) == 0);  // must be a multiple of 4
		assert(byteCount > 0 && data);
		vkCmdUpdateBuffer(
			_underlying.get(),
			buffer, 0,
			byteCount, (const uint32_t*)data);
	}

	void CommandList::BindDescriptorSets(
		VkPipelineBindPoint pipelineBindPoint,
		VkPipelineLayout layout,
		uint32_t firstSet,
		uint32_t descriptorSetCount,
		const VkDescriptorSet* pDescriptorSets,
		uint32_t dynamicOffsetCount,
		const uint32_t* pDynamicOffsets)
	{
		vkCmdBindDescriptorSets(
			_underlying.get(),
			pipelineBindPoint, layout, firstSet, 
			descriptorSetCount, pDescriptorSets,
			dynamicOffsetCount, pDynamicOffsets);
	}

	void CommandList::CopyBuffer(
		VkBuffer srcBuffer,
		VkBuffer dstBuffer,
		uint32_t regionCount,
		const VkBufferCopy* pRegions)
	{
		vkCmdCopyBuffer(_underlying.get(), srcBuffer, dstBuffer, regionCount, pRegions);
	}

	void CommandList::CopyImage(
		VkImage srcImage,
		VkImageLayout srcImageLayout,
		VkImage dstImage,
		VkImageLayout dstImageLayout,
		uint32_t regionCount,
		const VkImageCopy* pRegions)
	{
		vkCmdCopyImage(
			_underlying.get(), 
			srcImage, srcImageLayout, 
			dstImage, dstImageLayout, 
			regionCount, pRegions);
	}

	void CommandList::CopyBufferToImage(
		VkBuffer srcBuffer,
		VkImage dstImage,
		VkImageLayout dstImageLayout,
		uint32_t regionCount,
		const VkBufferImageCopy* pRegions)
	{
		vkCmdCopyBufferToImage(
			_underlying.get(),
			srcBuffer, 
			dstImage, dstImageLayout,
			regionCount, pRegions);
	}

	void CommandList::CopyImageToBuffer(
		VkImage srcImage,
		VkImageLayout srcImageLayout,
		VkBuffer dstBuffer,
		uint32_t regionCount,
		const VkBufferImageCopy* pRegions)
	{
		vkCmdCopyImageToBuffer(
			_underlying.get(),
			srcImage, srcImageLayout,
			dstBuffer,
			regionCount, pRegions);
	}

	void CommandList::ClearColorImage(
		VkImage image,
		VkImageLayout imageLayout,
		const VkClearColorValue* pColor,
		uint32_t rangeCount,
		const VkImageSubresourceRange* pRanges)
	{
		vkCmdClearColorImage(
			_underlying.get(),
			image, imageLayout, 
			pColor, rangeCount, pRanges);
	}

	void CommandList::ClearDepthStencilImage(
		VkImage image,
		VkImageLayout imageLayout,
		const VkClearDepthStencilValue* pDepthStencil,
		uint32_t rangeCount,
		const VkImageSubresourceRange* pRanges)
	{
		vkCmdClearDepthStencilImage(
			_underlying.get(),
			image, imageLayout, 
			pDepthStencil, rangeCount, pRanges);
	}

	void CommandList::PipelineBarrier(
		VkPipelineStageFlags            srcStageMask,
		VkPipelineStageFlags            dstStageMask,
		VkDependencyFlags               dependencyFlags,
		uint32_t                        memoryBarrierCount,
		const VkMemoryBarrier*          pMemoryBarriers,
		uint32_t                        bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier*    pBufferMemoryBarriers,
		uint32_t                        imageMemoryBarrierCount,
		const VkImageMemoryBarrier*     pImageMemoryBarriers)
	{
		vkCmdPipelineBarrier(
			_underlying.get(),
			srcStageMask, dstStageMask,
			dependencyFlags, 
			memoryBarrierCount, pMemoryBarriers,
			bufferMemoryBarrierCount, pBufferMemoryBarriers,
			imageMemoryBarrierCount, pImageMemoryBarriers);
	}

	void CommandList::PushConstants(
		VkPipelineLayout layout,
		VkShaderStageFlags stageFlags,
		uint32_t offset,
		uint32_t size,
		const void* pValues)
	{
		vkCmdPushConstants(
			_underlying.get(),
			layout, stageFlags,
			offset, size, pValues);
	}

	void CommandList::WriteTimestamp(
		VkPipelineStageFlagBits pipelineStage,
		VkQueryPool queryPool, uint32_t query)
	{
		vkCmdWriteTimestamp(_underlying.get(), pipelineStage, queryPool, query);
	}

	void CommandList::BeginQuery(VkQueryPool queryPool, uint32_t query, VkQueryControlFlags flags)
	{
		vkCmdBeginQuery(_underlying.get(), queryPool, query, flags);
	}

	void CommandList::EndQuery(VkQueryPool queryPool, uint32_t query)
	{
		vkCmdEndQuery(_underlying.get(), queryPool, query);
	}

	void CommandList::ResetQueryPool(
		VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
	{
		vkCmdResetQueryPool(_underlying.get(), queryPool, firstQuery, queryCount);
	}

	void CommandList::SetEvent(VkEvent evnt, VkPipelineStageFlags stageMask)
	{
		vkCmdSetEvent(_underlying.get(), evnt, stageMask);
	}

	CommandList::CommandList() {}
	CommandList::CommandList(const VulkanSharedPtr<VkCommandBuffer>& underlying)
	: _underlying(underlying) {}
	CommandList::~CommandList() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void VulkanEncoderSharedState::DescriptorCollection::ResetState(const CompiledPipelineLayout& layout)
	{
		_descriptorSets.resize(layout.GetDescriptorSetCount());
		for (unsigned c=0; c<_descriptorSets.size(); ++c) {
			_descriptorSets[c] = layout.GetBlankDescriptorSet(c).get();
			#if defined(VULKAN_VERBOSE_DEBUG)
				_currentlyBoundDesc[c] = layout.GetBlankDescriptorSetDebugInfo(c);
			#endif
		}
		_hasSetsAwaitingFlush = true;
	}

	VulkanEncoderSharedState::DescriptorCollection::DescriptorCollection(
		const ObjectFactory&    factory, 
		GlobalPools&            globalPools,
		unsigned				descriptorSetCount)
	: _factory(&factory), _globalPools(&globalPools)
	{
		_descriptorSets.resize(descriptorSetCount, nullptr);
		#if defined(VULKAN_VERBOSE_DEBUG)
			_currentlyBoundDesc.resize(descriptorSetCount);
		#endif
		_hasSetsAwaitingFlush = false;
	}

}}


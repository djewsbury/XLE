// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "InputLayout.h"
#include "Shader.h"
#include "State.h"
#include "TextureView.h"
#include "FrameBuffer.h"
#include "Pools.h"
#include "PipelineLayout.h"
#include "ShaderReflection.h"
#include "ExtensionFunctions.h"
#include "CommandList.h"
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

		struct DescriptorDebugTracking
		{
			#if defined(VULKAN_VERBOSE_DEBUG)
				std::vector<DescriptorSetDebugInfo> _currentlyBoundDesc;
			#endif
			void ResetState(const CompiledPipelineLayout&);
		};

		DescriptorDebugTracking	_graphicsDescriptorsTracking;
		DescriptorDebugTracking	_computeDescriptorsTracking;

		void* _currentEncoder = nullptr;
		SharedEncoder::EncoderType _currentEncoderType = SharedEncoder::EncoderType::None;

		bool _ibBound = false;		// (for debugging, validates that an index buffer actually is bound when calling DrawIndexed & alternatives)
		GlobalPools* _globalPools = nullptr;
		ObjectFactory* _objectFactory = nullptr;

		VulkanEncoderSharedState(ObjectFactory& factory, GlobalPools& globalPools);
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
		VLA(VkViewport, vkViewports, viewports.size());
		for (unsigned c=0; c<viewports.size(); ++c)
			vkViewports[c] = AsVkViewport(viewports[c], _sharedState->_renderTargetHeight);

		VLA(VkRect2D, vkScissors, scissorRects.size());
		for (unsigned c=0; c<scissorRects.size(); ++c)
			vkScissors[c] = AsVkRect2D(scissorRects[c], _sharedState->_renderTargetHeight);

		vkCmdSetViewport(_sharedState->_commandList.GetUnderlying().get(), 0, viewports.size(), vkViewports);
		vkCmdSetScissor(_sharedState->_commandList.GetUnderlying().get(), 0, scissorRects.size(), vkScissors);
	}

	inline void SharedEncoder::ValidateFlushedBoundUniforms()
	{
		#if defined(_DEBUG)
			// We will hit the following exception if a BoundUniforms object wasn't fully completed before ending the encoder
			// (ie, only some of the groups of the BoundUniforms were applied using ApplyLooseUniforms)
			// See BoundUniforms for more information.
			if (_pendingBoundUniforms) {
				_pendingBoundUniforms->AbortPendingApplies();
				_pendingBoundUniforms = nullptr;
				Throw(std::runtime_error("Incomplete BoundUniforms. Some required groups were not applied with ApplyLooseUniforms"));
			}
		#endif
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

	void GraphicsEncoder_ProgressivePipeline::Bind(const ShaderProgram& shaderProgram)
	{
		assert(shaderProgram.GetPipelineLayout().get() == _pipelineLayout.get());
		GraphicsPipelineBuilder::Bind(shaderProgram);
	}

	void GraphicsEncoder_ProgressivePipeline::Draw(unsigned vertexCount, unsigned startVertexLocation)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		ValidateFlushedBoundUniforms();
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
		ValidateFlushedBoundUniforms();
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
		ValidateFlushedBoundUniforms();
		if (BindGraphicsPipeline()) {
			vkCmdDraw(
				_sharedState->_commandList.GetUnderlying().get(),
				vertexCount, instanceCount,
				startVertexLocation, 0);
		}
	}

	void GraphicsEncoder_ProgressivePipeline::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
	{
		Log(Verbose) << "DrawIndexedInstances is very inefficient on Vulkan. Prefer pre-building buffers and vkCmdDrawIndirect" << std::endl;
		assert(_sharedState->_commandList.GetUnderlying());
		ValidateFlushedBoundUniforms();
		if (BindGraphicsPipeline()) {
			vkCmdDrawIndexed(
				_sharedState->_commandList.GetUnderlying().get(),
				indexCount, instanceCount,
				startIndexLocation, 0,
				0);
		}
	}

	void GraphicsEncoder_ProgressivePipeline::DrawAuto() 
	{
		assert(0);      // not implemented
	}

	inline void GraphicsEncoder_Optimized::BindPipeline(const GraphicsPipeline& pipeline)
	{
		assert(pipeline.get());
		// todo -- test compatibility of the bound pipeline layout with the shader pipeline layout
		assert(_sharedState->_commandList.GetUnderlying());
		if (_capturedStates) {
			if (_capturedStates->_currentPipeline != &pipeline) {
				vkCmdBindPipeline(
					_sharedState->_commandList.GetUnderlying().get(),
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					pipeline.get());
				DEBUG_ONLY(LogPipeline(pipeline));
				_capturedStates->_currentPipeline = &pipeline;
			}
		} else {
			vkCmdBindPipeline(
				_sharedState->_commandList.GetUnderlying().get(),
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipeline.get());
			DEBUG_ONLY(LogPipeline(pipeline));
		}
	}

	void GraphicsEncoder_Optimized::Draw(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned startVertexLocation)
	{
		assert(vertexCount);
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDraw(
			_sharedState->_commandList.GetUnderlying().get(),
			vertexCount, 1,
			startVertexLocation, 0);
	}
	
	void GraphicsEncoder_Optimized::DrawIndexed(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned startIndexLocation)
	{
		assert(_sharedState->_ibBound);
		assert(indexCount);
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDrawIndexed(
			_sharedState->_commandList.GetUnderlying().get(),
			indexCount, 1,
			startIndexLocation, 0,
			0);
	}

	void GraphicsEncoder_Optimized::DrawInstances(const GraphicsPipeline& pipeline, unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation)
	{
		assert(vertexCount);
		assert(instanceCount);
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDraw(
			_sharedState->_commandList.GetUnderlying().get(),
			vertexCount, instanceCount,
			startVertexLocation, 0);
	}

	void GraphicsEncoder_Optimized::DrawIndexedInstances(const GraphicsPipeline& pipeline, unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation)
	{
		assert(indexCount);
		assert(instanceCount);
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDrawIndexed(
			_sharedState->_commandList.GetUnderlying().get(),
			indexCount, instanceCount,
			startIndexLocation, 0,
			0);
	}

	void GraphicsEncoder_Optimized::DrawAuto(const GraphicsPipeline& pipeline)
	{
		assert(0);      // not implemented
	}

	void GraphicsEncoder_Optimized::DrawIndirect(const GraphicsPipeline& pipeline, const IResource& res, unsigned offset)
	{
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDrawIndirect(
			_sharedState->_commandList.GetUnderlying().get(),
			checked_cast<const Resource*>(&res)->GetBuffer(),
			offset,
			1, sizeof(uint32_t)*4);
	}

	void GraphicsEncoder_Optimized::DrawIndexedIndirect(const GraphicsPipeline& pipeline, const IResource& res, unsigned offset)
	{
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDrawIndexedIndirect(
			_sharedState->_commandList.GetUnderlying().get(),
			checked_cast<const Resource*>(&res)->GetBuffer(),
			offset,
			1, sizeof(uint32_t)*5);
	}

	void SharedEncoder::BindDescriptorSet(
		unsigned index, VkDescriptorSet set,
		IteratorRange<const unsigned*> dynamicOffsets
		VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo&& description))
	{
		auto encoderType = _sharedState->_currentEncoderType;
		assert(encoderType != EncoderType::None);
		assert(index < s_maxBoundDescriptorSetCount);
		assert(index < GetDescriptorSetCount());
		if (_capturedStates) {
			if (_capturedStates->_currentDescSet[index] != set)
				_sharedState->_commandList.BindDescriptorSets(
					(encoderType == EncoderType::Compute) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
					GetUnderlyingPipelineLayout(),
					index, 1, &set, 
					dynamicOffsets.size(), dynamicOffsets.begin());
			_capturedStates->_currentDescSet[index] = set;
		} else {
			_sharedState->_commandList.BindDescriptorSets(
				(encoderType == EncoderType::Compute) ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
				GetUnderlyingPipelineLayout(),
				index, 1, &set, 
				dynamicOffsets.size(), dynamicOffsets.begin());
		}

		#if defined(VULKAN_VERBOSE_DEBUG)
			auto& collection = (encoderType == EncoderType::Compute) ? _sharedState->_computeDescriptorsTracking : _sharedState->_graphicsDescriptorsTracking;
			collection._currentlyBoundDesc[index] = description;
		#endif
	}

	NumericUniformsInterface SharedEncoder::BeginNumericUniformsInterface()
	{
		auto& cmdList = _sharedState->_commandList;
		if (!cmdList._attachedStorage)
			cmdList._attachedStorage = _sharedState->_globalPools->_temporaryStorageManager->BeginCmdListReservation();

		return NumericUniformsInterface { 
			*_sharedState->_objectFactory,
			*_pipelineLayout,
			cmdList._attachedStorage,
			Internal::VulkanGlobalsTemp::GetInstance()._legacyRegisterBindings};
	}

	void			SharedEncoder::PushConstants(VkShaderStageFlags stageFlags, unsigned offset, IteratorRange<const void*> data)
	{
		auto encoderType = _sharedState->_currentEncoderType;
		if (encoderType == EncoderType::Compute) {
			assert(stageFlags == VK_SHADER_STAGE_COMPUTE_BIT);
		} else
			assert(!(stageFlags & VK_SHADER_STAGE_COMPUTE_BIT));
		_pipelineLayout->ValidatePushConstantsRange(offset, data.size(), stageFlags);
		_sharedState->_commandList.PushConstants(
			GetUnderlyingPipelineLayout(),
			stageFlags, offset, (uint32_t)data.size(), data.begin());
	}

	unsigned SharedEncoder::GetDescriptorSetCount()
	{
		return _pipelineLayout->GetDescriptorSetCount();
	}

	VkPipelineLayout SharedEncoder::GetUnderlyingPipelineLayout()
	{
		return _pipelineLayout->GetUnderlying();
	}

	void SharedEncoder::BeginStateCapture(CapturedStates& capturedStates)
	{
		assert(!_capturedStates);
		_capturedStates = &capturedStates;
	}
	
	void SharedEncoder::EndStateCapture()
	{
		assert(_capturedStates);
		_capturedStates = nullptr;
	}

	auto SharedEncoder::GetEncoderType() -> EncoderType
	{
		assert(_sharedState);
		return _sharedState->_currentEncoderType;
	}

	SharedEncoder::SharedEncoder(
		EncoderType encoderType,
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState)
	: _pipelineLayout(std::move(pipelineLayout))
	, _sharedState(std::move(sharedState))
	, _capturedStates(nullptr)
	{
		if (_sharedState) {
			assert(_sharedState->_currentEncoder == nullptr && _sharedState->_currentEncoderType == EncoderType::None);
			assert(encoderType == EncoderType::Compute || _sharedState->_renderPass != nullptr);
			assert(_pipelineLayout);
			_sharedState->_currentEncoder = this;
			_sharedState->_currentEncoderType = encoderType;

			auto* descCollection = (encoderType == EncoderType::Compute) ? &_sharedState->_computeDescriptorsTracking : &_sharedState->_graphicsDescriptorsTracking;
			descCollection->ResetState(*_pipelineLayout);

			// Bind default blank descriptor sets for the layout
			for (unsigned c=0; c<_pipelineLayout->GetDescriptorSetCount(); ++c) {
				BindDescriptorSet(
					c, _pipelineLayout->GetBlankDescriptorSet(c).get(), _pipelineLayout->GetBlankDescriptorSetDynamicOffsets(c)
					#if defined(VULKAN_VERBOSE_DEBUG)
						, DescriptorSetDebugInfo{_pipelineLayout->GetBlankDescriptorSetDebugInfo(c)}
					#endif
				);
			}
		}
	}

	SharedEncoder::~SharedEncoder()
	{
		if (_pendingBoundUniforms)
			_pendingBoundUniforms->AbortPendingApplies();		// this helps us surive exception scenarios -- but there is no lifecycle protection

		if (_sharedState) {
			assert(_sharedState->_currentEncoder == this);
			_sharedState->_currentEncoder = nullptr;
			_sharedState->_currentEncoderType = EncoderType::None;
		}
	}
	SharedEncoder::SharedEncoder(SharedEncoder&& moveFrom)
	{
		if (moveFrom._sharedState) {
			assert(moveFrom._sharedState->_currentEncoder == &moveFrom);
			_sharedState = std::move(moveFrom._sharedState);
			_sharedState->_currentEncoder = this;
		}
		_pipelineLayout = std::move(moveFrom._pipelineLayout);
		_capturedStates = std::move(moveFrom._capturedStates);
		moveFrom._capturedStates = nullptr;
		_pendingBoundUniforms = moveFrom._pendingBoundUniforms; moveFrom._pendingBoundUniforms = nullptr;
		_pendingBoundUniformsFlushGroupMask = moveFrom._pendingBoundUniformsFlushGroupMask;
		_pendingBoundUniformsCompletionMask = moveFrom._pendingBoundUniformsCompletionMask;
	}

	SharedEncoder& SharedEncoder::operator=(SharedEncoder&& moveFrom)
	{
		if (_pendingBoundUniforms)
			_pendingBoundUniforms->AbortPendingApplies();		// this helps us surive exception scenarios -- but there is no lifecycle protection

		if (_sharedState) {
			assert(_sharedState->_currentEncoder == this);
			_sharedState->_currentEncoder = nullptr;
			_sharedState->_currentEncoderType = EncoderType::None;
			_sharedState.reset();
		}

		if (moveFrom._sharedState) {
			assert(moveFrom._sharedState->_currentEncoder == &moveFrom);
			_sharedState = std::move(moveFrom._sharedState);
			_sharedState->_currentEncoder = this;
		}
		_pipelineLayout = std::move(moveFrom._pipelineLayout);
		_capturedStates = std::move(moveFrom._capturedStates);
		moveFrom._capturedStates = nullptr;
		_pendingBoundUniforms = moveFrom._pendingBoundUniforms; moveFrom._pendingBoundUniforms = nullptr;
		_pendingBoundUniformsFlushGroupMask = moveFrom._pendingBoundUniformsFlushGroupMask;
		_pendingBoundUniformsCompletionMask = moveFrom._pendingBoundUniformsCompletionMask;
		return *this;
	}

	GraphicsEncoder::GraphicsEncoder(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState,
		Type type)
	: SharedEncoder(EncoderType::Graphics, pipelineLayout, sharedState)
	, _type(type)
	{
	}

	GraphicsEncoder::~GraphicsEncoder()
	{
		if (_type == Type::StreamOutput && _sharedState) {
			assert(GetObjectFactory().GetExtensionFunctions()._endTransformFeedback);
			(*GetObjectFactory().GetExtensionFunctions()._endTransformFeedback)(
				_sharedState->_commandList.GetUnderlying().get(),
				0, 0, nullptr, nullptr);
		}
	}

	GraphicsEncoder::GraphicsEncoder(GraphicsEncoder&& moveFrom)
	: SharedEncoder(std::move(moveFrom))
	{
		_type = moveFrom._type;
		moveFrom._type = Type::Normal;
	}

	GraphicsEncoder& GraphicsEncoder::operator=(GraphicsEncoder&& moveFrom)
	{
		if (_sharedState && _type == Type::StreamOutput) {
			assert(GetObjectFactory().GetExtensionFunctions()._endTransformFeedback);
			(*GetObjectFactory().GetExtensionFunctions()._endTransformFeedback)(
				_sharedState->_commandList.GetUnderlying().get(),
				0, 0, nullptr, nullptr);
		}

		SharedEncoder::operator=(std::move(moveFrom));
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
		_sharedState->_currentEncoderType = EncoderType::ProgressiveGraphics;
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

	void ComputeEncoder::LogPipeline(const ComputePipeline& pipeline)
	{
		return;
		#if defined(_DEBUG)
			if (!Verbose.IsEnabled()) return;

			const CompiledShaderByteCode* shaders[(unsigned)ShaderStage::Max] = {};
			Log(Verbose) << "-------------ComputeShader------------" << std::endl;
			Log(Verbose) << SPIRVReflection(pipeline._shader.GetCompiledCode().GetByteCode()) << std::endl;
			shaders[(unsigned)ShaderStage::Compute] = &pipeline._shader.GetCompiledCode();

			#if defined(VULKAN_VERBOSE_DEBUG)
				const auto& descriptors = _sharedState->_computeDescriptors;
				_pipelineLayout->WriteDebugInfo(
					Log(Verbose),
					MakeIteratorRange(shaders),
					MakeIteratorRange(descriptors._currentlyBoundDesc));
			#endif
		#endif
	}

	inline void ComputeEncoder::BindPipeline(const ComputePipeline& pipeline)
	{
		if (_capturedStates) {
			if (_capturedStates->_currentPipeline != &pipeline) {
				// todo -- test compatibility of the bound pipeline layout with the shader pipeline layout
				vkCmdBindPipeline(
					_sharedState->_commandList.GetUnderlying().get(),
					VK_PIPELINE_BIND_POINT_COMPUTE,
					pipeline.get());
				DEBUG_ONLY(LogPipeline(pipeline));
				_capturedStates->_currentPipeline = &pipeline;
			}
		} else {
			// todo -- test compatibility of the bound pipeline layout with the shader pipeline layout
			vkCmdBindPipeline(
				_sharedState->_commandList.GetUnderlying().get(),
				VK_PIPELINE_BIND_POINT_COMPUTE,
				pipeline.get());
			DEBUG_ONLY(LogPipeline(pipeline));
		}
	}

	void ComputeEncoder::Dispatch(const ComputePipeline& pipeline, unsigned countX, unsigned countY, unsigned countZ)
	{
		assert(countX && countY && countZ);
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDispatch(
			_sharedState->_commandList.GetUnderlying().get(),
			countX, countY, countZ);
	}

	void ComputeEncoder::DispatchIndirect(const ComputePipeline& pipeline, const IResource& res, unsigned offset)
	{
		ValidateFlushedBoundUniforms();
		BindPipeline(pipeline);
		vkCmdDispatchIndirect(
			_sharedState->_commandList.GetUnderlying().get(),
			checked_cast<const Resource*>(&res)->GetBuffer(),
			offset);
	}

	ComputeEncoder::ComputeEncoder(ComputeEncoder&& moveFrom)
	: SharedEncoder(std::move(moveFrom))
	{
	}

	ComputeEncoder::ComputeEncoder() {}

	ComputeEncoder& ComputeEncoder::operator=(ComputeEncoder&& moveFrom)
	{
		SharedEncoder::operator=(std::move(moveFrom));
		return *this;
	}

	ComputeEncoder::ComputeEncoder(
		std::shared_ptr<CompiledPipelineLayout> pipelineLayout,
		std::shared_ptr<VulkanEncoderSharedState> sharedState)
	: SharedEncoder(EncoderType::Compute, std::move(pipelineLayout), std::move(sharedState))
	{
	}

	ComputeEncoder::~ComputeEncoder()
	{
	}

	GraphicsEncoder_Optimized DeviceContext::BeginGraphicsEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a graphics encoder while a blt encoder is in progress"));
		return GraphicsEncoder_Optimized { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState };
	}

	GraphicsEncoder_ProgressivePipeline DeviceContext::BeginGraphicsEncoder_ProgressivePipeline(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a graphics encoder while a blt encoder is in progress"));
		return GraphicsEncoder_ProgressivePipeline { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState, *_sharedState->_objectFactory, *_sharedState->_globalPools };
	}

	GraphicsEncoder_Optimized DeviceContext::BeginStreamOutputEncoder(
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		IteratorRange<const VertexBufferView*> outputBuffers)
	{
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a stream output encoder while a blt pass is in progress"));
		if (outputBuffers.empty())
			Throw(::Exceptions::BasicLabel("No stream output buffers provided to BeginStreamOutputEncoder"));

		if (!_sharedState->_objectFactory->GetExtensionFunctions()._beginTransformFeedback)
			Throw(::Exceptions::BasicLabel("Stream output extension not supported on this platform"));

		assert(_sharedState->_objectFactory->GetExtensionFunctions()._beginTransformFeedback);
		assert(_sharedState->_objectFactory->GetExtensionFunctions()._bindTransformFeedbackBuffers);

		VLA(VkDeviceSize, offsets, outputBuffers.size());
		VLA(VkBuffer, buffers, outputBuffers.size());
		for (unsigned c=0; c<outputBuffers.size(); ++c) {
			offsets[c] = outputBuffers[c]._offset;
			assert(const_cast<IResource*>(outputBuffers[c]._resource)->QueryInterface(typeid(Resource).hash_code()));
			buffers[c] = checked_cast<const Resource*>(outputBuffers[c]._resource)->GetBuffer();
		}

		(*_sharedState->_objectFactory->GetExtensionFunctions()._bindTransformFeedbackBuffers)(
			GetActiveCommandList().GetUnderlying().get(),
			0, outputBuffers.size(), 
			buffers, offsets, nullptr);

		(*_sharedState->_objectFactory->GetExtensionFunctions()._beginTransformFeedback)(
			GetActiveCommandList().GetUnderlying().get(),
			0, 0, nullptr, nullptr);

		return GraphicsEncoder_Optimized { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState, GraphicsEncoder::Type::StreamOutput };
	}

	ComputeEncoder DeviceContext::BeginComputeEncoder(std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
	{
		if (_sharedState->_renderPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a render pass is in progress"));
		if (_sharedState->_inBltPass)
			Throw(::Exceptions::BasicLabel("Attempting to begin a compute encoder while a blt pass is in progress"));
		return ComputeEncoder { checked_pointer_cast<CompiledPipelineLayout>(std::move(pipelineLayout)), _sharedState };
	}

	std::shared_ptr<DeviceContext> DeviceContext::Get(IThreadContext& threadContext)
	{
		IThreadContextVulkan* vulkanContext = 
			(IThreadContextVulkan*)threadContext.QueryInterface(
				typeid(IThreadContextVulkan).hash_code());
		if (vulkanContext)
			return vulkanContext->GetMetalContext();
		return nullptr;
	}

	GlobalPools&    DeviceContext::GetGlobalPools()
	{
		return *_sharedState->_globalPools;
	}

	ObjectFactory&	DeviceContext::GetFactory() const
	{
		return *_sharedState->_objectFactory;
	}

	VkDevice        DeviceContext::GetUnderlyingDevice()
	{
		return _sharedState->_objectFactory->GetDevice().get();
	}

	void		DeviceContext::BeginCommandList(std::shared_ptr<IAsyncTracker> asyncTracker)
	{
		assert(_sharedState && _sharedState->_globalPools && _cmdBufferPool);
		BeginCommandList(_cmdBufferPool->Allocate(_cmdBufferType), std::move(asyncTracker));
	}

	void		DeviceContext::BeginCommandList(const VulkanSharedPtr<VkCommandBuffer>& cmdList, std::shared_ptr<IAsyncTracker> asyncTracker)
	{
		assert(!_sharedState->_commandList.GetUnderlying());
		_sharedState->_commandList = CommandList(cmdList, std::move(asyncTracker));
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

	void		DeviceContext::ExecuteCommandList(CommandList&& cmdList)
	{
		assert(_sharedState->_commandList.GetUnderlying());
		_sharedState->_commandList.ExecuteSecondaryCommandList(std::move(cmdList));
	}

	auto        DeviceContext::ResolveCommandList() -> std::shared_ptr<CommandList>
	{
		assert(_sharedState->_commandList.GetUnderlying());
		assert(_sharedState->_commandList._asyncTracker);
		if (_captureForBindRecords)
			Internal::ValidateIsEmpty(*_captureForBindRecords);		// always complete these captures before completing a command list
		auto res = vkEndCommandBuffer(_sharedState->_commandList.GetUnderlying().get());
		if (res != VK_SUCCESS)
			Throw(VulkanAPIFailure(res, "Failure while ending command buffer"));

		// We will release our reference on _command list here.
		auto result = std::make_shared<CommandList>(std::move(_sharedState->_commandList));
		assert(!_sharedState->_commandList.GetUnderlying() && !_sharedState->_commandList._attachedStorage);
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
		
		VLA(VkClearValue, vkClearValues, fb._clearValuesOrdering.size());
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
		VkRect2D defaultScissor { {offset[0], offset[1]}, {extent[0], extent[1]} };
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

	void DeviceContext::BeginRenderPass(
        FrameBuffer& frameBuffer,
        IteratorRange<const ClearValue*> clearValues)
    {
        BeginRenderPass(
            frameBuffer, TextureSamples::Create(),
            frameBuffer.GetDefaultOffset(), frameBuffer.GetDefaultExtent(),
            clearValues);
    }

    void DeviceContext::BeginNextSubpass(FrameBuffer& frameBuffer)
    {
		NextSubpass(VK_SUBPASS_CONTENTS_INLINE);
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
				res._steadyStateImageLayout,
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
				res._steadyStateImageLayout,
				&clearValue, 1, &subResRange);
		} else {
			Throw(std::runtime_error("Attempting to clear non-image resource with GraphicsEncoder::Clear"));
		}
	}

	void        DeviceContext::ClearUInt(const IResourceView& renderTarget, const VectorPattern<unsigned,4>& clearColour)
	{
		// 
		auto& resView = *checked_cast<const ResourceView*>(&renderTarget);
		auto& res = *resView.GetVulkanResource();
		if (res.GetImage()) {
			VkClearColorValue clearValue;
			clearValue.uint32[0] = clearColour[0];
			clearValue.uint32[1] = clearColour[1];
			clearValue.uint32[2] = clearColour[2];
			clearValue.uint32[3] = clearColour[3];
			VkImageSubresourceRange subResRange = resView.GetImageSubresourceRange();
			_sharedState->_commandList.ClearColorImage(
				res.GetImage(),
				res._steadyStateImageLayout,
				&clearValue, 1, &subResRange);
		} else {
			Throw(std::runtime_error("Attempting to clear non-image resource with GraphicsEncoder::Clear"));
		}
	}

	void        DeviceContext::ClearFloat(const IResourceView& renderTarget, const VectorPattern<float,4>& clearColour)
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
				res._steadyStateImageLayout,
				&clearValue, 1, &subResRange);
		} else {
			Throw(std::runtime_error("Attempting to clear non-image resource with GraphicsEncoder::Clear"));
		}
	}

	void DeviceContext::BeginLabel(const char* label, const VectorPattern<float,4>& color)
	{
		#if defined(_DEBUG)
			VkDebugUtilsLabelEXT labelStruct =
			{
				VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, 	// sType
				NULL,                                    	// pNext
				label,                                   	// pLabelName
				{ color[0], color[1], color[2], color[3] }	// color
			};
			GetFactory().GetExtensionFunctions()._beginLabel(_sharedState->_commandList.GetUnderlying().get(), &labelStruct);
		#endif
	}

	void DeviceContext::EndLabel()
	{
		#if defined(_DEBUG)
			GetFactory().GetExtensionFunctions()._endLabel(_sharedState->_commandList.GetUnderlying().get());
		#endif
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

	TemporaryStorageResourceMap DeviceContext::MapTemporaryStorage(size_t byteCount, BindFlag::Enum type)
	{
		auto& cmdList = _sharedState->_commandList;
		if (!cmdList._attachedStorage)
			cmdList._attachedStorage = _sharedState->_globalPools->_temporaryStorageManager->BeginCmdListReservation();
		return cmdList._attachedStorage.MapStorage(byteCount, type);
	}

	DeviceContext::DeviceContext(
		ObjectFactory&			factory, 
		GlobalPools&            globalPools,
		std::shared_ptr<CommandBufferPool> cmdBufferPool,
		CommandBufferType		cmdBufferType)
	: _cmdBufferPool(std::move(cmdBufferPool)), _cmdBufferType(cmdBufferType)
	{
		_sharedState = std::make_shared<VulkanEncoderSharedState>(factory, globalPools);
	}

	DeviceContext::~DeviceContext()
	{
		if (_captureForBindRecords)
			Internal::ValidateIsEmpty(*_captureForBindRecords);
	}

	VulkanEncoderSharedState::VulkanEncoderSharedState(
		ObjectFactory&	factory, 
		GlobalPools&	globalPools)
	: _globalPools(&globalPools)
	, _objectFactory(&factory)
	{
		_renderPass = nullptr;
		_renderPassSubpass = 0u;
		_renderPassSamples = TextureSamples::Create();
		_currentEncoder = nullptr;
		_currentEncoderType = SharedEncoder::SharedEncoder::EncoderType::None;
		_ibBound = false;
		_inBltPass = false;
	}
	VulkanEncoderSharedState::~VulkanEncoderSharedState() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void VulkanEncoderSharedState::DescriptorDebugTracking::ResetState(const CompiledPipelineLayout& layout)
	{
		#if defined(VULKAN_VERBOSE_DEBUG)
			_currentlyBoundDesc.resize(layout.GetDescriptorSetCount());
		#endif
	}

}}


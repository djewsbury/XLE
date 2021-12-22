// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "InputLayout.h"
#include "Shader.h"
#include "State.h"
#include "Pools.h"
#include "ShaderReflection.h"
#include "PipelineLayout.h"
#include "../../Format.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ArithmeticUtils.h"

namespace RenderCore { namespace Metal_Vulkan
{
	static VkPrimitiveTopology AsNative(Topology topo)
	{
		switch (topo)
		{
		case Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case Topology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		default:
		case Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case Topology::LineListAdj: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;

		case Topology::PatchList1:
		/*case Topology::PatchList2:
		case Topology::PatchList3:
		case Topology::PatchList4:
		case Topology::PatchList5:
		case Topology::PatchList6:
		case Topology::PatchList7:
		case Topology::PatchList8:
		case Topology::PatchList9:
		case Topology::PatchList10:
		case Topology::PatchList11:
		case Topology::PatchList12:
		case Topology::PatchList13:
		case Topology::PatchList14:
		case Topology::PatchList15:
		case Topology::PatchList16:*/
			return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
		}

		// other Vulkan topologies:
		// VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
		// VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY
		// VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY
		// VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY
	}

	void        GraphicsPipelineBuilder::Bind(const RasterizationDesc& rasterizer)
	{
		_pipelineStale = true;
		_rasterizerState = rasterizer;
	}
	
	void        GraphicsPipelineBuilder::Bind(IteratorRange<const AttachmentBlendDesc*> blendStates)
	{
		_pipelineStale = true;
		_blendState = blendStates;
	}
	
	void        GraphicsPipelineBuilder::Bind(const DepthStencilDesc& depthStencilState)
	{
		_pipelineStale = true;
		_depthStencilState = depthStencilState;
	}

	void        GraphicsPipelineBuilder::Bind(const BoundInputLayout& inputLayout, Topology topology)
	{
		if (inputLayout.GetPipelineRelevantHash() != _iaHash) {
			auto newAttributes = inputLayout.GetAttributes();
			_iaAttributes.clear();
			_iaAttributes.insert(_iaAttributes.end(), newAttributes.begin(), newAttributes.end());
			auto newVBBindings = inputLayout.GetVBBindings();
			_iaVBBindings.clear();
			_iaVBBindings.insert(_iaVBBindings.end(), newVBBindings.begin(), newVBBindings.end());
			_iaHash = inputLayout.GetPipelineRelevantHash();
			_pipelineStale = true;
		}

		auto native = AsNative(topology);
		if (native != _topology) {
			_pipelineStale = true;
			_topology = native;
		}
	}

	void		GraphicsPipelineBuilder::UnbindInputLayout()
	{
		if (_iaHash != 0) {
			_iaAttributes.clear();
			_iaVBBindings.clear();
			_pipelineStale = true;
			_iaHash = 0;
		}
	}

	void        GraphicsPipelineBuilder::Bind(const ShaderProgram& shaderProgram)
	{
		if (_shaderProgram != &shaderProgram) {
			_shaderProgram = &shaderProgram;
			_pipelineStale = true;
		}
	}

	static VkPipelineShaderStageCreateInfo BuildShaderStage(VkShaderModule shader, VkShaderStageFlagBits stage, const std::string& entryPoint)
	{
		VkPipelineShaderStageCreateInfo result = {};
		result.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		result.pNext = nullptr;
		result.flags = 0;
		result.stage = stage;
		result.module = shader;
		result.pName = entryPoint.c_str();
		result.pSpecializationInfo = nullptr;
		return result;
	}

	std::shared_ptr<GraphicsPipeline> GraphicsPipelineBuilder::CreatePipeline(
		ObjectFactory& factory, VkPipelineCache pipelineCache,
		VkRenderPass renderPass, unsigned subpass, 
		TextureSamples samples)
	{
		assert(_shaderProgram && renderPass);

		VkPipelineShaderStageCreateInfo shaderStages[3];
		uint32_t shaderStageCount = 0;
		std::string vsEntryPoint = _shaderProgram->GetCompiledCode(ShaderStage::Vertex).GetEntryPoint().AsString();
		std::string psEntryPoint = _shaderProgram->GetCompiledCode(ShaderStage::Geometry).GetEntryPoint().AsString();
		std::string gsEntryPoint = _shaderProgram->GetCompiledCode(ShaderStage::Pixel).GetEntryPoint().AsString();
		const auto& vs = _shaderProgram->GetModule(ShaderStage::Vertex);
		const auto& gs = _shaderProgram->GetModule(ShaderStage::Geometry);
		const auto& ps = _shaderProgram->GetModule(ShaderStage::Pixel);
		if (vs) shaderStages[shaderStageCount++] = BuildShaderStage(vs.get(), VK_SHADER_STAGE_VERTEX_BIT, vsEntryPoint);
		if (gs) shaderStages[shaderStageCount++] = BuildShaderStage(gs.get(), VK_SHADER_STAGE_GEOMETRY_BIT, psEntryPoint);
		if (ps) shaderStages[shaderStageCount++] = BuildShaderStage(ps.get(), VK_SHADER_STAGE_FRAGMENT_BIT, gsEntryPoint);
		assert(shaderStageCount != 0);

		VkDynamicState dynamicStateEnables[4];
		VkPipelineDynamicStateCreateInfo dynamicState = {};
		memset(dynamicStateEnables, 0, sizeof(dynamicStateEnables));
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.pNext = nullptr;
		dynamicState.pDynamicStates = dynamicStateEnables;
		dynamicState.dynamicStateCount = 0;

		dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;

		if (_depthStencilState.depthBoundsTestEnable)
			dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;

		VkPipelineVertexInputStateCreateInfo vi = {};
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.pNext = nullptr;
		vi.flags = 0;
		vi.vertexBindingDescriptionCount = (uint32)_iaVBBindings.size();
		vi.pVertexBindingDescriptions = AsPointer(_iaVBBindings.begin());
		vi.vertexAttributeDescriptionCount = (uint32)_iaAttributes.size();
		vi.pVertexAttributeDescriptions = AsPointer(_iaAttributes.begin());

		VkPipelineInputAssemblyStateCreateInfo ia = {};
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.pNext = nullptr;
		ia.flags = 0;
		ia.primitiveRestartEnable = VK_FALSE;
		ia.topology = _topology;

		VkPipelineViewportStateCreateInfo vp = {};
		vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vp.pNext = nullptr;
		vp.flags = 0;
		vp.viewportCount = 1;
		dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_VIEWPORT;
		vp.scissorCount = 1;
		dynamicStateEnables[dynamicState.dynamicStateCount++] = VK_DYNAMIC_STATE_SCISSOR;
		vp.pScissors = nullptr;
		vp.pViewports = nullptr;

		VkPipelineMultisampleStateCreateInfo ms = {};
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.pNext = nullptr;
		ms.flags = 0;
		ms.pSampleMask = nullptr;
		ms.rasterizationSamples = (VkSampleCountFlagBits)AsSampleCountFlagBits(samples);
		ms.sampleShadingEnable = VK_FALSE;
		ms.alphaToCoverageEnable = VK_FALSE;
		ms.alphaToOneEnable = VK_FALSE;
		ms.minSampleShading = 0.0f;

		VkGraphicsPipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline.pNext = nullptr;
		pipeline.layout = _shaderProgram->GetPipelineLayout()->GetUnderlying();
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		pipeline.basePipelineIndex = 0;
		pipeline.flags = 0; // VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
		pipeline.pVertexInputState = &vi;
		pipeline.pInputAssemblyState = &ia;
		pipeline.pRasterizationState = &_rasterizerState;
		pipeline.pColorBlendState = &_blendState;
		pipeline.pTessellationState = nullptr;
		pipeline.pMultisampleState = &ms;
		pipeline.pDynamicState = &dynamicState;
		pipeline.pViewportState = &vp;
		pipeline.pDepthStencilState = &_depthStencilState;
		pipeline.pStages = shaderStages;
		pipeline.stageCount = shaderStageCount;
		pipeline.renderPass = renderPass;
		pipeline.subpass = subpass;

		auto vkPipeline = factory.CreateGraphicsPipeline(pipelineCache, pipeline);
		auto result = std::make_shared<GraphicsPipeline>(std::move(vkPipeline));
		result->_shader = *_shaderProgram;
		_pipelineStale = false;
		return result;
	}

	std::shared_ptr<GraphicsPipeline> GraphicsPipelineBuilder::CreatePipeline(ObjectFactory& factory)
	{
		assert(_currentSubpassIndex != ~0u && _currentRenderPass);
		return CreatePipeline(
			factory, GetGlobalPools()._mainPipelineCache.get(),
			_currentRenderPass.get(), _currentSubpassIndex, _currentTextureSamples);
	}

	void 		GraphicsPipelineBuilder::SetRenderPassConfiguration(const FrameBufferDesc& fbDesc, unsigned subPass)
	{
		const auto& samples = fbDesc.GetProperties()._samples;
		_renderPassConfigurationHash = CalculateFrameBufferRelevance(fbDesc, subPass);
		_currentRenderPass = GetGlobalPools()._renderPassPool.CreateVulkanRenderPass(fbDesc);
		_currentTextureSamples = fbDesc.GetProperties()._samples;
		_currentSubpassIndex = subPass;
	}

	uint64_t MergeHash(AttachmentViewDesc viewDesc, const FrameBufferDesc& fbDesc, uint64_t seed)
	{
		if (viewDesc._resourceName == ~0u) return seed;
		auto& attachment = fbDesc.GetAttachments()[viewDesc._resourceName];
		auto result = HashCombine(viewDesc._window.GetHash(), seed);
		result = HashCombine(uint64_t(attachment._format), seed);
		if (attachment._flags & AttachmentDesc::Flags::Multisampled)
			result = rotr64(result, fbDesc.GetProperties()._samples._sampleCount);
		return result;
	}

	uint64_t GraphicsPipelineBuilder::CalculateFrameBufferRelevance(const FrameBufferDesc& fbDesc, unsigned subPass)
	{
		// See section 8.2 in the Vulkan spec for more information about render pass compatibility
		// We need to take into account many of the properties of the FB, but not all of them
		// in particular, we don't need to consider layouts so much, nor load/store flags
		uint64_t hash = rotr64(DefaultSeed64, subPass);
		auto& sb = fbDesc.GetSubpasses()[subPass];
		for (auto& v:sb.GetOutputs()) hash = MergeHash(v, fbDesc, hash);
		hash = MergeHash(sb.GetDepthStencil(), fbDesc, hash);
		for (auto& v:sb.GetInputs()) hash = MergeHash(v, fbDesc, hash);
		if (fbDesc.GetSubpasses().size() != 1) {		// as per Vulkan spec, we can ignore resolve attachments but only if there's just a single subpass
			for (auto& v:sb.GetResolveOutputs()) hash = MergeHash(v, fbDesc, hash);
			hash = MergeHash(sb.GetResolveDepthStencil(), fbDesc, hash);
		}
		return hash;
	}

	GraphicsPipelineBuilder::GraphicsPipelineBuilder()
	{
		_shaderProgram = nullptr;
		_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		_pipelineStale = true;
		_iaHash = 0ull;
	}

	GraphicsPipelineBuilder::~GraphicsPipelineBuilder() {}

	GraphicsPipelineBuilder::GraphicsPipelineBuilder(const GraphicsPipelineBuilder& cloneFrom)
	{
		*this = cloneFrom;
	}

	GraphicsPipelineBuilder& GraphicsPipelineBuilder::operator=(const GraphicsPipelineBuilder& cloneFrom)
	{
		_rasterizerState = cloneFrom._rasterizerState;
		_blendState = cloneFrom._blendState;
		_depthStencilState = cloneFrom._depthStencilState;
		_topology = cloneFrom._topology;
		_iaAttributes = cloneFrom._iaAttributes;
		_iaVBBindings = cloneFrom._iaVBBindings;
		_iaHash = cloneFrom._iaHash;
		_shaderProgram = cloneFrom._shaderProgram;
		_pipelineStale = cloneFrom._pipelineStale;
		return *this;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	void        ComputePipelineBuilder::Bind(const ComputeShader& shader)
	{
		_shader = &shader;
		_pipelineStale = true;
	}

	std::shared_ptr<ComputePipeline> ComputePipelineBuilder::CreatePipeline(
		ObjectFactory& factory,
		VkPipelineCache pipelineCache)
	{
		VkComputePipelineCreateInfo pipeline = {};
		pipeline.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipeline.pNext = nullptr;
		pipeline.flags = 0; // VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
		pipeline.layout = _shader->GetPipelineLayout()->GetUnderlying();
		pipeline.basePipelineHandle = VK_NULL_HANDLE;
		pipeline.basePipelineIndex = 0;

		assert(_shader);
		std::string csEntryPoint = _shader->GetCompiledCode().GetEntryPoint().AsString();
		pipeline.stage = BuildShaderStage(_shader->GetModule().get(), VK_SHADER_STAGE_COMPUTE_BIT, csEntryPoint);

		auto vkPipeline = factory.CreateComputePipeline(pipelineCache, pipeline);
		auto result = std::make_shared<ComputePipeline>(std::move(vkPipeline));
		result->_shader = *_shader;
		_pipelineStale = false;
		return result;
	}

	std::shared_ptr<ComputePipeline> ComputePipelineBuilder::CreatePipeline(ObjectFactory& factory)
	{
		return CreatePipeline(factory, GetGlobalPools()._mainPipelineCache.get());
	}

	ComputePipelineBuilder::ComputePipelineBuilder()
	{
		_shader = nullptr;
		_pipelineStale = true;
	}
	ComputePipelineBuilder::~ComputePipelineBuilder() {}

	ComputePipelineBuilder::ComputePipelineBuilder(const ComputePipelineBuilder& cloneFrom)
	{
		*this = cloneFrom;
	}

	ComputePipelineBuilder& ComputePipelineBuilder::operator=(const ComputePipelineBuilder& cloneFrom)
	{
		_shader = cloneFrom._shader;
		_pipelineStale = cloneFrom._pipelineStale;
		return *this;
	}

	uint64_t GraphicsPipeline::GetInterfaceBindingGUID() const
	{
		return _shader.GetInterfaceBindingGUID();
	}

	GraphicsPipeline::GraphicsPipeline(VulkanUniquePtr<VkPipeline>&& pipeline)
	: VulkanUniquePtr<VkPipeline>(std::move(pipeline))
	{}
	GraphicsPipeline::~GraphicsPipeline() {}

	uint64_t ComputePipeline::GetInterfaceBindingGUID() const
	{
		return _shader.GetInterfaceBindingGUID();
	}

	ComputePipeline::ComputePipeline(VulkanUniquePtr<VkPipeline>&& pipeline)
	: VulkanUniquePtr<VkPipeline>(std::move(pipeline))
	{}
	ComputePipeline::~ComputePipeline() {}
}}


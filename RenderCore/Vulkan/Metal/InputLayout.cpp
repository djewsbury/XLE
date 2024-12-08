// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputLayout.h"
#include "ShaderReflection.h"
#include "Shader.h"
#include "Format.h"
#include "PipelineLayout.h"
#include "DeviceContext.h"
#include "Pools.h"
#include "../../Format.h"
#include "../../Types.h"
#include "../../BufferView.h"
#include "../../UniformsStream.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "../../../Utility/BitUtils.h"
#include "../../../Utility/StringFormat.h"
#include <sstream>
#include <map>
#include <set>

#include "IncludeVulkan.h"

namespace RenderCore { namespace Metal_Vulkan
{
	struct ReflectionVariableInformation
	{
		SPIRVReflection::Binding _binding = {};
		SPIRVReflection::StorageClass _storageClass = SPIRVReflection::StorageClass::Unknown;
		const SPIRVReflection::BasicType* _basicType = nullptr;
		const SPIRVReflection::ResourceType* _resourceType = nullptr;
		const SPIRVReflection::VectorType* _vectorType = nullptr;
		std::optional<unsigned> _arrayElementCount;
		bool _isStructType = false;
		bool _isRuntimeArrayStructType = false;
		StringSection<> _name;
	};

	static ReflectionVariableInformation GetReflectionVariableInformation(
		const SPIRVReflection& reflection, SPIRVReflection::ObjectId objectId)
	{
		ReflectionVariableInformation result;

		auto n = LowerBound(reflection._names, objectId);
		if (n != reflection._names.end() && n->first == objectId)
			result._name = n->second;

		if (result._name.IsEmpty()) return result;

		auto b = LowerBound(reflection._bindings, objectId);
		if (b != reflection._bindings.end() && b->first == objectId)
			result._binding = b->second;

		// Using the type info in reflection, figure out what descriptor slot is associated
		// The spir-v type system is fairly rich, but we don't really need to interpret everything
		// in it. We just need to know enough to figure out the descriptor set slot type.
		// We'll try to be a little flexible to try to avoid having to support all spir-v typing 
		// exhaustively

		auto v = LowerBound(reflection._variables, objectId);
		if (v != reflection._variables.end() && v->first == objectId) {
			result._storageClass = v->second._storage;
			auto typeToLookup = v->second._type;

			auto p = LowerBound(reflection._pointerTypes, typeToLookup);
			if (p != reflection._pointerTypes.end() && p->first == typeToLookup)
				typeToLookup = p->second._targetType;
			
			auto a = LowerBound(reflection._arrayTypes, typeToLookup);
			if (a != reflection._arrayTypes.end() && a->first == typeToLookup) {
				result._arrayElementCount = a->second._elementCount;
				typeToLookup = a->second._elementType;
			}

			auto t = LowerBound(reflection._basicTypes, typeToLookup);
			if (t != reflection._basicTypes.end() && t->first == typeToLookup) {
				result._basicType = &t->second;
			} else if (std::find(reflection._structTypes.begin(), reflection._structTypes.end(), typeToLookup) != reflection._structTypes.end()) {
				// a structure will require some kind of buffer as input
				result._isStructType = true;

				// When using the HLSLCC cross-compiler; we end up with the variable having name
				// like "<cbuffername>_inst" and the type will be "<cbuffername>"
				// In this case, the name we're interested in isn't actually the variable
				// name itself, but instead the name of the struct type. As per HLSL, this
				// is the name we use for binding
				// By contrast, when using the DX HLSL compiler, the variable will have the name
				// "<cbuffername>" and the type will be "<cbuffername>.type"
				if (XlEndsWith(result._name, MakeStringSectionLiteral("_inst"))) {
					auto n = LowerBound(reflection._names, typeToLookup);
					if (n != reflection._names.end() && n->first == typeToLookup)
						result._name = n->second;
				}
			} else if (std::find(reflection._runtimeArrayStructTypes.begin(), reflection._runtimeArrayStructTypes.end(), typeToLookup) != reflection._runtimeArrayStructTypes.end()) {
				result._isRuntimeArrayStructType = true;
			} else {
				auto v = LowerBound(reflection._vectorTypes, typeToLookup);
				if (v != reflection._vectorTypes.end() && v->first == typeToLookup) {
					result._vectorType = &v->second;
				} else {
					auto r = LowerBound(reflection._resourceTypes, typeToLookup);
					if (r != reflection._resourceTypes.end() && r->first == typeToLookup) {
						result._resourceType = &r->second;
					} else {
						#if defined(_DEBUG)
							std::cout << "Could not understand type information for input " << result._name << std::endl;
						#endif
					}
				}
			}
		}

		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static VkVertexInputRate AsVkVertexInputRate(InputDataRate dataRate)
	{
		switch (dataRate) {
		case InputDataRate::PerVertex: return VK_VERTEX_INPUT_RATE_VERTEX;
		case InputDataRate::PerInstance: return VK_VERTEX_INPUT_RATE_INSTANCE;
		default:
			UNREACHABLE();
			return VK_VERTEX_INPUT_RATE_VERTEX;
		}
	}

	BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader)
	{
		// find the vertex inputs into the shader, and match them against the input layout
		auto vertexStrides = CalculateVertexStrides(layout);

		SPIRVReflection reflection(shader.GetByteCode());
		_attributes.reserve(layout.size());

		VLA(unsigned, inputDataRatePerVB, vertexStrides.size());
		for (size_t c=0; c<vertexStrides.size(); ++c)
			inputDataRatePerVB[c] = ~0u;

		// Build the VkVertexInputAttributeDescription in the order of the
		// input slots to make it easy to generate the trackingOffset separately
		// for each input
		for (unsigned vbIndex=0; vbIndex<vertexStrides.size(); ++vbIndex) {
			unsigned trackingOffset = 0;
			for (unsigned c=0; c<layout.size(); ++c) {
				const auto& e = layout[c];
				if (e._inputSlot != vbIndex) continue;
				
				auto hash = Hash64(e._semanticName) + e._semanticIndex;
				auto offset = e._alignedByteOffset == ~0x0u ? trackingOffset : e._alignedByteOffset;
				trackingOffset = offset + BitsPerPixel(e._nativeFormat) / 8;

				assert((offset % VertexAttributeRequiredAlignment(e._nativeFormat)) == 0);

				auto i = LowerBound(reflection._inputInterfaceQuickLookup, hash);
				if (i == reflection._inputInterfaceQuickLookup.end() || i->first != hash)
					continue;   // Could not be bound

				VkVertexInputAttributeDescription desc;
				desc.location = i->second._location;
				desc.binding = e._inputSlot;
				desc.format = (VkFormat)AsVkFormat(e._nativeFormat);
				desc.offset = offset;
				_attributes.push_back(desc);

				if (inputDataRatePerVB[e._inputSlot] != ~0u) {
					// This is a unique restriction for Vulkan -- the data rate is on the vertex buffer
					// binding, not the attribute binding. This means that we can't mix data rates
					// for the same input slot.
					//
					// We could get around this by splitting a single binding into 2 _vbBindingDescriptions
					// (effectively binding the same VB twice, one for each data rate)
					// Then we would also need to remap the vertex buffer assignments when they are applied
					// via vkCmdBindVertexBuffers.
					//
					// However, I think this restriction is actually pretty practical. It probably makes 
					// more sense to just enforce this idea on all gfx-apis. The client can double up their
					// bindings if they really need to; but in practice they probably are already using
					// a separate VB for the per-instance data anyway.
					if (inputDataRatePerVB[e._inputSlot] != (unsigned)AsVkVertexInputRate(e._inputSlotClass))
						Throw(std::runtime_error("In Vulkan, the data rate for all attribute bindings from a given input vertex buffer must be the same. That is, if you want to mix data rates in a draw call, you must use separate vertex buffers for each data rate."));
				} else {
					inputDataRatePerVB[e._inputSlot] = (unsigned)AsVkVertexInputRate(e._inputSlotClass);
				}

				if (e._inputSlotClass == InputDataRate::PerInstance && e._instanceDataStepRate != 0 && e._instanceDataStepRate != 1)
					Throw(std::runtime_error("Instance step data rates other than 1 not supported"));
			}
		}

		_vbBindingDescriptions.reserve(vertexStrides.size());
		for (unsigned b=0; b<(unsigned)vertexStrides.size(); ++b) {
			// inputDataRatePerVB[b] will only be ~0u if there were no successful
			// binds for this bind slot
			if (inputDataRatePerVB[b] == ~0u)
				continue;
			assert(vertexStrides[b] != 0);
			_vbBindingDescriptions.push_back({b, vertexStrides[b], (VkVertexInputRate)inputDataRatePerVB[b]});
		}

		_pipelineRelevantHash = Hash64(AsPointer(_attributes.begin()), AsPointer(_attributes.end()));
		_pipelineRelevantHash = Hash64(AsPointer(_vbBindingDescriptions.begin()), AsPointer(_vbBindingDescriptions.end()), _pipelineRelevantHash);
		CalculateAllAttributesBound(reflection);
	}

	BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& shader)
	: BoundInputLayout(layout, shader.GetCompiledCode(ShaderStage::Vertex))
	{
	}

	BoundInputLayout::BoundInputLayout(
		IteratorRange<const SlotBinding*> layouts,
		const CompiledShaderByteCode& shader)
	{
		SPIRVReflection reflection(shader.GetByteCode());
		_vbBindingDescriptions.reserve(layouts.size());

		for (unsigned slot=0; slot<layouts.size(); ++slot) {
			bool boundAtLeastOne = false;
			uint32_t accumulatingOffset = (uint32_t)0;
			for (unsigned ei=0; ei<layouts[slot]._elements.size(); ++ei) {
				const auto& e = layouts[slot]._elements[ei];
				auto hash = e._semanticHash;

				assert((accumulatingOffset % VertexAttributeRequiredAlignment(e._nativeFormat)) == 0);

				auto i = LowerBound(reflection._inputInterfaceQuickLookup, hash);
				if (i == reflection._inputInterfaceQuickLookup.end() || i->first != hash) {
					accumulatingOffset += BitsPerPixel(e._nativeFormat) / 8;
					continue;
				}

				VkVertexInputAttributeDescription desc;
				desc.location = i->second._location;
				desc.binding = slot;
				desc.format = (VkFormat)AsVkFormat(e._nativeFormat);
				desc.offset = accumulatingOffset;
				_attributes.push_back(desc);

				accumulatingOffset += BitsPerPixel(e._nativeFormat) / 8;
				boundAtLeastOne = true;
			}

			if (boundAtLeastOne) {
				auto inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				if (layouts[slot]._instanceStepDataRate != 0)
					inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
				_vbBindingDescriptions.push_back({slot, accumulatingOffset, inputRate});
			}
		}

		_pipelineRelevantHash = Hash64(AsPointer(_attributes.begin()), AsPointer(_attributes.end()));
		_pipelineRelevantHash = Hash64(AsPointer(_vbBindingDescriptions.begin()), AsPointer(_vbBindingDescriptions.end()), _pipelineRelevantHash);
		CalculateAllAttributesBound(reflection);
	}

	void BoundInputLayout::CalculateAllAttributesBound(const SPIRVReflection& reflection)
	{
		_allAttributesBound = true;
		for (const auto&v:reflection._entryPoint._interface) {
			auto reflectionVariable = GetReflectionVariableInformation(reflection, v);
			if (reflectionVariable._storageClass != SPIRVReflection::StorageClass::Input) continue;
			if (reflectionVariable._binding._location == ~0u) continue;
			auto loc = reflectionVariable._binding._location;

			auto existing = std::find_if(
				_attributes.begin(), _attributes.end(), 
				[loc](const auto& c) { return c.location == loc; });
			_allAttributesBound &= (existing != _attributes.end());
		}
	}

	std::vector<std::string> BoundInputLayout::FindUnboundShaderAttributes(const CompiledShaderByteCode& shader) const
	{
		return FindUnboundShaderAttributes(SPIRVReflection{shader.GetByteCode()});
	}

	std::vector<std::string> BoundInputLayout::FindUnboundShaderAttributes(const ShaderProgram& shader) const
	{
		return FindUnboundShaderAttributes(SPIRVReflection{shader.GetCompiledCode(ShaderStage::Vertex).GetByteCode()});
	}

	std::vector<std::string> BoundInputLayout::FindUnboundShaderAttributes(const SPIRVReflection& reflection) const
	{
		assert(!_allAttributesBound);		// prefer not call this if AllAttributesBound() return true, given we've already cached that result

		std::vector<std::string> result;
		result.reserve(reflection._entryPoint._interface.size());
		for (const auto&v:reflection._entryPoint._interface) {
			auto reflectionVariable = GetReflectionVariableInformation(reflection, v);
			if (reflectionVariable._storageClass != SPIRVReflection::StorageClass::Input) continue;
			if (reflectionVariable._binding._location == ~0u) continue;
			auto loc = reflectionVariable._binding._location;

			auto existing = std::find_if(
				_attributes.begin(), _attributes.end(), 
				[loc](const auto& c) { return c.location == loc; });
			if (existing == _attributes.end())
				result.push_back(reflectionVariable._name.AsString());
		}
		return result;
	}

	BoundInputLayout::BoundInputLayout(
		IteratorRange<const SlotBinding*> layouts,
		const ShaderProgram& shader)
	: BoundInputLayout(layouts, shader.GetCompiledCode(ShaderStage::Vertex))
	{
	}

	BoundInputLayout::BoundInputLayout() : _pipelineRelevantHash(0ull), _allAttributesBound(true) {}
	BoundInputLayout::~BoundInputLayout() {}

		////////////////////////////////////////////////////////////////////////////////////////////////

	enum class UniformStreamType { ResourceView, ImmediateData, Sampler, Dummy, None };

	static std::tuple<UniformStreamType, unsigned, unsigned> FindBinding(
		IteratorRange<const UniformsStreamInterface* const*> looseUniforms,
		uint64_t bindingName)
	{
		for (signed groupIdx = (signed)looseUniforms.size()-1; groupIdx>=0; --groupIdx) {
			const auto& group = looseUniforms[groupIdx];
			auto srv = std::find(group->GetResourceViewBindings().begin(), group->GetResourceViewBindings().end(), bindingName);
			if (srv != group->GetResourceViewBindings().end()) {
				auto inputSlot = (unsigned)std::distance(group->GetResourceViewBindings().begin(), srv);
				return std::make_tuple(UniformStreamType::ResourceView, groupIdx, inputSlot);
			}

			auto imData = std::find(group->GetImmediateDataBindings().begin(), group->GetImmediateDataBindings().end(), bindingName);
			if (imData != group->GetImmediateDataBindings().end()) {
				auto inputSlot = (unsigned)std::distance(group->GetImmediateDataBindings().begin(), imData);
				return std::make_tuple(UniformStreamType::ImmediateData, groupIdx, inputSlot);
			}

			auto sampler = std::find(group->GetSamplerBindings().begin(), group->GetSamplerBindings().end(), bindingName);
			if (sampler != group->GetSamplerBindings().end()) {
				auto inputSlot = (unsigned)std::distance(group->GetSamplerBindings().begin(), sampler);
				return std::make_tuple(UniformStreamType::Sampler, groupIdx, inputSlot);
			}
		}

		return std::make_tuple(UniformStreamType::None, ~0u, ~0u);
	}

	const uint32_t s_arrayBindingFlag = 1u<<31u;

	static unsigned CalculateDynamicOffsetCount(IteratorRange<const DescriptorSlot*> signature)
	{
		unsigned result = 0;
		for (const auto& s:signature)
			if (s._type == DescriptorType::UniformBufferDynamicOffset || s._type == DescriptorType::UnorderedAccessBufferDynamicOffset)
				++result;
		return result;
	}

	static bool SlotTypeCompatibleWithBinding(UniformStreamType bindingType, DescriptorType slotType)
	{
		if (bindingType == UniformStreamType::ResourceView)
			return slotType == DescriptorType::SampledTexture || slotType == DescriptorType::UnorderedAccessTexture 
				|| slotType == DescriptorType::UniformBuffer || slotType == DescriptorType::UnorderedAccessBuffer 
				|| slotType == DescriptorType::InputAttachment
				|| slotType == DescriptorType::UniformTexelBuffer || slotType == DescriptorType::UnorderedAccessTexelBuffer
				|| slotType == DescriptorType::UniformBufferDynamicOffset || slotType == DescriptorType::UnorderedAccessBufferDynamicOffset;
		else if (bindingType == UniformStreamType::ImmediateData)
			return slotType == DescriptorType::UniformBuffer || slotType == DescriptorType::UniformBufferDynamicOffset;			// we can only actually write immediate data to uniform buffers currently -- storage buffers, texel buffers, etc, aren't supported (to avoid the extra complexity that support would bring)
		else if (bindingType == UniformStreamType::Sampler)
			return slotType == DescriptorType::Sampler;

		UNREACHABLE();
		return true;
	}

	static bool ShaderVariableCompatibleWithDescriptorSet(const ReflectionVariableInformation& reflectionVariable, DescriptorType slotType)
	{
		assert(!reflectionVariable._vectorType);		// raw vector types not supported
		switch (slotType) { 
		case DescriptorType::SampledTexture:
		case DescriptorType::UnorderedAccessTexture:
			if (reflectionVariable._basicType)
				return !reflectionVariable._isStructType && !reflectionVariable._isRuntimeArrayStructType && (*reflectionVariable._basicType == SPIRVReflection::BasicType::SampledImage || *reflectionVariable._basicType == SPIRVReflection::BasicType::Image);
			if (reflectionVariable._resourceType) {
				assert(reflectionVariable._resourceType->_readWriteVariation == (slotType == DescriptorType::UnorderedAccessTexture));
				return !reflectionVariable._isStructType && !reflectionVariable._isRuntimeArrayStructType && ((reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Image1D) || (reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Image2D) || (reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Image3D) || (reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::ImageCube));
			}
			return false;
		case DescriptorType::UniformBuffer:
		case DescriptorType::UniformBufferDynamicOffset:
			assert(!reflectionVariable._resourceType || !reflectionVariable._resourceType->_readWriteVariation);
			return reflectionVariable._isStructType || (reflectionVariable._basicType && (*reflectionVariable._basicType != SPIRVReflection::BasicType::Image && *reflectionVariable._basicType != SPIRVReflection::BasicType::SampledImage && *reflectionVariable._basicType != SPIRVReflection::BasicType::Sampler));
		case DescriptorType::UnorderedAccessBuffer:
		case DescriptorType::UnorderedAccessBufferDynamicOffset:
			return reflectionVariable._isStructType || reflectionVariable._isRuntimeArrayStructType || (reflectionVariable._resourceType && reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Buffer && reflectionVariable._resourceType->_readWriteVariation);
		case DescriptorType::UniformTexelBuffer:
			return !reflectionVariable._isStructType && !reflectionVariable._isRuntimeArrayStructType && reflectionVariable._resourceType && reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Buffer && !reflectionVariable._resourceType->_readWriteVariation;
		case DescriptorType::UnorderedAccessTexelBuffer:
			return !reflectionVariable._isStructType && !reflectionVariable._isRuntimeArrayStructType && reflectionVariable._resourceType && reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::Buffer && reflectionVariable._resourceType->_readWriteVariation;
		case DescriptorType::Sampler:
			return !reflectionVariable._isStructType && !reflectionVariable._isRuntimeArrayStructType && reflectionVariable._basicType && *reflectionVariable._basicType == SPIRVReflection::BasicType::Sampler; 
		case DescriptorType::InputAttachment:
			return (reflectionVariable._binding._inputAttachmentIndex != ~0u) && reflectionVariable._resourceType && reflectionVariable._resourceType->_category == SPIRVReflection::ResourceCategory::InputAttachment;
		case DescriptorType::Empty:
		default:
			return false;
		}
	}

	static ProgressiveDescriptorSetBuilder::ResourceDims ResourceDimsFromVariable(const ReflectionVariableInformation& reflectionVariable)
	{
		// For resource types, the shader has some resource requirements that are more specific than can be represented by the DescriptorType
		// These relate to the type of shader variable -- we can extract them as so:
		using ResourceDims = ProgressiveDescriptorSetBuilder::ResourceDims;
		if (reflectionVariable._resourceType) {
			auto* res = reflectionVariable._resourceType;
			// (note that we ignore the _readWriteVariation flag in these cases)
			switch (res->_category) {
			case SPIRVReflection::ResourceCategory::Image1D:
				assert(!res->_multisampleVariation);
				return res->_arrayVariation ? ResourceDims::Dim1DArray : ResourceDims::Dim1D;
			case SPIRVReflection::ResourceCategory::Image2D:
				if (res->_multisampleVariation)
					return res->_arrayVariation ? ResourceDims::Dim2DMSArray : ResourceDims::Dim2DMS;
				return res->_arrayVariation ? ResourceDims::Dim2DArray : ResourceDims::Dim2D;
			case SPIRVReflection::ResourceCategory::Image3D:
				assert(!res->_arrayVariation && !res->_multisampleVariation);
				return ResourceDims::Dim3D;
			case SPIRVReflection::ResourceCategory::ImageCube:
				assert(!res->_multisampleVariation);
				return res->_arrayVariation ? ResourceDims::DimCubeArray : ResourceDims::DimCube;
			case SPIRVReflection::ResourceCategory::Buffer:
				return ResourceDims::DimBuffer;
			case SPIRVReflection::ResourceCategory::InputAttachment:
				return ResourceDims::DimInputAttachment;
			default:
				break;
			}
		} else if (reflectionVariable._isStructType || reflectionVariable._isRuntimeArrayStructType) {
			return ResourceDims::DimBuffer;
		}

		return ProgressiveDescriptorSetBuilder::ResourceDims::Unknown;
	}

	class BoundUniforms::ConstructionHelper
	{
	public:
		std::map<unsigned, std::tuple<unsigned, unsigned, const DescriptorSetSignature*>> _fixedDescriptorSets;
		IteratorRange<const UniformsStreamInterface* const*> _looseUniforms = {};
		const CompiledPipelineLayout* _pipelineLayout = nullptr;
		GlobalPools* _globalPools = nullptr;	// only needed when getting the reusable descriptor set group from DescriptorPool

		struct GroupRules
		{
			std::vector<AdaptiveSetBindingRules> _adaptiveSetRules;
			std::vector<PushConstantBindingRules> _pushConstantsRules;
			std::vector<FixedDescriptorSetBindingRules> _fixedDescriptorSetRules;

			uint64_t _groupRulesHash;
			uint64_t _boundLooseImmediateDatas = 0;
			uint64_t _boundLooseResources = 0;
			uint64_t _boundLooseSamplerStates = 0;

			std::vector<unsigned> _defaultDescriptorSetRules;

			void Finalize(const CompiledPipelineLayout& pipelineLayout);
		};
		GroupRules _group[4];

		struct DescriptorSetInfo
		{
			std::vector<unsigned> _groupsThatWriteHere;
			uint64_t _shaderUsageMask = 0ull;
			unsigned _shaderStageMask = 0;
			unsigned _assignedSharedDescSetWriter = ~0u;
			uint64_t _dummyMask = 0ull;
			std::vector<unsigned> _shaderDummyTypes;		// ProgressiveDescriptorSetBuilder::ResourceDims
		};
		std::vector<DescriptorSetInfo> _descSetInfos;
		unsigned _sharedDescSetWriterCount = 0;

		void InitializeForPipelineLayout(const CompiledPipelineLayout& pipelineLayout)
		{
			_pipelineLayout = &pipelineLayout;

			for (unsigned c=0; c<_pipelineLayout->GetDescriptorSetCount(); ++c) {
				bool foundMapping = false;
				for (signed gIdx=3; gIdx>=0 && !foundMapping; --gIdx) {
					for (unsigned dIdx=0; dIdx<_looseUniforms[gIdx]->GetFixedDescriptorSetBindings().size() && !foundMapping; ++dIdx) {
						auto bindName = _looseUniforms[gIdx]->GetFixedDescriptorSetBindings()[dIdx];
						if (_pipelineLayout->GetDescriptorSetBindingNames()[c] == bindName) {
							// todo -- we should check compatibility between the given descriptor set and the pipeline layout
							_fixedDescriptorSets.insert({c, std::make_tuple(gIdx, dIdx, _looseUniforms[gIdx]->GetDescriptorSetSignature(bindName))});
							foundMapping = true;
						}
					}
				}
			}
		}

		void FinalizeRules()
		{
			unsigned firstLooseUniformsGroup = ~0u;
			for (unsigned c=0; c<_looseUniforms.size(); ++c)
				if (!_looseUniforms[c]->GetImmediateDataBindings().empty() || !_looseUniforms[c]->GetResourceViewBindings().empty() || !_looseUniforms[c]->GetSamplerBindings().empty()) {
					firstLooseUniformsGroup = c;	// assign this to the first group that is not just fixed descriptor sets
					break;
				}
			if (firstLooseUniformsGroup == ~0u)
				firstLooseUniformsGroup = 0;		// no loose uniforms at all; just fall back to using group 0

			for (unsigned descSetIdx=0; descSetIdx<_descSetInfos.size(); ++descSetIdx) {
				auto& ds = _descSetInfos[descSetIdx];
				if (!ds._groupsThatWriteHere.empty()) {
					std::sort(ds._groupsThatWriteHere.begin(), ds._groupsThatWriteHere.end());
					
					// assign the "dummies" for this desc set to the first group that writes here
					auto& groupForDummies = _group[ds._groupsThatWriteHere[0]];
					for (auto& set:groupForDummies._adaptiveSetRules) {
						if (set._descriptorSetIdx == descSetIdx) {
							assert(set._dummyMask == 0); 
							set._dummyMask = ds._dummyMask;
							set._shaderDummyTypes = ds._shaderDummyTypes;
							break;
						}
					}

					if (ds._groupsThatWriteHere.size() == 1) continue;

					// If multiple groups write here, assign a shared builder
					ds._assignedSharedDescSetWriter = _sharedDescSetWriterCount++;
					for (auto groupIdx:ds._groupsThatWriteHere)
						for (auto& set:_group[groupIdx]._adaptiveSetRules)
							if (set._descriptorSetIdx == descSetIdx)
								set._sharedBuilder = ds._assignedSharedDescSetWriter;
				} else {
					// This descriptor set requires some dummies, but there are no groups that
					// will write to it. We can instead just use the default descriptor set
					// from the pipeline layout, we just need to ensure it gets bound
					_group[firstLooseUniformsGroup]._defaultDescriptorSetRules.push_back(descSetIdx);
				}
			}
		}

		AdaptiveSetBindingRules* InitializeAdaptiveSetBindingRules(unsigned outputDescriptorSet, unsigned groupIdx, uint32_t shaderStageMask)
		{
			assert(groupIdx < 4);
			auto& groupRules = _group[groupIdx];
			auto adaptiveSet = std::find_if(
				groupRules._adaptiveSetRules.begin(), groupRules._adaptiveSetRules.end(),
				[outputDescriptorSet](const auto& c) { return c._descriptorSetIdx == outputDescriptorSet; });
			if (adaptiveSet == groupRules._adaptiveSetRules.end()) {
				auto layout = _pipelineLayout->GetDescriptorSetLayout(outputDescriptorSet);
				auto reusableGroup = _globalPools->_mainDescriptorPool.GetReusableGroup(layout);
				auto dynamicOffsetCount = CalculateDynamicOffsetCount(layout->GetDescriptorSlots());
				groupRules._adaptiveSetRules.push_back(
					AdaptiveSetBindingRules { outputDescriptorSet, 0u, std::move(layout), std::move(reusableGroup), dynamicOffsetCount });
				adaptiveSet = groupRules._adaptiveSetRules.end()-1;
			}
			adaptiveSet->_shaderStageMask |= shaderStageMask;
			return AsPointer(adaptiveSet);
		}

		void AddLooseUniformBinding(
			UniformStreamType uniformStreamType,
			unsigned outputDescriptorSet, unsigned outputDescriptorSetSlot,
			unsigned groupIdx, unsigned inputUniformStreamIdx, uint32_t shaderStageMask,
			ProgressiveDescriptorSetBuilder::ResourceDims resourceDims,
			StringSection<> variableName)
		{
			if (_descSetInfos.size() <= outputDescriptorSet)
				_descSetInfos.resize(outputDescriptorSet+1);

			_descSetInfos[outputDescriptorSet]._shaderUsageMask |= 1ull<<uint64_t(outputDescriptorSetSlot);
			_descSetInfos[outputDescriptorSet]._shaderStageMask |= shaderStageMask;
			if (uniformStreamType == UniformStreamType::Dummy) {
				auto* descSetLayout = _pipelineLayout->GetDescriptorSetLayout(outputDescriptorSet).get();
				auto descriptorType = descSetLayout->GetDescriptorSlots()[outputDescriptorSetSlot]._type;
				
				if (resourceDims == ProgressiveDescriptorSetBuilder::ResourceDims::DimInputAttachment || descriptorType == DescriptorType::InputAttachment)
					Throw(std::runtime_error(StringMeld<256>() << "No binding provided for shader input attachment (" << variableName << "). Dummy resources can't be bound for input attachments."));

				if (descriptorType == DescriptorType::UniformTexelBuffer || descriptorType == DescriptorType::UnorderedAccessTexelBuffer)
					// this is actually a "texel buffer" case -- not a UAV. We can't dummy it out without specializing the dummy
					// for the specific texel buffer required
					Throw(std::runtime_error(StringMeld<256>() << "No binding provided for shader texel buffer input (" << variableName << "). Dummy resources can't be bound for texel buffers."));

				if (resourceDims == ProgressiveDescriptorSetBuilder::ResourceDims::Dim2DMS || resourceDims == ProgressiveDescriptorSetBuilder::ResourceDims::Dim2DMSArray)
					Throw(std::runtime_error(StringMeld<256>() << "No binding provided for multisampled image input (" << variableName << "). Dummy resources can't be bound for multisampled inputs."));

				_descSetInfos[outputDescriptorSet]._dummyMask |= 1ull<<uint64_t(outputDescriptorSetSlot);
				if (_descSetInfos[outputDescriptorSet]._shaderDummyTypes.size() <= outputDescriptorSetSlot)
					_descSetInfos[outputDescriptorSet]._shaderDummyTypes.resize(outputDescriptorSetSlot+1, (unsigned)ProgressiveDescriptorSetBuilder::ResourceDims::Unknown);
				_descSetInfos[outputDescriptorSet]._shaderDummyTypes[outputDescriptorSetSlot] = (unsigned)resourceDims;
				return;
			}

			auto& groupsWr = _descSetInfos[outputDescriptorSet]._groupsThatWriteHere;
			if (std::find(groupsWr.begin(), groupsWr.end(), groupIdx) == groupsWr.end()) groupsWr.push_back(groupIdx);

			assert(groupIdx < 4);
			auto& groupRules = _group[groupIdx];
			auto adaptiveSet = InitializeAdaptiveSetBindingRules(outputDescriptorSet, groupIdx, shaderStageMask);

			std::vector<uint32_t>* binds;
			uint32_t* uniformStreamCount;
			DEBUG_ONLY(std::vector<std::string>* names);
			if (uniformStreamType == UniformStreamType::ImmediateData) {
				binds = &adaptiveSet->_immediateDataBinds;
				uniformStreamCount = &adaptiveSet->_immediateDataUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_immediateDataNames);
				groupRules._boundLooseImmediateDatas |= (1ull << uint64_t(inputUniformStreamIdx));
			} else if (uniformStreamType == UniformStreamType::ResourceView) {
				binds = &adaptiveSet->_resourceViewBinds;
				uniformStreamCount = &adaptiveSet->_resourceViewUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_resourceViewNames);
				groupRules._boundLooseResources |= (1ull << uint64_t(inputUniformStreamIdx));
			} else {
				assert(uniformStreamType == UniformStreamType::Sampler);
				binds = &adaptiveSet->_samplerBinds;
				uniformStreamCount = &adaptiveSet->_samplerUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_samplerNames);
				groupRules._boundLooseSamplerStates |= (1ull << uint64_t(inputUniformStreamIdx));
			}

			auto existing = binds->begin();
			while (existing != binds->end() && *existing != outputDescriptorSetSlot) existing += (existing[1]&s_arrayBindingFlag)?(2+(existing[1]&~s_arrayBindingFlag)):2;
			if (existing != binds->end()) {
				if (existing[1] != inputUniformStreamIdx)
					Throw(std::runtime_error("Attempting to bind more than one different inputs to the descriptor set slot (" + std::to_string(outputDescriptorSetSlot) + ")"));
			} else {
				assert(!(inputUniformStreamIdx& s_arrayBindingFlag));
				binds->push_back(outputDescriptorSetSlot);
				binds->push_back(inputUniformStreamIdx);
				*uniformStreamCount = std::max(*uniformStreamCount, inputUniformStreamIdx+1);
				DEBUG_ONLY(names->push_back(variableName.AsString()));
			}
		}

		void AddLooseUniformArrayBinding(
			UniformStreamType uniformStreamType,
			unsigned outputDescriptorSet, unsigned outputDescriptorSetSlot,
			unsigned groupIdx, IteratorRange<const unsigned*> inputUniformStreamIdx, uint32_t shaderStageMask,
			StringSection<> variableName)
		{
			if (_descSetInfos.size() <= outputDescriptorSet)
				_descSetInfos.resize(outputDescriptorSet+1);

			_descSetInfos[outputDescriptorSet]._shaderUsageMask |= 1ull<<uint64_t(outputDescriptorSetSlot);
			_descSetInfos[outputDescriptorSet]._shaderStageMask |= shaderStageMask;
			if (uniformStreamType == UniformStreamType::Dummy) {
				_descSetInfos[outputDescriptorSet]._dummyMask |= 1ull<<uint64_t(outputDescriptorSetSlot);
				return;
			}

			auto& groupsWr = _descSetInfos[outputDescriptorSet]._groupsThatWriteHere;
			if (std::find(groupsWr.begin(), groupsWr.end(), groupIdx) == groupsWr.end()) groupsWr.push_back(groupIdx);

			assert(groupIdx < 4);
			auto& groupRules = _group[groupIdx];
			auto adaptiveSet = InitializeAdaptiveSetBindingRules(outputDescriptorSet, groupIdx, shaderStageMask);

			std::vector<uint32_t>* binds;
			uint32_t* uniformStreamCount;
			DEBUG_ONLY(std::vector<std::string>* names);
			if (uniformStreamType == UniformStreamType::ImmediateData) {
				binds = &adaptiveSet->_immediateDataBinds;
				uniformStreamCount = &adaptiveSet->_immediateDataUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_immediateDataNames);
				for (auto streamIdx:inputUniformStreamIdx) groupRules._boundLooseImmediateDatas |= (1ull << uint64_t(streamIdx));
			} else if (uniformStreamType == UniformStreamType::ResourceView) {
				binds = &adaptiveSet->_resourceViewBinds;
				uniformStreamCount = &adaptiveSet->_resourceViewUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_resourceViewNames);
				for (auto streamIdx:inputUniformStreamIdx) groupRules._boundLooseResources |= (1ull << uint64_t(streamIdx));
			} else {
				assert(uniformStreamType == UniformStreamType::Sampler);
				binds = &adaptiveSet->_samplerBinds;
				uniformStreamCount = &adaptiveSet->_samplerUniformStreamCount;
				DEBUG_ONLY(names = &adaptiveSet->_samplerNames);
				for (auto streamIdx:inputUniformStreamIdx) groupRules._boundLooseSamplerStates |= (1ull << uint64_t(streamIdx));
			}

			auto existing = binds->begin();
			while (existing != binds->end() && *existing != outputDescriptorSetSlot) existing += (existing[1]&s_arrayBindingFlag)?(2+(existing[1]&~s_arrayBindingFlag)):2;
			if (existing != binds->end()) {
				Throw(std::runtime_error("Attempting to bind more than one different inputs to the descriptor set slot (" + std::to_string(outputDescriptorSetSlot) + ")"));
			} else {
				binds->push_back(outputDescriptorSetSlot);
				binds->push_back(uint32_t(inputUniformStreamIdx.size())|s_arrayBindingFlag);
				binds->insert(binds->end(), inputUniformStreamIdx.begin(), inputUniformStreamIdx.end());
				for (auto idx:inputUniformStreamIdx)
					*uniformStreamCount = std::max(*uniformStreamCount, idx+1);
				DEBUG_ONLY(names->push_back(variableName.AsString()));
			}
		}

		void BindReflection(const SPIRVReflection& reflection, uint32_t shaderStageMask)
		{
			assert(_looseUniforms.size() <= 4);
			const unsigned groupIdxForDummies = ~0u;

			// We'll need an input value for every binding in the shader reflection
			for (const auto&v:reflection._variables) {
				auto reflectionVariable = GetReflectionVariableInformation(reflection, v.first);
				if (   reflectionVariable._storageClass == SPIRVReflection::StorageClass::Input 	// storage "Input/Output" should be attributes and can be ignored
					|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Output
					|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Function) continue;

				uint64_t hashName = reflectionVariable._name.IsEmpty() ? 0 : Hash64(reflectionVariable._name.begin(), reflectionVariable._name.end());
				auto resourceDims = ResourceDimsFromVariable(reflectionVariable);

				// The _descriptorSet value can be ~0u for push constants, vertex attribute inputs, etc
				if (reflectionVariable._binding._descriptorSet != ~0u) {
					assert(!reflectionVariable._name.IsEmpty());
					auto fixedDescSet = _fixedDescriptorSets.find(reflectionVariable._binding._descriptorSet);
					if (fixedDescSet == _fixedDescriptorSets.end()) {

						// We need to got to the pipeline layout to find the signature for the descriptor set
						if (reflectionVariable._binding._descriptorSet >= _pipelineLayout->GetDescriptorSetCount())
							Throw(std::runtime_error("Shader input is assigned to a descriptor set that doesn't exist in the pipeline layout (variable: " + reflectionVariable._name.AsString() + ", ds index: " + std::to_string(reflectionVariable._binding._descriptorSet) + ")"));

						auto* descSetLayout = _pipelineLayout->GetDescriptorSetLayout(reflectionVariable._binding._descriptorSet).get();
						auto descSetSigBindings = descSetLayout->GetDescriptorSlots();

						if (reflectionVariable._binding._bindingPoint >= descSetSigBindings.size() || !ShaderVariableCompatibleWithDescriptorSet(reflectionVariable, descSetSigBindings[reflectionVariable._binding._bindingPoint]._type))
							Throw(std::runtime_error("Shader input assignment is off the pipeline layout, or the shader type does not agree with descriptor set (variable: " + reflectionVariable._name.AsString() + ")"));

						if ((descSetLayout->GetVkShaderStageMask() & shaderStageMask) != shaderStageMask)
							Throw(std::runtime_error("Shader is using a uniform, however that uniform is not enabled for the corresponding shader stage in the descriptor set layout (variable: " + reflectionVariable._name.AsString() + ")"));

						unsigned inputSlot = ~0u, groupIdx = ~0u;
						UniformStreamType bindingType = UniformStreamType::None;
						bool foundBinding = false;
						if (!reflectionVariable._arrayElementCount) {
							std::tie(bindingType, groupIdx, inputSlot) = FindBinding(_looseUniforms, hashName);
							if (bindingType == UniformStreamType::ResourceView || bindingType == UniformStreamType::ImmediateData || bindingType == UniformStreamType::Sampler) {
								if (!SlotTypeCompatibleWithBinding(bindingType, descSetSigBindings[reflectionVariable._binding._bindingPoint]._type))
									Throw(std::runtime_error("Shader input binding does not agree with descriptor set (variable: " + reflectionVariable._name.AsString() + ")"));

								AddLooseUniformBinding(
									bindingType,
									reflectionVariable._binding._descriptorSet, reflectionVariable._binding._bindingPoint,
									groupIdx, inputSlot, shaderStageMask, resourceDims,
									reflectionVariable._name);
								foundBinding = true;
							}
						} else {
							auto eleCount = reflectionVariable._arrayElementCount.value();
							VLA(unsigned, inputSlots, eleCount);
							for (unsigned c=0; c<eleCount; ++c) inputSlots[c] = ~0u;
							for (unsigned c=0; c<eleCount; ++c) {
								unsigned eleGroupIdx = ~0u;
								UniformStreamType eleBindingType = UniformStreamType::None;
								std::tie(eleBindingType, eleGroupIdx, inputSlot) = FindBinding(_looseUniforms, hashName+c);
								if (eleBindingType != UniformStreamType::None) {
									if (groupIdx != ~0u && eleGroupIdx != groupIdx)
										Throw(std::runtime_error("Array elements for shader input split across multiple BoundUniforms groups (variable: " + reflectionVariable._name.AsString() + "). This is not supported, elements for the same array must be in the same input group."));
									if (bindingType != UniformStreamType::None && eleBindingType != bindingType)
										Throw(std::runtime_error("Array elements for shader input given with different types (variable: " + reflectionVariable._name.AsString() + "). This is not supported, elements for the same array must have the same type."));
									groupIdx = eleGroupIdx;
									bindingType = eleBindingType;
									inputSlots[c] = inputSlot;
									foundBinding = true;
								}
							}

							if (foundBinding) {
								AddLooseUniformArrayBinding(
									bindingType,
									reflectionVariable._binding._descriptorSet, reflectionVariable._binding._bindingPoint,
									groupIdx, MakeIteratorRange(inputSlots, &inputSlots[eleCount]), shaderStageMask,
									reflectionVariable._name);
							}
						}
						
						if (!foundBinding) {
							// no binding found -- just mark it as an input variable we need, it will get filled in with a default binding
							bool isFixedSampler = false;
							if (reflectionVariable._binding._descriptorSet < _pipelineLayout->GetDescriptorSetCount())
								isFixedSampler = _pipelineLayout->GetDescriptorSetLayout(reflectionVariable._binding._descriptorSet)->IsFixedSampler(reflectionVariable._binding._bindingPoint);

							// we don't bind dummies to fixed samplers, because they just end up with a fixed value from the descriptor set layout
							if (!isFixedSampler) {
								AddLooseUniformBinding(
									UniformStreamType::Dummy,
									reflectionVariable._binding._descriptorSet, reflectionVariable._binding._bindingPoint,
									groupIdxForDummies, ~0u, shaderStageMask, resourceDims,
									reflectionVariable._name);
							}
						}
					} else {

						// There is a fixed descriptor set assigned that covers this input
						// Compare the slot within the fixed descriptor set to what the shader wants as input

						auto* signature = std::get<2>(fixedDescSet->second);
						if (signature) {
							if (reflectionVariable._binding._bindingPoint >= signature->_slots.size())
								Throw(std::runtime_error("Shader input variable is not included in fixed descriptor set (variable: " + reflectionVariable._name.AsString() + ")"));
							
							auto& descSetSlot = signature->_slots[reflectionVariable._binding._bindingPoint];
							if (!ShaderVariableCompatibleWithDescriptorSet(reflectionVariable, descSetSlot._type))
								Throw(std::runtime_error("Shader input variable type does not agree with the type in the given fixed descriptor set (variable: " + reflectionVariable._name.AsString() + ")"));
						}

						auto groupIdx = std::get<0>(fixedDescSet->second);
						auto inputSlot = std::get<1>(fixedDescSet->second);
						auto dynamicOffsetCount = signature ? CalculateDynamicOffsetCount(signature->_slots) : 0u;

						// We might have an existing registration for this binding; in which case we
						// just have to update the shader stage mask
						auto existing = std::find_if(
							_group[groupIdx]._fixedDescriptorSetRules.begin(), _group[groupIdx]._fixedDescriptorSetRules.end(),
							[inputSlot](const auto& c) { return c._inputSlot == inputSlot; });
						if (existing != _group[groupIdx]._fixedDescriptorSetRules.end()) {
							if (existing->_outputSlot != reflectionVariable._binding._descriptorSet)
								Throw(std::runtime_error("Attempting to bind a single input descriptor set to multiple descriptor sets in the shader inputs (ds index: " + std::to_string(reflectionVariable._binding._descriptorSet) + ")"));
							existing->_shaderStageMask |= shaderStageMask;
						} else {
							_group[groupIdx]._fixedDescriptorSetRules.push_back(
								FixedDescriptorSetBindingRules {
									inputSlot, reflectionVariable._binding._descriptorSet, shaderStageMask, dynamicOffsetCount
								});
						}

						// Check if this variable is auto assigned to a "loose" uniform
						#if defined(_DEBUG)
							auto looseBinding = FindBinding(_looseUniforms, hashName);
							if (std::get<0>(looseBinding) != UniformStreamType::None)
								Log(Verbose) << "Shader variable is explicitly bound as a loose uniform, but also falls into a fixed descriptor set. The loose uniform binding will be ignored in this case (variable: " << reflectionVariable._name << ")" << std::endl;
						#endif

						// ensure that we've recorded this group in the "_groupsThatWriteHere" array
						if (_descSetInfos.size() <= reflectionVariable._binding._descriptorSet)
							_descSetInfos.resize(reflectionVariable._binding._descriptorSet+1);
						auto& groupsWr = _descSetInfos[reflectionVariable._binding._descriptorSet]._groupsThatWriteHere;
						if (std::find(groupsWr.begin(), groupsWr.end(), groupIdx) == groupsWr.end()) groupsWr.push_back(groupIdx);

					}
				} else if (reflectionVariable._storageClass == SPIRVReflection::StorageClass::PushConstant) {

					assert(!reflectionVariable._name.IsEmpty());
					unsigned pipelineLayoutIdx = 0;
					for (; pipelineLayoutIdx<_pipelineLayout->GetPushConstantsBindingNames().size(); ++pipelineLayoutIdx) {
						if (_pipelineLayout->GetPushConstantsBindingNames()[pipelineLayoutIdx] != hashName) continue;
						if ((_pipelineLayout->GetPushConstantsRange(pipelineLayoutIdx).stageFlags & shaderStageMask) != shaderStageMask) continue;
						break;
					}
					if (pipelineLayoutIdx >= _pipelineLayout->GetPushConstantsBindingNames().size())
						Throw(std::runtime_error("Push constants declared in shader input does not exist in pipeline layout (while binding variable name: " + reflectionVariable._name.AsString() + ")"));

					// push constants must from the "loose uniforms" -- we can't extract them
					// from a prebuilt descriptor set. Furthermore, they must be a "immediateData"
					// type of input

					unsigned inputSlot = ~0u, groupIdx = ~0u;
					UniformStreamType bindingType = UniformStreamType::None;
					std::tie(bindingType, groupIdx, inputSlot) = FindBinding(_looseUniforms, hashName);
					if (bindingType == UniformStreamType::None)
						Throw(std::runtime_error("No input data provided for push constants used by shader (while binding variable name: " + reflectionVariable._name.AsString() + ")"));		// missing push constants input
					if (bindingType != UniformStreamType::ImmediateData)
						Throw(std::runtime_error("Attempting to bind a non-immediate-data input to a push constants shader input (while binding variable name:" + reflectionVariable._name.AsString() + ")"));		// Must bind immediate data to push constants (not a fixed constant buffer)

					for (const auto&group:_group) {
						auto existing = std::find_if(
							group._pushConstantsRules.begin(), group._pushConstantsRules.end(),
							[shaderStageMask](const auto& c) { return c._shaderStageBind == shaderStageMask; });
						if (existing != group._pushConstantsRules.end())
							Throw(std::runtime_error("Attempting to bind multiple push constants buffers for the same shader stage (while binding variable name: " + reflectionVariable._name.AsString() + ")"));		// we can only have one push constants per shader stage
					}

					auto& pipelineRange = _pipelineLayout->GetPushConstantsRange(pipelineLayoutIdx);
					_group[groupIdx]._pushConstantsRules.push_back({shaderStageMask, pipelineRange.offset, pipelineRange.size, inputSlot});
					_group[groupIdx]._boundLooseImmediateDatas |= 1ull << uint64_t(inputSlot);
				}
			}
		}

		static unsigned ShaderStageMaskForPipelineType(PipelineType pipelineType)
		{
			if (pipelineType == PipelineType::Graphics)
				return Internal::AsVkShaderStageFlags(ShaderStage::Vertex)|Internal::AsVkShaderStageFlags(ShaderStage::Pixel);	// note; no Geometry, etc...
			return Internal::AsVkShaderStageFlags(ShaderStage::Compute);
		}

		void BindPipelineLayout(const PipelineLayoutInitializer& pipelineLayout)
		{
			assert(_looseUniforms.size() <= 4);

			for(unsigned descSetIdx=0; descSetIdx<pipelineLayout.GetDescriptorSets().size(); ++descSetIdx) {

				auto fixedDescSet = _fixedDescriptorSets.find(descSetIdx);
				if (fixedDescSet == _fixedDescriptorSets.end()) {
					const auto& descSet = pipelineLayout.GetDescriptorSets()[descSetIdx];
					for (unsigned slotIdx=0; slotIdx<descSet._signature._slots.size(); ++slotIdx) {
						auto bindingName = slotIdx<descSet._signature._slotNames.size() ? descSet._signature._slotNames[slotIdx] : 0ull;
						if (!bindingName) continue;

						unsigned inputSlot = ~0u, groupIdx = ~0u;
						UniformStreamType bindingType = UniformStreamType::None;

						if (descSet._signature._slots[slotIdx]._count <= 1) {
							std::tie(bindingType, groupIdx, inputSlot) = FindBinding(_looseUniforms, bindingName);

							if (bindingType == UniformStreamType::ResourceView || bindingType == UniformStreamType::ImmediateData || bindingType == UniformStreamType::Sampler) {
								assert(SlotTypeCompatibleWithBinding(bindingType, descSet._signature._slots[slotIdx]._type));
								AddLooseUniformBinding(
									bindingType,
									descSetIdx, slotIdx,
									groupIdx, inputSlot, ShaderStageMaskForPipelineType(descSet._pipelineType),
									ProgressiveDescriptorSetBuilder::ResourceDims::Unknown,
									"pipeline-layout-binding");
							}
						} else {
							auto eleCount = descSet._signature._slots[slotIdx]._count;
							bool foundBinding = false;
							VLA(unsigned, inputSlots, eleCount);
							for (unsigned c=0; c<eleCount; ++c) inputSlots[c] = ~0u;
							for (unsigned c=0; c<eleCount; ++c) {
								unsigned eleGroupIdx = ~0u;
								UniformStreamType eleBindingType = UniformStreamType::None;
								std::tie(eleBindingType, eleGroupIdx, inputSlot) = FindBinding(_looseUniforms, bindingName+c);
								if (eleBindingType != UniformStreamType::None) {
									if (groupIdx != ~0u && eleGroupIdx != groupIdx)
										Throw(std::runtime_error("Array elements for shader input split across multiple BoundUniforms groups (variable: " + std::string{"pipeline-layout-binding"} + "). This is not supported, elements for the same array must be in the same input group."));
									if (bindingType != UniformStreamType::None && eleBindingType != bindingType)
										Throw(std::runtime_error("Array elements for shader input given with diferent types (variable: " + std::string{"pipeline-layout-binding"} + "). This is not supported, elements for the same array must have the same type."));
									groupIdx = eleGroupIdx;
									bindingType = eleBindingType;
									inputSlots[c] = inputSlot;
									foundBinding = true;
								}
							}

							if (foundBinding) {
								AddLooseUniformArrayBinding(
									bindingType,
									descSetIdx, slotIdx,
									groupIdx, MakeIteratorRange(inputSlots, &inputSlots[eleCount]), ShaderStageMaskForPipelineType(descSet._pipelineType),
									"pipeline-layout-binding");
							}
						}
					}
				} else {
					auto groupIdx = std::get<0>(fixedDescSet->second);
					auto inputSlot = std::get<1>(fixedDescSet->second);
					auto existing = std::find_if(
						_group[groupIdx]._fixedDescriptorSetRules.begin(), _group[groupIdx]._fixedDescriptorSetRules.end(),
						[inputSlot](const auto& c) { return c._inputSlot == inputSlot; });
					if (existing == _group[groupIdx]._fixedDescriptorSetRules.end()) {
						_group[groupIdx]._fixedDescriptorSetRules.push_back(
							FixedDescriptorSetBindingRules {
								inputSlot, descSetIdx, ShaderStageMaskForPipelineType(pipelineLayout.GetDescriptorSets()[descSetIdx]._pipelineType)
							});
					}
				}
			}

			unsigned pushConstantsIterator = 0;
			for (unsigned pushConstantsIdx=0; pushConstantsIdx<pipelineLayout.GetPushConstants().size(); ++pushConstantsIdx) {
				const auto& pushConstants = pipelineLayout.GetPushConstants()[pushConstantsIdx];
				auto hashName = Hash64(pushConstants._name);

				unsigned inputSlot = ~0u, groupIdx = ~0u;
				UniformStreamType bindingType = UniformStreamType::None;
				std::tie(bindingType, groupIdx, inputSlot) = FindBinding(_looseUniforms, hashName);
				if (bindingType == UniformStreamType::None)
					Throw(std::runtime_error("No input data provided for push constants used by shader (while binding variable name: " + pushConstants._name + ")"));		// missing push constants input
				if (bindingType != UniformStreamType::ImmediateData)
					Throw(std::runtime_error("Attempting to bind a non-immediate-data input to a push constants shader input (while binding variable name:" + pushConstants._name + ")"));		// Must bind immediate data to push constants (not a fixed constant buffer)

				auto size = CeilToMultiplePow2(pushConstants._cbSize, 4);
				_group[groupIdx]._pushConstantsRules.push_back({Internal::AsVkShaderStageFlags(pushConstants._shaderStage), pushConstantsIterator, size, inputSlot});
				pushConstantsIterator += size;
			}
		}
	};

	void BoundUniforms::ConstructionHelper::GroupRules::Finalize(const CompiledPipelineLayout& pipelineLayout)
	{
		// Hash the contents of all of the rules, so we can determine when 2 binding operations
		// do the same thing
		// Also sort some of the arrays to ensure consistency
		std::sort(
			_pushConstantsRules.begin(), _pushConstantsRules.end(),
			[](const auto& lhs, const auto& rhs) {
				return lhs._offset < rhs._offset;
			});
		std::sort(
			_fixedDescriptorSetRules.begin(), _fixedDescriptorSetRules.end(),
			[](const auto& lhs, const auto& rhs) {
				return lhs._outputSlot < rhs._outputSlot;
			});
		std::sort(
			_adaptiveSetRules.begin(), _adaptiveSetRules.end(),
			[](const auto& lhs, const auto& rhs) {
				return lhs._descriptorSetIdx < rhs._descriptorSetIdx;
			});
		std::sort(_defaultDescriptorSetRules.begin(), _defaultDescriptorSetRules.end());

		uint64_t hash;

		// In Vulkan; descriptor sets earlier in the pipeline layout determine positioning for descriptors
		// in later descriptor sets
		// We can account for this by hashing in the layout information for all descriptor sets up to the last
		// one we write to. In this way, a group will be incompatible with another BoundUniform's group based
		// not just on the contents of the particular desc sets it's written to, but also if there's a difference
		// in previous desc sets
		int lastDescSetWrittenTo = -1;
		for (const auto& r:_fixedDescriptorSetRules) lastDescSetWrittenTo = std::max(lastDescSetWrittenTo, int(r._outputSlot));
		for (const auto& r:_adaptiveSetRules) lastDescSetWrittenTo = std::max(lastDescSetWrittenTo, int(r._descriptorSetIdx));
		if (lastDescSetWrittenTo > 0) {
			hash = pipelineLayout.GetSequentialDescSetHashes()[lastDescSetWrittenTo-1];
		} else
		 	hash = DefaultSeed64;

		hash = Hash64(AsPointer(_pushConstantsRules.begin()), AsPointer(_pushConstantsRules.end()), hash);
		hash = Hash64(AsPointer(_fixedDescriptorSetRules.begin()), AsPointer(_fixedDescriptorSetRules.end()), hash);
		hash = Hash64(AsPointer(_defaultDescriptorSetRules.begin()), AsPointer(_defaultDescriptorSetRules.end()), hash);
		for (const auto& a:_adaptiveSetRules)
			hash = a.CalculateHash(hash);
		_groupRulesHash = hash;
	}

	uint64_t BoundUniforms::AdaptiveSetBindingRules::CalculateHash(uint64_t seed) const
	{
		auto hash = Hash64(AsPointer(_resourceViewBinds.begin()), AsPointer(_resourceViewBinds.end()), seed);
		hash = Hash64(AsPointer(_immediateDataBinds.begin()), AsPointer(_immediateDataBinds.end()), hash);
		hash = Hash64(AsPointer(_samplerBinds.begin()), AsPointer(_samplerBinds.end()), hash);
		hash = rotr64(hash, _descriptorSetIdx);
		return hash;
	}

	void BoundUniforms::UnbindLooseUniforms(DeviceContext& context, SharedEncoder& encoder, unsigned groupIdx) const
	{
		assert(0);		// todo -- unimplemented
	}

	namespace Internal
	{
		VkShaderStageFlags_ AsVkShaderStageFlags(ShaderStage input)
		{
			switch (input) {
			case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
			case ShaderStage::Pixel: return VK_SHADER_STAGE_FRAGMENT_BIT;
			case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
			case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;

			case ShaderStage::Hull:
			case ShaderStage::Domain:
				// VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
				// not supported on Vulkan yet
				assert(0);
				return 0;

			case ShaderStage::Null:
			default:
				return 0;
			}
		}
	}

	class BoundUniforms::SharedDescSetBuilder 
	{
	public:
		ProgressiveDescriptorSetBuilder _builder;
		unsigned _groupMask = 0;
		std::vector<DescriptorSlot> _signature;
		uint64_t _tiedToCommandList = 0;

		SharedDescSetBuilder(IteratorRange<const DescriptorSlot*> signature)
		: _builder(signature), _groupMask(0), _signature(signature.begin(), signature.end()) {}
		SharedDescSetBuilder(SharedDescSetBuilder&&) = default;
		SharedDescSetBuilder& operator=(SharedDescSetBuilder&&) = default;
		SharedDescSetBuilder(const SharedDescSetBuilder& copyFrom)
		: _builder(MakeIteratorRange(copyFrom._signature))
		, _groupMask(copyFrom._groupMask)
		, _signature(copyFrom._signature)
		{
			assert(!copyFrom._tiedToCommandList);
		}
		~SharedDescSetBuilder()
		{
			// if you hit this, it could mean that a descriptor set was partially built, and then not flushed
			assert(!_tiedToCommandList);
		}
		SharedDescSetBuilder& operator=(const SharedDescSetBuilder& copyFrom)
		{
			if (&copyFrom != this) {
				assert(!_tiedToCommandList && !copyFrom._tiedToCommandList);
				_builder = ProgressiveDescriptorSetBuilder{MakeIteratorRange(copyFrom._signature)};
				_groupMask = copyFrom._groupMask;
				_signature = copyFrom._signature;
			}
			return *this;
		}
	};

	BoundUniforms::BoundUniforms(
		const ShaderProgram& shader,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2,
		const UniformsStreamInterface& group3)
	{
		_pipelineType = PipelineType::Graphics;
		_pipelineLayout = shader.GetPipelineLayout();

		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		helper._globalPools = &GetGlobalPools();
		helper.InitializeForPipelineLayout(*_pipelineLayout);

		for (unsigned stage=0; stage<ShaderProgram::s_maxShaderStages; ++stage) {
			const auto& compiledCode = shader.GetCompiledCode((ShaderStage)stage);
			if (compiledCode.GetByteCode().size()) {
				helper.BindReflection(SPIRVReflection(compiledCode.GetByteCode()), Internal::AsVkShaderStageFlags((ShaderStage)stage));
			}
		}

		helper.FinalizeRules();

		if (helper._sharedDescSetWriterCount) {
			_sharedDescSetBuilders.reserve(helper._sharedDescSetWriterCount);
			for (unsigned descSetIdx=0; descSetIdx<helper._descSetInfos.size(); ++descSetIdx) {
				if (helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == ~0u) continue;
				assert(helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == _sharedDescSetBuilders.size());
				auto& i = _sharedDescSetBuilders.emplace_back(_pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize(*_pipelineLayout);
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
			_group[c]._defaultDescriptorSetRules = std::move(helper._group[c]._defaultDescriptorSetRules);
			_group[c]._groupRulesHash = helper._group[c]._groupRulesHash;
		}
	}

	BoundUniforms::BoundUniforms(
		const ComputePipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2,
		const UniformsStreamInterface& group3)
	{
		_pipelineType = PipelineType::Compute;
		_pipelineLayout = pipeline._shader.GetPipelineLayout();

		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		auto& shader = pipeline._shader;
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		helper._globalPools = &GetGlobalPools();
		helper.InitializeForPipelineLayout(*_pipelineLayout);
		
		const auto& compiledCode = shader.GetCompiledCode();
		if (compiledCode.GetByteCode().size())
			helper.BindReflection(SPIRVReflection(compiledCode.GetByteCode()), VK_SHADER_STAGE_COMPUTE_BIT);
		helper.FinalizeRules();

		if (helper._sharedDescSetWriterCount) {
			_sharedDescSetBuilders.reserve(helper._sharedDescSetWriterCount);
			for (unsigned descSetIdx=0; descSetIdx<helper._descSetInfos.size(); ++descSetIdx) {
				if (helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == ~0u) continue;
				assert(helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == _sharedDescSetBuilders.size());
				auto& i = _sharedDescSetBuilders.emplace_back(_pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize(*_pipelineLayout);
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
			_group[c]._defaultDescriptorSetRules = std::move(helper._group[c]._defaultDescriptorSetRules);
			_group[c]._groupRulesHash = helper._group[c]._groupRulesHash;
		}
	}

	BoundUniforms::BoundUniforms(
		const GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2,
		const UniformsStreamInterface& group3)
	: BoundUniforms(pipeline._shader, group0, group1, group2, group3) {}

	BoundUniforms::BoundUniforms(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2,
		const UniformsStreamInterface& group3)
	{
		_pipelineType = PipelineType::Graphics;
		_pipelineLayout = checked_pointer_cast<CompiledPipelineLayout>(pipelineLayout);
		
		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		helper._globalPools = &GetGlobalPools();
		helper.InitializeForPipelineLayout(*_pipelineLayout);
		helper.BindPipelineLayout(_pipelineLayout->GetInitializer());
		helper.FinalizeRules();

		if (helper._sharedDescSetWriterCount) {
			_sharedDescSetBuilders.reserve(helper._sharedDescSetWriterCount);
			for (unsigned descSetIdx=0; descSetIdx<helper._descSetInfos.size(); ++descSetIdx) {
				if (helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == ~0u) continue;
				assert(helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == _sharedDescSetBuilders.size());
				auto& i = _sharedDescSetBuilders.emplace_back(_pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize(*_pipelineLayout);
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
			_group[c]._defaultDescriptorSetRules = std::move(helper._group[c]._defaultDescriptorSetRules);
			_group[c]._groupRulesHash = helper._group[c]._groupRulesHash;
		}
	}

	BoundUniforms::BoundUniforms(const BoundUniforms&) = default;
	BoundUniforms& BoundUniforms::operator=(const BoundUniforms&) = default;
	BoundUniforms::BoundUniforms(BoundUniforms&&) never_throws = default;
	BoundUniforms& BoundUniforms::operator=(BoundUniforms&&) never_throws = default;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class BoundUniforms::BindingHelper
	{
	public:
		static uint64_t WriteImmediateDataBindings(
			DeviceContext& context,
			ProgressiveDescriptorSetBuilder& builder,
			ObjectFactory& factory,
			IteratorRange<const UniformsStream::ImmediateData*> pkts,
			IteratorRange<const uint32_t*> bindingIndicies,
			IteratorRange<const std::string*> shaderVariableNames,
			BindFlag::Enum bindType)
		{
			if (bindingIndicies.empty()) return {};

			uint64_t bindingsWrittenTo = 0u;
			VkDeviceSize totalSize = 0;

			auto alignment = (bindType == BindFlag::ConstantBuffer) 
				? factory.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
				: factory.GetPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < pkts.size());
					auto alignedSize = CeilToMultiple((VkDeviceSize)pkts[bind[1]].size(), (unsigned)alignment);
					totalSize += alignedSize;
					bind+=2;
				} else {
					assert(0);		// arrays for immediate data bindings not supported
				}
			}
			assert(totalSize != 0);

			DEBUG_ONLY(auto nameIterator = shaderVariableNames.begin());
			auto temporaryMapping = context.MapTemporaryStorage(totalSize, bindType);
			if (!temporaryMapping.GetData().empty()) {
				assert(temporaryMapping.GetData().size() == totalSize);
				VkDeviceSize iterator = 0;
				auto beginInResource = temporaryMapping.GetBeginAndEndInResource().first;

				for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
					assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
					assert(!(bind[1] & s_arrayBindingFlag));
					
					auto& pkt = pkts[bind[1]];
					assert(!pkt.empty());
											
					std::memcpy(PtrAdd(temporaryMapping.GetData().begin(), iterator), pkt.data(), pkt.size());
					VkDescriptorBufferInfo tempSpace;
					tempSpace.buffer = checked_cast<Resource*>(temporaryMapping.GetResource().get())->GetBuffer();
					tempSpace.offset = beginInResource + iterator;
					tempSpace.range = pkt.size();
					builder.Bind(bind[0], tempSpace DEBUG_ONLY(, *nameIterator++, "temporary buffer"));

					auto alignedSize = CeilToMultiple((VkDeviceSize)pkt.size(), (unsigned)alignment);
					iterator += alignedSize;

					bindingsWrittenTo |= (1ull << uint64_t(bind[0]));
					bind += 2;
				}
			} else {
				// This path is very much not recommended. It's just here to catch extreme cases
				Log(Warning) << "Failed to allocate temporary buffer space. Falling back to new buffer." << std::endl;
				for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
					assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
					assert(!(bind[1] & s_arrayBindingFlag));
					auto& pkt = pkts[bind[1]];
					assert(!pkt.empty());
					Resource cb{
						factory, 
						CreateDesc(BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite, LinearBufferDesc::Create(unsigned(pkt.size()))),
						"overflow-buf",
						SubResourceInitData{pkt}};
					builder.Bind(bind[0], { cb.GetBuffer(), 0, VK_WHOLE_SIZE } DEBUG_ONLY(, *nameIterator++, "temporary buffer"));
					bindingsWrittenTo |= (1ull << uint64_t(bind[0]));
					bind += 2;
				}
			}

			return bindingsWrittenTo;
		}

		static uint64_t WriteResourceViewBindings(
			ProgressiveDescriptorSetBuilder& builder,
			IteratorRange<const IResourceView*const*> srvs,
			IteratorRange<const uint32_t*> bindingIndicies,
			IteratorRange<const std::string*> shaderVariableNames)
		{
			uint64_t bindingsWrittenTo = 0u;
			DEBUG_ONLY(auto nameIterator = shaderVariableNames.begin());

			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				bindingsWrittenTo |= (1ull << uint64_t(bind[0]));

				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < srvs.size());
					auto* srv = srvs[bind[1]];
					builder.Bind(bind[0], *checked_cast<const ResourceView*>(srv) DEBUG_ONLY(, *nameIterator++));
					bind += 2;
				} else {
					auto count = bind[1]&~s_arrayBindingFlag;
					VLA(const ResourceView*, resViews, count);
					for (unsigned c=0; c<count; ++c) {
						assert(bind[2+c] != ~0u);
						resViews[c] = checked_cast<const ResourceView*>(srvs[bind[2+c]]);
					}
					builder.BindArray(bind[0], MakeIteratorRange(resViews, &resViews[count]) DEBUG_ONLY(, *nameIterator++));
					bind += 2+count;
				}
			}

			return bindingsWrittenTo;
		}

		static uint64_t WriteSamplerStateBindings(
			ProgressiveDescriptorSetBuilder& builder,
			IteratorRange<const SamplerState*const*> samplerStates,
			IteratorRange<const uint32_t*> bindingIndicies,
			IteratorRange<const std::string*> shaderVariableNames)
		{
			uint64_t bindingsWrittenTo = 0u;
			DEBUG_ONLY(auto nameIterator = shaderVariableNames.begin());

			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				bindingsWrittenTo |= (1ull << uint64_t(bind[0]));
				
				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < samplerStates.size());
					auto& samplerState = samplerStates[bind[1]];
					builder.Bind(bind[0], samplerState->GetUnderlying() DEBUG_ONLY(, *nameIterator++));
					bind += 2;
				} else {
					assert(0);	// array sampler bindings not supported yet
					auto count = bind[1]&~s_arrayBindingFlag;
					bind += 2+count;
				}
			}

			return bindingsWrittenTo;
		}
	};

	static std::string s_looseUniforms = "loose-uniforms";

	void BoundUniforms::ApplyLooseUniforms(
		DeviceContext& context,
		SharedEncoder& encoder,
		const UniformsStream& stream,
		unsigned groupIdx) const
	{
		// todo -- consider using VK_KHR_descriptor_update_template as an optimized way of updating many descriptors
		// in one go

		// We can hit the following exception in some cases when we have a BoundUniforms with multiple groups, but
		// do not all ApplyLooseUniforms for every group in that bound uniforms. When multiple groups contribute to the
		// same descriptor set, the descriptor set isn't actually applied to the device until all of the relevant groups
		// are applied.
		// When this happens, the exception will trigger on the *next* bound uniforms we attempt to apply
		// "encoder._pendingBoundUniforms" will be the incomplete BoundUniforms
		if (encoder._pendingBoundUniforms != nullptr && encoder._pendingBoundUniforms != this)
			Throw(std::runtime_error("Attempting to apply BoundUniforms while a previously BoundUniforms has not been fully completed."));

		// assert(encoder.GetPipelineLayout().get() == _pipelineLayout.get()); todo -- pipeline layout compatibility validation
		assert(groupIdx < dimof(_group));
		for (const auto& adaptiveSet:_group[groupIdx]._adaptiveSetRules) {

			// Descriptor sets can't be written to again after they've been bound to a command buffer (unless we're
			// sure that all of the commands have already been completed).
			//
			// So, in effect writing a new descriptor set will always be a allocate operation. We may have a pool
			// of prebuilt sets that we can reuse; or we can just allocate and free every time.
			//
			// Because each uniform stream can be set independently, and at different rates, we'll use a separate
			// descriptor set for each uniform stream. 
			//
			// In this call, we could attempt to reuse another descriptor set that was created from exactly the same
			// inputs and already used earlier this frame...? But that may not be worth it. It seems like it will
			// make more sense to just create and set a full descriptor set for every call to this function.

			auto descriptorSet = adaptiveSet._reusableDescriptorSetGroup->AllocateSingleImmediateUse();
			#if defined(VULKAN_VERBOSE_DEBUG)
				DescriptorSetDebugInfo verboseDescription;
				verboseDescription._descriptorSetInfo = s_looseUniforms;
			#endif

			// -------- write descriptor set --------
			ProgressiveDescriptorSetBuilder builderT { adaptiveSet._layout->GetDescriptorSlots() };
			bool doFlushNow = true;
			ProgressiveDescriptorSetBuilder* builder = &builderT;
			if (adaptiveSet._sharedBuilder != ~0u) {
				auto& sharedBuilder = _sharedDescSetBuilders[adaptiveSet._sharedBuilder];
				builder = &sharedBuilder._builder;
				// Flush only when all of the groups that will write to this descriptor set have done
				// their thing
				assert(encoder._pendingBoundUniforms == nullptr || encoder._pendingBoundUniforms == this);
				if (!encoder._pendingBoundUniforms) {
					encoder._pendingBoundUniforms = this;
					encoder._pendingBoundUniformsFlushGroupMask = 0;
				}
				encoder._pendingBoundUniformsCompletionMask |= sharedBuilder._groupMask;		// everything is complete when encoder._pendingBoundUniformsFlushGroupMask == encoder._pendingBoundUniformsCompletionMask
				encoder._pendingBoundUniformsFlushGroupMask |= 1 << groupIdx;
				doFlushNow = (encoder._pendingBoundUniformsFlushGroupMask & sharedBuilder._groupMask) == sharedBuilder._groupMask;	// flush only when everything is in pending state

				// If you hit the following assert, it means that this shared descriptor set was partially built for another command list, but not
				// flushed. This could be a caused by a threading issue, but more likely we just didn't get a ApplyLooseUniforms() for all of the groups
				// for this shared builder last time.
				assert(!sharedBuilder._tiedToCommandList || sharedBuilder._tiedToCommandList == context.GetActiveCommandList().GetGUID());
				sharedBuilder._tiedToCommandList = context.GetActiveCommandList().GetGUID();
			}

			// If we haven't been given enough uniform binding objects, throw an exception
			// (particularly since this only tracks the uniforms required for this adaptive sets, and doesn't count
			// bindings given that we're needed by the shader)
			char buffer[128];
			if (stream._immediateData.size() < adaptiveSet._immediateDataUniformStreamCount)
				Throw(std::runtime_error(StringMeldInPlace(buffer) << "Too few immediate data objects provided to ApplyLooseUniforms (expected " << adaptiveSet._immediateDataUniformStreamCount << " but got " << stream._immediateData.size() <<  ")"));
			if (stream._resourceViews.size() < adaptiveSet._resourceViewUniformStreamCount)
				Throw(std::runtime_error(StringMeldInPlace(buffer) << "Too few resource views provided to ApplyLooseUniforms (expected " << adaptiveSet._resourceViewUniformStreamCount << " but got " << stream._resourceViews.size() <<  ")"));
			if (stream._samplers.size() < adaptiveSet._samplerUniformStreamCount)
				Throw(std::runtime_error(StringMeldInPlace(buffer) << "Too few samplers provided to ApplyLooseUniforms (expected " << adaptiveSet._samplerUniformStreamCount << " but got " << stream._samplers.size() <<  ")"));
			
			auto descSetSlots = BindingHelper::WriteImmediateDataBindings(
				context,
				*builder,
				context.GetFactory(),
				stream._immediateData,
				MakeIteratorRange(adaptiveSet._immediateDataBinds),
				#if defined(_DEBUG)
					MakeIteratorRange(adaptiveSet._immediateDataNames),
				#else
					{},
				#endif
				BindFlag::ConstantBuffer);

			descSetSlots |= BindingHelper::WriteResourceViewBindings(
				*builder,
				stream._resourceViews,
				MakeIteratorRange(adaptiveSet._resourceViewBinds)
				#if defined(_DEBUG)
					, MakeIteratorRange(adaptiveSet._resourceViewNames)
				#else
					, {}
				#endif
				);

			descSetSlots |= BindingHelper::WriteSamplerStateBindings(
				*builder,
				MakeIteratorRange((const SamplerState*const*)stream._samplers.begin(), (const SamplerState*const*)stream._samplers.end()),
				MakeIteratorRange(adaptiveSet._samplerBinds)
				#if defined(_DEBUG)
					, MakeIteratorRange(adaptiveSet._samplerNames)
				#else
					, {}
				#endif
				);

			// Any locations referenced by the descriptor layout, by not written by the values in
			// the streams must now be filled in with the defaults.
			// Vulkan doesn't seem to have well defined behaviour for descriptor set entries that
			// are part of the layout, but never written.
			// We can do this with "write" operations, or with "copy" operations. It seems like copy
			// might be inefficient on many platforms, so we'll prefer "write"
			//
			// In the most common case, there should be no dummy descriptors to fill in here... So we'll 
			// optimise for that case.
			uint64_t dummyDescWriteMask = (~descSetSlots) & adaptiveSet._dummyMask;
			if (dummyDescWriteMask != 0)
				builder->BindDummyDescriptors(
					context.GetGlobalPools(), dummyDescWriteMask,
					MakeIteratorRange((const ProgressiveDescriptorSetBuilder::ResourceDims*)AsPointer(adaptiveSet._shaderDummyTypes.begin()), (const ProgressiveDescriptorSetBuilder::ResourceDims*)AsPointer(adaptiveSet._shaderDummyTypes.end())));

			if (doFlushNow) {
				if (descSetSlots | dummyDescWriteMask) {
					#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
						// we don't care about which slots resources are assigned to, so ignore _pendingResourceVisibilityChangesSlotAndCount
						if (!builder->_pendingResourceVisibilityChanges.empty())
							context.GetActiveCommandList().RequireResourceVisibility(builder->_pendingResourceVisibilityChanges);
					#endif

					builder->FlushChanges(
						context.GetUnderlyingDevice(), descriptorSet, nullptr, 0
						VULKAN_VERBOSE_DEBUG_ONLY(, verboseDescription));
				}

				unsigned dynamicOffsetCount = adaptiveSet._layoutDynamicOffsetCount;		// we should prefer this to be zero in the majority of cases
				VLA(unsigned, dynamicOffsets, dynamicOffsetCount);
				for (unsigned c=0; c<dynamicOffsetCount; ++c) dynamicOffsets[c] = 0;
			
				encoder.BindDescriptorSet(
					adaptiveSet._descriptorSetIdx, descriptorSet,
					MakeIteratorRange(dynamicOffsets, &dynamicOffsets[dynamicOffsetCount])
					VULKAN_VERBOSE_DEBUG_ONLY(, std::move(verboseDescription)));

				if (encoder._pendingBoundUniformsFlushGroupMask == encoder._pendingBoundUniformsCompletionMask)
					encoder._pendingBoundUniforms = nullptr;

				if (adaptiveSet._sharedBuilder != ~0u)
					_sharedDescSetBuilders[adaptiveSet._sharedBuilder]._tiedToCommandList = 0;		// reset this tracking
			}
		}

		for (auto def:_group[groupIdx]._defaultDescriptorSetRules)
			encoder.BindDescriptorSet(def, _pipelineLayout->GetBlankDescriptorSet(def).get(), {});

		for (const auto&pushConstants:_group[groupIdx]._pushConstantsRules) {
			auto cb = stream._immediateData[pushConstants._inputCBSlot];
			assert(cb.size() == pushConstants._size);
			encoder.PushConstants(pushConstants._shaderStageBind, pushConstants._offset, cb);
		}
	}

	void BoundUniforms::ApplyDescriptorSets(
		DeviceContext& context,
		SharedEncoder& encoder,
		IteratorRange<const IDescriptorSet* const*> descriptorSets,
		unsigned groupIdx) const
	{
		assert(groupIdx < dimof(_group));
		for (const auto& fixedSet:_group[groupIdx]._fixedDescriptorSetRules) {
			auto* descSet = checked_cast<const CompiledDescriptorSet*>(descriptorSets[fixedSet._inputSlot]);
			assert(descSet);
			#if defined(_DEBUG)
				// validate that the descriptor set is going to be compatible with the encoder type
				if (encoder.GetEncoderType() == SharedEncoder::EncoderType::Compute) {
					assert(descSet->GetLayout().GetVkShaderStageMask() & VK_SHADER_STAGE_COMPUTE_BIT);
				} else {
					assert(encoder.GetEncoderType() == SharedEncoder::EncoderType::Graphics || encoder.GetEncoderType() == SharedEncoder::EncoderType::ProgressiveGraphics);
					assert((descSet->GetLayout().GetVkShaderStageMask() & VK_SHADER_STAGE_ALL_GRAPHICS) != 0);
				}
				assert(!descSet->GetCommandListRestriction() || descSet->GetCommandListRestriction() == context.GetActiveCommandList().GetGUID());
			#endif
			assert(fixedSet._expectedDynamicOffsetCount == 0);
			encoder.BindDescriptorSet(
				fixedSet._outputSlot, descSet->GetUnderlying(),
				{}
				VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo{descSet->GetDescription()} ));

			#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
				context.GetActiveCommandList().RequireResourceVisibilityAlreadySorted(descSet->GetResourcesThatMustBeVisibleSorted());
			#endif
		}
	}

	void BoundUniforms::ApplyDescriptorSet(
		DeviceContext& context,
		SharedEncoder& encoder,
		const IDescriptorSet& descriptorSet,
		unsigned groupIdx, unsigned slotIdx,
		IteratorRange<const unsigned*> dynamicOffsets) const
	{
		assert(groupIdx < dimof(_group));
		for (const auto& fixedSet:_group[groupIdx]._fixedDescriptorSetRules)
			if (fixedSet._inputSlot == slotIdx) {
				auto* descSet = checked_cast<const CompiledDescriptorSet*>(&descriptorSet);
				assert(descSet);
				#if defined(_DEBUG)
					// validate that the descriptor set is going to be compatible with the encoder type
					if (encoder.GetEncoderType() == SharedEncoder::EncoderType::Compute) {
						assert(descSet->GetLayout().GetVkShaderStageMask() & VK_SHADER_STAGE_COMPUTE_BIT);
					} else {
						assert(encoder.GetEncoderType() == SharedEncoder::EncoderType::Graphics || encoder.GetEncoderType() == SharedEncoder::EncoderType::ProgressiveGraphics);
						assert((descSet->GetLayout().GetVkShaderStageMask() & VK_SHADER_STAGE_ALL_GRAPHICS) != 0);
					}
					assert(!descSet->GetCommandListRestriction() || descSet->GetCommandListRestriction() == context.GetActiveCommandList().GetGUID());
				#endif
				// assert(fixedSet._expectedDynamicOffsetCount == dynamicOffsets.size());
				encoder.BindDescriptorSet(
					fixedSet._outputSlot, descSet->GetUnderlying(),
					dynamicOffsets
					VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo{descSet->GetDescription()} ));

				#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
					context.GetActiveCommandList().RequireResourceVisibilityAlreadySorted(descSet->GetResourcesThatMustBeVisibleSorted());
				#endif
				break;
			}
	}

	void BoundUniforms::AbortPendingApplies() const
	{
		// cancel incomplete descriptor sets. This is useful when multiple groups apply to the same descriptor set, 
		// and only some of those groups have been applied with ApplyLooseUniforms.
		// Reset should abandon the previous changes and return us to a fresh state
		for (auto& sharedBuilder:_sharedDescSetBuilders)
			sharedBuilder._builder.Reset();
	}

	BoundUniforms::BoundUniforms() 
	{
		_pipelineType = PipelineType::Graphics;
	}
	BoundUniforms::~BoundUniforms() {}

	DescriptorSlot AsDescriptorSlot(const ReflectionVariableInformation& varinfo)
	{
		if (varinfo._isStructType)
			return DescriptorSlot { DescriptorType::UniformBuffer, 1 };
		if (varinfo._isRuntimeArrayStructType)
			return DescriptorSlot { DescriptorType::UnorderedAccessBuffer, 1 };

		DescriptorSlot result;
		if (varinfo._basicType) {
			if (*varinfo._basicType == SPIRVReflection::BasicType::Image || *varinfo._basicType == SPIRVReflection::BasicType::SampledImage) {
				result._type = DescriptorType::SampledTexture;
			} else if (*varinfo._basicType == SPIRVReflection::BasicType::Sampler) {
				result._type = DescriptorType::Sampler;
			} else
				result._type = DescriptorType::UniformBuffer;
		} else if (varinfo._resourceType) {
			if (varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::Buffer) {
				result._type = varinfo._resourceType->_readWriteVariation ? DescriptorType::UnorderedAccessTexelBuffer : DescriptorType::UniformTexelBuffer;
			} else if (varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::InputAttachment) {
				result._type = DescriptorType::InputAttachment;
			} else if (varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::Unknown) {
				return {};
			} else {
				assert(varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::Image1D
					|| varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::Image2D
					|| varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::Image3D
					|| varinfo._resourceType->_category == SPIRVReflection::ResourceCategory::ImageCube);
				result._type = varinfo._resourceType->_readWriteVariation ? DescriptorType::UnorderedAccessTexture : DescriptorType::SampledTexture;
				// note that varinfo._resourceType->_arrayVariation & varinfo._resourceType->_multisampleVariation don't have an impact
			}
		} else if (varinfo._vectorType) {
			result._type = DescriptorType::UniformBuffer;
		} else
			return {};

		result._count = varinfo._arrayElementCount.value_or(1);
		return result;
	}

	static std::string s_auto{"auto"};

	static void AddToPushConstants(
		PipelineLayoutInitializer::PushConstantsBinding& pushConstants,
		const SPIRVReflection& reflection, SPIRVReflection::ObjectId type)
	{
		auto typeToLookup = type;
		auto p = LowerBound(reflection._pointerTypes, typeToLookup);
		if (p != reflection._pointerTypes.end() && p->first == typeToLookup)
			typeToLookup = p->second._targetType;

		for (const auto&m:reflection._memberBindings) {
			if (m.first.first != typeToLookup) continue;

			auto end = m.second._offset + 16;	// assuming everything is just 16 bytes
			pushConstants._cbSize = std::max(pushConstants._cbSize, end);
			ConstantBufferElementDesc member;
			member._semanticHash = 0;
			auto n = LowerBound(reflection._memberNames, m.first);
			if (n != reflection._memberNames.end() && n->first == m.first) member._semanticHash = Hash64(n->second);
			member._nativeFormat = Format::Unknown;		// format conversion not handled
			member._offset = m.second._offset;
			member._arrayElementCount = 1;
			pushConstants._cbElements.push_back(member);
		}
	}

	PipelineLayoutInitializer BuildPipelineLayoutInitializer(const CompiledShaderByteCode& byteCode)
	{
		SPIRVReflection reflection(byteCode.GetByteCode());
		/*#if defined(_DEBUG)
			Log(Debug) << reflection << std::endl;
			DiassembleByteCode(Log(Debug), byteCode.GetByteCode());
		#endif*/

		std::vector<PipelineLayoutInitializer::DescriptorSetBinding> descriptorSets;
		PipelineLayoutInitializer::PushConstantsBinding pushConstants;
		pushConstants._shaderStage = byteCode.GetStage();

		auto pipelineType = (byteCode.GetStage() == ShaderStage::Compute) ? PipelineType::Compute : PipelineType::Graphics;

		for (const auto&v:reflection._variables) {
			auto reflectionVariable = GetReflectionVariableInformation(reflection, v.first);
			if (   reflectionVariable._storageClass == SPIRVReflection::StorageClass::Input 	// storage "Input/Output" should be attributes and can be ignored
				|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Output
				|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Function) continue;

			if (reflectionVariable._storageClass == SPIRVReflection::StorageClass::PushConstant) {
				if (!pushConstants._cbElements.empty())
					Throw(std::runtime_error("Multiple separate push constant structures detected"));
				assert(reflectionVariable._isStructType);
				pushConstants._name = reflectionVariable._name.AsString();

				AddToPushConstants(pushConstants, reflection, v.second._type);
				continue;
			}

			if (reflectionVariable._binding._bindingPoint == ~0u || reflectionVariable._binding._descriptorSet == ~0u) continue;

			if (descriptorSets.size() <= reflectionVariable._binding._descriptorSet)
				descriptorSets.resize(reflectionVariable._binding._descriptorSet+1, {s_auto, {}, pipelineType});

			auto& descSet = descriptorSets[reflectionVariable._binding._descriptorSet];
			if (descSet._signature._slots.size() <= reflectionVariable._binding._bindingPoint) {
				descSet._signature._slots.resize(reflectionVariable._binding._bindingPoint+1);
				descSet._signature._slotNames.resize(reflectionVariable._binding._bindingPoint+1);
			}
			auto& slot = descSet._signature._slots[reflectionVariable._binding._bindingPoint];
			slot = AsDescriptorSlot(reflectionVariable);
			descSet._signature._slotNames[reflectionVariable._binding._bindingPoint] = Hash64(reflectionVariable._name);
		}

		if (!pushConstants._cbElements.empty()) {
			std::sort(pushConstants._cbElements.begin(), pushConstants._cbElements.end(), [](const auto& lhs, const auto& rhs) { return lhs._offset < rhs._offset; });
			return PipelineLayoutInitializer{descriptorSets, MakeIteratorRange(&pushConstants, &pushConstants+1)};
		} else {
			return PipelineLayoutInitializer{descriptorSets, {}};
		}
	}

	template<typename Meld>
		NO_RETURN_PREFIX void ThrowFromMeld(Meld&& meld) { Throw(std::runtime_error(meld.AsString())); }

	void ValidateShaderToPipelineLayout(
		const CompiledShaderByteCode& byteCode,
		const ICompiledPipelineLayout& genericPipelineLayout)
	{
		auto* pipelineLayout = checked_cast<const CompiledPipelineLayout*>(&genericPipelineLayout);
		char buffer[256];

		// Check each uniform to see if it agrees with the pipeline layout
		SPIRVReflection reflection{byteCode.GetByteCode()};
		for (const auto&v:reflection._variables) {
			auto reflectionVariable = GetReflectionVariableInformation(reflection, v.first);
			if (   reflectionVariable._storageClass == SPIRVReflection::StorageClass::Input
				|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Output
				|| reflectionVariable._storageClass == SPIRVReflection::StorageClass::Function) continue;

			if (reflectionVariable._binding._descriptorSet != ~0u) {
				if (reflectionVariable._binding._descriptorSet >= pipelineLayout->GetDescriptorSetCount())
					ThrowFromMeld(StringMeldInPlace(buffer) << "Shader input is assigned to a descriptor set that doesn't exist in the pipeline layout (variable:" << reflectionVariable._name);

				auto descSetSigBindings = pipelineLayout->GetDescriptorSetLayout(reflectionVariable._binding._descriptorSet)->GetDescriptorSlots();
				if (reflectionVariable._binding._bindingPoint >= descSetSigBindings.size() || !ShaderVariableCompatibleWithDescriptorSet(reflectionVariable, descSetSigBindings[reflectionVariable._binding._bindingPoint]._type))
					ThrowFromMeld(StringMeldInPlace(buffer) << "Shader input assignment is off the pipeline layout, or the shader type does not agree with descriptor set (variable: " << reflectionVariable._name << ")");
			} else if (reflectionVariable._storageClass == SPIRVReflection::StorageClass::PushConstant) {
				PipelineLayoutInitializer::PushConstantsBinding pushConstants;
				pushConstants._shaderStage = byteCode.GetStage();
				AddToPushConstants(pushConstants, reflection, v.second._type);
				pipelineLayout->ValidatePushConstantsRange(0, pushConstants._cbSize, Internal::AsVkShaderStageFlags(pushConstants._shaderStage));
			}
		}
	}

}}


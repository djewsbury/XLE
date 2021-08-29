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
		SPIRVReflection::StorageType _storageType = SPIRVReflection::StorageType::Unknown;
		const SPIRVReflection::BasicType* _type = nullptr;
		std::optional<unsigned> _arrayElementCount;
		bool _isStructType = false;
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
			result._storageType = v->second._storage;
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
				result._type = &t->second;
			} else {
				if (std::find(reflection._structTypes.begin(), reflection._structTypes.end(), typeToLookup) != reflection._structTypes.end()) {
					// a structure will require some kind of buffer as input
					result._isStructType = true;

					// When using the HLSLCC cross-compiler; we end up with the variable having name
					// like "<cbuffername>_inst" and the type will be "<cbuffername>"
					// In this case, the name we're interested in isn't actually the variable
					// name itself, but instead the name of the struct type. As per HLSL, this
					// is the name we use for binding
					// By contrast, when using the DX HLSL compiler, the variable will have the name
					// "<cbuffername>" and the type will be "<cbuffername>.type"
					if (XlEndsWith(result._name, MakeStringSection("_inst"))) {
						auto n = LowerBound(reflection._names, typeToLookup);
						if (n != reflection._names.end() && n->first == typeToLookup)
							result._name = n->second;
					}
				} else {
					#if defined(_DEBUG)
						std::cout << "Could not understand type information for input " << result._name << std::endl;
					#endif
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
		}
	}

	BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader)
	{
		// find the vertex inputs into the shader, and match them against the input layout
		auto vertexStrides = CalculateVertexStrides(layout);

		SPIRVReflection reflection(shader.GetByteCode());
		_attributes.reserve(layout.size());

		unsigned inputDataRatePerVB[vertexStrides.size()];
		for (auto&i:inputDataRatePerVB) i = ~0u;

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
			if (reflectionVariable._storageType != SPIRVReflection::StorageType::Input) continue;
			if (reflectionVariable._binding._location == ~0u) continue;
			auto loc = reflectionVariable._binding._location;

			auto existing = std::find_if(
				_attributes.begin(), _attributes.end(), 
				[loc](const auto& c) { return c.location == loc; });
			_allAttributesBound &= (existing != _attributes.end());

			if (existing == _attributes.end())
				Log(Warning) << "Did not find binding for shader input attribute (" << reflectionVariable._name << ")" << std::endl;
		}
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
			auto srv = std::find(group->_resourceViewBindings.begin(), group->_resourceViewBindings.end(), bindingName);
			if (srv != group->_resourceViewBindings.end()) {
				auto inputSlot = (unsigned)std::distance(group->_resourceViewBindings.begin(), srv);
				return std::make_tuple(UniformStreamType::ResourceView, groupIdx, inputSlot);
			}

			auto imData = std::find(group->_immediateDataBindings.begin(), group->_immediateDataBindings.end(), bindingName);
			if (imData != group->_immediateDataBindings.end()) {
				auto inputSlot = (unsigned)std::distance(group->_immediateDataBindings.begin(), imData);
				return std::make_tuple(UniformStreamType::ImmediateData, groupIdx, inputSlot);
			}

			auto sampler = std::find(group->_samplerBindings.begin(), group->_samplerBindings.end(), bindingName);
			if (sampler != group->_samplerBindings.end()) {
				auto inputSlot = (unsigned)std::distance(group->_samplerBindings.begin(), sampler);
				return std::make_tuple(UniformStreamType::Sampler, groupIdx, inputSlot);
			}
		}

		return std::make_tuple(UniformStreamType::None, ~0u, ~0u);
	}

	const uint32_t s_arrayBindingFlag = 1u<<31u;

	class BoundUniforms::ConstructionHelper
	{
	public:
		std::map<unsigned, std::tuple<unsigned, unsigned, const DescriptorSetSignature*>> _fixedDescriptorSets;
		IteratorRange<const UniformsStreamInterface* const*> _looseUniforms = {};
		const CompiledPipelineLayout* _pipelineLayout = nullptr;

		struct GroupRules
		{
			std::vector<AdaptiveSetBindingRules> _adaptiveSetRules;
			std::vector<PushConstantBindingRules> _pushConstantsRules;
			std::vector<FixedDescriptorSetBindingRules> _fixedDescriptorSetRules;

			uint64_t _groupRulesHash;
			uint64_t _boundLooseImmediateDatas = 0;
			uint64_t _boundLooseResources = 0;
			uint64_t _boundLooseSamplerStates = 0;

			void Finalize();
		};
		GroupRules _group[4];

		struct DescriptorSetInfo
		{
			std::vector<unsigned> _groupsThatWriteHere;
			uint64_t _shaderUsageMask = 0ull;
			uint64_t _dummyMask = 0ull;
			unsigned _shaderStageMask = 0;
			unsigned _assignedSharedDescSetWriter = ~0u;
		};
		std::vector<DescriptorSetInfo> _descSetInfos;
		unsigned _sharedDescSetWriterCount = 0;

		void InitializeForPipelineLayout(const CompiledPipelineLayout& pipelineLayout)
		{
			_pipelineLayout = &pipelineLayout;

			for (unsigned c=0; c<_pipelineLayout->GetDescriptorSetCount(); ++c) {
				bool foundMapping = false;
				for (signed gIdx=3; gIdx>=0 && !foundMapping; --gIdx) {
					for (unsigned dIdx=0; dIdx<_looseUniforms[gIdx]->_fixedDescriptorSetBindings.size() && !foundMapping; ++dIdx) {
						auto bindName = _looseUniforms[gIdx]->_fixedDescriptorSetBindings[dIdx];
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
				if (!_looseUniforms[c]->_immediateDataBindings.empty() || !_looseUniforms[c]->_resourceViewBindings.empty() || !_looseUniforms[c]->_samplerBindings.empty()) {
					firstLooseUniformsGroup = c;	// assign this to the first group that is not just fixed descriptor sets
					break;
				}
			if (firstLooseUniformsGroup == ~0u)
				firstLooseUniformsGroup = 0;		// no loose uniforms at all; just fall back to using group 0

			for (unsigned descSetIdx=0; descSetIdx<_descSetInfos.size(); ++descSetIdx) {
				auto& ds = _descSetInfos[descSetIdx];
				if (ds._groupsThatWriteHere.empty()) {
					// If there are some dummys that need to be written, we'll create some rules for that
					// here and continue on
					// Otherwise this desc set is not needed by the shader and we'll ignore it
					if (ds._dummyMask != 0) {
						assert(firstLooseUniformsGroup != ~0u);
						ds._groupsThatWriteHere.push_back(firstLooseUniformsGroup);
						InitializeAdaptiveSetBindingRules(descSetIdx, firstLooseUniformsGroup, ds._shaderStageMask);
					} else
						continue;
				}
				std::sort(ds._groupsThatWriteHere.begin(), ds._groupsThatWriteHere.end());
				
				// assign the "dummies" for this desc set to the first group that writes here
				auto& groupForDummies = _group[ds._groupsThatWriteHere[0]];
				for (auto& set:groupForDummies._adaptiveSetRules) {
					if (set._descriptorSetIdx == descSetIdx) {
						assert(set._dummyMask == 0); 
						set._dummyMask = ds._dummyMask;
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
				groupRules._adaptiveSetRules.push_back(
					AdaptiveSetBindingRules { outputDescriptorSet, 0u, _pipelineLayout->GetDescriptorSetLayout(outputDescriptorSet) });
				adaptiveSet = groupRules._adaptiveSetRules.end()-1;
				auto bindings = _pipelineLayout->GetDescriptorSetLayout(outputDescriptorSet)->GetDescriptorSlots();
				adaptiveSet->_sig = std::vector<DescriptorSlot> { bindings.begin(), bindings.end() };
			}
			adaptiveSet->_shaderStageMask |= shaderStageMask;
			return AsPointer(adaptiveSet);
		}

		void AddLooseUniformBinding(
			UniformStreamType uniformStreamType,
			unsigned outputDescriptorSet, unsigned outputDescriptorSetSlot,
			unsigned groupIdx, unsigned inputUniformStreamIdx, uint32_t shaderStageMask)
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
			if (uniformStreamType == UniformStreamType::ImmediateData) {
				binds = &adaptiveSet->_immediateDataBinds;
				groupRules._boundLooseImmediateDatas |= (1ull << uint64_t(inputUniformStreamIdx));
			} else if (uniformStreamType == UniformStreamType::ResourceView) {
				binds = &adaptiveSet->_resourceViewBinds;
				groupRules._boundLooseResources |= (1ull << uint64_t(inputUniformStreamIdx));
			} else {
				assert(uniformStreamType == UniformStreamType::Sampler);
				binds = &adaptiveSet->_samplerBinds;
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
			}
		}

		void AddLooseUniformArrayBinding(
			UniformStreamType uniformStreamType,
			unsigned outputDescriptorSet, unsigned outputDescriptorSetSlot,
			unsigned groupIdx, IteratorRange<const unsigned*> inputUniformStreamIdx, uint32_t shaderStageMask)
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
			if (uniformStreamType == UniformStreamType::ImmediateData) {
				binds = &adaptiveSet->_immediateDataBinds;
				for (auto streamIdx:inputUniformStreamIdx) groupRules._boundLooseImmediateDatas |= (1ull << uint64_t(streamIdx));
			} else if (uniformStreamType == UniformStreamType::ResourceView) {
				binds = &adaptiveSet->_resourceViewBinds;
				for (auto streamIdx:inputUniformStreamIdx) groupRules._boundLooseResources |= (1ull << uint64_t(streamIdx));
			} else {
				assert(uniformStreamType == UniformStreamType::Sampler);
				binds = &adaptiveSet->_samplerBinds;
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
			}
		}

		static bool SlotTypeCompatibleWithBinding(UniformStreamType bindingType, DescriptorType slotType)
		{
			if (bindingType == UniformStreamType::ResourceView)
				return slotType == DescriptorType::SampledTexture || slotType == DescriptorType::UnorderedAccessTexture 
					|| slotType == DescriptorType::UniformBuffer || slotType == DescriptorType::UnorderedAccessBuffer 
					|| slotType == DescriptorType::InputAttachment
					|| slotType == DescriptorType::UniformTexelBuffer || slotType == DescriptorType::UnorderedAccessTexelBuffer;
			else if (bindingType == UniformStreamType::ImmediateData)
				return slotType == DescriptorType::UniformBuffer;			// we can only actually write immediate data to uniform buffers currently -- storage buffers, texel buffers, etc, aren't supported (to avoid the extra complexity that support would bring)
			else if (bindingType == UniformStreamType::Sampler)
				return slotType == DescriptorType::Sampler;

			assert(0);
			return true;
		}

		static bool ShaderVariableCompatibleWithDescriptorSet(const ReflectionVariableInformation& reflectionVariable, DescriptorType slotType)
		{
			switch (slotType) { 
			case DescriptorType::SampledTexture:
			case DescriptorType::UnorderedAccessTexture:
				return !reflectionVariable._isStructType && reflectionVariable._type && (*reflectionVariable._type == SPIRVReflection::BasicType::SampledImage || *reflectionVariable._type == SPIRVReflection::BasicType::Image || *reflectionVariable._type == SPIRVReflection::BasicType::StorageImage); 
			case DescriptorType::UniformBuffer:
			case DescriptorType::UnorderedAccessBuffer:
				return reflectionVariable._isStructType || !reflectionVariable._type || (*reflectionVariable._type != SPIRVReflection::BasicType::Image && *reflectionVariable._type != SPIRVReflection::BasicType::SampledImage && *reflectionVariable._type != SPIRVReflection::BasicType::Sampler);
			case DescriptorType::UniformTexelBuffer:
				return !reflectionVariable._isStructType && reflectionVariable._type && *reflectionVariable._type == SPIRVReflection::BasicType::TexelBuffer; 
			case DescriptorType::UnorderedAccessTexelBuffer:
				return !reflectionVariable._isStructType && reflectionVariable._type && *reflectionVariable._type == SPIRVReflection::BasicType::StorageTexelBuffer; 
			case DescriptorType::Sampler:
				return !reflectionVariable._isStructType && reflectionVariable._type && *reflectionVariable._type == SPIRVReflection::BasicType::Sampler; 
			case DescriptorType::InputAttachment:
				return reflectionVariable._binding._inputAttachmentIndex != ~0u;
			case DescriptorType::Unknown:
			default:
				assert(0);
				return true;
			}
		}

		void BindReflection(const SPIRVReflection& reflection, uint32_t shaderStageMask)
		{
			assert(_looseUniforms.size() <= 4);
			const unsigned groupIdxForDummies = ~0u;

			// We'll need an input value for every binding in the shader reflection
			for (const auto&v:reflection._variables) {
				auto reflectionVariable = GetReflectionVariableInformation(reflection, v.first);
				if (   reflectionVariable._storageType == SPIRVReflection::StorageType::Input 	// storage "Input/Output" should be attributes and can be ignored
					|| reflectionVariable._storageType == SPIRVReflection::StorageType::Output
					|| reflectionVariable._storageType == SPIRVReflection::StorageType::Function) continue;
				uint64_t hashName = reflectionVariable._name.IsEmpty() ? 0 : Hash64(reflectionVariable._name.begin(), reflectionVariable._name.end());

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
									groupIdx, inputSlot, shaderStageMask);
								foundBinding = true;
							}
						} else {
							auto eleCount = reflectionVariable._arrayElementCount.value();
							unsigned inputSlots[eleCount];
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
									groupIdx, MakeIteratorRange(inputSlots, &inputSlots[eleCount]), shaderStageMask);
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
									groupIdxForDummies, ~0u, shaderStageMask);
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

						// We might have an existing registration for this binding; in which case we
						// just have to update the shader stage mask
						auto existing = std::find_if(
							_group[groupIdx]._fixedDescriptorSetRules.begin(), _group[groupIdx]._fixedDescriptorSetRules.end(),
							[inputSlot](const auto& c) { return c._inputSlot == inputSlot; });
						if (existing != _group[groupIdx]._fixedDescriptorSetRules.end()) {
							if (existing->_outputSlot != reflectionVariable._binding._descriptorSet)
								Throw(std::runtime_error("Attempting to a single input descriptor set to multiple descriptor sets in the shader inputs (ds index: " + std::to_string(reflectionVariable._binding._descriptorSet) + ")"));
							existing->_shaderStageMask |= shaderStageMask;
						} else {
							_group[groupIdx]._fixedDescriptorSetRules.push_back(
								FixedDescriptorSetBindingRules {
									inputSlot, reflectionVariable._binding._descriptorSet, shaderStageMask
								});
						}
					}
				} else if (reflectionVariable._storageType == SPIRVReflection::StorageType::PushConstant) {

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

		void BindPipelineLayout(const PipelineLayoutInitializer& pipelineLayout, unsigned shaderStageMask)
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
						std::tie(bindingType, groupIdx, inputSlot) = FindBinding(_looseUniforms, bindingName);

						if (bindingType == UniformStreamType::ResourceView || bindingType == UniformStreamType::ImmediateData || bindingType == UniformStreamType::Sampler) {
							assert(SlotTypeCompatibleWithBinding(bindingType, descSet._signature._slots[slotIdx]._type));
							AddLooseUniformBinding(
								bindingType,
								descSetIdx, slotIdx,
								groupIdx, inputSlot, shaderStageMask);
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
								inputSlot, descSetIdx, shaderStageMask
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
				_group[groupIdx]._pushConstantsRules.push_back({shaderStageMask, pushConstantsIterator, size, inputSlot});
				pushConstantsIterator += size;
			}
		}
	};

	void BoundUniforms::ConstructionHelper::GroupRules::Finalize()
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
		auto hash = DefaultSeed64;
		for (const auto& a:_pushConstantsRules)
			hash = Hash64(AsPointer(_pushConstantsRules.begin()), AsPointer(_pushConstantsRules.end()), hash);
		for (const auto& a:_fixedDescriptorSetRules)
			hash = Hash64(AsPointer(_fixedDescriptorSetRules.begin()), AsPointer(_fixedDescriptorSetRules.end()), hash);
		for (const auto& a:_adaptiveSetRules)
			hash = a.CalculateHash(hash);
		_groupRulesHash = hash;
	}

	uint64_t BoundUniforms::AdaptiveSetBindingRules::CalculateHash(uint64_t seed) const
	{
		auto hash = Hash64(AsPointer(_resourceViewBinds.begin()), AsPointer(_resourceViewBinds.end()), seed);
		hash = Hash64(AsPointer(_immediateDataBinds.begin()), AsPointer(_immediateDataBinds.end()), seed);
		hash = Hash64(AsPointer(_samplerBinds.begin()), AsPointer(_samplerBinds.end()), seed);
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
		unsigned _unflushedGroupMask = 0;
		unsigned _groupMask = 0;
		std::vector<DescriptorSlot> _signature;

		SharedDescSetBuilder(IteratorRange<const DescriptorSlot*> signature)
		: _builder(signature), _unflushedGroupMask(0), _groupMask(0), _signature(signature.begin(), signature.end()) {}
		SharedDescSetBuilder(SharedDescSetBuilder&&) = default;
		SharedDescSetBuilder& operator=(SharedDescSetBuilder&&) = default;
		SharedDescSetBuilder(const SharedDescSetBuilder& copyFrom)
		: _builder(MakeIteratorRange(copyFrom._signature))
		, _groupMask(copyFrom._groupMask)
		, _signature(copyFrom._signature)
		, _unflushedGroupMask(0) {}
		SharedDescSetBuilder& operator=(const SharedDescSetBuilder& copyFrom)
		{
			if (&copyFrom != this) {
				_builder = ProgressiveDescriptorSetBuilder{MakeIteratorRange(copyFrom._signature)};
				_groupMask = copyFrom._groupMask;
				_signature = copyFrom._signature;
				_unflushedGroupMask = 0;
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

		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		auto* pipelineLayout = shader.GetPipelineLayout().get();
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		helper.InitializeForPipelineLayout(*pipelineLayout);

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
				auto& i = _sharedDescSetBuilders.emplace_back(pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize();
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
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

		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		auto& shader = pipeline._shader;
		auto* pipelineLayout = &shader.GetPipelineLayout();
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		helper.InitializeForPipelineLayout(*pipelineLayout);
		
		const auto& compiledCode = shader.GetCompiledCode();
		if (compiledCode.GetByteCode().size())
			helper.BindReflection(SPIRVReflection(compiledCode.GetByteCode()), VK_SHADER_STAGE_COMPUTE_BIT);
		helper.FinalizeRules();

		if (helper._sharedDescSetWriterCount) {
			_sharedDescSetBuilders.reserve(helper._sharedDescSetWriterCount);
			for (unsigned descSetIdx=0; descSetIdx<helper._descSetInfos.size(); ++descSetIdx) {
				if (helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == ~0u) continue;
				assert(helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == _sharedDescSetBuilders.size());
				auto& i = _sharedDescSetBuilders.emplace_back(pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize();
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
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
		ICompiledPipelineLayout& pipelineLayout,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2,
		const UniformsStreamInterface& group3)
	{
		_pipelineType = PipelineType::Graphics;
		auto pipelineLayoutInitializer = pipelineLayout.GetInitializer();
		const UniformsStreamInterface* groups[] = { &group0, &group1, &group2, &group3 };

		// We need to map on the input descriptor set bindings to the slots understood
		// by the shader's pipeline layout
		ConstructionHelper helper;
		helper._looseUniforms = MakeIteratorRange(groups);
		auto& metalPipelineLayout = *checked_cast<CompiledPipelineLayout*>(&pipelineLayout);
		helper.InitializeForPipelineLayout(metalPipelineLayout);
		auto shaderStageMask = Internal::AsVkShaderStageFlags(ShaderStage::Vertex)|Internal::AsVkShaderStageFlags(ShaderStage::Pixel);
		helper.BindPipelineLayout(pipelineLayoutInitializer, shaderStageMask);
		helper.FinalizeRules();

		if (helper._sharedDescSetWriterCount) {
			_sharedDescSetBuilders.reserve(helper._sharedDescSetWriterCount);
			for (unsigned descSetIdx=0; descSetIdx<helper._descSetInfos.size(); ++descSetIdx) {
				if (helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == ~0u) continue;
				assert(helper._descSetInfos[descSetIdx]._assignedSharedDescSetWriter == _sharedDescSetBuilders.size());
				auto& i = _sharedDescSetBuilders.emplace_back(helper._pipelineLayout->GetDescriptorSetLayout(descSetIdx)->GetDescriptorSlots());
				for (auto g:helper._descSetInfos[descSetIdx]._groupsThatWriteHere)
					i._groupMask |= 1 << g;
			}
		}

		for (unsigned c=0; c<4; ++c) {
			helper._group[c].Finalize();
			_group[c]._adaptiveSetRules = std::move(helper._group[c]._adaptiveSetRules);
			_group[c]._fixedDescriptorSetRules = std::move(helper._group[c]._fixedDescriptorSetRules);
			_group[c]._pushConstantsRules = std::move(helper._group[c]._pushConstantsRules);
			_group[c]._boundLooseImmediateDatas = helper._group[c]._boundLooseImmediateDatas;
			_group[c]._boundLooseResources = helper._group[c]._boundLooseResources;
			_group[c]._boundLooseSamplerStates = helper._group[c]._boundLooseSamplerStates;
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
			BindFlag::Enum bindType)
		{
			if (bindingIndicies.empty()) return {};

			uint64_t bindingsWrittenTo = 0u;
			size_t totalSize = 0;

			auto alignment = (bindType == BindFlag::ConstantBuffer) 
				? factory.GetPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment
				: factory.GetPhysicalDeviceProperties().limits.minStorageBufferOffsetAlignment;
			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < pkts.size());
					auto alignedSize = CeilToMultiple((unsigned)pkts[bind[1]].size(), alignment);
					totalSize += alignedSize;
					bind+=2;
				} else {
					assert(0);		// arrays for immediate data bindings not supported
				}
			}
			assert(totalSize != 0);

			auto temporaryMapping = context.MapTemporaryStorage(totalSize, bindType);
			if (!temporaryMapping.GetData().empty()) {
				assert(temporaryMapping.GetData().size() == totalSize);
				size_t iterator = 0;
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
					builder.Bind(bind[0], tempSpace VULKAN_VERBOSE_DEBUG_ONLY(, "temporary buffer"));

					auto alignedSize = CeilToMultiple((unsigned)pkt.size(), alignment);
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
						CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create(unsigned(pkt.size())), "overflow-buf"), 
						SubResourceInitData{pkt}};
					builder.Bind(bind[0], { cb.GetBuffer(), 0, VK_WHOLE_SIZE } VULKAN_VERBOSE_DEBUG_ONLY(, "temporary buffer"));
					bindingsWrittenTo |= (1ull << uint64_t(bind[0]));
					bind += 2;
				}
			}

			return bindingsWrittenTo;
		}

		static uint64_t WriteResourceViewBindings(
			ProgressiveDescriptorSetBuilder& builder,
			IteratorRange<const IResourceView*const*> srvs,
			IteratorRange<const uint32_t*> bindingIndicies)
		{
			uint64_t bindingsWrittenTo = 0u;

			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				bindingsWrittenTo |= (1ull << uint64_t(bind[0]));

				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < srvs.size());
					auto* srv = srvs[bind[1]];
					builder.Bind(bind[0], *checked_cast<const ResourceView*>(srv));
					bind += 2;
				} else {
					auto count = bind[1]&~s_arrayBindingFlag;
					const ResourceView* resViews[count];
					for (unsigned c=0; c<count; ++c) {
						assert(bind[2+c] != ~0u);
						resViews[c] = checked_cast<const ResourceView*>(srvs[bind[2+c]]);
					}
					builder.BindArray(bind[0], MakeIteratorRange(resViews, &resViews[count]));
					bind += 2+count;
				}
			}

			return bindingsWrittenTo;
		}

		static uint64_t WriteSamplerStateBindings(
			ProgressiveDescriptorSetBuilder& builder,
			IteratorRange<const SamplerState*const*> samplerStates,
			IteratorRange<const uint32_t*> bindingIndicies)
		{
			uint64_t bindingsWrittenTo = 0u;

			for (auto bind=bindingIndicies.begin(); bind!=bindingIndicies.end();) {
				assert(!(bindingsWrittenTo & (1ull<<uint64_t(bind[0]))));
				bindingsWrittenTo |= (1ull << uint64_t(bind[0]));
				
				if (!(bind[1]&s_arrayBindingFlag)) {
					assert(bind[1] < samplerStates.size());
					auto& samplerState = samplerStates[bind[1]];
					builder.Bind(bind[0], samplerState->GetUnderlying());
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
		assert(groupIdx < dimof(_group));
		for (const auto& adaptiveSet:_group[groupIdx]._adaptiveSetRules) {

			// Descriptor sets can't be written to again after they've been bound to a command buffer (unless we're
			// sure that all of the commands have already been completed).
			//
			// So, in effect writing a new descriptor set will always be a allocate operation. We may have a pool
			// of prebuild sets that we can reuse; or we can just allocate and free every time.
			//
			// Because each uniform stream can be set independantly, and at different rates, we'll use a separate
			// descriptor set for each uniform stream. 
			//
			// In this call, we could attempt to reuse another descriptor set that was created from exactly the same
			// inputs and already used earlier this frame...? But that may not be worth it. It seems like it will
			// make more sense to just create and set a full descriptor set for every call to this function.

			auto& globalPools = context.GetGlobalPools();
			auto descriptorSet = globalPools._mainDescriptorPool.Allocate(adaptiveSet._layout->GetUnderlying());
			#if defined(VULKAN_VERBOSE_DEBUG)
				DescriptorSetDebugInfo verboseDescription;
				verboseDescription._descriptorSetInfo = s_looseUniforms;
			#endif

			// -------- write descriptor set --------
			ProgressiveDescriptorSetBuilder builderT { MakeIteratorRange(adaptiveSet._sig) };
			bool doFlushNow = true;
			ProgressiveDescriptorSetBuilder* builder = &builderT;
			if (adaptiveSet._sharedBuilder != ~0u) {
				auto& sharedBuilder = _sharedDescSetBuilders[adaptiveSet._sharedBuilder];
				builder = &sharedBuilder._builder;
				// Flush only when all of the groups that will write to this descriptor set have done
				// their thing
				sharedBuilder._unflushedGroupMask |= 1 << groupIdx;
				assert((sharedBuilder._unflushedGroupMask & sharedBuilder._groupMask) == sharedBuilder._unflushedGroupMask);
				if (sharedBuilder._unflushedGroupMask == sharedBuilder._groupMask) {
					sharedBuilder._unflushedGroupMask = 0;
				} else {
					doFlushNow = false;
				}
			}
			
			auto descSetSlots = BindingHelper::WriteImmediateDataBindings(
				context,
				*builder,
				context.GetFactory(),
				stream._immediateData,
				MakeIteratorRange(adaptiveSet._immediateDataBinds),
				BindFlag::ConstantBuffer);

			descSetSlots |= BindingHelper::WriteResourceViewBindings(
				*builder,
				stream._resourceViews,
				MakeIteratorRange(adaptiveSet._resourceViewBinds));

			descSetSlots |= BindingHelper::WriteSamplerStateBindings(
				*builder,
				MakeIteratorRange((const SamplerState*const*)stream._samplers.begin(), (const SamplerState*const*)stream._samplers.end()),
				MakeIteratorRange(adaptiveSet._samplerBinds));

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
			uint64_t dummyDescWritten = 0;
			if (dummyDescWriteMask != 0)
				dummyDescWritten = builder->BindDummyDescriptors(context.GetGlobalPools(), dummyDescWriteMask);

			if (doFlushNow) {
				if (descSetSlots | dummyDescWriteMask) {
					std::vector<uint64_t> resourceVisibilityList;
					builder->FlushChanges(context.GetUnderlyingDevice(), descriptorSet.get(), nullptr, 0, resourceVisibilityList VULKAN_VERBOSE_DEBUG_ONLY(, verboseDescription));
					context.GetActiveCommandList().RequireResourceVisbility(MakeIteratorRange(resourceVisibilityList));
				}
			
				encoder.BindDescriptorSet(
					adaptiveSet._descriptorSetIdx, descriptorSet.get()
					VULKAN_VERBOSE_DEBUG_ONLY(, std::move(verboseDescription)));
			}
		}

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
			encoder.BindDescriptorSet(
				fixedSet._outputSlot, descSet->GetUnderlying()
				VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo{descSet->GetDescription()} ));

			#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
				context.GetActiveCommandList().RequireResourceVisbility(descSet->GetResourcesThatMustBeVisible());
			#endif
		}
	}

	void BoundUniforms::ApplyDescriptorSet(
		DeviceContext& context,
		SharedEncoder& encoder,
		const IDescriptorSet& descriptorSet,
		unsigned groupIdx, unsigned slotIdx) const
	{
		assert(groupIdx < dimof(_group));
		for (const auto& fixedSet:_group[groupIdx]._fixedDescriptorSetRules)
			if (fixedSet._inputSlot == slotIdx) {
				auto* descSet = checked_cast<const CompiledDescriptorSet*>(&descriptorSet);
				assert(descSet);
				encoder.BindDescriptorSet(
					fixedSet._outputSlot, descSet->GetUnderlying()
					VULKAN_VERBOSE_DEBUG_ONLY(, DescriptorSetDebugInfo{descSet->GetDescription()} ));

				#if defined(VULKAN_VALIDATE_RESOURCE_VISIBILITY)
					context.GetActiveCommandList().RequireResourceVisbility(descSet->GetResourcesThatMustBeVisible());
				#endif
				break;
			}
	}

	BoundUniforms::BoundUniforms() 
	{
		_pipelineType = PipelineType::Graphics;
	}
	BoundUniforms::~BoundUniforms() {}

}}


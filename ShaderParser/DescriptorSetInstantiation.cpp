// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetInstantiation.h"
#include "NodeGraphSignature.h"
#include "../RenderCore/Assets/PredefinedCBLayout.h"
#include "../RenderCore/ShaderLangUtil.h"
#include "../RenderCore/UniformsStream.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <set>
#include <unordered_map>
#include <iostream>
#include <sstream>

namespace ShaderSourceParser
{
	static std::string MakeGlobalName(const std::string& str)
	{
		auto i = str.find('.');
		if (i != std::string::npos)
			return str.substr(i+1);
		return str;
	}

	std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> MakeMaterialDescriptorSet(
		IteratorRange<const GraphLanguage::NodeGraphSignature::Parameter*> captures,
		RenderCore::ShaderLanguage shaderLanguage,
		std::ostream& warningStream)
	{
		using NameAndType = RenderCore::Assets::PredefinedCBLayout::NameAndType;
		struct WorkingCB
		{
			std::vector<NameAndType> _cbElements;
			ParameterBox _defaults;
		};
		std::unordered_map<std::string, WorkingCB> workingCBs;
		std::set<std::string> objectsAlreadyStored;
		auto result = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>();

		// hack -- skip DiffuseTexture and NormalsTexture, because these are provided by the system headers
		// objectsAlreadyStored.insert("DiffuseTexture");
		// objectsAlreadyStored.insert("NormalsTexture");

		using DescriptorSlot = RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot;

		for (const auto&c : captures) {
			if (c._direction != GraphLanguage::ParameterDirection::In)
				continue;

			DescriptorSlot newSlot;			
			newSlot._type = RenderCore::ShaderLangTypeNameAsDescriptorType(c._type);

			// If we didn't get a descriptor slot type from the type name, we'll treat this as a
			// constant within a constant buffer
			if (newSlot._type == RenderCore::DescriptorType::Empty) {
				auto fmt = RenderCore::ShaderLangTypeNameAsTypeDesc(c._type);
				if (fmt._type == ImpliedTyping::TypeCat::Void) {
					warningStream << "\t// Could not convert type (" << c._type << ") to shader language type for capture (" << c._name << "). Skipping cbuffer entry." << std::endl;
					continue;
				}

				std::string cbName, memberName;
				auto i = c._name.find('.');
				if (i != std::string::npos) {
					cbName = c._name.substr(0, i);
					memberName = c._name.substr(i+1);
				} else {
					cbName = "BasicMaterialConstants";
					memberName = c._name;
				}

				auto cbi = workingCBs.find(cbName);
				if (cbi == workingCBs.end())
					cbi = workingCBs.insert(std::make_pair(cbName, WorkingCB{})).first;

				cbi->second._cbElements.push_back(NameAndType{ memberName, fmt });
				if (!c._default.empty())
					cbi->second._defaults.SetParameter(
						MakeStringSection(memberName).Cast<utf8>(),
						MakeStringSection(c._default));

				newSlot._cbIdx = (unsigned)std::distance(workingCBs.begin(), cbi);
				newSlot._name = cbName;
				newSlot._type = RenderCore::DescriptorType::UniformBuffer;
			} else {
				newSlot._name = MakeGlobalName(c._name);
			}

			if (objectsAlreadyStored.find(newSlot._name) == objectsAlreadyStored.end()) {
				objectsAlreadyStored.insert(newSlot._name);
				result->_slots.push_back(newSlot);
			}
		}

		for (auto&cb:workingCBs) {
			if (cb.second._cbElements.empty())
				continue;

			// Sort first in alphabetical order, and then optimize for
			// type packing. This ensures that we get the same output layout for a given
			// input, regardless of the input's original ordering.
			std::sort(
				cb.second._cbElements.begin(), cb.second._cbElements.end(),
				[](const NameAndType& lhs, const NameAndType& rhs) {
					return lhs._name < rhs._name;
				});
			RenderCore::Assets::PredefinedCBLayout::OptimizeElementOrder(MakeIteratorRange(cb.second._cbElements), shaderLanguage);
			
			auto layout = std::make_shared<RenderCore::Assets::PredefinedCBLayout>(
				MakeIteratorRange(cb.second._cbElements), cb.second._defaults);
			result->_constantBuffers.emplace_back(std::move(layout));
		}

		return result;
	}

	static bool MatchableDescriptorType(RenderCore::DescriptorType inputSlotType, RenderCore::DescriptorType pipelineLayoutSlotType)
	{
		// We can assign a non-dynamic-offset UniformBuffer / UnorderedAccessBuffer to a "dynamic offset" slot in the pipeline layout
		// However not the other way around. If the "input/material layout" version is dynamic offset, it can't match with a non-dynamic-offset
		// pipeline layout slot
		return inputSlotType == pipelineLayoutSlotType
			|| (inputSlotType == RenderCore::DescriptorType::UniformBuffer) && pipelineLayoutSlotType == RenderCore::DescriptorType::UniformBufferDynamicOffset
			|| (inputSlotType == RenderCore::DescriptorType::UnorderedAccessBuffer) && pipelineLayoutSlotType == RenderCore::DescriptorType::UnorderedAccessBufferDynamicOffset
			;
	}

	std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> LinkToFixedLayout(
		const RenderCore::Assets::PredefinedDescriptorSetLayout& input,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& pipelineLayoutVersion,
		bool allowSlotReassignment)
	{
		// Generate a version of "input" that conforms to the slots in "pipelineLayoutVersion"
		// The idea is that "pipelineLayoutVersion" was used to construct the pipeline layout itself
		// So, some slots could be textures, some could be cbuffers, etc
		// "input" would have been generated by the captures from the node graph. We want to 
		// arrange that layout so that the slot types correspond to the pipeline layout version
		// It's not critical that the names of slots agree between the pipeline layout version and
		// our result (since the layouts are still compatible so long as the slot types line up)
		// However, let's match up the names where we can to encourage consistency for where we
		// put common resources
		auto result = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>();

		int maxSlotIdxInput = -1, maxSlotIdxPipelineLayout = -1;
		for (const auto& slot:input._slots) maxSlotIdxInput = std::max(maxSlotIdxInput, int(slot._slotIdx));
		for (const auto& slot:pipelineLayoutVersion._slots) maxSlotIdxPipelineLayout = std::max(maxSlotIdxPipelineLayout, int(slot._slotIdx));
		if (maxSlotIdxInput > maxSlotIdxPipelineLayout) {
			std::stringstream str;
			str << "Could not link custom pipeline layout in LinkToFixedLayout. There are too many slots required by the custom layout (" << maxSlotIdxInput+1 << ") used, but only (" << maxSlotIdxPipelineLayout+1 << ") available";
			Throw(std::runtime_error(str.str()));
		}

		VLA(bool, assignedSlots_final, maxSlotIdxPipelineLayout+1);
		VLA(bool, usedSlots_input, input._slots.size());
		for (unsigned c=0; c<maxSlotIdxPipelineLayout+1; ++c) assignedSlots_final[c] = false;
		for (unsigned c=0; c<input._slots.size(); ++c) usedSlots_input[c] = false;
		result->_slots.reserve(maxSlotIdxPipelineLayout+1);

		// First look for cases where the names agree
		if (allowSlotReassignment) {
			for (unsigned c=0; c<input._slots.size(); c++) {
				auto name = input._slots[c]._name;
				if (name.empty()) continue;
				auto i = std::find_if(
					pipelineLayoutVersion._slots.begin(), pipelineLayoutVersion._slots.end(),
					[name](const auto& q) { return q._name == name; });
				if (i != pipelineLayoutVersion._slots.end()) {
					auto finalIdx = std::distance(pipelineLayoutVersion._slots.begin(), i);
					// If the types do not agree, we can't use this slot. We will just treat them as unmatching
					if (!MatchableDescriptorType(input._slots[c]._type, i->_type) || input._slots[c]._arrayElementCount != i->_arrayElementCount)
						continue;
						
					if (assignedSlots_final[i->_slotIdx])
						Throw(std::runtime_error("Multiple descriptor set slots with the same name discovered"));
					assignedSlots_final[i->_slotIdx] = true;
					usedSlots_input[c] = true;
					auto finalSlot = input._slots[c];
					finalSlot._slotIdx = i->_slotIdx;
					finalSlot._type = i->_type;
					result->_slots.push_back(finalSlot);

					// We could try to align up the CB layout in some way, to try to encourage consistency there, as well
					// ... may not be critical, though
				}
			}
		}

		// Fill in everything that didn't get assigned by name, this time just maintaining relative
		// ordering wherever possible
		for (unsigned c=0; c<input._slots.size(); c++) {
			if (usedSlots_input[c]) continue;

			unsigned q=0;
			if (!allowSlotReassignment) {
				for (; q<pipelineLayoutVersion._slots.size(); q++)
					if (pipelineLayoutVersion._slots[q]._slotIdx == input._slots[c]._slotIdx)
						break;
				if (q == pipelineLayoutVersion._slots.size()) {
					std::stringstream str;
					str << "Custom pipeline layout does not agree with fixed layout in LinkToFixedLayout. No slot (" << input._slots[c]._slotIdx << ") in the fixed layout. Required to match entry (" << input._slots[c]._name << ") in the custom layout";
					Throw(std::runtime_error(str.str()));
				}
				const auto& i = pipelineLayoutVersion._slots[q];
				if (!MatchableDescriptorType(input._slots[c]._type, i._type) || input._slots[c]._arrayElementCount != i._arrayElementCount) {
					std::stringstream str;
					str << "Custom pipeline layout does not agree with fixed layout in LinkToFixedLayout. Matching slot (" << i._slotIdx << "), which has type (" << AsString(i._type) << ") in the fixed layout but type (" << AsString(input._slots[c]._type) << ") in the custom layout (" << input._slots[c]._name << ")";
					Throw(std::runtime_error(str.str()));
				}
			} else {
				for (; q<pipelineLayoutVersion._slots.size(); q++) {
					if (!assignedSlots_final[pipelineLayoutVersion._slots[q]._slotIdx]
						&& MatchableDescriptorType(input._slots[c]._type, pipelineLayoutVersion._slots[q]._type)
						&& input._slots[c]._arrayElementCount == pipelineLayoutVersion._slots[q]._arrayElementCount) {
						break;
					}
				}
				if (q == pipelineLayoutVersion._slots.size())
					Throw(std::runtime_error("Could not find a slot in the pipeline layout for material descriptor set slot (" + input._slots[c]._name + "), when linking the instantiated layout to the shared fixed layout. You may have exceeded the maximum number of resources of this type"));
			}

			assignedSlots_final[pipelineLayoutVersion._slots[q]._slotIdx] = true;
			auto finalSlot = input._slots[c];
			finalSlot._slotIdx = pipelineLayoutVersion._slots[q]._slotIdx;
			finalSlot._type = pipelineLayoutVersion._slots[q]._type;
			result->_slots.push_back(finalSlot);
		}

		// Fill in unallocated slots with the original pipeline layout slots
		for (unsigned c=0; c<pipelineLayoutVersion._slots.size(); c++) {
			if (assignedSlots_final[pipelineLayoutVersion._slots[c]._slotIdx])
				continue;
			result->_slots.push_back(pipelineLayoutVersion._slots[c]);
		}

		// fill in the _cbIdx fields and copy across CB details
		for (unsigned q=0; q<result->_slots.size(); q++) {
			if (result->_slots[q]._cbIdx == ~0u) continue;

			std::shared_ptr<RenderCore::Assets::PredefinedCBLayout> layout;
			if (assignedSlots_final[result->_slots[q]._slotIdx]) {
				layout = input._constantBuffers[result->_slots[q]._cbIdx];
			} else {
				layout = pipelineLayoutVersion._constantBuffers[result->_slots[q]._cbIdx];
			}

			auto i = std::find(result->_constantBuffers.begin(), result->_constantBuffers.end(), layout);
			if (i != result->_constantBuffers.end()) {
				result->_slots[q]._cbIdx = (unsigned)std::distance(result->_constantBuffers.begin(), i);
			} else {
				result->_slots[q]._cbIdx = (unsigned)result->_constantBuffers.size();
				result->_constantBuffers.push_back(layout);
			}
		}

		std::sort(
			result->_slots.begin(), result->_slots.end(),
			[](const auto& lhs, const auto& rhs) { return lhs._slotIdx < rhs._slotIdx; });

		return result;
	}
}

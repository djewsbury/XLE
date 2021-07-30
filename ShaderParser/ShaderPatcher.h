// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NodeGraphSignature.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"
#include "../Core/Types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <iosfwd>

namespace GraphLanguage { class NodeGraph; class NodeGraphSignature; }
namespace RenderCore { namespace Assets { class PredefinedCBLayout; class PredefinedDescriptorSetLayout; } }

namespace ShaderSourceParser
{
	std::string GenerateDescriptorVariables(
		const RenderCore::Assets::PredefinedDescriptorSetLayout& descriptorSet,
		unsigned descriptorSetSlotIdx,
		IteratorRange<const GraphLanguage::NodeGraphSignature::Parameter*> captures);

    class PreviewOptions
    {
    public:
        enum class Type { Object, Chart };
        Type _type;
        std::string _outputToVisualize;
		using VariableRestrictions = std::vector<std::pair<std::string, std::string>>;
        VariableRestrictions _variableRestrictions;
    };

    std::string GenerateStructureForPreview(
        StringSection<> graphName, 
        const GraphLanguage::NodeGraphSignature& interf, 
        const PreviewOptions& previewOptions = { PreviewOptions::Type::Object, std::string(), PreviewOptions::VariableRestrictions() });

	std::string GenerateStructureForTechniqueConfig(
		const GraphLanguage::NodeGraphSignature& interf, 
		StringSection<char> graphName);

	namespace ScaffoldFunctionFlags
	{
		enum Bits { ScaffoldeeUsesReturnSlot = 1<<0 };
		using BitField = unsigned;
	}
	std::string GenerateScaffoldFunction(
		const GraphLanguage::NodeGraphSignature& outputSignature, 
		const GraphLanguage::NodeGraphSignature& generatedFunctionSignature, 
		StringSection<char> scaffoldFunctionName,
		StringSection<char> implementationFunctionName,
		ScaffoldFunctionFlags::BitField flags = 0);
}


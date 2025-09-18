// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShaderPatcher.h"
#include "NodeGraph.h"
#include "NodeGraphProvider.h"
#include "../Utility/StringUtils.h"
#include <string>
#include <unordered_map>

namespace Assets { class DirectorySearchRules; }
namespace GraphLanguage
{
	class GraphSyntaxFile
	{
	public:
		class SubGraph
		{
		public:
			NodeGraphSignature 	_signature;
			NodeGraph 			_graph;
		};

		std::unordered_map<std::string, SubGraph> _subGraphs;
		std::unordered_map<std::string, std::string> _imports;
		std::unordered_map<std::string, AttributeTable> _attributeTables;
	};
	GraphSyntaxFile ParseGraphSyntax(StringSection<char> sourceCode);
	std::ostream& SerializationOperator(std::ostream& str, const GraphSyntaxFile& graphSyntaxFile);

	std::shared_ptr<INodeGraphProvider> MakeGraphSyntaxProvider(
		const std::shared_ptr<GraphSyntaxFile>& parsedGraphFile,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& dependencyValidation,
		const ::Assets::DependentFileState& parsedGraphFileState);

	INodeGraphProvider::NodeGraph LoadGraphSyntaxFile(StringSection<> filename, StringSection<> entryPoint);

	std::string GenerateGraphSyntax(const NodeGraph& graph, const NodeGraphSignature& interf, StringSection<> name);
	std::string GenerateSignature(const NodeGraphSignature& sig, StringSection<char> name, bool useReturnType = true, bool includeTemplateParameters = false);
}


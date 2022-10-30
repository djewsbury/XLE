// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GraphSyntax.h"
#include "NodeGraphSignature.h"
#include "../Utility/StringFormat.h"
#include <sstream>

namespace GraphLanguage
{
	static std::string RemoveTemplateRestrictions(const std::string& input)
	{
		// for strings like "something<parameters>", remove the part in angle brackets (ie, just return "something")
		if (input.empty() || *(input.end()-1) != '>') return input;
		auto i = input.find_first_of('<');
		if (i != std::string::npos)
			return input.substr(i+1, input.size()-i-2);
		return input;
	}

	static void GenerateGraphSyntaxInstantiation(
		std::ostream& result,
		const NodeGraph& graph,
		const NodeGraphSignature& interf,
		NodeId nodeId,
		std::unordered_map<NodeId, std::string>& instantiatedNodes);

	static void GenerateConnectionInstantiation(
		std::ostream& result,
		const NodeGraph& graph,
		const NodeGraphSignature& interf,
		const Connection& connection,
		std::unordered_map<NodeId, std::string>& instantiatedNodes)
	{
		result << connection.OutputParameterName() << ":";

		if (connection.InputNodeId() == NodeId_Interface) {
			result << connection.InputParameterName();
		} else if (connection.InputNodeId() == NodeId_Constant) {
			result << "\"" << connection.InputParameterName() << "\"";
		} else {
			GenerateGraphSyntaxInstantiation(result, graph, interf, connection.InputNodeId(), instantiatedNodes);
			if (!XlEqString(MakeStringSection(connection.InputParameterName()), MakeStringSection(ParameterName_NodeInstantiation)))
				result << "." << connection.InputParameterName();
		}
	}

	static void GenerateGraphSyntaxInstantiation(
		std::ostream& result,
		const NodeGraph& graph,
		const NodeGraphSignature& interf,
		NodeId nodeId,
		std::unordered_map<NodeId, std::string>& instantiatedNodes)
	{
		auto instantiation = instantiatedNodes.find(nodeId);
		if (instantiation != instantiatedNodes.end()) {
			result << instantiation->second;
			return;
		}

		auto n = graph.GetNode(nodeId);

		if (!n->AttributeTableName().empty())
			result << "[[" << n->AttributeTableName() << "]]";

		if (n->GetType() == Node::Type::Procedure) {
			result << RemoveTemplateRestrictions(n->ArchiveName()) << "(";

			bool atLeastOneParam = false;
			for (const auto&i:graph.GetConnections()) {
				if (i.OutputNodeId() != nodeId) continue;
				if (!i._condition.empty()) continue;
				if (atLeastOneParam) result << ", ";
				atLeastOneParam = true;
				GenerateConnectionInstantiation(result, graph, interf, i, instantiatedNodes);
			}
		} else {
			result << "(";
			assert(n->GetType() == Node::Type::Captures);
			// We must get the list of capture parameters from the graph signature itself. The names of the parameters & their default
			// values should be there
			bool atLeastOneParam = false;
			for (const auto& p:interf.GetCapturedParameters()) {
				auto firstDot = std::find(p._name.begin(), p._name.end(), '.');
				if (firstDot == p._name.end()) continue;
				if (!XlEqString(MakeStringSection(p._name.begin(), firstDot), MakeStringSection(n->ArchiveName()))) continue;

				if (atLeastOneParam) result << ", ";
				atLeastOneParam = true;

				result << p._type << " " << std::string(firstDot+1, p._name.end());
				if (!p._default.empty())
					result << " = \"" << p._default << "\"";
			}
		}

		result << ")";
	}

	std::string GenerateGraphSyntax(const NodeGraph& graph, const NodeGraphSignature& interf, StringSection<> name)
	{
		std::stringstream result;

		result << GenerateSignature(interf, name, true, true) << std::endl;
		result << "{" << std::endl;

		bool acyclic = false;
		auto sortedNodes = SortNodes(graph, acyclic);
		if (!acyclic) {
            result << "// Warning! found a cycle in the graph of nodes. Result will be incomplete!" << std::endl;
        }

		std::unordered_map<NodeId, std::string> instantiatedNodes;
		unsigned n = 0;
		for (auto id:sortedNodes) {
			unsigned outputCount = 0;
			for (const auto&i:graph.GetConnections())
				if (i.InputNodeId() == id) ++outputCount;

			auto i2 = std::find_if(graph.GetNodes().cbegin(), graph.GetNodes().cend(), [id](const Node& n) { return n.NodeId() == id; });
            if (i2 == graph.GetNodes().cend()) continue;

			// No need to instantiate nodes that have exactly 1 output connection. These will be instantiated in-place
			// when the connected node is instantiated. We do actually want to instantiate nodes with zero output connections
			// -- these are redundant to the final product, but if we don't create them here, they will disappear from the
			// graph entirely
			if (outputCount == 1 && i2->GetType() == Node::Type::Procedure) continue;

			assert(instantiatedNodes.find(id) == instantiatedNodes.end());
			std::string nodeName;
			if (i2->GetType() == Node::Type::Procedure) {
				nodeName = "node_" + std::to_string(n); ++n;
				result << "\tnode " << nodeName;
			} else {
				assert(i2->GetType() == Node::Type::Captures);
				nodeName = i2->ArchiveName();
				result << "\tcaptures " << nodeName;
			}
			result << " = ";
			GenerateGraphSyntaxInstantiation(result, graph, interf, id, instantiatedNodes);
			result << ";" << std::endl;

			instantiatedNodes.insert(std::make_pair(id, nodeName));
		}

		for (const auto&i:graph.GetConnections()) {
			if (i.OutputNodeId() != NodeId_Interface || i.OutputParameterName() == GraphLanguage::s_resultName) continue;
			result << "\t" << i.OutputParameterName() << " = ";
			GenerateGraphSyntaxInstantiation(result, graph, interf, i.InputNodeId(), instantiatedNodes);
			result << "." << i.InputParameterName() << ";" << std::endl;
		}

		for (const auto&i:graph.GetConnections()) {
			if (i._condition.empty() || i.OutputParameterName() == GraphLanguage::s_resultName) continue;
			result << "\tif \"" << i._condition << "\"" << std::endl;
			result << "\t\t" << instantiatedNodes[i.OutputNodeId()] << ".";
			GenerateConnectionInstantiation(result, graph, interf, i, instantiatedNodes);
			result << ";" << std::endl;
		}

		for (const auto&i:graph.GetConnections()) {
			if (i.OutputNodeId() != NodeId_Interface || i.OutputParameterName() != GraphLanguage::s_resultName) continue;
			if (!i._condition.empty()) {
				result << "\tif \"" << i._condition << "\"" << std::endl << "\t\treturn ";
			} else
				result << "\treturn ";
			GenerateGraphSyntaxInstantiation(result, graph, interf, i.InputNodeId(), instantiatedNodes);
			result << "." << i.InputParameterName() << ";" << std::endl;
		}

		result << "}" << std::endl;

		return result.str();
	}

	static void MaybeComma(std::stringstream& stream) { if (stream.tellp() != std::stringstream::pos_type(0)) stream << ", "; }

    std::string GenerateSignature(const NodeGraphSignature& sig, StringSection<char> name, bool useReturnType, bool includeTemplateParameters)
	{
        std::string returnType, returnSemantic;

		std::stringstream mainFunctionDeclParameters;
		for (const auto& i:sig.GetParameters()) {
            if (useReturnType && i._name == s_resultName && i._direction == ParameterDirection::Out) {
                assert(returnType.empty() && returnSemantic.empty());
                returnType = i._type;
                returnSemantic = i._semantic;
                continue;
            }

			//
            //      Our graph function is always a "void" function, and all of the output
            //      parameters are just function parameters with the "out" keyword. This is
            //      convenient for writing out generated functions
            //      We don't want to put the "node id" in the name -- because node ids can
            MaybeComma(mainFunctionDeclParameters);
			if (i._direction == ParameterDirection::Out) {
				mainFunctionDeclParameters << "out ";
			}
            mainFunctionDeclParameters << i._type << " " << i._name;
			if (!i._semantic.empty())
				mainFunctionDeclParameters << " : " << i._semantic;
        }

		if (includeTemplateParameters) {
			for (const auto& i:sig.GetTemplateParameters()) {
				MaybeComma(mainFunctionDeclParameters);
				mainFunctionDeclParameters << "graph<" << i._restriction << "> " << i._name;
			}
		}

        std::stringstream result;
		if (!returnType.empty()) result << returnType << " ";
		else result << "void ";
        result << name << "(" << mainFunctionDeclParameters.str() << ")";
		if (!returnSemantic.empty())
			result << " : " << returnSemantic;

		if (includeTemplateParameters && !sig.GetImplements().empty())
			result << " implements " << sig.GetImplements();

		return result.str();
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::ostream& SerializationOperator(std::ostream& str, const GraphSyntaxFile& graphSyntaxFile)
	{
		for (auto& i:graphSyntaxFile._imports)
			str << "import " << i.first << " = \"" << i.second << "\"" << std::endl;
		if (!graphSyntaxFile._imports.empty()) str << std::endl;
		for (auto& sg:graphSyntaxFile._subGraphs)
			str << GraphLanguage::GenerateGraphSyntax(sg.second._graph, sg.second._signature, sg.first);

		for (auto& at:graphSyntaxFile._attributeTables) {
			str << "attributes " << at.first << "(";
			bool first = true;
			for (auto& key:at.second) {
				if (!first) str << ", ";
				first = false;
				str << key.first << ":\"" << key.second << "\"";
			}
			str << ");" << std::endl;
		}
		return str;
	}

}

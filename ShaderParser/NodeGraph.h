// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/IteratorUtils.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace GraphLanguage 
{
	using NodeId = uint32_t;

	static NodeId NodeId_Interface = (NodeId)-1;
	static NodeId NodeId_Constant = (NodeId)-2;
	extern const std::string ParameterName_NodeInstantiation;
	extern const std::string s_resultName;

        ///////////////////////////////////////////////////////////////

    class Node
    {
    public:
        enum class Type { Procedure, Captures };

		std::string				_archiveName;
        NodeId					_nodeId = 0;
        Type					_type = Type::Procedure;
		std::string				_attributeTableName;

        const std::string&		ArchiveName() const         { return _archiveName; }
        NodeId					NodeId() const              { return _nodeId; }
        Type					GetType() const             { return _type; }
		const std::string&		AttributeTableName() const  { return _attributeTableName; }
    };

        ///////////////////////////////////////////////////////////////

    class Connection
    {
    public:
        NodeId				_inputNodeId;
        std::string			_inputParameterName;
		NodeId				_outputNodeId;
        std::string			_outputParameterName;
		std::string			_condition;

        NodeId              InputNodeId() const				{ return _inputNodeId; }
        const std::string&  InputParameterName() const		{ return _inputParameterName; }
		NodeId				OutputNodeId() const            { return _outputNodeId; }
        const std::string&  OutputParameterName() const		{ return _outputParameterName; }
    };
  
        ///////////////////////////////////////////////////////////////

    class NodeGraph
    {
    public:
        IteratorRange<const Node*>			GetNodes() const			{ return MakeIteratorRange(_nodes); }
        IteratorRange<const Connection*>	GetConnections() const		{ return MakeIteratorRange(_connections); }

		IteratorRange<Node*>				GetNodes()			{ return MakeIteratorRange(_nodes); }
        IteratorRange<Connection*>			GetConnections()	{ return MakeIteratorRange(_connections); }

        void			Add(Node&&);
        void			Add(Connection&&);

		void			Trim(const NodeId* trimNodesBegin, const NodeId* trimNodesEnd);
        void			Trim(NodeId previewNode);

        const Node*     GetNode(NodeId nodeId) const;

        NodeGraph();
        ~NodeGraph();

    private:
        std::vector<Node>		_nodes;
        std::vector<Connection> _connections;

        bool        IsUpstream(NodeId startNode, NodeId searchingForNode);
        bool        IsDownstream(NodeId startNode, const NodeId* searchingForNodesStart, const NodeId* searchingForNodesEnd);
        bool        HasNode(NodeId nodeId);
    };

		///////////////////////////////////////////////////////////////

	using AttributeTable = std::unordered_map<std::string, std::string>;
	
	class AttributeTableSet
	{
	public:
		std::unordered_map<std::string, AttributeTable> _tables;
	};

		///////////////////////////////////////////////////////////////

	std::vector<NodeId> SortNodes(const NodeGraph& graph, bool& isAcyclic);
}


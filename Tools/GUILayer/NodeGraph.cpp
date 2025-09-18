// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NodeGraph.h"
#include "AssetsLayer.h"
#include "MarshalString.h"
#include "../../Assets/IFileSystem.h"
#include "../../Utility/Streams/PathUtils.h"
#include <msclr/auto_gcroot.h>
#include <regex>

namespace GUILayer
{
	NodeGraph::NodeGraph()
    {
        _nodes          = gcnew List<Node^>();
        _connections    = gcnew List<Connection^>();
    }

	void NodeGraph::AddNode(Node^ node)
	{
		_nodes->Add(node);
	}

	void NodeGraph::AddConnection(Connection^ connection)
	{
		_connections->Add(connection);
	}

	static std::string AddToImportTable(StringSection<> import, ConversionContext& context)
	{
		char imprt[MaxPath];
		MakeSplitPath(import).Rebuild(imprt);
		auto existing = std::find_if(
			context._importTable.begin(), context._importTable.end(), 
			[imprt](const std::pair<std::string, std::string>& p) { return p.second == imprt; } );
		if (existing == context._importTable.end()) {
			for (unsigned pass=0;; ++pass) {
				std::string attempt = MakeFileNameSplitter(imprt).File().AsString();
				if (pass != 0) attempt += std::to_string(pass);
				if (context._importTable.find(attempt) == context._importTable.end()) {
					existing = context._importTable.insert({attempt, std::string(imprt)}).first;
					break;
				}
			}
		}

		return existing->first;
	}

	static std::string CompressImportedName(const std::string& name, ConversionContext& context)
	{
		static std::regex basicImport("(.*):(.*)");
		static std::regex templatedImport("(.*)<(.*):(.*)>");

		std::smatch smatch;
		if (std::regex_match(name, smatch, templatedImport)) {
			auto imported = AddToImportTable(MakeStringSection(smatch[2].first, smatch[2].second), context);
			return smatch[1].str() + "<" + imported + "::" + smatch[3].str() + ">";	// note downgrade to single colon here
		} else if (std::regex_match(name, smatch, basicImport)) {
			auto imported = AddToImportTable(MakeStringSection(smatch[1].first, smatch[1].second), context);
			return imported + "::" + smatch[2].str();	// note downgrade to single colon here
		}

		return name;
	}

	static std::string ExpandImportedName(const std::string& name, const ConversionContext& context)
	{
		static std::regex basicImport("(.*)::(.*)");
		static std::regex templatedImport("(.*)<(.*)::(.*)>");

		std::smatch smatch;
		if (std::regex_match(name, smatch, templatedImport)) {
			auto existing = context._importTable.find(smatch[2]);
			if (existing != context._importTable.end())
				return smatch[1].str() + "<" + existing->second + ":" + smatch[3].str() + ">";	// note downgrade to single colon here
		} else if (std::regex_match(name, smatch, basicImport)) {
			auto existing = context._importTable.find(smatch[1]);
			if (existing != context._importTable.end())
				return existing->second + ":" + smatch[2].str();	// note downgrade to single colon here
		}

		return name;
	}

	static GraphLanguage::Node::Type		ConvertToNative(Node::Type e)
    {
        switch (e) {
        default:
        case Node::Type::Procedure:					return GraphLanguage::Node::Type::Procedure;
        case Node::Type::Captures:                  return GraphLanguage::Node::Type::Captures;
        }
    }

    static GraphLanguage::Node				ConvertToNative(Node^ node, ConversionContext& context)
    {
        return GraphLanguage::Node{
            CompressImportedName(clix::marshalString<clix::E_UTF8>(node->FragmentArchiveName), context), 
			node->NodeId, ConvertToNative(node->NodeType),
			!String::IsNullOrEmpty(node->AttributeTableName) ? clix::marshalString<clix::E_UTF8>(node->AttributeTableName) : std::string{} };
    }
    
    static GraphLanguage::Connection        ConvertToNative(Connection^ connection)
    {
        return GraphLanguage::Connection{
            connection->InputNodeID, clix::marshalString<clix::E_UTF8>(connection->InputParameterName),
			connection->OutputNodeID, clix::marshalString<clix::E_UTF8>(connection->OutputParameterName),
			clix::marshalString<clix::E_UTF8>(connection->Condition)};
    }
    
    GraphLanguage::NodeGraph        NodeGraph::ConvertToNative(ConversionContext& context)
    {
        GraphLanguage::NodeGraph res;
        for each(Node^ n in Nodes)
            res.Add(GUILayer::ConvertToNative(n, context));
        for each(Connection^ c in Connections)
            res.Add(GUILayer::ConvertToNative(c));
        return res;
    }

	static void PrimeImportTable(NodeGraph^ nodeGraph, ConversionContext& context)
	{
		for each(Node^ node in nodeGraph->Nodes)
			CompressImportedName(clix::marshalString<clix::E_UTF8>(node->FragmentArchiveName), context);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static Node::Type     ConvertFromNative(GraphLanguage::Node::Type e)
    {
        switch (e) {
        default:
		case GraphLanguage::Node::Type::Procedure : return Node::Type::Procedure;
		case GraphLanguage::Node::Type::Captures: return Node::Type::Captures;
        }
    }

	static Node^ ConvertFromNative(const GraphLanguage::Node& node, const ConversionContext& context)
    {
        Node^ result = gcnew Node;
		result->FragmentArchiveName = clix::marshalString<clix::E_UTF8>(ExpandImportedName(node.ArchiveName(), context));
		result->NodeId = node.NodeId();
		result->NodeType = ConvertFromNative(node.GetType());
		result->AttributeTableName = node.AttributeTableName().empty() ? String::Empty : clix::marshalString<clix::E_UTF8>(node.AttributeTableName());
		return result;
    }
    
    static Connection^ ConvertFromNative(const GraphLanguage::Connection& connection)
    {
		Connection^ result = gcnew Connection;
		result->OutputNodeID = connection.OutputNodeId(); 
		result->OutputParameterName = clix::marshalString<clix::E_UTF8>(connection.OutputParameterName());
		result->InputNodeID = connection.InputNodeId();
		result->InputParameterName = clix::marshalString<clix::E_UTF8>(connection.InputParameterName());
		result->Condition = clix::marshalString<clix::E_UTF8>(connection._condition);
		return result;
    }

	NodeGraph^ NodeGraph::ConvertFromNative(const GraphLanguage::NodeGraph& input, const ConversionContext& context)
	{
		NodeGraph^ result = gcnew NodeGraph;
		for (const auto& n:input.GetNodes())
			result->AddNode(GUILayer::ConvertFromNative(n, context));
		for (const auto& c:input.GetConnections())
			result->AddConnection(GUILayer::ConvertFromNative(c));
		return result;
	}

	String^ NodeGraph::Print(NodeGraphSignature^ signature, String^ name)
	{
		ConversionContext context;
		auto sigNative = signature->ConvertToNative(context);
		auto native = ConvertToNative(context);

		auto nativeResult = GraphLanguage::GenerateGraphSyntax(
			native,
			sigNative,
			clix::marshalString<clix::E_UTF8>(name));
		return clix::marshalString<clix::E_UTF8>(nativeResult);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static NodeGraphSignature::ParameterDirection AsManagedParameterDirections(GraphLanguage::ParameterDirection input)
	{
		switch (input) {
		default:
		case GraphLanguage::ParameterDirection::In:
			return NodeGraphSignature::ParameterDirection::In;
		case GraphLanguage::ParameterDirection::Out:
			return NodeGraphSignature::ParameterDirection::Out;
		}
	}

	static GraphLanguage::ParameterDirection AsNativeParameterDirections(NodeGraphSignature::ParameterDirection input)
	{
		switch (input) {
		default:
		case NodeGraphSignature::ParameterDirection::In:
			return GraphLanguage::ParameterDirection::In;
		case NodeGraphSignature::ParameterDirection::Out:
			return GraphLanguage::ParameterDirection::Out;
		}
	}
	
	GraphLanguage::NodeGraphSignature	NodeGraphSignature::ConvertToNative(ConversionContext& context)
	{
		GraphLanguage::NodeGraphSignature result;
		for each(auto p in Parameters) {
			result.AddParameter(
				GraphLanguage::NodeGraphSignature::Parameter {
					p->Type ? clix::marshalString<clix::E_UTF8>(p->Type) : std::string(),
					clix::marshalString<clix::E_UTF8>(p->Name),
					AsNativeParameterDirections(p->Direction),
					p->Semantic ? clix::marshalString<clix::E_UTF8>(p->Semantic) : std::string(),
					p->Default ? clix::marshalString<clix::E_UTF8>(p->Default) : std::string()});
		}
		for each(auto p in CapturedParameters) {
			result.AddCapturedParameter(
				GraphLanguage::NodeGraphSignature::Parameter {
					p->Type ? clix::marshalString<clix::E_UTF8>(p->Type) : std::string(),
					clix::marshalString<clix::E_UTF8>(p->Name),
					AsNativeParameterDirections(p->Direction),
					p->Semantic ? clix::marshalString<clix::E_UTF8>(p->Semantic) : std::string(),
					p->Default ? clix::marshalString<clix::E_UTF8>(p->Default) : std::string()});
		}
		for each(auto p in TemplateParameters) {
			result.AddTemplateParameter(
				GraphLanguage::NodeGraphSignature::TemplateParameter {
					clix::marshalString<clix::E_UTF8>(p->Name),
					CompressImportedName(clix::marshalString<clix::E_UTF8>(p->Restriction), context)});
		}

		result.SetImplements(CompressImportedName(clix::marshalString<clix::E_UTF8>(Implements), context));
		return result;
	}

	static void PrimeImportTable(NodeGraphSignature^ signature, ConversionContext& context)
	{
		for each(auto node in signature->TemplateParameters)
			CompressImportedName(clix::marshalString<clix::E_UTF8>(node->Restriction), context);
		CompressImportedName(clix::marshalString<clix::E_UTF8>(signature->Implements), context);
	}

	NodeGraphSignature^			NodeGraphSignature::ConvertFromNative(const GraphLanguage::NodeGraphSignature& input, const ConversionContext& context)
	{
		NodeGraphSignature^ result = gcnew NodeGraphSignature;
		result->_parameters = gcnew List<Parameter^>();
		for (auto&p:input.GetParameters()) {
			Parameter^ param = gcnew Parameter;
			param->Type = clix::marshalString<clix::E_UTF8>(p._type);
			param->Name = clix::marshalString<clix::E_UTF8>(p._name);
			param->Direction = AsManagedParameterDirections(p._direction);
			param->Semantic = clix::marshalString<clix::E_UTF8>(p._semantic);
			param->Default = clix::marshalString<clix::E_UTF8>(p._default);
			result->_parameters->Add(param);
		}
		result->_capturedParameters = gcnew List<Parameter^>();
		for (auto&p:input.GetCapturedParameters()) {
			Parameter^ param = gcnew Parameter;
			param->Type = clix::marshalString<clix::E_UTF8>(p._type);
			param->Name = clix::marshalString<clix::E_UTF8>(p._name);
			param->Direction = AsManagedParameterDirections(p._direction);
			param->Semantic = clix::marshalString<clix::E_UTF8>(p._semantic);
			param->Default = clix::marshalString<clix::E_UTF8>(p._default);
			result->_capturedParameters->Add(param);
		}
		result->_templateParameters = gcnew List<TemplateParameter^>();
		for (auto&p:input.GetTemplateParameters()) {
			TemplateParameter^ param = gcnew TemplateParameter;
			param->Name = clix::marshalString<clix::E_UTF8>(p._name);
			param->Restriction = clix::marshalString<clix::E_UTF8>(ExpandImportedName(p._restriction, context));
			result->_templateParameters->Add(param);
		}
		result->Implements = clix::marshalString<clix::E_UTF8>(ExpandImportedName(input.GetImplements(), context));
		return result;
	}

	String^ NodeGraphSignature::Print(String^ name)
	{
		ConversionContext context;
		auto native = ConvertToNative(context);

		auto nativeResult = GraphLanguage::GenerateSignature(
			native,
			clix::marshalString<clix::E_UTF8>(name));
		return clix::marshalString<clix::E_UTF8>(nativeResult);
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UniformBufferSignature^			UniformBufferSignature::ConvertFromNative(const GraphLanguage::UniformBufferSignature& input)
	{
		UniformBufferSignature^ result = gcnew UniformBufferSignature();

        using namespace clix;
        for (auto i=input._parameters.begin(); i!=input._parameters.end(); ++i) {
            Parameter^ p = gcnew Parameter();
            p->Name = clix::marshalString<clix::E_UTF8>(i->_name);
            p->Type = clix::marshalString<clix::E_UTF8>(i->_type);
            p->Semantic = clix::marshalString<clix::E_UTF8>(i->_semantic);
            result->Parameters->Add(p);
        }

		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::unordered_map<std::string, std::string> ConvertToNative(AttributeTable^ input)
	{
		std::unordered_map<std::string, std::string> result;
		for each(KeyValuePair<String^, String^> entry in input)
			result.emplace(std::make_pair(
				clix::marshalString<clix::E_UTF8>(entry.Key),
				clix::marshalString<clix::E_UTF8>(entry.Value)));
		return result;
	}

	static AttributeTable^ ConvertFromNative(const std::unordered_map<std::string, std::string>& input)
	{
		AttributeTable^ result = gcnew AttributeTable;
		for (const auto&entry:input)
			result->Add(clix::marshalString<clix::E_UTF8>(entry.first), clix::marshalString<clix::E_UTF8>(entry.second));
		return result;
	}

	GraphLanguage::GraphSyntaxFile	NodeGraphFile::ConvertToNative()
	{
		ConversionContext context;
		GraphLanguage::GraphSyntaxFile result;
		for each(KeyValuePair<String^, SubGraph^> entry in SubGraphs) {
			result._subGraphs.insert(
				std::make_pair(
					clix::marshalString<clix::E_UTF8>(entry.Key),
					GraphLanguage::GraphSyntaxFile::SubGraph {
						entry.Value->Signature->ConvertToNative(context), 
						entry.Value->Graph->ConvertToNative(context) }));
		}
		for each(KeyValuePair<String^, AttributeTable^> at in AttributeTables) {
			result._attributeTables.emplace(
				std::make_pair(clix::marshalString<clix::E_UTF8>(at.Key), GUILayer::ConvertToNative(at.Value)));
		}
		result._imports = std::move(context._importTable);
		return result;
	}

	static std::unordered_map<std::string, std::string> GetImportTable(NodeGraphFile^ file)
	{
		ConversionContext context;
		for each(auto entry in file->SubGraphs) {
			PrimeImportTable(entry.Value->Signature, context);
			PrimeImportTable(entry.Value->Graph, context);
		}
		return context._importTable;
	}

	NodeGraphFile^			NodeGraphFile::ConvertFromNative(const GraphLanguage::GraphSyntaxFile& input, const ::Assets::DirectorySearchRules& searchRules)
	{
		ConversionContext context;
		context._importTable = input._imports;
		NodeGraphFile^ result = gcnew NodeGraphFile;
		for (const auto&p:input._subGraphs) {
			NodeGraphFile::SubGraph^ subGraph = gcnew NodeGraphFile::SubGraph;
			subGraph->Signature = NodeGraphSignature::ConvertFromNative(p.second._signature, context);
			subGraph->Graph = NodeGraph::ConvertFromNative(p.second._graph, context);
			result->SubGraphs->Add(clix::marshalString<clix::E_UTF8>(p.first), subGraph);
		}
		for (const auto&at:input._attributeTables) {
			result->AttributeTables->Add(
				clix::marshalString<clix::E_UTF8>(at.first),
				GUILayer::ConvertFromNative(at.second));
		}
		result->_searchRules = gcnew GUILayer::DirectorySearchRules(searchRules);
		return result;
	}

	GUILayer::DirectorySearchRules^ NodeGraphFile::GetSearchRules()
	{
		return _searchRules;
	}

	NodeGraphFile::NodeGraphFile()
	{
		_searchRules = gcnew GUILayer::DirectorySearchRules();
	}

	NodeGraphFile::~NodeGraphFile()
	{
		delete _searchRules;
		_searchRules = nullptr;
		delete _subGraphs;
		_subGraphs = nullptr;
		delete _attributeTables;
		_attributeTables = nullptr;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class GraphNodeGraphProvider : public GraphLanguage::BasicNodeGraphProvider, public std::enable_shared_from_this<GraphNodeGraphProvider>
    {
    public:
        std::vector<Signature> FindSignatures(StringSection<> name);
		std::optional<NodeGraph> FindGraph(StringSection<> name);
		std::string TryFindAttachedFile(StringSection<> name);

        GraphNodeGraphProvider(
			NodeGraphFile^ parsedGraphFile,
			const std::unordered_map<std::string, std::string>& imports,
			const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& dependencyValidation,
			const ::Assets::DependentFileState& parsedGraphFileState);
        ~GraphNodeGraphProvider();
    protected:
		msclr::gcroot<NodeGraphFile^> _parsedGraphFile;
		::Assets::DependencyValidation _parsedGraphFileDepVal;
		std::unordered_map<std::string, std::string> _imports;
    };

	std::shared_ptr<GraphLanguage::INodeGraphProvider> MakeGraphSyntaxProvider(
		NodeGraphFile^ parsedGraphFile,
		const std::unordered_map<std::string, std::string>& imports,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& dependencyValidation)
	{
		return std::make_shared<GraphNodeGraphProvider>(parsedGraphFile, imports, searchRules, dependencyValidation);
	}

	auto GraphNodeGraphProvider::FindSignatures(StringSection<> name) -> std::vector<Signature>
	{
		// Interpret the given string to find a function signature that matches it
		// First, check to see if it's scoped as an imported function
		// There's similar logic in the "GraphNodeGraphProvider" implementation in GraphSyntaxParse
		if (!name.IsEmpty()) {
			auto importedName = _imports.find(name.AsString());
			if (importedName != _imports.end())
				return BasicNodeGraphProvider::FindSignatures(importedName->second);
			return BasicNodeGraphProvider::FindSignatures(name);
		}

		// Look for all of the the function within the parsed graph syntax file
		std::vector<Signature> result;
		for each(KeyValuePair<String^, NodeGraphFile::SubGraph^> subGraph in _parsedGraphFile->SubGraphs) {	
			ConversionContext convContext;
			result.push_back(
				Signature{ 
					clix::marshalString<clix::E_UTF8>(subGraph.Key), 
					subGraph.Value->Signature->ConvertToNative(convContext), 
					std::string{}, true, _parsedGraphFileDepVal });
		}
		return result;
	}

	auto GraphNodeGraphProvider::FindGraph(StringSection<> name) -> std::optional<NodeGraph>
	{
		auto *scopingOperator = name.begin() + 1;
		while (scopingOperator < name.end()) {
			if (*(scopingOperator-1) == ':' && *scopingOperator == ':')
				break;
			++scopingOperator;
		}
		if (scopingOperator < name.end()) {
			auto import = MakeStringSection(name.begin(), scopingOperator-1).AsString();
			auto functionName = MakeStringSection(scopingOperator+1, name.end());

			auto importedName = _imports.find(import);
			if (importedName != _imports.end())
				return BasicNodeGraphProvider::FindGraph(importedName->second + "::" + functionName.AsString());
			return BasicNodeGraphProvider::FindGraph(import + "::" + functionName.AsString());
		}

		// Look for the function within the parsed graph syntax file
		NodeGraphFile::SubGraph^ subGraph = nullptr;
		System::String^ str = clix::marshalString<clix::E_UTF8>(name);
		if (_parsedGraphFile->SubGraphs->TryGetValue(str, subGraph)) {
			ConversionContext convContext;
			NodeGraph result { name.AsString(), subGraph->Graph->ConvertToNative(convContext), subGraph->Signature->ConvertToNative(convContext), nullptr, _parsedGraphFileDepVal };
			convContext._importTable.insert(_imports.begin(), _imports.end());
			result._subProvider = MakeGraphSyntaxProvider(
				_parsedGraphFile, convContext._importTable, 
				_parsedGraphFile->GetSearchRules()->GetNative(),
				_parsedGraphFileDepVal);
			return result;
		}

		// Just fallback to default behaviour
		return BasicNodeGraphProvider::FindGraph(name);
	}

	std::string GraphNodeGraphProvider::TryFindAttachedFile(StringSection<> name)
	{
		char resolvedName[MaxPath];
		auto splitter = MakeFileNameSplitter(name);
		auto rootName = splitter.StemPathAndFilename();
		auto importedName = _imports.find(rootName.AsString());
		if (importedName != _imports.end()) {
			resolvedName[0] = '\0';
			XlCatString(resolvedName, importedName->second);
			XlCatString(resolvedName, splitter.ExtensionWithPeriod());
			GetDirectorySearchRules().ResolveFile(resolvedName, resolvedName);
		} else {
			GetDirectorySearchRules().ResolveFile(resolvedName, name);
		}
		return resolvedName;
	}

	GraphNodeGraphProvider::GraphNodeGraphProvider(
		NodeGraphFile^ parsedGraphFile,
		const std::unordered_map<std::string, std::string>& imports,
		const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& dependencyValidation)
	: BasicNodeGraphProvider(searchRules)
	, _parsedGraphFile(parsedGraphFile)
	, _imports(imports)
	, _parsedGraphFileDepVal(dependencyValidation)
	{}

	GraphNodeGraphProvider::~GraphNodeGraphProvider()
	{}

	std::shared_ptr<GraphLanguage::INodeGraphProvider> NodeGraphFile::MakeNodeGraphProvider()
	{
		return MakeGraphSyntaxProvider(this, GetImportTable(this), GetSearchRules()->GetNative(), {});
	}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	AttachedSchemaFile::AttachedSchemaFile(String^ schemaFileName, String^ schemaName)
	{
		SchemaFileName = schemaFileName;
		SchemaName = schemaName;
	}

	AttachedSchemaFile::~AttachedSchemaFile() {}

	IEnumerable<AttachedSchemaFile^>^ NodeGraphFile::FindAttachedSchemaFilesForNode(String^ nodeArchiveName)
	{
		auto nodeGraphProvider = MakeNodeGraphProvider();

		std::vector<std::pair<std::string, std::string>> schemaFiles;
		AddAttachedSchemaFiles(
			schemaFiles, 
			clix::marshalString<clix::E_UTF8>(nodeArchiveName), 
			*nodeGraphProvider);

		if (schemaFiles.empty())
			return nullptr;

		List<AttachedSchemaFile^>^ result = gcnew List<AttachedSchemaFile^>();
		for (const auto&s:schemaFiles)
			result->Add(gcnew AttachedSchemaFile(
				clix::marshalString<clix::E_UTF8>(s.first),
				clix::marshalString<clix::E_UTF8>(s.second)
			));
		return result;
	}
}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NodeGraph.h"
#include "NodeGraphSignature.h"
#include "../Assets/AssetsCore.h"
#include "../Assets/IFileSystem.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Optional.h"
#include <unordered_map>
#include <set>

namespace GraphLanguage
{
	class FunctionSignature;

	class INodeGraphProvider
    {
    public:
        struct Signature 
        {
            std::string _name;
            NodeGraphSignature _signature;
			std::string _sourceFile;
			bool _isGraphSyntax = false;
			::Assets::DependencyValidation _depVal;
			::Assets::DependentFileState _fileState;
        };
		virtual std::vector<Signature> FindSignatures(StringSection<> name) = 0;

		struct NodeGraph
        {
            std::string _name;
            GraphLanguage::NodeGraph _graph;
			NodeGraphSignature _signature;
			std::shared_ptr<INodeGraphProvider> _subProvider;
			::Assets::DependencyValidation _depVal;
			::Assets::DependentFileState _fileState;
        };
        virtual std::optional<NodeGraph> FindGraph(StringSection<> name) = 0;

		virtual std::string TryFindAttachedFile(StringSection<> name) = 0;
		
		XLE_DEPRECATED_ATTRIBUTE virtual std::optional<Signature> FindSignature(StringSection<> name) final ;	// legacy interface -- prefer the pural FindSignatures()

        virtual ~INodeGraphProvider();
    };

    class BasicNodeGraphProvider : public INodeGraphProvider
    {
    public:
        std::vector<Signature> FindSignatures(StringSection<> name) override;
		std::optional<NodeGraph> FindGraph(StringSection<> name) override;
		std::string TryFindAttachedFile(StringSection<> name) override;

        BasicNodeGraphProvider(const ::Assets::DirectorySearchRules& searchRules);
        ~BasicNodeGraphProvider();
    protected:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const;
    };

	void AddAttachedSchemaFiles(
		std::vector<std::pair<std::string, std::string>>& result,
		const std::string& graphArchiveName,
		GraphLanguage::INodeGraphProvider& nodeGraphProvider);
}


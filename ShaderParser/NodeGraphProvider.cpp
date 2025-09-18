// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NodeGraphProvider.h"
#include "SignatureAsset.h"
#include "GraphSyntax.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/DepVal.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/ChunkFileContainer.h"
#include "../Assets/IArtifact.h"

namespace GraphLanguage
{
	static auto GetFunction(const ShaderFragmentSignature& sig, StringSection<char> fnName) -> const NodeGraphSignature*
	{
		auto i = std::find_if(
			sig._functions.cbegin(), sig._functions.cend(),
            [fnName](const auto& signature) { return XlEqString(signature.first.AsStringSection(), fnName); });
        if (i!=sig._functions.cend())
			return &i->second;
		return nullptr;
	}

	static auto GetUniformBuffer(const ShaderFragmentSignature& sig, StringSection<char> structName) -> const UniformBufferSignature*
	{
		auto i = std::find_if(
			sig._uniformBuffers.cbegin(), sig._uniformBuffers.cend(),
            [structName](const auto& signature) { return XlEqString(signature.first.AsStringSection(), structName); });
        if (i!=sig._uniformBuffers.cend())
			return &i->second;
		return nullptr;
	}

    static std::pair<StringSection<>, StringSection<>> SplitArchiveName(StringSection<> input)
    {
        auto pos = std::find(input.begin(), input.end(), ':');
        if (pos != input.end())
			if ((pos+1) != input.end() && *(pos+1) == ':')
				return std::make_pair(MakeStringSection(input.begin(), pos), MakeStringSection(pos+2, input.end()));

		return std::make_pair(input, StringSection<>{});
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class BasicNodeGraphProvider::Pimpl
	{
	public:
        ::Assets::DirectorySearchRules _searchRules;

		struct CachedItem
		{
			std::shared_ptr<ShaderSourceParser::SignatureAsset> _signature;
			std::string _fn;
		};
        std::vector<std::pair<uint64_t, CachedItem>> _cache;
	};

    auto BasicNodeGraphProvider::FindSignatures(StringSection<> name) -> std::vector<Signature>
    {
		if (name.IsEmpty())
			return {};

        auto hash = Hash64(name.begin(), name.end());
        auto existing = LowerBound(_pimpl->_cache, hash);
        if (existing == _pimpl->_cache.end() || existing->first != hash || existing->second._signature->GetDependencyValidation().GetValidationIndex() > 0) {
			char resolvedFile[MaxPath];
			_pimpl->_searchRules.ResolveFile(resolvedFile, name);
			if (!resolvedFile[0])
				return {};

			// note -- synchronized construction!
			auto fragment = ::Assets::ActualizeAssetPtr<ShaderSourceParser::SignatureAsset>(resolvedFile);
			if (existing == _pimpl->_cache.end() || existing->first != hash)
				existing = _pimpl->_cache.emplace(existing, hash, Pimpl::CachedItem{fragment, resolvedFile});
			else
				existing->second = Pimpl::CachedItem{fragment, resolvedFile};
		}

		std::vector<Signature> result;
		for (const auto&fn:existing->second._signature->GetSignature()._functions) {
			INodeGraphProvider::Signature rSig;
			rSig._name = fn.first;
			rSig._signature = fn.second;
			rSig._sourceFile = existing->second._fn;
			rSig._isGraphSyntax = existing->second._signature->IsGraphSyntaxFile();
			rSig._depVal = existing->second._signature->GetDependencyValidation();
			result.push_back(rSig);
		}
		return result;
    }

	auto BasicNodeGraphProvider::FindGraph(StringSection<> name) -> std::optional<NodeGraph>
	{
		assert(0);		// note -- requires GraphSyntax parsing code, which we're trying to avoid
		auto splitName = SplitArchiveName(name);
		char resolvedName[MaxPath];
		_pimpl->_searchRules.ResolveFile(resolvedName, splitName.first);
		return LoadGraphSyntaxFile(resolvedName, splitName.second);
	}

	std::string BasicNodeGraphProvider::TryFindAttachedFile(StringSection<> name)
	{
		char resolvedName[MaxPath];
		_pimpl->_searchRules.ResolveFile(resolvedName, name);
		if (resolvedName[0])
			return resolvedName;
		return {};
	}

	const ::Assets::DirectorySearchRules& BasicNodeGraphProvider::GetDirectorySearchRules() const
	{
		return _pimpl->_searchRules;
	}

    BasicNodeGraphProvider::BasicNodeGraphProvider(const ::Assets::DirectorySearchRules& searchRules)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_searchRules = searchRules;
	}
        
    BasicNodeGraphProvider::~BasicNodeGraphProvider() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	auto INodeGraphProvider::FindSignature(StringSection<> name) -> std::optional<Signature>
	{
		// This is the legacy interface, wherein we search for individual signatures at a time
		// (as opposed to getting all of the signatures from a full file)
		auto split = SplitArchiveName(name);
		if (split.second.IsEmpty()) {
			// To support legacy behaviour, when we're searching for a signature with just a flat name,
			// and no archive name divider (ie, no ::), we will call FindSignatures with an empty string.
			// Some implementations of INodeGraphProvider have special behaviour when search for signatures
			// with an empty string (eg, GraphNodeGraphProvider can look within a root/source node graph file)
			split.first = {};
			split.second = name;
		}

		auto sigs = FindSignatures(split.first);
		for (const auto&s:sigs)
			if (XlEqString(MakeStringSection(s._name), split.second))
				return s;
		return {};
	}

	INodeGraphProvider::~INodeGraphProvider() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void AddAttachedSchemaFiles(
		std::vector<std::pair<std::string, std::string>>& result,
		const std::string& graphArchiveName,
		GraphLanguage::INodeGraphProvider& nodeGraphProvider)
	{
		auto scopingOperator = graphArchiveName.begin();
		while (scopingOperator < graphArchiveName.end() && *scopingOperator != ':')
			++scopingOperator;

		auto attachedFileName = MakeStringSection(graphArchiveName.begin(), scopingOperator).AsString() + ".py";
		attachedFileName = nodeGraphProvider.TryFindAttachedFile(attachedFileName);
		if (	!attachedFileName.empty()
			&&	::Assets::MainFileSystem::TryGetDesc(attachedFileName)._snapshot._state == ::Assets::FileSnapshot::State::Normal) {

			while (scopingOperator < graphArchiveName.end() && *scopingOperator == ':')
				++scopingOperator;
			auto schemaName = MakeStringSection(scopingOperator, graphArchiveName.end());

			bool foundExisting = false;
			for (const auto&r:result) {
				if (XlEqString(MakeStringSection(attachedFileName), r.first) && XlEqString(schemaName, r.second)) {
					foundExisting = true;
					break;
				}
			}

			if (!foundExisting)
				result.push_back(std::make_pair(attachedFileName, schemaName.AsString()));
		}

		// If this node is actually a node graph itself, we must recurse into it and look for more attached schema files inside
		auto sig = nodeGraphProvider.FindSignature(graphArchiveName);
		if (sig.has_value() && sig.value()._isGraphSyntax) {
			auto oSubGraph = nodeGraphProvider.FindGraph(graphArchiveName);
			if (oSubGraph.has_value()) {
				const auto& subGraph = oSubGraph.value();
				for (const auto&n:subGraph._graph.GetNodes()) {
					AddAttachedSchemaFiles(result, n.ArchiveName(), *subGraph._subProvider);
				}
			}
		}
	}

}


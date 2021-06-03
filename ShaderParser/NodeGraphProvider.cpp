// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NodeGraphProvider.h"
#include "GraphSyntax.h"
#include "ShaderSignatureParser.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/DepVal.h"
#include "../Assets/Assets.h"
#include "../Utility/Streams/PathUtils.h"

namespace GraphLanguage
{
    static std::pair<std::string, ::Assets::DependentFileState> LoadSourceFile(StringSection<char> sourceFileName)
    {
		::Assets::DependentFileState fileState;
		size_t size = 0;
		auto data = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(sourceFileName.AsString().c_str(), &size, &fileState);
		return std::make_pair(std::string((const char*)data.get(), (const char*)PtrAdd(data.get(), size)), fileState);
    }

	class ShaderFragment
	{
	public:
		auto GetFunction(StringSection<char> fnName) const -> const NodeGraphSignature*;
		auto GetUniformBuffer(StringSection<char> structName) const -> const UniformBufferSignature*;

		const ShaderFragmentSignature& GetSignature() const { return _sig; }
		const std::string& GetSourceFileName() const { return _srcFileName; }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const ::Assets::DependentFileState& GetFileState() const { return _fileState; }
		ShaderFragment(StringSection<::Assets::ResChar> fn);
		~ShaderFragment();

		bool _isGraphSyntaxFile = false;
	private:
		ShaderFragmentSignature _sig;
		::Assets::DependencyValidation _depVal;
		::Assets::DependentFileState _fileState;
		std::string _srcFileName;
	};

	auto ShaderFragment::GetFunction(StringSection<char> fnName) const -> const NodeGraphSignature*
	{
		auto i = std::find_if(
			_sig._functions.cbegin(), _sig._functions.cend(),
            [fnName](const std::pair<std::string, NodeGraphSignature>& signature) { return XlEqString(MakeStringSection(signature.first), fnName); });
        if (i!=_sig._functions.cend())
			return &i->second;
		return nullptr;
	}

	auto ShaderFragment::GetUniformBuffer(StringSection<char> structName) const -> const UniformBufferSignature*
	{
		auto i = std::find_if(
			_sig._uniformBuffers.cbegin(), _sig._uniformBuffers.cend(),
            [structName](const std::pair<std::string, UniformBufferSignature>& signature) { return XlEqString(MakeStringSection(signature.first), structName); });
        if (i!=_sig._uniformBuffers.cend())
			return &i->second;
		return nullptr;
	}

	ShaderFragment::ShaderFragment(StringSection<::Assets::ResChar> fn)
	: _srcFileName(fn.AsString())
	{
		auto shaderFile = LoadSourceFile(fn);
		_depVal = ::Assets::GetDepValSys().Make(shaderFile.second);

		if (shaderFile.first.empty())
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::MissingFile,
				_depVal,
				"Missing or empty file while loading: %s", fn.AsString().c_str()));

		TRY {
			if (XlEqStringI(MakeFileNameSplitter(fn).Extension(), "graph")) {
				auto graphSyntax = ParseGraphSyntax(shaderFile.first);
				for (auto& subGraph:graphSyntax._subGraphs)
					_sig._functions.emplace_back(std::make_pair(subGraph.first, std::move(subGraph.second._signature)));
				_isGraphSyntaxFile = true;
			} else {
				_sig = ShaderSourceParser::ParseHLSL(MakeStringSection(shaderFile.first));
			}
			_fileState = shaderFile.second;
		} CATCH(const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _depVal));
		} CATCH_END
	}

	ShaderFragment::~ShaderFragment() {}

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
        std::unordered_map<uint64_t, std::shared_ptr<ShaderFragment>> _cache;
	};

    auto BasicNodeGraphProvider::FindSignatures(StringSection<> name) -> std::vector<Signature>
    {
		if (name.IsEmpty())
			return {};

        auto hash = Hash64(name.begin(), name.end());
        auto existing = _pimpl->_cache.find(hash);
        if (existing == _pimpl->_cache.end() || existing->second->GetDependencyValidation() > 0) {
			char resolvedFile[MaxPath];
			_pimpl->_searchRules.ResolveFile(resolvedFile, name);
			if (!resolvedFile[0])
				return {};

			std::shared_ptr<ShaderFragment> fragment = ::Assets::AutoConstructAsset<std::shared_ptr<ShaderFragment>>(resolvedFile);
			existing = _pimpl->_cache.insert(std::make_pair(hash, fragment)).first;
		}

		std::vector<Signature> result;
		for (const auto&fn:existing->second->GetSignature()._functions) {
			INodeGraphProvider::Signature rSig;
			rSig._name = fn.first;
			rSig._signature = fn.second;
			rSig._sourceFile = existing->second->GetSourceFileName();
			rSig._isGraphSyntax = existing->second->_isGraphSyntaxFile;
			rSig._depVal = existing->second->GetDependencyValidation();
			rSig._fileState = existing->second->GetFileState();
			result.push_back(rSig);
		}
		return result;
    }

	auto BasicNodeGraphProvider::FindGraph(StringSection<> name) -> std::optional<NodeGraph>
	{
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
			&&	::Assets::MainFileSystem::TryGetDesc(attachedFileName)._state == ::Assets::FileDesc::State::Normal) {

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


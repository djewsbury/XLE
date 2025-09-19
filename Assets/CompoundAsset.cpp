#include "CompoundAsset.h"
#include "ConfigFileContainer.h"
#include "Assets.h"
#include "AssetMixins.h"
#include "IFileSystem.h"
#include "AssetTraits.h"		// for RemoveSmartPtr
#include "../Formatters/TextFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include <stack>

using namespace Utility::Literals;

namespace AssetsNew
{

	void CompoundAssetScaffold::Deserialize(Formatters::TextInputFormatter<char>& fmttr)
	{
		StringSection<> keyname;
		unsigned nextComponentTableIdx = 0;
		while (Formatters::TryKeyedItem(fmttr, keyname)) {
			if (XlEqString(keyname, "Entity")) {

				auto entityName = Formatters::RequireStringValue(fmttr);
				auto hashName = Hash64(entityName);
				auto i2 = LowerBound(_entityLookup, hashName);
				if (i2 != _entityLookup.end() && i2->first == hashName) Throw(::Formatters::FormatException("Duplicate entity name", fmttr.GetLocation()));
				_entityLookup.emplace(i2, hashName, EntityBookkeeping{ nextComponentTableIdx++, entityName });

			} else if (XlEqString(keyname, "Inherit")) {

				// this is handled natively by StructuredTextDocument
				auto entityName = Hash64(Formatters::RequireStringValue(fmttr));
				auto i2 = LowerBound(_entityLookup, entityName);
				if (i2 == _entityLookup.end() || i2->first != entityName) Throw(::Formatters::FormatException("Unknown entity name", fmttr.GetLocation()));
				if (i2->second._inheritBegin != ~0u) Throw(::Formatters::FormatException("Multiple inherit lists for the same entity", fmttr.GetLocation()));

				i2->second._inheritBegin = (unsigned)_inheritLists.size();

				Formatters::RequireBeginElement(fmttr);
				while (fmttr.TryStringValue(keyname)) {
					if (Hash64(keyname) == entityName)  Throw(::Formatters::FormatException("Entity inherits itself (" + keyname.AsString() + ")", fmttr.GetLocation()));
					// attach the given inherit information to the entity
					auto i3 = std::find_if(_inheritLists.begin()+i2->second._inheritBegin, _inheritLists.end(), [keyname](const auto& q) { return XlEqString(q, keyname); });
					if (i3 == _inheritLists.end()) _inheritLists.emplace_back(keyname);
				}
				Formatters::RequireEndElement(fmttr);

				i2->second._inheritEnd = (unsigned)_inheritLists.size();

			} else {

				auto entityName = Hash64(Formatters::RequireStringValue(fmttr));
				auto i2 = LowerBound(_entityLookup, entityName);
				if (i2 == _entityLookup.end() || i2->first != entityName) Throw(::Formatters::FormatException("Unknown entity name", fmttr.GetLocation()));

				auto componentTypeName = Hash64(keyname);
				auto i3 = LowerBound(_components, componentTypeName);
				if (i3 == _components.end() || i3->first != componentTypeName)
					i3 = _components.emplace(i3, componentTypeName, Component{});

				// we have a block of component information, but we don't know how to interpret it yet
				auto entityIdx = i2->second._componentTableIdx;
				if (fmttr.PeekNext() == Formatters::FormatterBlob::BeginElement) {
					fmttr.TryBeginElement();
					if (i3->second._inlineChunks.size() <= entityIdx) i3->second._inlineChunks.resize(entityIdx+1);
					i3->second._inlineChunks[entityIdx] = fmttr.SkipElement();
					Formatters::RequireEndElement(fmttr);
				} else {
					if (i3->second._externalReferences.size() <= entityIdx) i3->second._externalReferences.resize(entityIdx+1);
					i3->second._externalReferences[entityIdx] = Formatters::RequireStringValue(fmttr);
				}

			}
		}
		assert(fmttr.PeekNext() == Formatters::FormatterBlob::None);
	}

	CompoundAssetScaffold::CompoundAssetScaffold(::Assets::Blob&& blob)
	: _blob(std::move(blob))
	{
		assert(_blob);
		// ReadCompoundTextDocument will fail quickly if the input is not actually this style of compound text document
		auto compound = ::Assets::ReadCompoundTextDocument(MakeStringSection((const char*)AsPointer(_blob->begin()), (const char*)AsPointer(_blob->end())));
		if (!compound.empty()) {
			auto i = std::find_if(compound.begin(), compound.end(), [](const auto&q) { return XlEqString(q._type, "StructuredDocument") && XlEqString(q._name, "main"); });
			if (i == compound.end()) Throw(::Assets::Exceptions::ConstructionError(::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood, {}, "Expecting chunk with type=StructuredDocument and name=main"));

			Formatters::TextInputFormatter<> fmttr { i->_content };
			Deserialize(fmttr);
			_uniqueId = Hash64(i->_content);		// used for building cache keys for objects read out of this. Could alternatively just be an incrementing value
		} else {
			Formatters::TextInputFormatter<> fmttr { MakeIteratorRange(*_blob) };
			Deserialize(fmttr);
			_uniqueId = Hash64(MakeIteratorRange(*_blob));		// used for building cache keys for objects read out of this. Could alternatively just be an incrementing value
		}
	}

	CompoundAssetScaffold::~CompoundAssetScaffold() = default;

	bool CompoundAssetUtil::NeedToIncorporatedInheritedAssets(const ScaffoldIndexer& rootEntity)
	{
		if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&rootEntity)) {

			// In the scaffold case, we can check ahead of time to see if there is actually anything that can be inherited
			auto entityNameHash = scaffoldAndEntity->_entityNameHash;
			auto* scaffold = scaffoldAndEntity->GetCompoundAssetScaffold().get();
			auto ei = LowerBound(scaffold->_entityLookup, entityNameHash);
			if (ei == scaffold->_entityLookup.end() || ei->first != entityNameHash) 
				return false;

			return ei->second._inheritBegin != ei->second._inheritEnd;

		} else if (auto* contextAndIdentifier = std::get_if<ContextAndIdentifier>(&rootEntity)) {

			auto splitName = MakeFileNameSplitter(contextAndIdentifier->_identifier);
			return XlEqString(splitName.Extension(), "compound") || XlEqString(splitName.Extension(), "hlsl");

		} else {

			return true;

		}
	}

	namespace Internal
	{
		std::optional<ScaffoldAndEntityName> TryMakeScaffoldAndEntityNameSync(StringSection<> str, const ::Assets::DirectorySearchRules& searchRules)
		{
			auto splitName = MakeFileNameSplitter(str);
			if (!splitName.ParametersWithDivider().IsEmpty() && (XlEqString(splitName.Extension(), "compound") || XlEqString(splitName.Extension(), "hlsl"))) {
				char buffer[MaxPath];
				searchRules.ResolveFile(buffer, splitName.AllExceptParameters());
				return ScaffoldAndEntityName { ::Assets::ActualizeAsset<::Assets::ContextImbuedAsset<std::shared_ptr<CompoundAssetScaffold>>>(buffer), Hash64(splitName.Parameters()) DEBUG_ONLY(, splitName.Parameters().AsString()) };
			} else {
				return {};
			}
		}
	}

	ScaffoldIndexer CompoundAssetUtil::FindFirstDeserializableSync(uint64_t componentTypeName, const ScaffoldAndEntityName& indexer)
	{
		auto* scaffold = indexer.GetCompoundAssetScaffold().get();
		auto compTable = LowerBound(scaffold->_components, componentTypeName);
		if (compTable == scaffold->_components.end() || compTable->first != componentTypeName)
			return std::monostate{};

		std::stack<std::variant<CompoundAssetScaffold::EntityHashName, StringSection<>>> checkStack;
		checkStack.emplace(indexer._entityNameHash);

		while (!checkStack.empty()) {
			auto check = checkStack.top();
			checkStack.pop();

			// once we find a reference to another file, we have to end here
			// todo -- if this is a compound asset, we should look deeper
			if (check.index() == 1)
				return ContextAndIdentifier{std::get<StringSection<>>(check).AsString(), indexer.GetDirectorySearchRules()};

			auto entityNameHash = std::get<CompoundAssetScaffold::EntityHashName>(check);
			
			auto ei = LowerBound(scaffold->_entityLookup, entityNameHash);
			if (ei == scaffold->_entityLookup.end() || ei->first != entityNameHash) {
				#if defined(_DEBUG)
					Throw(std::runtime_error(Concatenate("Missing entity (while looking up ", indexer._entityName, " in ", indexer.GetDirectorySearchRules().GetBaseFile(), ")")));
				#else
					Throw(std::runtime_error("Missing entity"));
				#endif
			}

			// Does the requested
			if (compTable->second._inlineChunks.size() > ei->second._componentTableIdx && !compTable->second._inlineChunks[ei->second._componentTableIdx].IsEmpty())
				return ScaffoldAndEntityName { indexer._scaffold, entityNameHash DEBUG_ONLY(, indexer._entityName) };

			// Note that we'll check the inherits in reverse order (because of the stack)
			if (ei->second._inheritBegin != ei->second._inheritEnd)
				for (auto i:MakeIteratorRange(scaffold->_inheritLists.begin()+ei->second._inheritBegin, scaffold->_inheritLists.begin()+ei->second._inheritEnd)) {
					auto hashName = Hash64(i);
					auto q = LowerBound(scaffold->_entityLookup, hashName);
					if (q != scaffold->_entityLookup.end() && q->first == hashName) {
						checkStack.emplace(hashName);
					} else if (auto q = Internal::TryMakeScaffoldAndEntityNameSync(i, indexer.GetDirectorySearchRules())) {
						auto subItem = FindFirstDeserializableSync(componentTypeName, *q);
						if (subItem.index() != 0)
							return subItem;
					} else
						checkStack.emplace(i);
				}
		}

		return std::monostate{};
	}

	uint64_t CompoundAssetUtil::MakeCacheKey(const ScaffoldIndexer& indexer)
	{
		uint64_t cacheKey = 0;
		if (auto* scaffoldAndEntity = std::get_if<ScaffoldAndEntityName>(&indexer)) {

			cacheKey = HashCombine(scaffoldAndEntity->GetCompoundAssetScaffold()->_uniqueId, scaffoldAndEntity->_entityNameHash);
			
		} else if (auto* contextAndId = std::get_if<ContextAndIdentifier>(&indexer)) {

			// We have to resolve the file in order to have a reliable cache key
			auto splitName = MakeFileNameSplitter(contextAndId->_identifier);
			char resolvedFile[MaxPath];
			contextAndId->_searchRules.ResolveFile(resolvedFile, splitName.AllExceptParameters());
			cacheKey = HashFilenameAndPath(MakeStringSectionNullTerm(resolvedFile));
			cacheKey = HashCombine(cacheKey, Hash64(splitName.ParametersWithDivider()));

		} else {
			Throw(std::runtime_error("ScaffoldIndexer type unsupported"));
		}

		return cacheKey;
	}

	static_assert(::Assets::Internal::HasStdGetDependencyValidation<::Assets::ContextImbuedAsset<std::shared_ptr<CompoundAssetScaffold>>>);
	static_assert(!::Assets::Internal::HasStdGetActualizationLog<::Assets::ContextImbuedAsset<std::shared_ptr<CompoundAssetScaffold>>>);

}


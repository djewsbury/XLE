// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderPatchCollection.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Assets
{
	static const uint64_t s_rebuildHash = ~0ull;

	void ShaderPatchCollection::MergeIn(const ShaderPatchCollection& src)
	{
		for (const auto&p:src._patches) {
			if (p.first.empty()) {
				_patches.push_back(p);	// empty name -- can't override
			} else {
				auto i = std::find_if(
					_patches.begin(), _patches.end(),
					[&p](const std::pair<std::string, ShaderSourceParser::InstantiationRequest>& q) { return q.first == p.first; });
				if (i == _patches.end()) {
					_patches.push_back(p);
				} else {
					i->second = p.second;
				}
			}
		}
		if (!src._descriptorSet.empty())
			_descriptorSet = src._descriptorSet;
		if (!src._preconfiguration.empty())
			_preconfiguration = src._preconfiguration;

		SortAndCalculateHash();
		::Assets::DependencyValidationMarker depVals[] { _depVal, src._depVal };
		_depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals));
	}

	void ShaderPatchCollection::AddPatch(const std::string& name, const ShaderSourceParser::InstantiationRequest& instRequest)
	{
		auto i = std::find_if(_patches.begin(), _patches.end(), [name](const auto& q) { return q.first == name; });
		if (i != _patches.end())
			Throw(std::runtime_error("Cannot add shader patch named (" + name + ") because one with this name already exists"));
		_patches.emplace_back(name, instRequest);
		_hash = s_rebuildHash;
	}

	void ShaderPatchCollection::SetDescriptorSetFileName(const std::string& name)
	{
		_descriptorSet = name;
		_hash = s_rebuildHash;
	}

	void ShaderPatchCollection::SetPreconfigurationFileName(const std::string& name)
	{
		_preconfiguration = name;
		_hash = s_rebuildHash;
	}

	uint64_t ShaderPatchCollection::GetHash() const
	{
		if (_hash == s_rebuildHash)
			const_cast<ShaderPatchCollection*>(this)->SortAndCalculateHash();
		return _hash;
	}

	ShaderPatchCollection::ShaderPatchCollection()
	{
		_hash = 0;
	}

	ShaderPatchCollection::ShaderPatchCollection(IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> patches)
	: _patches(patches.begin(), patches.end())
	{
		SortAndCalculateHash();
	}

	ShaderPatchCollection::ShaderPatchCollection(std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>>&& patches)
	{
		_patches.reserve(patches.size());
		for (auto& p:patches)
			_patches.emplace_back(std::move(p.first), std::move(p.second));
		SortAndCalculateHash();
	}

	ShaderPatchCollection::~ShaderPatchCollection() {}

	void ShaderPatchCollection::SortAndCalculateHash()
	{
		if (_patches.empty()) {
			_hash = 0;
			return;
		}

		using Pair = std::pair<std::string, ShaderSourceParser::InstantiationRequest>;
		std::stable_sort(
			_patches.begin(), _patches.end(), 
			[](const Pair& lhs, const Pair& rhs) { return lhs.second._archiveName < rhs.second._archiveName; });

		_hash = DefaultSeed64;
		for (const auto&p:_patches) {
			// note that p.first doesn't actually contribute to the hash -- it's not used during the merge operation
			assert(!p.second._customProvider);
			_hash = Hash64(p.second._archiveName, _hash);
			_hash = HashCombine(p.second.CalculateInstanceHash(), _hash);
			if (!p.second._implementsArchiveName.empty())
				_hash = Hash64(p.second._implementsArchiveName, _hash);
		}
		if (!_descriptorSet.empty())
			_hash = Hash64(_descriptorSet, _hash);
		if (!_preconfiguration.empty())
			_hash = Hash64(_preconfiguration, _hash);
	}

	bool operator<(const ShaderPatchCollection& lhs, const ShaderPatchCollection& rhs) { return lhs.GetHash() < rhs.GetHash(); }
	bool operator<(const ShaderPatchCollection& lhs, uint64_t rhs) { return lhs.GetHash() < rhs; }
	bool operator<(uint64_t lhs, const ShaderPatchCollection& rhs) { return lhs < rhs.GetHash(); }

	static void SerializeInstantiationRequest(
		OutputStreamFormatter& formatter, 
		const ShaderSourceParser::InstantiationRequest& instRequest)
	{
		formatter.WriteSequencedValue(instRequest._archiveName);
		for (const auto&p:instRequest._parameterBindings) {
			auto ele = formatter.BeginKeyedElement(p.first);
			SerializeInstantiationRequest(formatter, *p.second);
			formatter.EndElement(ele);
		}
		if (!instRequest._implementsArchiveName.empty())
			formatter.WriteKeyedValue("Implements", instRequest._implementsArchiveName);
	}

	void SerializationOperator(OutputStreamFormatter& formatter, const ShaderPatchCollection& patchCollection)
	{
		for (const auto& p:patchCollection._patches) {
			auto pele = (p.first.empty()) ? formatter.BeginSequencedElement() : formatter.BeginKeyedElement(p.first);
			SerializeInstantiationRequest(formatter, p.second);
			formatter.EndElement(pele);
		}
		if (!patchCollection.GetDescriptorSetFileName().IsEmpty())
			formatter.WriteKeyedValue("DescriptorSet", patchCollection.GetDescriptorSetFileName());
		if (!patchCollection.GetPreconfigurationFileName().IsEmpty())
			formatter.WriteKeyedValue("Preconfiguration", patchCollection.GetPreconfigurationFileName());
	}

	std::ostream& SerializationOperator(std::ostream& str, const ShaderPatchCollection& patchCollection)
	{
		str << "PatchCollection[" << patchCollection.GetHash() << "]";
		return str;
	}

	static std::string ResolveArchiveName(StringSection<> src, const ::Assets::DirectorySearchRules& searchRules)
	{
		auto splitName = MakeFileNameSplitter(src);
		if (splitName.DriveAndPath().IsEmpty()) {
			char resolvedFile[MaxPath];
			searchRules.ResolveFile(resolvedFile, splitName.FileAndExtension());
			if (resolvedFile[0]) {
				std::string result = resolvedFile;
				result.insert(result.end(), splitName.ParametersWithDivider().begin(), splitName.ParametersWithDivider().end());
				return result;
			} else {
				return src.AsString();
			}
		} else {
			return src.AsString();
		}
	}

	static ShaderSourceParser::InstantiationRequest DeserializeInstantiationRequest(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules)
	{
		ShaderSourceParser::InstantiationRequest result;

		auto archiveNameInput = RequireStringValue(formatter);		// Expecting only a single sequenced value in each fragment, which is the entry point name
		result._archiveName = ResolveArchiveName(archiveNameInput, searchRules);
		assert(!result._archiveName.empty());

		StringSection<> bindingName;
		while (formatter.TryKeyedItem(bindingName)) {
			if (XlEqString(bindingName, "Implements")) {
				if (!result._implementsArchiveName.empty())
					Throw(FormatException("Multiple \"Implements\" specifications found", formatter.GetLocation()));
				result._implementsArchiveName = ResolveArchiveName(RequireStringValue(formatter), searchRules);
			} else {
				RequireBeginElement(formatter);
				result._parameterBindings.emplace(
					std::make_pair(
						bindingName.AsString(),
						std::make_unique<ShaderSourceParser::InstantiationRequest>(DeserializeInstantiationRequest(formatter, searchRules))));
				RequireEndElement(formatter);
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating InstantiationRequest", formatter.GetLocation()));

		return result;
	}

	ShaderPatchCollection::ShaderPatchCollection(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		for (;;) {
			auto next = formatter.PeekNext();
			if (next == FormatterBlob::KeyedItem) {
				auto name = RequireKeyedItem(formatter);
				
				if (XlEqString(name, "DescriptorSet")) {
					_descriptorSet = RequireStringValue(formatter).AsString();
					continue;
				} else if (XlEqString(name, "Preconfiguration")) {
					_preconfiguration = RequireStringValue(formatter).AsString();
					continue;
				}
				
				if (formatter.PeekNext() != FormatterBlob::BeginElement)
					Throw(FormatException(StringMeld<256>() << "Unexpected attribute (" << name << ") in ShaderPatchCollection", formatter.GetLocation()));

				RequireBeginElement(formatter);
				_patches.emplace_back(std::make_pair(name.AsString(), DeserializeInstantiationRequest(formatter, searchRules)));
				RequireEndElement(formatter);
			} else if (next == FormatterBlob::BeginElement) {
				RequireBeginElement(formatter);
				_patches.emplace_back(std::make_pair(std::string{}, DeserializeInstantiationRequest(formatter, searchRules)));
				RequireEndElement(formatter);
			} else
				break;
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating ShaderPatchCollection", formatter.GetLocation()));

		SortAndCalculateHash();
	}

	std::vector<ShaderPatchCollection> DeserializeShaderPatchCollectionSet(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	{
		std::vector<ShaderPatchCollection> result;
		while (formatter.TryBeginElement()) {
			result.emplace_back(ShaderPatchCollection(formatter, searchRules, depVal));
			RequireEndElement(formatter);
		}
		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating ShaderPatchCollection", formatter.GetLocation()));
		std::sort(result.begin(), result.end());
		return result;
	}

	void SerializeShaderPatchCollectionSet(OutputStreamFormatter& formatter, IteratorRange<const ShaderPatchCollection*> patchCollections)
	{
		for (const auto& p:patchCollections) {
			auto ele = formatter.BeginSequencedElement();
			SerializationOperator(formatter, p);
			formatter.EndElement(ele);
		}
	}

}}


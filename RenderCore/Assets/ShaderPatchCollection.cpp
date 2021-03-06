// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderPatchCollection.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Assets
{
	void ShaderPatchCollection::MergeInto(ShaderPatchCollection& dest) const
	{
		for (const auto&p:_patches) {
			auto i = std::find_if(
				dest._patches.begin(), dest._patches.end(),
				[&p](const std::pair<std::string, ShaderSourceParser::InstantiationRequest>& q) { return q.first == p.first; });
			if (i == dest._patches.end()) {
				dest._patches.push_back(p);
			} else {
				i->second = p.second;
			}
		}
		if (!_descriptorSet.empty())
			dest._descriptorSet = _descriptorSet;

		dest.SortAndCalculateHash();
	}

	ShaderPatchCollection::ShaderPatchCollection()
	{
		_hash = 0;
		_depVal = std::make_shared<::Assets::DependencyValidation>();
	}

	ShaderPatchCollection::ShaderPatchCollection(IteratorRange<const std::pair<std::string, ShaderSourceParser::InstantiationRequest>*> patches)
	: _patches(patches.begin(), patches.end())
	{
		_depVal = std::make_shared<::Assets::DependencyValidation>();
		SortAndCalculateHash();
	}

	ShaderPatchCollection::ShaderPatchCollection(std::vector<std::pair<std::string, ShaderSourceParser::InstantiationRequest>>&& patches)
	: _patches(std::move(patches))
	{
		_depVal = std::make_shared<::Assets::DependencyValidation>();
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
		}

		if (!_descriptorSet.empty()) {
			_hash = Hash64(_descriptorSet, _hash);
		}
	}

	bool operator<(const ShaderPatchCollection& lhs, const ShaderPatchCollection& rhs) { return lhs.GetHash() < rhs.GetHash(); }
	bool operator<(const ShaderPatchCollection& lhs, uint64_t rhs) { return lhs.GetHash() < rhs; }
	bool operator<(uint64_t lhs, const ShaderPatchCollection& rhs) { return lhs < rhs.GetHash(); }
	std::ostream& operator<<(std::ostream& str, const ShaderPatchCollection& patchCollection)
	{
		return str << patchCollection.GetHash();
	}

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
	}

	void ShaderPatchCollection::SerializeMethod(OutputStreamFormatter& formatter) const
	{
		for (const auto& p:_patches) {
			auto pele = formatter.BeginKeyedElement(p.first);
			SerializeInstantiationRequest(formatter, p.second);
			formatter.EndElement(pele);
		}
		if (!_descriptorSet.empty())
			formatter.WriteKeyedValue("DescriptorSet", _descriptorSet);
	}

	static ShaderSourceParser::InstantiationRequest DeserializeInstantiationRequest(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules)
	{
		ShaderSourceParser::InstantiationRequest result;

		auto archiveNameInput = RequireValue(formatter);		// Expecting only a single sequenced value in each fragment, which is the entry point name
		auto splitName = MakeFileNameSplitter(archiveNameInput);
		if (splitName.DriveAndPath().IsEmpty()) {
			char resolvedFile[MaxPath];
			searchRules.ResolveFile(resolvedFile, splitName.FileAndExtension());
			if (resolvedFile[0]) {
				result._archiveName = resolvedFile;
				result._archiveName.insert(result._archiveName.end(), splitName.ParametersWithDivider().begin(), splitName.ParametersWithDivider().end());
			} else {
				result._archiveName = archiveNameInput.AsString();
			}
		} else {
			result._archiveName = archiveNameInput.AsString();
		}
		assert(!result._archiveName.empty());

		StringSection<> bindingName;
		while (formatter.TryKeyedItem(bindingName)) {
			RequireBeginElement(formatter);
			result._parameterBindings.emplace(
				std::make_pair(
					bindingName.AsString(),
					std::make_unique<ShaderSourceParser::InstantiationRequest>(DeserializeInstantiationRequest(formatter, searchRules))));
			RequireEndElement(formatter);
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating InstantiationRequest", formatter.GetLocation()));

		return result;
	}

	ShaderPatchCollection::ShaderPatchCollection(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DepValPtr& depVal)
	: _depVal(depVal)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			auto name = RequireKeyedItem(formatter);
			
			if (XlEqString(name, "DescriptorSet")) {
				if (!_descriptorSet.empty())
					Throw(FormatException("Descriptor set specified multiple times", formatter.GetLocation()));
				_descriptorSet = RequireValue(formatter).AsString();
			} else {
				if (formatter.PeekNext() != FormatterBlob::BeginElement)
					Throw(FormatException(StringMeld<256>() << "Unexpected attribute (" << name << ") in ShaderPatchCollection", formatter.GetLocation()));

				RequireBeginElement(formatter);
				_patches.emplace_back(std::make_pair(name.AsString(), DeserializeInstantiationRequest(formatter, searchRules)));
				RequireEndElement(formatter);
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating ShaderPatchCollection", formatter.GetLocation()));

		SortAndCalculateHash();
	}

	std::vector<ShaderPatchCollection> DeserializeShaderPatchCollectionSet(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DepValPtr& depVal)
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

	std::ostream& SerializationOperator(std::ostream& str, const ShaderPatchCollection& patchCollection)
	{
		str << "PatchCollection[" << patchCollection.GetHash() << "]";
		return str;
	}

}}


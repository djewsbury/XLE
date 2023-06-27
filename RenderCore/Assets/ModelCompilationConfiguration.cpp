// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelCompilationConfiguration.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/FastParseValue.h"

using namespace Utility::Literals;

namespace RenderCore { namespace Assets
{
	static void DifferenceSortedSet(std::vector<uint64_t>& a, IteratorRange<const uint64_t*> b)
	{
		auto ia = a.begin();
		auto ib = b.begin();
		while (ia != a.end()) {
			while (ib != b.end() && *ib < *ia) ++ib;
			if (ib != b.end() && *ib == *ia) {
				ia = a.erase(ia);
			} else
				++ia;
		}
	}

	static void UnionSortedSet(std::vector<uint64_t>& a, IteratorRange<const uint64_t*> b)
	{
		auto aSize = a.size();
		a.insert(a.end(), b.begin(), b.end());
		std::partial_sort(a.begin(), a.begin()+aSize, a.end());
	}

	void ModelCompilationConfiguration::RawGeoRules::MergeIn(const ModelCompilationConfiguration::RawGeoRules& src)
	{
		if (src._16BitNativeTypes) _16BitNativeTypes = src._16BitNativeTypes;
		if (src._rebuildTangents) _rebuildTangents = src._rebuildTangents;
		if (src._rebuildNormals) _rebuildNormals = src._rebuildNormals;
		DifferenceSortedSet(_includeAttributes, src._excludeAttributes);
		DifferenceSortedSet(_excludeAttributes, src._includeAttributes);
		UnionSortedSet(_includeAttributes, src._includeAttributes);
		UnionSortedSet(_excludeAttributes, src._excludeAttributes);
	}

	void ModelCompilationConfiguration::SkeletonRules::MergeIn(const ModelCompilationConfiguration::SkeletonRules& src)
	{
		UnionSortedSet(_animatableBones, src._animatableBones);
		UnionSortedSet(_outputBones, src._outputBones);
	}

	void ModelCompilationConfiguration::MergeInWithFilenameResolve(const ModelCompilationConfiguration& src, const ::Assets::DirectorySearchRules&)
	{
		// Merge in, overwriting existing settings
		for (const auto& rawGeoRule:src._rawGeoRules) {
			auto i = std::find_if(_rawGeoRules.begin(), _rawGeoRules.end(), [n=rawGeoRule.first](const auto&q) { return q.first == n; });
			if (i != _rawGeoRules.end()) {
				i->second.MergeIn(rawGeoRule.second);
			} else {
				_rawGeoRules.emplace_back(rawGeoRule);
			}
		}

		for (const auto& cmdStream:src._commandStreams) {
			auto i = std::find_if(_commandStreams.begin(), _commandStreams.end(), [n=cmdStream.first](const auto&q) { return q.first == n; });
			if (i != _commandStreams.end()) {
			} else {
				_commandStreams.emplace_back(cmdStream);
			}
		}

		for (const auto& skeletonRule:src._skeletonRules) {
			auto i = std::find_if(_skeletonRules.begin(), _skeletonRules.end(), [n=skeletonRule.first](const auto&q) { return q.first == n; });
			if (i != _skeletonRules.end()) {
				i->second.MergeIn(skeletonRule.second);
			} else {
				_skeletonRules.emplace_back(skeletonRule);
			}
		}

		if (src._autoProcessTextures)
			_autoProcessTextures = src._autoProcessTextures;

		_inheritConfigurations.insert(_inheritConfigurations.end(), src._inheritConfigurations.begin(), src._inheritConfigurations.end());
	}

	void ModelCompilationConfiguration::DeserializeRawGeoRules(Formatters::TextInputFormatter<>& fmttr)
	{
		StringSection<> ruleFilteringPattern;
		while (TryKeyedItem(fmttr, ruleFilteringPattern)) {
			auto filteringPattern = ruleFilteringPattern.AsString();
			if (std::find_if(_rawGeoRules.begin(), _rawGeoRules.end(), [filteringPattern](const auto& q){ return q.first==filteringPattern; }) != _rawGeoRules.end())
				Throw(Formatters::FormatException("Multiple RawGeoRules with the same filtering pattern. Was this intended?", fmttr.GetLocation()));

			RawGeoRules rules;
			RequireBeginElement(fmttr);
			uint64_t keyName; StringSection<> str;
			while (TryKeyedItem(fmttr, keyName)) {
				switch (keyName) {
				case "16Bit"_h:
					rules._16BitNativeTypes = Formatters::RequireCastValue<bool>(fmttr);
					break;
				case "RebuildTangents"_h:
					rules._rebuildTangents = Formatters::RequireCastValue<bool>(fmttr);
					break;
				case "RebuildNormals"_h:
					rules._rebuildNormals = Formatters::RequireCastValue<bool>(fmttr);
					break;
				case "ExcludeAttributes"_h:
					RequireBeginElement(fmttr);
					while (fmttr.TryStringValue(str))
						rules._excludeAttributes.insert(rules._excludeAttributes.end(), Hash64(str));
					RequireEndElement(fmttr);
					break;
				case "IncludeAttributes"_h:
					RequireBeginElement(fmttr);
					while (fmttr.TryStringValue(str))
						rules._includeAttributes.insert(rules._includeAttributes.end(), Hash64(str));
					RequireEndElement(fmttr);
					break;
				}
			}

			RequireEndElement(fmttr);

			std::sort(rules._excludeAttributes.begin(), rules._excludeAttributes.end());
			rules._excludeAttributes.erase(
				std::unique(rules._excludeAttributes.begin(), rules._excludeAttributes.end()),
				rules._excludeAttributes.end());

			std::sort(rules._includeAttributes.begin(), rules._includeAttributes.end());
			rules._includeAttributes.erase(
				std::unique(rules._includeAttributes.begin(), rules._includeAttributes.end()),
				rules._includeAttributes.end());
			_rawGeoRules.emplace_back(std::move(filteringPattern), std::move(rules));
		}
	}

	static uint64_t NumberOrHash(StringSection<> str)
	{
		uint64_t h = 0;
		auto e = FastParseValue(str, h);
		if (e == str.end()) {
			return h;
		} else {
			return Hash64(str);
		}
	}

	void ModelCompilationConfiguration::DeserializeCommandStreams(Formatters::TextInputFormatter<>& fmttr)
	{
		for (;;) {
			auto next = fmttr.PeekNext();
			if (next == Formatters::TextInputFormatter<>::Blob::KeyedItem) {
				StringSection<> value;
				auto b = TryKeyedItem(fmttr, value);
				assert(b);
				_commandStreams.emplace_back(NumberOrHash(value), CommandStream{});
				SkipValueOrElement(fmttr);
			} else if (next == Formatters::TextInputFormatter<>::Blob::Value) {
				StringSection<char> value;
				auto b = fmttr.TryStringValue(value);
				assert(b);
				_commandStreams.emplace_back(NumberOrHash(value), CommandStream{});
			} else
				break;
		}
	}

	void ModelCompilationConfiguration::DeserializeSkeletonRules(Formatters::TextInputFormatter<>& fmttr)
	{
		StringSection<> ruleFilteringPattern;
		while (TryKeyedItem(fmttr, ruleFilteringPattern)) {
			auto filteringPattern = ruleFilteringPattern.AsString();
			if (std::find_if(_skeletonRules.begin(), _skeletonRules.end(), [filteringPattern](const auto& q){ return q.first==filteringPattern; }) != _skeletonRules.end())
				Throw(Formatters::FormatException("Multiple SkeletonRules with the same filtering pattern. Was this intended?", fmttr.GetLocation()));

			SkeletonRules rules;
			RequireBeginElement(fmttr);
			uint64_t keyName; StringSection<> str;
			while (TryKeyedItem(fmttr, keyName)) {
				switch (keyName) {
				case "AnimatableBones"_h:
					RequireBeginElement(fmttr);
					while (fmttr.TryStringValue(str))
						rules._animatableBones.insert(rules._animatableBones.end(), Hash64(str));
					RequireEndElement(fmttr);
					break;
				case "OutputBones"_h:
					RequireBeginElement(fmttr);
					while (fmttr.TryStringValue(str))
						rules._outputBones.insert(rules._outputBones.end(), Hash64(str));
					RequireEndElement(fmttr);
					break;
				}
			}

			RequireEndElement(fmttr);

			std::sort(rules._animatableBones.begin(), rules._animatableBones.end());
			rules._animatableBones.erase(
				std::unique(rules._animatableBones.begin(), rules._animatableBones.end()),
				rules._animatableBones.end());

			std::sort(rules._outputBones.begin(), rules._outputBones.end());
			rules._outputBones.erase(
				std::unique(rules._outputBones.begin(), rules._outputBones.end()),
				rules._outputBones.end());

			_skeletonRules.emplace_back(std::move(filteringPattern), std::move(rules));
		}
	}

	uint64_t ModelCompilationConfiguration::RawGeoRules::CalculateHash(uint64_t hash) const
	{
		if (_16BitNativeTypes) hash = HashCombine(_16BitNativeTypes.value(), hash);
		if (_rebuildTangents) hash = HashCombine(_rebuildTangents.value(), hash);
		if (_rebuildNormals) hash = HashCombine(_rebuildNormals.value(), hash);
		hash = Hash64(AsPointer(_includeAttributes.begin()), AsPointer(_includeAttributes.end()), hash);
		hash = Hash64(AsPointer(_excludeAttributes.begin()), AsPointer(_excludeAttributes.end()), hash);
		return hash;
	}

	uint64_t ModelCompilationConfiguration::SkeletonRules::CalculateHash(uint64_t hash) const
	{
		hash = Hash64(AsPointer(_animatableBones.begin()), AsPointer(_animatableBones.end()), hash);
		hash = Hash64(AsPointer(_outputBones.begin()), AsPointer(_outputBones.end()), hash);
		return hash;
	}

	uint64_t ModelCompilationConfiguration::CalculateHash(uint64_t seed) const
	{
		uint64_t result = seed;
		for (const auto& q:_rawGeoRules)
			result = q.second.CalculateHash(Hash64(q.first, result));
		for (const auto& q:_commandStreams)
			result = HashCombine(q.first, result);
		for (const auto& q:_skeletonRules)
			result = q.second.CalculateHash(Hash64(q.first, result));
		if (_autoProcessTextures)
			result = HashCombine(_autoProcessTextures.value(), result);
		for (const auto& q:_inheritConfigurations)
			result = Hash64(q, result);
		return result;
	}

	ModelCompilationConfiguration::ModelCompilationConfiguration(Formatters::TextInputFormatter<>& fmttr)
	{
		uint64_t keyName;
		while (TryKeyedItem(fmttr, keyName)) {
			switch (keyName) {
			case "RawGeoRules"_h:
				RequireBeginElement(fmttr);
				DeserializeRawGeoRules(fmttr);
				RequireEndElement(fmttr);
				break;

			case "CommandStreams"_h:
				RequireBeginElement(fmttr);
				DeserializeCommandStreams(fmttr);
				RequireEndElement(fmttr);
				break;

			case "SkeletonRules"_h:
				RequireBeginElement(fmttr);
				DeserializeSkeletonRules(fmttr);
				RequireEndElement(fmttr);
				break;

			case "Material"_h:
				RequireBeginElement(fmttr);
				while (TryKeyedItem(fmttr, keyName)) {
					switch (keyName) {
					case "AutoProcessTextures"_h:
						_autoProcessTextures = Formatters::RequireCastValue<bool>(fmttr);
						break;
					}
				}
				RequireEndElement(fmttr);
				break;
			
			case "Inherit"_h:
				RequireBeginElement(fmttr);
				while (fmttr.PeekNext() == Formatters::FormatterBlob::Value)
					_inheritConfigurations.push_back(RequireStringValue(fmttr).AsString());
				RequireEndElement(fmttr);
				break;
			}
		}
	}

	ModelCompilationConfiguration::ModelCompilationConfiguration() {}

	ModelCompilationConfiguration::~ModelCompilationConfiguration() {}

}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/DepVal.h"
#include "../../Assets/AssetUtils.h"
#include <vector>
#include <string>

namespace Formatters { template<typename CharType> class TextInputFormatter; }

namespace RenderCore { namespace Assets { namespace GeoProc
{

	class ModelScaffoldConfiguration
	{
	public:
		using StringWildcardMatcher = std::string;

		struct RawGeoRules
		{
			std::optional<bool> _16BitNativeTypes;
			std::vector<uint64_t> _includeAttributes, _excludeAttributes;
		};
		std::vector<std::pair<StringWildcardMatcher, RawGeoRules>> _rawGeoRules;

		struct CommandStream
		{};
		std::vector<std::pair<uint64_t, CommandStream>> _commandStreams;

		struct SkeletonRules
		{
			std::vector<uint64_t> _animatableBones;
			std::vector<uint64_t> _outputBones;
		};
		std::vector<std::pair<StringWildcardMatcher, SkeletonRules>> _skeletonRules;

		std::optional<bool> _autoProcessTextures;

		std::vector<std::string> _inheritConfigurations;

		IteratorRange<const std::string*> GetInherited() const { return _inheritConfigurations; }
		void MergeInWithFilenameResolve(const ModelScaffoldConfiguration&, const ::Assets::DirectorySearchRules&);

		ModelScaffoldConfiguration(Formatters::TextInputFormatter<char>& fmttr);
		ModelScaffoldConfiguration();
		~ModelScaffoldConfiguration();

	private:
		void DeserializeRawGeoRules(Formatters::TextInputFormatter<char>& fmttr);
		void DeserializeCommandStreams(Formatters::TextInputFormatter<char>& fmttr);
		void DeserializeSkeletonRules(Formatters::TextInputFormatter<char>& fmttr);
	};

}}}
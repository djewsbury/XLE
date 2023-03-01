// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/DepVal.h"
#include "../Assets/AssetUtils.h"
#include "../Math/Vector.h"
#include "../Formatters/TextFormatter.h"
#include "../Utility/MemoryUtils.h"
#include <vector>

namespace SceneEngine
{
    class WorldPlacementsConfig
    {
    public:
        class Cell
        {
        public:
            Float3 _offset;
            Float3 _mins, _maxs;
            ::Assets::ResChar _file[MaxPath];
        };
        std::vector<Cell> _cells;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

        WorldPlacementsConfig(
            Formatters::TextInputFormatter<>& formatter,
            const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
        WorldPlacementsConfig();

        static void ConstructToPromise(
			std::promise<std::shared_ptr<WorldPlacementsConfig>>&&,
			StringSection<::Assets::ResChar> initializer);

	private:
		::Assets::DependencyValidation _depVal;
    };

    constexpr auto CompileProcessType_WorldPlacementsConfig = ConstHash64Legacy<'Worl', 'dPla', 'ceme', 'nts'>::Value;

    class PlacementCellSet;
    void InitializeCellSet(
        PlacementCellSet& cellSet,
        const WorldPlacementsConfig& cfg, const Float3& worldOffset);
}

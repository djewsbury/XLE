// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <cstdint>

namespace SceneEngine
{
    using TerrainCoverageId = uint32_t;
    static const TerrainCoverageId CoverageId_Heights = 1;
    static const TerrainCoverageId CoverageId_AngleBasedShadows = 2;
    static const TerrainCoverageId CoverageId_AmbientOcclusion = 3;
    static const TerrainCoverageId CoverageId_ArchiveHeights = 100;

    enum class TerrainToolResult
	{
		Success, OutsideLock, 
		PendingAsset, InvalidAsset,
		Error
	};
}

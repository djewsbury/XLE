// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Scaffold.h"
#include "../Utility/StringUtils.h"
#include "../Utility/IteratorUtils.h"
#include <vector>

namespace RenderCore { namespace Assets { namespace GeoProc { class NascentSkeleton; }}}

namespace ColladaConversion
{
	class Node; class URIResolveContext;

	void BuildSkeleton(RenderCore::Assets::GeoProc::NascentSkeleton& skeleton, const Node& node, StringSection<> skeletonName = {});
    Float4x4 CalculateLocalToWorld(Node node);

	auto BuildMaterialTableStrings(
        IteratorRange<const InstanceGeometry::MaterialBinding*> bindings, 
        const std::vector<uint64_t>& rawGeoBindingSymbols,
        const URIResolveContext& resolveContext) -> std::vector<std::string>;

	std::string SkeletonBindingName(const Node& node);

	struct LODDesc
    {
    public:
        unsigned                _lod;
        bool                    _isLODRoot;
        StringSection<utf8>     _remainingName;
    };

    LODDesc GetLevelOfDetail(const ::ColladaConversion::Node& node);
}

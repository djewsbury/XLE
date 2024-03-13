// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/GeoProc/NascentModel.h"
#include "../Math/Matrix.h"
#include <vector>
#include <stdint.h>

namespace RenderCore { namespace Assets { namespace GeoProc
{
    class UnboundSkinController;
}}}

namespace ColladaConversion
{
	class ImportConfiguration;
    class MeshGeometry;
    class SkinController;
    class URIResolveContext;
	class Node;

	class ConvertedMeshGeometry
	{
	public:
		RenderCore::Assets::GeoProc::NascentModel::GeometryBlock _geoBlock;
		std::vector<uint64_t> _matBindingSymbols;
	};

	ConvertedMeshGeometry Convert(const MeshGeometry& mesh, const URIResolveContext& pubEles, const ImportConfiguration& cfgs);

	using SkeletonBindRootFn = std::function<Float4x4(const Node&)>;
    auto Convert(
		const SkinController& controller, const URIResolveContext& pubEles, 
		const ImportConfiguration& cfg, SkeletonBindRootFn&)
        -> RenderCore::Assets::GeoProc::UnboundSkinController;
}


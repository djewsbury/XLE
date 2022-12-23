// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
    namespace ObjectCB
    {
        constexpr auto LocalTransform = ConstHash64("LocalTransform");
        constexpr auto GlobalTransform = ConstHash64("GlobalTransform");
		constexpr auto DrawCallProperties = ConstHash64("DrawCallProperties");
        constexpr auto BasicMaterialConstants = ConstHash64("BasicMaterialConstants");
        constexpr auto Globals = ConstHash64("$Globals");
    }

    /// <summary>Technique type binding indicies</summary>
    /// We use a hard coded set of technique indices. This non-ideal in the sense that it limits
    /// the number of different ways we can render things. But it's also important for
    /// performance, since technique lookups can happen very frequently. It's hard to
    /// find a good balance between performance and flexibility for this case.
    namespace TechniqueIndex
    {
        constexpr auto Forward       = 0u;
        constexpr auto DepthOnly     = 1u;
        constexpr auto Deferred      = 2u;
        constexpr auto ShadowGen     = 3u;
        constexpr auto OrderIndependentTransparency = 4u;
        constexpr auto PrepareVegetationSpawn = 5u;
        constexpr auto RayTest       = 6u;
        constexpr auto VisNormals    = 7u;
        constexpr auto VisWireframe  = 8u;
        constexpr auto WriteTriangleIndex = 9u;
        constexpr auto StochasticTransparency = 10u;
        constexpr auto DepthWeightedTransparency = 11u;

        constexpr auto Max = 12u;
    };

	namespace AttachmentSemantics
    {
        constexpr auto MultisampleDepth = ConstHash64("MultisampleDepth");
        constexpr auto GBufferDiffuse   = ConstHash64("GBufferDiffuse");
        constexpr auto GBufferNormal    = ConstHash64("GBufferNormal");
        constexpr auto GBufferParameter = ConstHash64("GBufferParameter");
        constexpr auto GBufferMotion    = ConstHash64("GBufferMotion");
        constexpr auto HistoryAcc       = ConstHash64("HistoryAcc");

        constexpr auto ColorLDR         = ConstHash64("ColorLDR");
        constexpr auto ColorHDR         = ConstHash64("ColorHDR");
        constexpr auto Depth            = ConstHash64("Depth");

		constexpr auto ShadowDepthMap	= ConstHash64("ShadowDepthMap");

        constexpr auto HierarchicalDepths	= ConstHash64("HierarchicalDepths");
        constexpr auto TiledLightBitField	= ConstHash64("TiledLightBitField");

        constexpr auto MultisampleDepthPrev = ConstHash64("MultisampleDepth")+1;
        constexpr auto GBufferNormalPrev    = ConstHash64("GBufferNormal")+1;
        constexpr auto ColorHDRPrev         = ConstHash64("ColorHDR")+1;

        const char* TryDehash(uint64_t);
	}

    namespace CommonSemantics
    {        
        constexpr auto POSITION = ConstHash64("POSITION");
        constexpr auto PIXELPOSITION = ConstHash64("PIXELPOSITION");
        constexpr auto TEXCOORD = ConstHash64("TEXCOORD");
		constexpr auto COLOR = ConstHash64("COLOR");
		constexpr auto NORMAL = ConstHash64("NORMAL");
		constexpr auto TEXTANGENT = ConstHash64("TEXTANGENT");
		constexpr auto TEXBITANGENT = ConstHash64("TEXBITANGENT");
		constexpr auto BONEINDICES = ConstHash64("BONEINDICES");
		constexpr auto BONEWEIGHTS = ConstHash64("BONEWEIGHTS");
		constexpr auto PER_VERTEX_AO = ConstHash64("PER_VERTEX_AO");
        constexpr auto RADIUS = ConstHash64("RADIUS");
        constexpr auto FONTTABLE = ConstHash64("FONTTABLE");

        std::pair<const char*, unsigned> TryDehash(uint64_t);
    }

    constexpr unsigned s_defaultMaterialDescSetSlot = 2;
}}


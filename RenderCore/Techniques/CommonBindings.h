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
        constexpr uint64_t MultisampleDepth = ConstHash64Legacy<'Mult', 'isam', 'pleD', 'epth'>::Value;
        constexpr uint64_t GBufferDiffuse   = ConstHash64Legacy<'GBuf', 'ferD', 'iffu', 'se'>::Value;
        constexpr uint64_t GBufferNormal    = ConstHash64Legacy<'GBuf', 'ferN', 'orma', 'l'>::Value;
        constexpr uint64_t GBufferParameter = ConstHash64Legacy<'GBuf', 'ferP', 'aram', 'eter'>::Value;
        constexpr uint64_t GBufferMotion    = ConstHash64Legacy<'GBuf', 'ferM', 'otio', 'n'>::Value;
        constexpr uint64_t HistoryAcc       = ConstHash64Legacy<'Hist', 'oryA', 'cc'>::Value;

        constexpr uint64_t ColorLDR         = ConstHash64Legacy<'Colo', 'rLDR'>::Value;
        constexpr uint64_t ColorHDR         = ConstHash64Legacy<'Colo', 'rHDR'>::Value;
        constexpr uint64_t Depth            = ConstHash64Legacy<'Dept', 'h'>::Value;

		constexpr uint64_t ShadowDepthMap	= ConstHash64Legacy<'Shad', 'owDe', 'pthM', 'ap'>::Value;

        constexpr uint64_t HierarchicalDepths	= ConstHash64Legacy<'Hier', 'arch', 'ical', 'Dept'>::Value;
        constexpr uint64_t TiledLightBitField	= ConstHash64Legacy<'Tile', 'dLig', 'htBi', 'tFie'>::Value;

        constexpr uint64_t MultisampleDepthPrev = ConstHash64Legacy<'Mult', 'isam', 'pleD', 'epth'>::Value+1;
        constexpr uint64_t GBufferNormalPrev    = ConstHash64Legacy<'GBuf', 'ferN', 'orma', 'l'>::Value+1;
        constexpr uint64_t ColorHDRPrev         = ConstHash64Legacy<'Colo', 'rHDR'>::Value+1;

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


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
        static const auto LocalTransform = Utility::Hash64("LocalTransform");
        static const auto GlobalTransform = Utility::Hash64("GlobalTransform");
		static const auto DrawCallProperties = Utility::Hash64("DrawCallProperties");
        static const auto BasicMaterialConstants = Utility::Hash64("BasicMaterialConstants");
        static const auto Globals = Utility::Hash64("$Globals");
    }

    /// <summary>Technique type binding indicies</summary>
    /// We use a hard coded set of technique indices. This non-ideal in the sense that it limits
    /// the number of different ways we can render things. But it's also important for
    /// performance, since technique lookups can happen very frequently. It's hard to
    /// find a good balance between performance and flexibility for this case.
    namespace TechniqueIndex
    {
        static const auto Forward       = 0u;
        static const auto DepthOnly     = 1u;
        static const auto Deferred      = 2u;
        static const auto ShadowGen     = 3u;
        static const auto OrderIndependentTransparency = 4u;
        static const auto PrepareVegetationSpawn = 5u;
        static const auto RayTest       = 6u;
        static const auto VisNormals    = 7u;
        static const auto VisWireframe  = 8u;
        static const auto WriteTriangleIndex = 9u;
        static const auto StochasticTransparency = 10u;
        static const auto DepthWeightedTransparency = 11u;

        static const auto Max = 12u;
    };

	namespace AttachmentSemantics
    {
        constexpr uint64_t MultisampleDepth = ConstHash64<'Mult', 'isam', 'pleD', 'epth'>::Value;
        constexpr uint64_t GBufferDiffuse   = ConstHash64<'GBuf', 'ferD', 'iffu', 'se'>::Value;
        constexpr uint64_t GBufferNormal    = ConstHash64<'GBuf', 'ferN', 'orma', 'l'>::Value;
        constexpr uint64_t GBufferNormalPrev= ConstHash64<'GBuf', 'ferN', 'orma', 'l'>::Value+1;
        constexpr uint64_t GBufferParameter = ConstHash64<'GBuf', 'ferP', 'aram', 'eter'>::Value;
        constexpr uint64_t GBufferMotion    = ConstHash64<'GBuf', 'ferM', 'otio', 'n'>::Value;

        constexpr uint64_t ColorLDR         = ConstHash64<'Colo', 'rLDR'>::Value;
        constexpr uint64_t ColorHDR         = ConstHash64<'Colo', 'rHDR'>::Value;
        constexpr uint64_t Depth            = ConstHash64<'Dept', 'h'>::Value;

		constexpr uint64_t ShadowDepthMap	= ConstHash64<'Shad', 'owDe', 'pthM', 'ap'>::Value;

        constexpr uint64_t HierarchicalDepths	= ConstHash64<'Hier', 'arch', 'ical', 'Dept'>::Value;
        constexpr uint64_t TiledLightBitField	= ConstHash64<'Tile', 'dLig', 'htBi', 'tFie'>::Value;

        const char* TryDehash(uint64_t);
	}

    namespace CommonSemantics
    {        
        static const auto POSITION = Utility::Hash64("POSITION");
        static const auto PIXELPOSITION = Utility::Hash64("PIXELPOSITION");
        static const auto TEXCOORD = Utility::Hash64("TEXCOORD");
		static const auto COLOR = Utility::Hash64("COLOR");
		static const auto NORMAL = Utility::Hash64("NORMAL");
		static const auto TEXTANGENT = Utility::Hash64("TEXTANGENT");
		static const auto TEXBITANGENT = Utility::Hash64("TEXBITANGENT");
		static const auto BONEINDICES = Utility::Hash64("BONEINDICES");
		static const auto BONEWEIGHTS = Utility::Hash64("BONEWEIGHTS");
		static const auto PER_VERTEX_AO = Utility::Hash64("PER_VERTEX_AO");
        static const auto RADIUS = Utility::Hash64("RADIUS");

        std::pair<const char*, unsigned> TryDehash(uint64_t);
    }

    static constexpr unsigned s_defaultMaterialDescSetSlot = 2;
}}


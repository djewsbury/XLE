// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StateDesc.h"
#include "../Utility/MemoryUtils.h"
#include <cassert>
#include <ostream>

namespace RenderCore
{
	uint64_t DepthStencilDesc::HashDepthAspect() const
    {
        assert((unsigned(_depthTest) & ~0xfu) == 0);
        assert((unsigned(_depthTest) & ~0x1u) == 0);
        return  ((uint64_t(_depthTest) & 0xf) << 0ull)
            |   ((uint64_t(_depthWrite) & 0x1) << 52ull)
            ;
    }

    uint64_t DepthStencilDesc::HashStencilAspect() const
    {
        assert((unsigned(_stencilEnable) & ~0x1u) == 0);
        assert((unsigned(_stencilReadMask) & ~0xffu) == 0);
        assert((unsigned(_stencilWriteMask) & ~0xffu) == 0);
        assert((unsigned(_frontFaceStencil._passOp) & ~0xfu) == 0);
        assert((unsigned(_frontFaceStencil._failOp) & ~0xfu) == 0);
        assert((unsigned(_frontFaceStencil._depthFailOp) & ~0xfu) == 0);
        assert((unsigned(_frontFaceStencil._comparisonOp) & ~0xfu) == 0);
        assert((unsigned(_backFaceStencil._passOp) & ~0xfu) == 0);
        assert((unsigned(_backFaceStencil._failOp) & ~0xfu) == 0);
        assert((unsigned(_backFaceStencil._depthFailOp) & ~0xfu) == 0);
        assert((unsigned(_backFaceStencil._comparisonOp) & ~0xfu) == 0);

        return  ((uint64_t(_frontFaceStencil._passOp) & 0xf) << 4ull)
            |   ((uint64_t(_frontFaceStencil._failOp) & 0xf) << 8ull)
            |   ((uint64_t(_frontFaceStencil._depthFailOp) & 0xf) << 12ull)
            |   ((uint64_t(_frontFaceStencil._comparisonOp) & 0xf) << 16ull)

            |   ((uint64_t(_backFaceStencil._passOp) & 0xf) << 20ull)
            |   ((uint64_t(_backFaceStencil._failOp) & 0xf) << 24ull)
            |   ((uint64_t(_backFaceStencil._depthFailOp) & 0xf) << 28ull)
            |   ((uint64_t(_backFaceStencil._comparisonOp) & 0xf) << 32ull)

            |   ((uint64_t(_stencilReadMask) & 0xf) << 36ull)
            |   ((uint64_t(_stencilWriteMask) & 0xf) << 44ull)
            |   ((uint64_t(_stencilEnable) & 0x1) << 53ull)
            ;
    }

    uint64_t AttachmentBlendDesc::Hash() const
    {
        // Note that we're checking that each element fits in 4 bits, and then space them out
        // to give each 8 bits (well, there's room for expansion)
        assert((unsigned(_srcColorBlendFactor) & ~0xfu) == 0);
        assert((unsigned(_dstColorBlendFactor) & ~0xfu) == 0);
        assert((unsigned(_colorBlendOp) & ~0xfu) == 0);
        assert((unsigned(_srcAlphaBlendFactor) & ~0xfu) == 0);
        assert((unsigned(_dstAlphaBlendFactor) & ~0xfu) == 0);
        assert((unsigned(_alphaBlendOp) & ~0xfu) == 0);
        assert((unsigned(_writeMask) & ~0xfu) == 0);
        return  ((uint64_t(_blendEnable) & 1) << 0ull)

            |   ((uint64_t(_srcColorBlendFactor) & 0xf) << 8ull)
            |   ((uint64_t(_dstColorBlendFactor) & 0xf) << 16ull)
            |   ((uint64_t(_colorBlendOp) & 0xf) << 24ull)

            |   ((uint64_t(_srcAlphaBlendFactor) & 0xf) << 32ull)
            |   ((uint64_t(_dstAlphaBlendFactor) & 0xf) << 40ull)
            |   ((uint64_t(_alphaBlendOp) & 0xf) << 48ull)

            |   ((uint64_t(_writeMask) & 0xf) << 56ull)
            ;
    }

	static unsigned int FloatBits(float input)
    {
            // (or just use a reinterpret cast)
        union Converter { float f; unsigned int i; };
        Converter c; c.f = input; 
        return c.i;
    }

	uint64_t RasterizationDesc::Hash() const
	{
		assert((unsigned(_cullMode) & ~0xff) == 0);
        assert((unsigned(_frontFaceWinding) & ~0xff) == 0);
		uint64_t p0 = 
			  uint64_t(_cullMode) 
			| (uint64_t(_frontFaceWinding) << 8ull)
			| (uint64_t(FloatBits(_depthBiasConstantFactor)) << 32ull);
		uint64_t p1 = 
				uint64_t(FloatBits(_depthBiasClamp))
			|	(uint64_t(FloatBits(_depthBiasSlopeFactor)) << 32ull);
		return HashCombine(p0, p1);
	}

    uint64_t SamplerDesc::Hash() const
    {
        assert((unsigned(_filter) & ~0xff) == 0);
        assert((unsigned(_addressU) & ~0xf) == 0);
        assert((unsigned(_addressV) & ~0xf) == 0);
        assert((unsigned(_comparison) & ~0xf) == 0);

        return  uint64_t(_filter)
            |   (uint64_t(_addressU) << 8ull)
            |   (uint64_t(_addressV) << 12ull)
            |   (uint64_t(_comparison) << 16ull)
            |   (uint64_t(_flags) << 20ull)
            ;
    }

    std::ostream& operator<<(std::ostream& str, const SamplerDesc& desc)
    {
        str << "{Filter: " << AsString(desc._filter) << ", U: " << AsString(desc._addressU) << ", V: " << AsString(desc._addressV);
        if (desc._comparison != CompareOp::Never)
            str << ", Compare: " << AsString(desc._comparison);
        if (desc._flags & SamplerDescFlags::DisableMipmaps)
            str << ", Mipmaps disabled";
        if (desc._flags & SamplerDescFlags::UnnormalizedCoordinates)
            str << ", unnormalized coordinates";
        str << "}";
        return str;
    }

	StencilDesc StencilDesc::NoEffect { StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Always };
	StencilDesc StencilDesc::AlwaysWrite { StencilOp::Replace, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Always };

    const char* AsString(AddressMode addressMode)
    {
        switch (addressMode) {
        case AddressMode::Wrap: return "Wrap";
        case AddressMode::Mirror: return "Mirror";
        case AddressMode::Clamp: return "Clamp";
        case AddressMode::Border: return "Border";
        default: return "<<unknown>>";
        }
    }

	const char* AsString(FilterMode filterMode)
    {
        switch (filterMode) {
        case FilterMode::Point: return "Point";
        case FilterMode::Trilinear: return "Trilinear";
        case FilterMode::Anisotropic: return "Anisotropic";
        case FilterMode::Bilinear: return "Bilinear";
        case FilterMode::ComparisonBilinear: return "ComparisonBilinear";
        default: return "<<unknown>>";
        }
    }

	const char* AsString(CompareOp compareOp)
    {
        switch (compareOp) {
        case CompareOp::Never: return "Never";
        case CompareOp::Less: return "Less";
        case CompareOp::Equal: return "Equal";
        case CompareOp::LessEqual: return "LessEqual";
        case CompareOp::Greater: return "Greater";
        case CompareOp::NotEqual: return "NotEqual";
        case CompareOp::GreaterEqual: return "GreaterEqual";
        case CompareOp::Always: return "Always";
        default: return "<<unknown>>";
        }
    }

    const char* AsString(Topology topology)
	{
		switch (topology) {
		case Topology::PointList: return "PointList";
		case Topology::LineList: return "LineList";
		case Topology::LineStrip: return "LineStrip";
		case Topology::TriangleList: return "TriangleList";
        case Topology::TriangleStrip: return "TriangleStrip";
        case Topology::LineListAdj: return "LineListAdj";
        case Topology::PatchList1: return "PatchList1";
        case Topology::PatchList2: return "PatchList2";
        case Topology::PatchList3: return "PatchList3";
        case Topology::PatchList4: return "PatchList4";
        case Topology::PatchList5: return "PatchList5";
        case Topology::PatchList6: return "PatchList6";
        case Topology::PatchList7: return "PatchList7";
        case Topology::PatchList8: return "PatchList8";
        case Topology::PatchList9: return "PatchList9";
        case Topology::PatchList10: return "PatchList10";
        case Topology::PatchList11: return "PatchList11";
        case Topology::PatchList12: return "PatchList12";
        case Topology::PatchList13: return "PatchList13";
        case Topology::PatchList14: return "PatchList14";
        case Topology::PatchList15: return "PatchList15";
        case Topology::PatchList16: return "PatchList16";
		default: return "<<unknown>>";
		}
	}
}


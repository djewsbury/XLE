// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include <cstdint>
#include <iosfwd>
#include <assert.h>
#include <optional>

namespace RenderCore
{
		/// <summary>Texture address modes</summary>
	///
	///     These are used to determine how the texture sampler
	///     reads texture data outside of the [0, 1] range.
	///     Normally Wrap and Clamp are used.
	///     <seealso cref="SamplerState"/>
	enum class AddressMode
    {
        Wrap = 1,   // D3D11_TEXTURE_ADDRESS_WRAP
        Mirror = 2, // D3D11_TEXTURE_ADDRESS_MIRROR
        Clamp = 3,  // D3D11_TEXTURE_ADDRESS_CLAMP
        Border = 4  // D3D11_TEXTURE_ADDRESS_BORDER
    };

    enum class FaceWinding
    {
        CCW = 0,    // Front faces are counter clockwise
        CW = 1      // Front faces are clockwise
    };

    /// <summary>Texture filtering modes</summary>
    ///
    ///     These are used when sampling a texture at a floating
    ///     point address. In other words, when sampling at a
    ///     midway point between texels, how do we filter the 
    ///     surrounding texels?
    ///     <seealso cref="SamplerState"/>
    enum class FilterMode
    {
        Point = 0,                  // D3D11_FILTER_MIN_MAG_MIP_POINT
        Trilinear = 0x15,           // D3D11_FILTER_MIN_MAG_MIP_LINEAR
        Anisotropic = 0x55,         // D3D11_FILTER_ANISOTROPIC
        Bilinear = 0x14,            // D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT
        ComparisonBilinear = 0x94   // D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
    };

    enum class CompareOp
    {
        Never = 1,          // D3D11_COMPARISON_NEVER
        Less = 2,           // D3D11_COMPARISON_LESS
        Equal = 3,          // D3D11_COMPARISON_EQUAL
        LessEqual = 4,      // D3D11_COMPARISON_LESS_EQUAL
        Greater = 5,        // D3D11_COMPARISON_GREATER
        NotEqual = 6,       // D3D11_COMPARISON_NOT_EQUAL
        GreaterEqual = 7,   // D3D11_COMPARISON_GREATER_EQUAL
        Always = 8          // D3D11_COMPARISON_ALWAYS
    };

	/// <summary>Back face culling mode</summary>
	///
	///     Used to determine which side of a triangle to cull.
	///
	///     Note that there is another flag the in rasteriser state
	///     that determines which side of a triangle is the "back"
	///     (ie, clockwise or counterclockwise order). 
	///     Only use the "Front" option if you really want to cull
	///     the front facing triangles (useful for some effects)
	///     <seealso cref="RasterizerState"/>
	enum class CullMode
	{
		None = 1,   // D3D11_CULL_NONE,
		Front = 2,  // D3D11_CULL_FRONT,
		Back = 3    // D3D11_CULL_BACK
	};

	enum class FillMode
	{
		Solid = 3,      // D3D11_FILL_SOLID
		Wireframe = 2   // D3D11_FILL_WIREFRAME
	};

	/// <summary>Settings used for describing a blend state</summary>
	///
	///     The blend operation takes the form:
	///         out colour = Operation(Param1 * (Source colour), Param2 * (Destination colour))
	///         out alpha = Operation(Param1 * (Source alpha), Param2 * (Destination alpha))
	///
	///     Where "Operation" is typically addition.
	///
	///     This enum is used for "Param1" and "Param2"
	///     <seealso cref="BlendOp::Enum"/>
	///     <seealso cref="BlendState"/>
	enum class Blend
	{
		Zero = 1, // D3D11_BLEND_ZERO,
		One = 2, // D3D11_BLEND_ONE,

		SrcColor = 3, // D3D11_BLEND_SRC_COLOR,
		InvSrcColor = 4, // D3D11_BLEND_INV_SRC_COLOR,
		DestColor = 9, // D3D11_BLEND_DEST_COLOR,
		InvDestColor = 10, // D3D11_BLEND_INV_DEST_COLOR,

		SrcAlpha = 5, // D3D11_BLEND_SRC_ALPHA,
		InvSrcAlpha = 6, // D3D11_BLEND_INV_SRC_ALPHA,
		DestAlpha = 7, // D3D11_BLEND_DEST_ALPHA,
		InvDestAlpha = 8 // D3D11_BLEND_INV_DEST_ALPHA
	};

	/// <summary>Settings used for describing a blend state</summary>
	///
	///     The blend operation takes the form:
	///         out colour = Operation(Param1 * (Source colour), Param2 * (Destination colour))
	///         out alpha = Operation(Param1 * (Source alpha), Param2 * (Destination alpha))
	///
	///     This enum is used for "Operation"
	///     <seealso cref="BlendOp::Enum"/>
	///     <seealso cref="BlendState"/>
	enum class BlendOp
	{
		NoBlending,
		Add = 1, // D3D11_BLEND_OP_ADD,
		Subtract = 2, // D3D11_BLEND_OP_SUBTRACT,
		RevSubtract = 3, // D3D11_BLEND_OP_REV_SUBTRACT,
		Min = 4, // D3D11_BLEND_OP_MIN,
		Max = 5 // D3D11_BLEND_OP_MAX
	};

	enum class StencilOp
	{
        Keep = 1,           // D3D11_STENCIL_OP_KEEP
		DontWrite = 1,      // D3D11_STENCIL_OP_KEEP

		Zero = 2,           // D3D11_STENCIL_OP_ZERO
		Replace = 3,        // D3D11_STENCIL_OP_REPLACE
		IncreaseSat = 4,    // D3D11_STENCIL_OP_INCR_SAT
		DecreaseSat = 5,    // D3D11_STENCIL_OP_DECR_SAT
		Invert = 6,         // D3D11_STENCIL_OP_INVERT
		Increase = 7,       // D3D11_STENCIL_OP_INCR
		Decrease = 8        // D3D11_STENCIL_OP_DECR
	};

	/// Equivalent to MTLStencilDescriptor or D3D12_DEPTH_STENCILOP_DESC or VkStencilOpState
    /// Note that OpenGLES2 & Vulkan allow for separate readmask/writemask/reference values per
    /// face, but DX & Metal do not.
    class StencilDesc
    {
    public:
        StencilOp       _passOp = StencilOp::Keep;        ///< pass stencil & depth tests
        StencilOp       _failOp = StencilOp::Keep;        ///< fail stencil test
        StencilOp       _depthFailOp = StencilOp::Keep;   ///< pass stencil but fail depth tests
        CompareOp       _comparisonOp = CompareOp::Always;

		static StencilDesc NoEffect;
		static StencilDesc AlwaysWrite;
    };

    /// Equivalent to MTLDepthStencilDescriptor or D3D12_DEPTH_STENCIL_DESC or VkPipelineDepthStencilStateCreateInfo
    class DepthStencilDesc
    {
    public:
        CompareOp       _depthTest = CompareOp::LessEqual;
        bool            _depthWrite = true;
        bool            _stencilEnable = false;
        uint8_t         _stencilReadMask = 0x0;
        uint8_t         _stencilWriteMask = 0x0;
        StencilDesc     _frontFaceStencil;
        StencilDesc     _backFaceStencil;
        bool            _depthBoundsTestEnable = false;

		uint64_t HashDepthAspect() const;
        uint64_t HashStencilAspect() const;
    };

    namespace RasterizationDescFlags
    {
        enum Flag { ConservativeRaster = 1u<<0u };
        using BitField = unsigned;
    }

    /// Similar to VkPipelineRasterizationStateCreateInfo or D3D12_RASTERIZER_DESC
    /// (Metal just has separate function calls)
    class RasterizationDesc
    {
    public:
        CullMode        _cullMode = CullMode::Back;
        FaceWinding     _frontFaceWinding = FaceWinding::CCW;
		float			_depthBiasConstantFactor = 0.f;		///< truncated to integer on DX11 or DX12
		float			_depthBiasClamp = 0.f;				///< zero means no clamping
		float			_depthBiasSlopeFactor = 0.f;
        RasterizationDescFlags::BitField _flags = 0;

		uint64_t Hash() const;
    };

    namespace SamplerDescFlags
    {
        enum Flag { DisableMipmaps = 1u<<0u, UnnormalizedCoordinates = 1u<<1u };
        using BitField = unsigned;
    }

    class SamplerDesc
    {
    public:
        FilterMode _filter = FilterMode::Trilinear;
        AddressMode _addressU = AddressMode::Wrap;
        AddressMode _addressV = AddressMode::Wrap;
        AddressMode _addressW = AddressMode::Wrap;
        CompareOp _comparison = CompareOp::Never;
        SamplerDescFlags::BitField _flags = 0;

		uint64_t Hash() const;
		friend std::ostream& operator<<(std::ostream&, const SamplerDesc&);
    };

    namespace ColorWriteMask
    {
        enum Channels
        {
            Red     = (1<<0),
            Green   = (1<<1),
            Blue    = (1<<2),
            Alpha   = (1<<3)
        };
        using BitField = unsigned;

        const BitField All = (Red | Green | Blue | Alpha);
        const BitField None = 0;
    };

    /**
     * Similar to MTLRenderPipelineColorAttachmentDescriptor or D3D12_RENDER_TARGET_BLEND_DESC or VkPipelineColorBlendAttachmentState
     */
    class AttachmentBlendDesc
    {
    public:
        bool _blendEnable = false;
        Blend _srcColorBlendFactor = Blend::One;
        Blend _dstColorBlendFactor = Blend::Zero;
        BlendOp _colorBlendOp = BlendOp::Add;
        Blend _srcAlphaBlendFactor = Blend::One;
        Blend _dstAlphaBlendFactor = Blend::Zero;
        BlendOp _alphaBlendOp = BlendOp::Add;
        ColorWriteMask::BitField _writeMask = ColorWriteMask::All;

		uint64_t Hash() const;
    };

    enum class Topology
	{
		PointList = 1,		// D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
		LineList = 2,		// D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
		LineStrip = 3,		// D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
		TriangleList = 4,   // D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		TriangleStrip = 5,  // D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
		LineListAdj = 10,   // D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ


		PatchList1 = 33,    // D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST	= 33,
		PatchList2 = 34,    // D3D11_PRIMITIVE_TOPOLOGY_2_CONTROL_POINT_PATCHLIST	= 34,
		PatchList3 = 35,    // D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST	= 35,
		PatchList4 = 36,    // D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST	= 36,
		PatchList5 = 37,    // D3D11_PRIMITIVE_TOPOLOGY_5_CONTROL_POINT_PATCHLIST	= 37,
		PatchList6 = 38,    // D3D11_PRIMITIVE_TOPOLOGY_6_CONTROL_POINT_PATCHLIST	= 38,
		PatchList7 = 39,    // D3D11_PRIMITIVE_TOPOLOGY_7_CONTROL_POINT_PATCHLIST	= 39,
		PatchList8 = 40,    // D3D11_PRIMITIVE_TOPOLOGY_8_CONTROL_POINT_PATCHLIST	= 40,
		PatchList9 = 41,    // D3D11_PRIMITIVE_TOPOLOGY_9_CONTROL_POINT_PATCHLIST	= 41,
		PatchList10 = 42,   // D3D11_PRIMITIVE_TOPOLOGY_10_CONTROL_POINT_PATCHLIST	= 42,
		PatchList11 = 43,   // D3D11_PRIMITIVE_TOPOLOGY_11_CONTROL_POINT_PATCHLIST	= 43,
		PatchList12 = 44,   // D3D11_PRIMITIVE_TOPOLOGY_12_CONTROL_POINT_PATCHLIST	= 44,
		PatchList13 = 45,   // D3D11_PRIMITIVE_TOPOLOGY_13_CONTROL_POINT_PATCHLIST	= 45,
		PatchList14 = 46,   // D3D11_PRIMITIVE_TOPOLOGY_14_CONTROL_POINT_PATCHLIST	= 46,
		PatchList15 = 47,   // D3D11_PRIMITIVE_TOPOLOGY_15_CONTROL_POINT_PATCHLIST	= 47,
		PatchList16 = 48    // D3D11_PRIMITIVE_TOPOLOGY_16_CONTROL_POINT_PATCHLIST	= 48
	};

	class ViewportDesc
    {
    public:
        float _x, _y;
        float _width, _height;
        float _minDepth, _maxDepth;
        bool _originIsUpperLeft;

        ViewportDesc(float x=0.f, float y=0.f, float width=0.f, float height=0.f, float minDepth=0.f, float maxDepth=1.f, bool originIsUpperLeft=true)
        : _x(x), _y(y)
        , _width(width), _height(height)
        , _minDepth(minDepth), _maxDepth(maxDepth)
        , _originIsUpperLeft(originIsUpperLeft)
        {
            // To avoid confusion that might stem from flipped viewports, disallow them.  Viewport size must be non-negative.
            assert(_width >= 0.f);
            assert(_height >= 0.f);
        }
    };

    class ScissorRect
    {
    public:
        int _x, _y;
        unsigned _width, _height;
        bool _originIsUpperLeft;

        ScissorRect(int x=0, int y=0, unsigned width=0, unsigned height=0, bool originIsUpperLeft=true)
        : _x(x), _y(y)
        , _width(width), _height(height)
        , _originIsUpperLeft(originIsUpperLeft)
        {}
    };

    const char* AsString(AddressMode);
	const char* AsString(FilterMode);
	const char* AsString(CompareOp);
	const char* AsString(Topology);
    const char* AsString(CullMode);

    std::optional<AddressMode> AsAddressMode(StringSection<>);
	std::optional<FilterMode> AsFilterMode(StringSection<>);
	std::optional<CompareOp> AsCompareOp(StringSection<>);
    std::optional<SamplerDescFlags::Flag> AsSamplerDescFlag(StringSection<>);
    std::optional<CullMode> AsCullMode(StringSection<>);
}


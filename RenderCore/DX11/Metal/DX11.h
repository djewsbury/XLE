// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../../Core/Prefix.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmicrosoft-enum-forward-reference"

    //
    //      Forward declarations for many DX11 types.
    //
    //      Most the Metal interface for DX11 can be included without DX11.h
    //      by just forward declarations for the DX11 types (as so...)
    //
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11DepthStencilView;
struct ID3D11Resource;
struct ID3D11DeviceContext;
struct ID3D11Device;
struct ID3D11CommandList;
struct ID3D11Buffer;
struct ID3D11Texture1D;
struct ID3D11Texture3D;
struct ID3D11DeviceChild;
struct ID3D11Query;
struct ID3D11View;
struct ID3D10Blob;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11GeometryShader;
struct ID3D11ComputeShader;
struct ID3D11HullShader;
struct ID3D11DomainShader;
struct ID3D11InputLayout;
struct ID3D11SamplerState;
struct ID3D11RasterizerState;
struct ID3D11BlendState;
struct ID3D11DepthStencilState;
struct ID3D11ShaderReflection;
struct ID3D11ShaderReflectionConstantBuffer;
struct ID3D11ShaderReflectionVariable;
struct ID3D11UnorderedAccessView;
struct ID3D11ClassLinkage;
struct ID3D11ClassInstance;
struct IUnknown;

struct ID3D11Resource1;
struct ID3D11DeviceChild1;
struct ID3D11DeviceContext1;
struct ID3D11Device1;

struct ID3DUserDefinedAnnotation;

struct _GUID;
typedef struct _GUID GUID;
typedef long HRESULT;
enum D3D_FEATURE_LEVEL;

struct IDXGIDevice1;
struct IDXGISwapChain;
struct IDXGIAdapter1;
struct IDXGIFactory1;
typedef enum DXGI_FORMAT DXGI_FORMAT;

typedef struct D3D11_BLEND_DESC D3D11_BLEND_DESC;
typedef struct D3D11_DEPTH_STENCIL_DESC D3D11_DEPTH_STENCIL_DESC;
typedef struct D3D11_RASTERIZER_DESC D3D11_RASTERIZER_DESC;
typedef struct D3D11_SAMPLER_DESC D3D11_SAMPLER_DESC;
typedef struct D3D11_BUFFER_DESC D3D11_BUFFER_DESC;
typedef struct D3D11_TEXTURE1D_DESC D3D11_TEXTURE1D_DESC;
typedef struct D3D11_TEXTURE2D_DESC D3D11_TEXTURE2D_DESC;
typedef struct D3D11_TEXTURE3D_DESC D3D11_TEXTURE3D_DESC;
typedef struct D3D11_SUBRESOURCE_DATA D3D11_SUBRESOURCE_DATA;
typedef struct D3D11_RENDER_TARGET_VIEW_DESC D3D11_RENDER_TARGET_VIEW_DESC;
typedef struct D3D11_SHADER_RESOURCE_VIEW_DESC D3D11_SHADER_RESOURCE_VIEW_DESC;
typedef struct D3D11_UNORDERED_ACCESS_VIEW_DESC D3D11_UNORDERED_ACCESS_VIEW_DESC;
typedef struct D3D11_DEPTH_STENCIL_VIEW_DESC D3D11_DEPTH_STENCIL_VIEW_DESC;
typedef struct D3D11_SO_DECLARATION_ENTRY D3D11_SO_DECLARATION_ENTRY;
typedef struct D3D11_INPUT_ELEMENT_DESC D3D11_INPUT_ELEMENT_DESC;
typedef struct D3D11_QUERY_DESC D3D11_QUERY_DESC;
typedef struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT D3D11_QUERY_DATA_TIMESTAMP_DISJOINT;

#define DX_VERSION_11           1
#define DX_VERSION_11_1         2
#define DX_VERSION              DX_VERSION_11_1

namespace ID3D
{
    using Texture2D             = ID3D11Texture2D;
    using TextureCube           = ID3D11Texture2D;
    using RenderTargetView      = ID3D11RenderTargetView;
    using ShaderResourceView    = ID3D11ShaderResourceView;
    using DepthStencilView      = ID3D11DepthStencilView;
    using CommandList           = ID3D11CommandList;
    using Buffer                = ID3D11Buffer;
    using Texture1D             = ID3D11Texture1D;
    using Texture3D             = ID3D11Texture3D;
    using Query                 = ID3D11Query;
    using View                  = ID3D11View;
    using Blob                  = ID3D10Blob;
    using VertexShader          = ID3D11VertexShader;
    using PixelShader           = ID3D11PixelShader;
    using GeometryShader        = ID3D11GeometryShader;
    using ComputeShader         = ID3D11ComputeShader;
    using HullShader            = ID3D11HullShader;
    using DomainShader          = ID3D11DomainShader;
    using InputLayout           = ID3D11InputLayout;
    using SamplerState          = ID3D11SamplerState;
    using RasterizerState       = ID3D11RasterizerState;
    using BlendState            = ID3D11BlendState;
    using DepthStencilState     = ID3D11DepthStencilState;
    using ShaderReflection      = ID3D11ShaderReflection;
    using ShaderReflectionConstantBuffer    = ID3D11ShaderReflectionConstantBuffer;
    using ShaderReflectionVariable          = ID3D11ShaderReflectionVariable;
    using UnorderedAccessView   = ID3D11UnorderedAccessView;
    using ClassLinkage          = ID3D11ClassLinkage;
    using ClassInstance         = ID3D11ClassInstance;

    #if DX_VERSION == DX_VERSION_11_1
        using DeviceContext1    = ID3D11DeviceContext1;
        using Device1           = ID3D11Device1;
    #endif

    using DeviceContext         = ID3D11DeviceContext;
    using Device                = ID3D11Device;
    using DeviceChild           = ID3D11DeviceChild;
    using Resource              = ID3D11Resource;
    using UserDefinedAnnotation = ID3DUserDefinedAnnotation;
}

namespace IDXGI
{
    using Device        = IDXGIDevice1;
    using SwapChain     = IDXGISwapChain;
    using Adapter       = IDXGIAdapter1;
    using Factory       = IDXGIFactory1;
}

#pragma GCC diagnostic pop

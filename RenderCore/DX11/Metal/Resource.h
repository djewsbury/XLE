// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DX11.h"
#include "../../IDevice.h"
#include "../../ResourceDesc.h"
#include "../../../Utility/IntrusivePtr.h"
#include "../../../Utility/IteratorUtils.h"

#include "IncludeDX11.h"

namespace RenderCore { class IResource; }
namespace RenderCore { namespace Metal_DX11
{
	class DeviceContext;
	class ObjectFactory;

	enum class ImageLayout
	{
		Undefined,
		General,
		ColorAttachmentOptimal,
		DepthStencilAttachmentOptimal,
		DepthStencilReadOnlyOptimal,
		ShaderReadOnlyOptimal,
		TransferSrcOptimal,
		TransferDstOptimal,
		Preinitialized,
		PresentSrc
	};

	/// <summary>Helper object to catch multiple similar pointers</summary>
	/// To help with platform abstraction, RenderCore::Resource* is actually the
	/// same as a Metal::Resource*. This helper allows us to catch both equally.
	using UnderlyingResourcePtr = intrusive_ptr<ID3D::Resource>;

	class Resource : public IResource
	{
	public:
		intrusive_ptr<ID3D::Resource> _underlying;

		const intrusive_ptr<ID3D::Resource>& GetUnderlying() const { return _underlying; }

		virtual void*			QueryInterface(size_t guid) override;
		virtual ResourceDesc	GetDesc() const override;
		virtual uint64_t        GetGUID() const override;

		virtual std::vector<uint8_t>    ReadBack(IThreadContext& context, SubResourceId subRes) const override;

		Resource();
		explicit Resource(const intrusive_ptr<ID3D::Resource>& underlying);
		explicit Resource(intrusive_ptr<ID3D::Resource>&& underlying);

	private:
		uint64_t _guid;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      M E M O R Y   M A P       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Locks a resource's memory for access from the CPU</summary>
    /// This is a low level mapping operation that happens immediately. The GPU must not
    /// be using the resource at the same time. If the GPU attempts to read while the CPU
    /// is written, the results will be undefined.
    /// A resource cannot be mapped more than once at the same time. However, multiple 
    /// subresources can be mapped in a single mapping operation.
    /// The caller is responsible for ensuring that the map is safe.
	class ResourceMap
	{
	public:
		IteratorRange<void*>        GetData()               { return { _map.pData, PtrAdd(_map.pData, _mapSize) }; }
        IteratorRange<const void*>  GetData() const         { return { _map.pData, PtrAdd(_map.pData, _mapSize) }; }
		TexturePitches				GetPitches() const      { return { _map.RowPitch, _map.DepthPitch }; }

		enum class Mode { Read, WriteDiscardPrevious };

		ResourceMap(
			DeviceContext& context, const Resource& resource,
			Mode mapMode,
			SubResourceId subResource = {});
		ResourceMap();
		~ResourceMap();

		ResourceMap(const ResourceMap&) = delete;
		ResourceMap& operator=(const ResourceMap&) = delete;
		ResourceMap(ResourceMap&&) never_throws;
		ResourceMap& operator=(ResourceMap&&) never_throws;

		HRESULT GetMapResultCode() const { return _mapResultCode; }		// (DX specific)

	private:
		D3D11_MAPPED_SUBRESOURCE _map;
		size_t _mapSize;
		intrusive_ptr<ID3D::Resource> _underlyingResource;
		intrusive_ptr<ID3D::DeviceContext> _devContext;
		HRESULT _mapResultCode = 0;

		void TryUnmap();
	};

    class DeviceContext;
    class ShaderResourceView;
    class DepthStencilView;
    class RenderTargetView;
    class UnorderedAccessView;

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      C O P Y I N G       //
///////////////////////////////////////////////////////////////////////////////////////////////////

	void Copy(
		DeviceContext&, Resource& dst, const Resource& src, 
		ImageLayout dstLayout = ImageLayout::Undefined, ImageLayout srcLayout = ImageLayout::Undefined);

	class BlitPass
    {
    public:
        class CopyPartial_Dest
        {
        public:
            IResource*          _resource;
            SubResourceId       _subResource;
            VectorPattern<unsigned, 3>      _leftTopFront;
        };

        class CopyPartial_Src
        {
        public:
            IResource*          _resource;
            SubResourceId       _subResource;
            VectorPattern<unsigned, 3>      _leftTopFront;
            VectorPattern<unsigned, 3>      _rightBottomBack;
        };

        void    Write(
            const CopyPartial_Dest& dst,
            const SubResourceInitData& srcData,
            Format srcDataFormat,
            VectorPattern<unsigned, 3> srcDataDimensions);

        void    Copy(
            const CopyPartial_Dest& dst,
            const CopyPartial_Src& src);

        BlitPass(IThreadContext& threadContext);
        ~BlitPass();

	private:
		DeviceContext* _boundContext;

		BlitPass(DeviceContext& devContext);

		friend void CopyPartial(DeviceContext&, const BlitPass::CopyPartial_Dest&, const BlitPass::CopyPartial_Src&, ImageLayout, ImageLayout);
    };

	// (deprecated pre-BlitPass version)
	using CopyPartial_Dest = BlitPass::CopyPartial_Dest;
	using CopyPartial_Src = BlitPass::CopyPartial_Src;
	XLE_DEPRECATED_ATTRIBUTE void CopyPartial(
		DeviceContext&, const BlitPass::CopyPartial_Dest& dst, const BlitPass::CopyPartial_Src& src,
		ImageLayout dstLayout = ImageLayout::Undefined, ImageLayout srcLayout = ImageLayout::Undefined);
	

    intrusive_ptr<ID3D::Resource> Duplicate(DeviceContext& context, intrusive_ptr<ID3D::Resource> inputResource);
	IResourcePtr Duplicate(DeviceContext&, Resource& inputResource);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      G E T   D E S C       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    ResourceDesc ExtractDesc(const intrusive_ptr<ID3D::Resource>& res);
	ResourceDesc ExtractDesc(const ShaderResourceView& res);
	ResourceDesc ExtractDesc(const RenderTargetView& res);
    ResourceDesc ExtractDesc(const DepthStencilView& res);
    ResourceDesc ExtractDesc(const UnorderedAccessView& res);

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      U T I L S       //
///////////////////////////////////////////////////////////////////////////////////////////////////

    class LayoutTransition
    {
    public:
        Resource* _res = nullptr;
		ImageLayout _oldLayout = ImageLayout::Undefined;
		ImageLayout _newLayout = ImageLayout::Undefined;
    };
    inline void SetImageLayouts(DeviceContext& context, IteratorRange<const LayoutTransition*> changes) {}

	/////////////// Resource creation and access ///////////////

	using ResourceInitializer = std::function<SubResourceInitData(SubResourceId)>;
	std::shared_ptr<IResource> CreateResource(
		const ObjectFactory& factory,
		const ResourceDesc& desc, 
		const ResourceInitializer& init = ResourceInitializer());

	intrusive_ptr<ID3D::Resource> CreateUnderlyingResource(
		const ObjectFactory& factory,
		const ResourceDesc& desc,
		const ResourceInitializer& init = ResourceInitializer());

	ID3D::Resource* AsID3DResource(UnderlyingResourcePtr);
	ID3D::Resource* AsID3DResource(IResource&);
	IResourcePtr AsResourcePtr(ID3D::Resource*);
	IResourcePtr AsResourcePtr(intrusive_ptr<ID3D::Resource>&&);
	Resource& AsResource(IResource& res);
}}

	/// \cond INTERNAL
	//
	//      Implement the intrusive_ptr for resources in only one CPP file. 
	//      Note this means that AddRef/Release are pulled out-of-line
	//
	//      But this is required, otherwise each use of intrusive_ptr<Render::Underlying::Resource>
	//      requires #include <d3d11.h>
	//

namespace Utility
{
	void intrusive_ptr_add_ref(ID3D::Resource*);
	void intrusive_ptr_release(ID3D::Resource*);
}

	/// \endcond

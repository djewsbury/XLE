// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Buffer.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "DX11Utils.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Core/Exceptions.h"

namespace RenderCore { namespace Metal_DX11
{
        
    Buffer::Buffer(
		ObjectFactory& factory, const ResourceDesc& desc,
		IteratorRange<const void*> initData)
	: Resource(CreateUnderlyingResource(factory, desc, 
		[initData](SubResourceId subr) {
			assert(subr._mip == 0 && subr._arrayLayer == 0);
			return SubResourceInitData { initData, {} };
		}))
    {
	}

    Buffer::Buffer() {}

    Buffer::~Buffer() {}

    Buffer::Buffer(UnderlyingResourcePtr cloneFrom)
    : Resource(cloneFrom)
    {}

	void	Buffer::Update(DeviceContext& context, const void* data, size_t byteCount, size_t writeOffset, UpdateFlags::BitField flags)
	{
		if (!(flags & UpdateFlags::UnsynchronizedWrite) && !(flags && UpdateFlags::Internal_Copy) && context.InRenderPass()) {
            Throw(::Exceptions::BasicLabel("Buffer::Update synchronized can only be called between render passes."));
        }

		D3D11_MAPPED_SUBRESOURCE result;
        ID3D::DeviceContext* devContext = context.GetUnderlying();
		D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD;
		if (flags & UpdateFlags::UnsynchronizedWrite)
			mapType = D3D11_MAP_WRITE_NO_OVERWRITE;
        HRESULT hresult = devContext->Map(_underlying.get(), 0, mapType, 0, &result);
        if (SUCCEEDED(hresult) && result.pData) {
            XlCopyMemory(result.pData, data, byteCount);
            devContext->Unmap(_underlying.get(), 0);
        } else {
			assert(0);		// mapping failure
		}
	}

	void* Buffer::QueryInterface(size_t guid)
	{
		if (guid == TypeHashCode<Buffer>)
			return this;
		return Resource::QueryInterface(guid);
	}

	static ResourceDesc BuildDesc(BindFlag::BitField bindingFlags, size_t byteCount, bool immutable=true)
    {
		assert(byteCount!=0);
        return CreateDesc(
            bindingFlags,
            immutable ? 0 : CPUAccess::WriteDynamic,
            GPUAccess::Read,
            LinearBufferDesc::Create(unsigned(byteCount)),
            "buf");
    }

    Buffer MakeVertexBuffer(ObjectFactory& factory, IteratorRange<const void*> data)
    {
		assert(!data.empty());
        return Buffer(
            factory,
            BuildDesc(BindFlag::VertexBuffer, data.size(), true),
            data);
    }
    
    Buffer MakeIndexBuffer(ObjectFactory& factory, IteratorRange<const void*> data)
    {
		assert(!data.empty());
        return Buffer(
            factory,
            BuildDesc(BindFlag::IndexBuffer, data.size(), true),
            data);
    }

    Buffer MakeConstantBuffer(ObjectFactory& factory, IteratorRange<const void*> data, bool immutable)
    {
		assert(!data.empty());
        return Buffer(
            factory,
            BuildDesc(BindFlag::ConstantBuffer, data.size(), immutable),
            data);
    }

	Buffer MakeConstantBuffer(ObjectFactory& factory, size_t size)
	{
		assert(size!=0);
		return Buffer(
            factory,
            BuildDesc(BindFlag::ConstantBuffer, size, false));
	}


}}


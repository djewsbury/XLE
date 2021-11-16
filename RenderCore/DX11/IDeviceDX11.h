// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "Metal/DX11.h"

namespace RenderCore
{
    namespace Metal_DX11 { class DeviceContext; }

    ////////////////////////////////////////////////////////////////////////////////

    /// <summary>IDevice extension for DX11</summary>
    /// Use IDevice::QueryInterface to query for this type from a
    /// plain IDevice.
    class IDeviceDX11
    {
    public:
        virtual ID3D::Device*           GetUnderlyingDevice() = 0;
		virtual ID3D::DeviceContext*    GetImmediateDeviceContext() = 0;
		virtual ~IDeviceDX11();
    };

    ////////////////////////////////////////////////////////////////////////////////

    /// <summary>IThreadContext extension for DX11</summary>
    class IThreadContextDX11
    {
    public:
        virtual std::shared_ptr<Metal_DX11::DeviceContext>&  GetUnderlying() = 0;
		virtual ID3D::Device*  GetUnderlyingDevice() = 0;
		virtual ~IThreadContextDX11();
    };

}

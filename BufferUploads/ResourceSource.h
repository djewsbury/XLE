// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderCore { class IResource; }
namespace BufferUploads
{
	class IResourcePool
	{
	public:
		virtual void AddRef(
			uint64_t resourceMarker, RenderCore::IResource& resource, 
			size_t offset, size_t size) = 0;
		virtual void Release(
			uint64_t resourceMarker, std::shared_ptr<RenderCore::IResource>&& resource, 
			size_t offset, size_t size) = 0;
		virtual ~IResourcePool();
	};
}

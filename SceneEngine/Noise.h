// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>
#include <future>

namespace RenderCore { namespace Techniques { class IShaderResourceDelegate; }}

namespace SceneEngine
{
	std::future<std::shared_ptr<RenderCore::Techniques::IShaderResourceDelegate>> CreatePerlinNoiseResources();
}


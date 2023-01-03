// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace Utility { class IHierarchicalProfiler; }

namespace PlatformRig { namespace Overlays
{
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateHierarchicalProfilerDisplay(Utility::IHierarchicalProfiler& profiler);
}}


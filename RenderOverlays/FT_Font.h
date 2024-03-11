// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Utility/StringUtils.h"
#include <memory>

namespace RenderOverlays
{
	class Font;
	std::shared_ptr<Font> CreateFTFont(StringSection<> faceName, int faceSize);
}


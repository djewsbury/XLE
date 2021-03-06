// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Types_Forward.h"

using VkFormat_ = uint32_t;

namespace RenderCore { namespace Metal_Vulkan
{
    VkFormat_ AsVkFormat(Format);
	Format AsFormat(VkFormat_);
    void InitFormatConversionTables();
}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ScaffoldCmdStream.h"

namespace RenderCore { namespace Assets
{
	enum class MaterialCommand : uint32_t
	{
		AttachShaderResourceBindings,
		AttachSelectors,
		AttachStateSet,
		AttachConstants,
		AttachSamplerBindings,
		AttachPatchCollectionId,
	};
}}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/IntermediateCompilers.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Assets
{
	static const auto TextureCompilerProcessType = ConstHash64<'Text', 'ure'>::Value;

	::Assets::CompilerRegistration RegisterTextureCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);
}}


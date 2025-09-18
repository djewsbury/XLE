// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"

namespace GraphLanguage { class ShaderFragmentSignature; }

namespace ShaderSourceParser
{
    GraphLanguage::ShaderFragmentSignature     ParseHLSL(StringSection<> sourceCode);
}

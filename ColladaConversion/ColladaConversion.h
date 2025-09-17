// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/StringUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Core/Prefix.h"
#include <memory>

namespace Assets { class ICompileOperation; }

namespace ColladaConversion
{
	static const uint64_t Type_Model = ConstHash64Legacy<'Mode', 'l'>::Value;
	static const uint64_t Type_AnimationSet = ConstHash64Legacy<'Anim', 'Set'>::Value;
	static const uint64_t Type_Skeleton = ConstHash64Legacy<'Skel', 'eton'>::Value;
	static const uint64_t Type_RawMat = ConstHash64Legacy<'RawM', 'at'>::Value;
}

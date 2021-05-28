// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/MemoryUtils.h"

namespace RenderCore { namespace LightingEngine
{
	enum class LightSourceShape { Directional, Sphere, Tube, Rectangle, Disc };
	enum class DiffuseModel { Lambert, Disney };

	class LightSourceOperatorDesc
	{
	public:
		LightSourceShape _shape = LightSourceShape::Directional;
		DiffuseModel _diffuseModel = DiffuseModel::Disney;

		uint64_t Hash(uint64_t seed = DefaultSeed64) const;
	};

}}

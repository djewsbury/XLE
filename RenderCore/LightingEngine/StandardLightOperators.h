// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include <optional>
#include <limits>

namespace RenderCore { namespace LightingEngine
{
	enum class LightSourceShape { Directional, Sphere, Tube, Rectangle, Disc };
	enum class DiffuseModel { Lambert, Disney };

	struct LightSourceOperatorDesc
	{
		LightSourceShape _shape = LightSourceShape::Directional;
		DiffuseModel _diffuseModel = DiffuseModel::Lambert;

		struct Flags
		{
			enum Enum { DominantLight = 1<<0, NeverStencil = 1<<1 };
			using BitField = unsigned;
		};
		Flags::BitField _flags = 0;

		uint64_t GetHash(uint64_t seed = DefaultSeed64) const;
	};

	struct AmbientLightOperatorDesc
	{
	};

	std::optional<LightSourceShape> AsLightSourceShape(StringSection<>);
	const char* AsString(LightSourceShape);
	std::optional<DiffuseModel> AsDiffuseModel(StringSection<>);
	const char* AsString(DiffuseModel);

}}

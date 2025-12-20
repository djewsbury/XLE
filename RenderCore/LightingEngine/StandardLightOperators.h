// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include <optional>

namespace RenderCore { namespace LightingEngine
{
	// Note the ordering of LightSourceShape must match LightDesc.hlsl
	// Also there's a requirement that (Sphere|2) must equal Cone
	enum class LightSourceShape { Directional, Sphere, Tube, Cone, Rectangle, Disc };
	enum class DiffuseModel { Lambert, Disney };

	struct PositionalLightOperatorDesc
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

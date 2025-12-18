
#pragma once

#include "Math/Matrix.h"
#include <memory>

namespace XLEMath { class ArbitraryConvexVolumeTester; }

namespace RenderCore::LightingEngine { class IOrthoShadowProjections; }
namespace RenderCore::Techniques { class ParsingContext; }

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase;

	class IShadowProjectionDriver
	{
	public:
		virtual std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> UpdateProjections(
			const Techniques::ParsingContext&,
			const Float4x4& lightLocalToWorld,
			IOrthoShadowProjections& destination) = 0;
		virtual ~IShadowProjectionDriver() = default;
	};

	class IAttachDriver
	{
	public:
		virtual void AttachDriver(std::shared_ptr<ILightBase> driver, const void* system=nullptr) = 0;
		virtual ~IAttachDriver() = default;
	};
}}}

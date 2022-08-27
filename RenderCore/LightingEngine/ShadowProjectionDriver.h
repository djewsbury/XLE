
#pragma once

#include "StandardLightScene.h"			// for ILightBase

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class IShadowProjectionDriver
	{
	public:
		virtual std::shared_ptr<XLEMath::ArbitraryConvexVolumeTester> UpdateProjections(
			const Techniques::ParsingContext&,
			IPositionalLightSource& lightSource,
			IOrthoShadowProjections& destination) = 0;
		virtual ~IShadowProjectionDriver() = default;
	};

	class IAttachDriver
	{
	public:
		virtual void AttachDriver(std::shared_ptr<Internal::ILightBase> driver) = 0;
		virtual ~IAttachDriver() = default;
	};
}}}

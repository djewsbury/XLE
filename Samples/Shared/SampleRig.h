// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderCore/ResourceDesc.h"		// (for BindFlag::BitField)
#include "../../Math/Vector.h"
#include <memory>
#include <optional>
#include <string>

namespace PlatformRig { class AppRigGlobals; }
namespace Formatters { template<typename CharType> class CommandLineFormatter; }

namespace Sample
{
	struct SampleConfiguration
	{
		RenderCore::BindFlag::BitField _presentationChainBindFlags = 0;
		std::string _windowTitle;
		std::optional<UInt2> _initialWindowSize;
	};

	class ISampleOverlay
	{
	public:
		virtual void OnUpdate(float deltaTime);
		virtual void OnStartup(const PlatformRig::AppRigGlobals& globals);
		virtual void Configure(SampleConfiguration& cfg);
	};

	void ExecuteSample(std::shared_ptr<ISampleOverlay>&& sampleOverlay, Formatters::CommandLineFormatter<char>& cmdLine);
}

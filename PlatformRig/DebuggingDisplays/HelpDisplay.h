// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../Utility/StringUtils.h"
#include <memory>

namespace PlatformRig { namespace Overlays
{
	class IHelpDisplay : public RenderOverlays::DebuggingDisplay::IWidget
	{
	public:
		virtual void AddKey(StringSection<> key, StringSection<> helpText) = 0;
		virtual void AddText(StringSection<> text) = 0;
		virtual ~IHelpDisplay();
	};

	std::shared_ptr<IHelpDisplay> CreateHelpDisplay();
}}

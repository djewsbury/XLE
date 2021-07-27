// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace RenderCore { class IResourceView; class IDevice; }

namespace RenderCore { namespace LightingEngine
{
	class BlueNoiseGeneratorTables
	{
	public:
		std::shared_ptr<IResourceView> _sobolBufferView;
		std::shared_ptr<IResourceView> _rankingTileBufferView;
		std::shared_ptr<IResourceView> _scramblingTileBufferView;

		BlueNoiseGeneratorTables(IDevice& device);
	};
}}


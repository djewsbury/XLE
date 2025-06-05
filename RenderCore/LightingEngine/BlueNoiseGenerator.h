// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class IResourceView; class IDevice; class IThreadContext; }

namespace RenderCore { namespace LightingEngine
{
	class BlueNoiseGeneratorTables
	{
	public:
		std::shared_ptr<IResourceView> _sobolBufferView;
		std::shared_ptr<IResourceView> _rankingTileBufferView;
		std::shared_ptr<IResourceView> _scramblingTileBufferView;
		bool _pendingInitialization = false;

		void CompleteInitialization(IThreadContext&);
		BlueNoiseGeneratorTables(IDevice& device);
	};

	/// <summary>Utility for generating halton sampling patterns in shaders</summary>
	/// Halton sampling in shaders isn't ideal, because there's a fair level of overhead in
	/// generating the sample values. However, it's quite convenient to work with (as well as
	/// begin easy to understand intuitively).
	///
	/// This is intended for preprocessing shaders and reference shaders, where performance isn't
	/// the primary concern.
	class HaltonSamplerHelper
	{
	public:
		std::shared_ptr<IResourceView> _pixelToSampleIndex;
		std::shared_ptr<IResourceView> _pixelToSampleIndexParams;		// cbuffer
		unsigned _repeatingStride = 0;

		HaltonSamplerHelper(IThreadContext& threadContext, unsigned width, unsigned height);

		static uint32_t WriteHaltonSamplerIndices(IteratorRange<uint32_t*> dst, uint32_t width, uint32_t height);
	};
}}


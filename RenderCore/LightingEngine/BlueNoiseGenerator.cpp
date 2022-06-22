// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BlueNoiseGenerator.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include <cstdint>

namespace RenderCore { namespace LightingEngine
{
	namespace _1spp
	{
	#include "Foreign/FidelityFX-SSSR/sample/libs/samplerCPP/samplerBlueNoiseErrorDistribution_128x128_OptimizedFor_2d2d2d2d_8spp.cpp"
	}

	/**
		The available blue noise sampler with 2ssp sampling mode.
	*/
	struct
	{
		std::int32_t const (&sobol_buffer_)[256 * 256];
		std::int32_t const (&ranking_tile_buffer_)[128 * 128 * 8];
		std::int32_t const (&scrambling_tile_buffer_)[128 * 128 * 8];
	}
	const g_blue_noise_sampler_state = { _1spp::sobol_256spp_256d,  _1spp::rankingTile,  _1spp::scramblingTile };

	BlueNoiseGeneratorTables::BlueNoiseGeneratorTables(IDevice& device)
	{
		auto sobolBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.sobol_buffer_)),
				"blue-noise-sobol"
			),
			SubResourceInitData{MakeIteratorRange(g_blue_noise_sampler_state.sobol_buffer_)});
		_sobolBufferView = sobolBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto rankingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.ranking_tile_buffer_)),
				"blue-noise-ranking"
			),
			SubResourceInitData{MakeIteratorRange(g_blue_noise_sampler_state.ranking_tile_buffer_)});
		_rankingTileBufferView = rankingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto scramblingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.scrambling_tile_buffer_)),
				"blue-noise-scrambling"
			),
			SubResourceInitData{MakeIteratorRange(g_blue_noise_sampler_state.scrambling_tile_buffer_)});
		_scramblingTileBufferView = scramblingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
	}

}}


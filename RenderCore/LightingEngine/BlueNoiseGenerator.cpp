// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BlueNoiseGenerator.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../Metal/DeviceContext.h"
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

	void BlueNoiseGeneratorTables::CompleteInitialization(IThreadContext& threadContext)
	{
		if (!_pendingInitialization) return;

		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto blitEncoder = metalContext.BeginBlitEncoder();
		blitEncoder.Write(*_sobolBufferView->GetResource(), MakeIteratorRange(g_blue_noise_sampler_state.sobol_buffer_));
		blitEncoder.Write(*_rankingTileBufferView->GetResource(), MakeIteratorRange(g_blue_noise_sampler_state.ranking_tile_buffer_));
		blitEncoder.Write(*_scramblingTileBufferView->GetResource(), MakeIteratorRange(g_blue_noise_sampler_state.scrambling_tile_buffer_));
		_pendingInitialization = false;

		Metal::BarrierHelper(metalContext).Add(*_sobolBufferView->GetResource(), BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
		Metal::BarrierHelper(metalContext).Add(*_rankingTileBufferView->GetResource(), BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
		Metal::BarrierHelper(metalContext).Add(*_scramblingTileBufferView->GetResource(), BindFlag::TransferDst, Metal::BarrierResourceUsage::AllCommandsRead());
	}

	BlueNoiseGeneratorTables::BlueNoiseGeneratorTables(IDevice& device)
	{
		auto sobolBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.sobol_buffer_)),
				"blue-noise-sobol"
			));
		_sobolBufferView = sobolBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto rankingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.ranking_tile_buffer_)),
				"blue-noise-ranking"
			));
		_rankingTileBufferView = rankingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto scramblingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.scrambling_tile_buffer_)),
				"blue-noise-scrambling"
			));
		_scramblingTileBufferView = scramblingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
		_pendingInitialization = true;
	}

}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "BlueNoiseGenerator.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/Resource.h"
#include "../../Math/Vector.h"
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
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.sobol_buffer_))),
			"blue-noise-sobol");
		_sobolBufferView = sobolBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto rankingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.ranking_tile_buffer_))),
			"blue-noise-ranking");
		_rankingTileBufferView = rankingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});

		auto scramblingTileBuffer = device.CreateResource(
			CreateDesc(
				BindFlag::TransferDst | BindFlag::ShaderResource | BindFlag::TexelBuffer,
				LinearBufferDesc::Create(sizeof(g_blue_noise_sampler_state.scrambling_tile_buffer_))),
			"blue-noise-scrambling");
		_scramblingTileBufferView = scramblingTileBuffer->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{Format::R32_UINT}});
		_pendingInitialization = true;
	}

	template<int Base, typename FloatType=float>
		static FloatType RadicalInverseSpecialized(uint64_t a)
	{
		const FloatType reciprocalBase = FloatType(1.0) / FloatType(Base);
		uint64_t reversedDigits = 0;
		FloatType reciprocalBaseN = 1;
		while (a) {
			uint64_t next = a / Base;
			uint64_t digit = a - next * Base;
			reversedDigits = reversedDigits * Base + digit;
			reciprocalBaseN *= reciprocalBase;
			a = next;
		}
		return reversedDigits * reciprocalBaseN;
	}

	HaltonSamplerHelper::HaltonSamplerHelper(IThreadContext& threadContext, unsigned width, unsigned height)
	{
		// For a given texture, we're going to create a lookup table that converts from 
		// xy coords to first sample index in the Halton sequence
		//
		// That is, if (radical-inverse-base-2(i), radical-inverse-base-3(i)) is the xy
		// coords associated with sample i; we want to be able to go backwards and get i
		// from a given sample coords
		//
		// This will then allow us to generate more well distributed numbers based on i,
		// by using the deeper dimensions of the Halton sequence
		//
		// Furthermore, we can cause samples in a given pixel to repeat with a constant
		// interval by multiplying the sampling coordinate space by a specific scale
		//
		// See pbr-book chapter 7.4 for more reference on this
		// Though, we're not going to use a mathematically sophisticated method for this,
		// instead something pretty rudimentary

		float j = std::ceil(std::log2(float(width)));
		float log3Height = std::log(float(height))/std::log(3.0f);
		float k = std::ceil(log3Height);
		float scaledWidth = std::pow(2.f, j), scaledHeight = std::pow(3.f, k);

		auto data = std::make_unique<unsigned[]>(width*height);
		std::memset(data.get(), 0, sizeof(unsigned)*width*height);

		// We can do this in a smarter way by using the inverse-radical-inverse, and solving some simultaneous
		// equations with modular arithmetic. But since we're building a lookup table anyway, that doesn't seem
		// of any practical purpose
		unsigned repeatingStride = (unsigned)(scaledWidth*scaledHeight);
		for (unsigned sampleIdx=0; sampleIdx<repeatingStride; ++sampleIdx) {
			auto x = unsigned(scaledWidth * RadicalInverseSpecialized<2>(sampleIdx)), 
				y = unsigned(scaledHeight * RadicalInverseSpecialized<3>(sampleIdx));
			if (x >= width || y >= height) continue;
			data[x+y*width] = sampleIdx;
		}

		auto texture = threadContext.GetDevice()->CreateResource(
			CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(width, height, Format::R32_UINT)),
			"sample-idx-lookup");
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		Metal::BarrierHelper{metalContext}.Add(*texture, Metal::BarrierResourceUsage::NoState(), BindFlag::TransferDst);
		auto pitches = TexturePitches{width*(unsigned)sizeof(unsigned), width*height*(unsigned)sizeof(unsigned)};
		metalContext.BeginBlitEncoder().Write(
			*texture, 
			SubResourceInitData{MakeIteratorRange(data.get(), PtrAdd(data.get(), sizeof(unsigned)*width*height)), pitches},
			Format::R32_UINT,
			UInt3{width, height, 1},
			pitches);
		Metal::BarrierHelper{metalContext}.Add(*texture, BindFlag::TransferDst, BindFlag::ShaderResource);

		_pixelToSampleIndex = texture->CreateTextureView();

		struct Uniforms
		{
			float _j, _k;
			unsigned _repeatingStride;
			unsigned _dummy;
		} uniforms {
			j, k, repeatingStride, 0
		};

		auto cbuffer = threadContext.GetDevice()->CreateResource(
			CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(sizeof(Uniforms))),
			"sample-idx-uniforms");
		Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(
			*cbuffer, MakeOpaqueIteratorRange(uniforms));
		_pixelToSampleIndexParams = cbuffer->CreateBufferView();
		_repeatingStride = repeatingStride;
	}

}}


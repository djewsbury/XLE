// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Noise.h"
#include "../RenderCore/Techniques/DrawableDelegates.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Format.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../Assets/Continuation.h"
#include "../Math/Vector.h"

#include "../RenderCore/IDevice.h"

namespace SceneEngine
{
	using namespace RenderCore;

	class PerlinNoiseResources : public Techniques::IShaderResourceDelegate
	{
	public:
		std::shared_ptr<IResourceView> _gradView, _permView;

		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			assert(dst.size() == 2);
			assert(bindingFlags == 0x3);
			dst[0] = _gradView.get();
			dst[1] = _permView.get();
		}

		static void ConstructToPromise(std::promise<std::shared_ptr<IShaderResourceDelegate>>&& promise)
		{
			const Float4 g[] = {
				Float4(1,1,0,0),    Float4(-1,1,0,0),    Float4(1,-1,0,0),    Float4(-1,-1,0,0),
				Float4(1,0,1,0),    Float4(-1,0,1,0),    Float4(1,0,-1,0),    Float4(-1,0,-1,0),
				Float4(0,1,1,0),    Float4(0,-1,1,0),    Float4(0,1,-1,0),    Float4(0,-1,-1,0),
				Float4(1,1,0,0),    Float4(0,-1,1,0),    Float4(-1,1,0,0),    Float4(0,-1,-1,0),
			};

			const uint8_t perm[256] = {
				151,160,137,91,90,15,
				131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
				190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
				88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
				77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
				102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
				135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
				5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
				223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
				129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
				251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
				49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
				138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
			};

			auto& uploads = Techniques::Services::GetBufferUploads();
			auto gradDesc = CreateDesc(BufferUploads::BindFlag::ShaderResource, BufferUploads::TextureDesc::Plain1D(dimof(g), Format::R32G32B32A32_TYPELESS), "NoiseGrad");
			auto permDesc = CreateDesc(BufferUploads::BindFlag::ShaderResource, BufferUploads::TextureDesc::Plain1D(dimof(perm), Format::R8_TYPELESS), "NoisePerm");
			auto gradMarker = uploads.Begin(gradDesc, BufferUploads::CreateBasicPacket(MakeIteratorRange(g)));
			auto permMarker = uploads.Begin(permDesc, BufferUploads::CreateBasicPacket(MakeIteratorRange(perm)));

			::Assets::WhenAll(std::move(gradMarker._future), std::move(permMarker._future)).ThenConstructToPromise(
				std::move(promise),
				[](const BufferUploads::ResourceLocator& gradLocator, const BufferUploads::ResourceLocator& permLocator) -> std::shared_ptr<IShaderResourceDelegate> {
					auto result = std::make_shared<PerlinNoiseResources>();
					result->_gradView = gradLocator.CreateTextureView(BindFlag::ShaderResource, {Format::R32G32B32A32_FLOAT});
					result->_permView = permLocator.CreateTextureView(BindFlag::ShaderResource, {Format::R8_UNORM});
					result->_interface.BindResourceView(0, Hash64("GradTexture"));
					result->_interface.BindResourceView(1, Hash64("PermTexture"));
					result->_completionCmdList = std::max(gradLocator.GetCompletionCommandList(), permLocator.GetCompletionCommandList());
					return result;
				});
		}
	};

	std::future<std::shared_ptr<RenderCore::Techniques::IShaderResourceDelegate>> CreatePerlinNoiseResources()
	{
		std::promise<std::shared_ptr<RenderCore::Techniques::IShaderResourceDelegate>> promise;
		auto result = promise.get_future();
		PerlinNoiseResources::ConstructToPromise(std::move(promise));
		return result;
	}
}



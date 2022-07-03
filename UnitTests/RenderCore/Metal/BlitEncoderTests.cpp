// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MetalTestHelper.h"
#include "../../../RenderCore/Metal/Resource.h"
#include "../../../RenderCore/Metal/DeviceContext.h"
#include "../../../Utility/StringFormat.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
namespace UnitTests
{

	std::vector<uint8_t> CreateInitData(const RenderCore::ResourceDesc& desc)
	{
		std::vector<uint8_t> result(ByteCount(desc));
		for (unsigned c=0; c<result.size(); ++c)
			result[c] = c&0xff;
		return result;
	}

	static RenderCore::ResourceDesc AsStagingDesc(const RenderCore::ResourceDesc& desc)
	{
		using namespace RenderCore;
		auto result = desc;
		result._bindFlags = BindFlag::TransferSrc;
		result._allocationRules = AllocationRules::HostVisibleSequentialWrite;
		StringMeldInPlace(result._name) << "staging-" << desc._name;
		return result;
	}

	TEST_CASE( "BlitEncoder-CopyToAndFromStaging", "[rendercore_metal]" )
	{
		using namespace RenderCore;
		auto testHelper = MakeTestHelper();
		auto& device = *testHelper->_device;
		auto threadContext = testHelper->_device->GetImmediateContext();
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);

		SECTION("SingleSubResourceCopy")
		{
			auto desc = CreateDesc(BindFlag::ShaderResource, TextureDesc::Plain2D(512, 512, Format::R8_UNORM), "test");
			auto stagingDesc = AsStagingDesc(desc);
			auto initData = CreateInitData(desc);
			
			auto deviceResource = device.CreateResource(desc); 
			auto staging = device.CreateResource(stagingDesc, SubResourceInitData{MakeIteratorRange(initData)});
			auto blitEncoder = metalContext.BeginBlitEncoder();
			blitEncoder.Copy(*deviceResource, *staging);

			{
				auto destaging = device.CreateResource(stagingDesc);
				blitEncoder.Copy(*destaging, *deviceResource);
				auto readback = destaging->ReadBackSynchronized(*threadContext);
				REQUIRE(readback.size() == initData.size());
				for (auto i=readback.begin(), i2=initData.begin(); i!=readback.end(); ++i, ++i2)
					REQUIRE(*i == *i2);
			}

			// copy from larger buffer
			auto largeStagingResource = AsStagingDesc(CreateDesc(0, LinearBufferDesc{8*1024*1024}, "largebuffer"));
			auto largeInitData = CreateInitData(largeStagingResource);
			auto largeStaging = device.CreateResource(largeStagingResource, SubResourceInitData{MakeIteratorRange(largeInitData)});
			blitEncoder.Copy(*deviceResource, *staging);

			{
				auto destaging = device.CreateResource(stagingDesc);
				blitEncoder.Copy(*destaging, *deviceResource);
				auto readback = destaging->ReadBackSynchronized(*threadContext);
				REQUIRE(readback.size() <= largeInitData.size());
				for (auto i=readback.begin(), i2=largeInitData.begin(); i!=readback.end(); ++i, ++i2)
					REQUIRE(*i == *i2);
			}

			// copy from offset within larger buffer
			unsigned offsetWithinLargeBuffer = 923;
			blitEncoder.Copy(
				CopyPartial_Dest{*deviceResource}, 
				CopyPartial_Src{*largeStaging, offsetWithinLargeBuffer});

			{
				auto destaging = device.CreateResource(stagingDesc);
				blitEncoder.Copy(*destaging, *deviceResource);
				auto readback = destaging->ReadBackSynchronized(*threadContext);
				REQUIRE(readback.size() <= largeInitData.size());
				for (auto i=readback.begin(), i2=largeInitData.begin()+offsetWithinLargeBuffer; i!=readback.end(); ++i, ++i2)
					REQUIRE(*i == *i2);
			}

			// copy from offset within larger buffer to offset within texture
			unsigned offsetWithinLargeBuffer2 = 3727;
			unsigned offsetToCopyTo[3] = { 78, 123, 0 };
			TexturePitches stagingPitches;
			stagingPitches._rowPitch = (desc._textureDesc._width - offsetToCopyTo[0]) * BitsPerPixel(desc._textureDesc._format) / 8;
			stagingPitches._slicePitch = (desc._textureDesc._height - offsetToCopyTo[1]) * stagingPitches._rowPitch;
			stagingPitches._arrayPitch = stagingPitches._slicePitch;
			blitEncoder.Copy(
				CopyPartial_Dest{*deviceResource, {}, offsetToCopyTo}, 
				CopyPartial_Src{*largeStaging, offsetWithinLargeBuffer2}.PartialSubresource({0,0,0}, {desc._textureDesc._width - offsetToCopyTo[0], desc._textureDesc._height - offsetToCopyTo[1], 1}, stagingPitches));

			{
				auto destaging = device.CreateResource(stagingDesc);
				blitEncoder.Copy(*destaging, *deviceResource);
				auto readback = destaging->ReadBackSynchronized(*threadContext);
				REQUIRE(readback.size() <= largeInitData.size());
				for (unsigned y=0; y<desc._textureDesc._height; ++y)
					for (unsigned x=0; x<desc._textureDesc._width; ++x) {
						if (y >= offsetToCopyTo[1] && x >= offsetToCopyTo[0]) {
							auto idxInBuffer = (x - offsetToCopyTo[0]) + (y - offsetToCopyTo[1]) * stagingPitches._rowPitch;
							REQUIRE(largeInitData[offsetWithinLargeBuffer2+idxInBuffer] == readback[y*desc._textureDesc._width+x]);
						} else {
							// should still contain the data from the previous upload
							auto idxInBuffer = x + y * desc._textureDesc._width;
							REQUIRE(largeInitData[offsetWithinLargeBuffer+idxInBuffer] == readback[y*desc._textureDesc._width+x]);
						}
					}
			}

			// use BlitEncoder::Write to reinitialize deviceResource to something simplier
			blitEncoder.Write(
				CopyPartial_Dest(*deviceResource),
				SubResourceInitData{MakeIteratorRange(initData)},
				desc._textureDesc._format,
				{desc._textureDesc._width, desc._textureDesc._height, desc._textureDesc._depth},
				MakeTexturePitches(desc._textureDesc));

			// copy subrectangle deviceResource to a destaging texture
			// first with a "partial" copy that is actually a full copy -- and then with a subcube
			{
				auto destaging = device.CreateResource(stagingDesc);
				unsigned destOffset[] = {0, 0, 0};
				unsigned srcLeftTopFront[] = {0, 0, 0};
				unsigned srcRightBottomBack[] = {desc._textureDesc._width, desc._textureDesc._height, 1};
				blitEncoder.Copy(
					CopyPartial_Dest(*destaging, {}, destOffset),
					CopyPartial_Src{*deviceResource}.PartialSubresource(srcLeftTopFront, srcRightBottomBack, MakeTexturePitches(deviceResource->GetDesc()._textureDesc)));

				auto readback = destaging->ReadBackSynchronized(*threadContext);
				for (unsigned y=0; y<desc._textureDesc._height; ++y)
					for (unsigned x=0; x<desc._textureDesc._width; ++x) {
						if (x < destOffset[0] || y < destOffset[1]) continue;
						if (	(x - destOffset[0]) >= (srcRightBottomBack[0] - srcLeftTopFront[0])
							||  (y - destOffset[1]) >= (srcRightBottomBack[1] - srcLeftTopFront[1]))
							continue;
						unsigned xFromOriginal = (x-destOffset[0]) + srcLeftTopFront[0];
						unsigned yFromOriginal = (y-destOffset[1]) + srcLeftTopFront[1];
						auto value = readback[y*desc._textureDesc._width+x];
						auto expectedValue = initData[yFromOriginal*desc._textureDesc._width+xFromOriginal];
						REQUIRE(value == expectedValue);
					}
			}			
			{
				auto destaging = device.CreateResource(stagingDesc);
				unsigned destOffset[] = {32, 32, 0};
				unsigned srcLeftTopFront[] = {67, 133, 0};
				unsigned srcRightBottomBack[] = {324, 493, 1};
				blitEncoder.Copy(
					CopyPartial_Dest(*destaging, {}, destOffset),
					CopyPartial_Src{*deviceResource}.PartialSubresource(srcLeftTopFront, srcRightBottomBack, MakeTexturePitches(deviceResource->GetDesc()._textureDesc)));

				auto readback = destaging->ReadBackSynchronized(*threadContext);
				for (unsigned y=0; y<desc._textureDesc._height; ++y)
					for (unsigned x=0; x<desc._textureDesc._width; ++x) {
						if (x < destOffset[0] || y < destOffset[1]) continue;
						if (	(x - destOffset[0]) >= (srcRightBottomBack[0] - srcLeftTopFront[0])
							||  (y - destOffset[1]) >= (srcRightBottomBack[1] - srcLeftTopFront[1]))
							continue;
						unsigned xFromOriginal = (x-destOffset[0]) + srcLeftTopFront[0];
						unsigned yFromOriginal = (y-destOffset[1]) + srcLeftTopFront[1];
						auto value = readback[y*desc._textureDesc._width+x];
						auto expectedValue = initData[yFromOriginal*desc._textureDesc._width+xFromOriginal];
						REQUIRE(value == expectedValue);
					}
			}
		}

		SECTION("Multi-subresource copy")
		{
			auto desc = CreateDesc(BindFlag::ShaderResource, TextureDesc::Plain2D(227, 227, Format::R8_UNORM, 8), "test");
			auto stagingDesc = AsStagingDesc(desc);
			auto initData = CreateInitData(desc);
			
			auto deviceResource = device.CreateResource(desc); 
			auto staging = device.CreateResource(
				stagingDesc, 
				[&initData, &desc](const auto subres) -> SubResourceInitData {
					auto offset = GetSubResourceOffset(desc._textureDesc, subres._mip, subres._arrayLayer);
					return { MakeIteratorRange(initData.begin()+offset._offset, initData.begin()+offset._offset+offset._size), offset._pitches }; 
				});
			auto blitEncoder = metalContext.BeginBlitEncoder();
			blitEncoder.Copy(*deviceResource, *staging);

			{
				auto destaging = device.CreateResource(stagingDesc);
				blitEncoder.Copy(*destaging, *deviceResource);

				// ReadBackSynchronized only reads a single subresource, so we have to loop over them all
				for (unsigned mip=0; mip<desc._textureDesc._mipCount; ++mip) {
					auto readback = destaging->ReadBackSynchronized(*threadContext, {mip, 0});
					auto offset = GetSubResourceOffset(desc._textureDesc, mip, 0);
					REQUIRE(readback.size() == offset._size);
					for (auto i=readback.begin(), i2=initData.begin()+offset._offset; i!=readback.end(); ++i, ++i2)
						REQUIRE(*i == *i2);
				}
			}

			// copy out just a single subresource to a destaging "texture"
			{
				unsigned mipToGet = 3;
				auto singleMipDesc = stagingDesc;
				singleMipDesc._textureDesc = CalculateMipMapDesc(singleMipDesc._textureDesc, mipToGet);
				singleMipDesc._textureDesc._mipCount = 1;
				auto destaging = device.CreateResource(singleMipDesc);
				blitEncoder.Copy(
					CopyPartial_Dest{*destaging},
					CopyPartial_Src{*deviceResource}.SingleSubresource({mipToGet, 0}));

				auto readback = destaging->ReadBackSynchronized(*threadContext);
				auto srcInitDataOffset = GetSubResourceOffset(stagingDesc._textureDesc, mipToGet, 0);
				REQUIRE(readback.size() == srcInitDataOffset._size);
				for (unsigned y=0; y<singleMipDesc._textureDesc._height; ++y)
					for (unsigned x=0; x<singleMipDesc._textureDesc._width; ++x) {
						REQUIRE(readback[y*singleMipDesc._textureDesc._width+x] == initData[srcInitDataOffset._offset+y*singleMipDesc._textureDesc._width+x]);
					}
			}

			// copy out just a single subresource to a destaging "buffer"
			{
				unsigned mipToGet = 3;
				auto singleMipDesc = stagingDesc;
				auto singleMipTextDesc = CalculateMipMapDesc(singleMipDesc._textureDesc, mipToGet);
				singleMipTextDesc._mipCount = 1;
				singleMipDesc._type = ResourceDesc::Type::LinearBuffer;
				singleMipDesc._linearBufferDesc._sizeInBytes = ByteCount(singleMipTextDesc);
				auto destaging = device.CreateResource(singleMipDesc);
				blitEncoder.Copy(
					CopyPartial_Dest{*destaging},
					CopyPartial_Src{*deviceResource}.SingleSubresource({mipToGet, 0}));

				auto readback = destaging->ReadBackSynchronized(*threadContext);
				auto srcInitDataOffset = GetSubResourceOffset(stagingDesc._textureDesc, mipToGet, 0);
				REQUIRE(readback.size() == srcInitDataOffset._size);
				for (unsigned y=0; y<singleMipTextDesc._height; ++y)
					for (unsigned x=0; x<singleMipTextDesc._width; ++x) {
						REQUIRE(readback[y*singleMipTextDesc._width+x] == initData[srcInitDataOffset._offset+y*singleMipTextDesc._width+x]);
					}
			}

			// copy from a subresource in a texture to another subresource in a destaging texture
			{
				unsigned mipInDeviceResource = 2;
				auto singleMipDesc = CalculateMipMapDesc(desc._textureDesc, mipInDeviceResource);
				singleMipDesc._mipCount = 1;
				blitEncoder.Write(
					CopyPartial_Dest(*deviceResource, {mipInDeviceResource, 0}),
					SubResourceInitData{MakeIteratorRange(initData.begin(), initData.begin()+ByteCount(singleMipDesc))},
					desc._textureDesc._format, {singleMipDesc._width, singleMipDesc._height, 1}, MakeTexturePitches(singleMipDesc));
				
				auto destaging = device.CreateResource(
					AsStagingDesc(CreateDesc(0, CalculateMipMapDesc(desc._textureDesc, 1), "temp")));
				unsigned mipInDestaging = 1;
				blitEncoder.Copy(
					CopyPartial_Dest{*destaging, {mipInDestaging, 0}},
					CopyPartial_Src{*deviceResource}.SingleSubresource({mipInDeviceResource, 0}));

				auto readback = destaging->ReadBackSynchronized(*threadContext, {mipInDestaging, 0});
				REQUIRE(readback.size() == ByteCount(singleMipDesc));
				for (unsigned y=0; y<singleMipDesc._height; ++y)
					for (unsigned x=0; x<singleMipDesc._width; ++x) {
						REQUIRE(readback[y*singleMipDesc._width+x] == initData[y*singleMipDesc._width+x]);
					}
			}

			// copy from a subresource in a texture to just a buffer
			{
				unsigned mipInDeviceResource = 2;
				auto singleMipDesc = CalculateMipMapDesc(desc._textureDesc, mipInDeviceResource);
				singleMipDesc._mipCount = 1;
				blitEncoder.Write(
					CopyPartial_Dest(*deviceResource, {mipInDeviceResource, 0}),
					SubResourceInitData{MakeIteratorRange(initData.begin(), initData.begin()+ByteCount(singleMipDesc))},
					desc._textureDesc._format, {singleMipDesc._width, singleMipDesc._height, 1}, MakeTexturePitches(singleMipDesc));
				
				auto destaging = device.CreateResource(
					AsStagingDesc(CreateDesc(0, LinearBufferDesc{ByteCount(singleMipDesc)}, "temp")));
				blitEncoder.Copy(
					CopyPartial_Dest{*destaging},
					CopyPartial_Src{*deviceResource}.SingleSubresource({mipInDeviceResource, 0}));

				auto readback = destaging->ReadBackSynchronized(*threadContext);
				REQUIRE(readback.size() == ByteCount(singleMipDesc));
				for (unsigned y=0; y<singleMipDesc._height; ++y)
					for (unsigned x=0; x<singleMipDesc._width; ++x) {
						REQUIRE(readback[y*singleMipDesc._width+x] == initData[y*singleMipDesc._width+x]);
					}
			}
		}
	}

}


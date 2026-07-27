// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompiler.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/InitializerPack.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Math/SamplingUtil.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include "../../Math/XLEMath.h"
#include "../../Core/Exceptions.h"

#include "thousandeyes/futures/then.h"

#if XLE_COMPRESSONATOR_ENABLE
	#include "Compressonator.h"
#endif

#if ENABLE_DXTEX
	#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
		#include "../../OSServices/WinAPI/IncludeWindows.h"	// get in before DirectXTex includes it
	#endif
	#define DeleteFile DeleteFileA
	#include "../../Foreign/DirectXTex/DirectXTex/DirectXTex.h"
	#undef DeleteFile
#endif

using namespace Utility::Literals;

namespace RenderCore { namespace Assets
{
	::Assets::Blob PrepareDDSBlob(const TextureDesc& tDesc, size_t& headerSize);

#if XLE_COMPRESSONATOR_ENABLE
	static CMP_FORMAT AsCompressonatorFormat(Format fmt)
	{
		switch (fmt) {
		case Format::R32G32B32A32_FLOAT: return CMP_FORMAT_ARGB_32F;
		case Format::R32G32B32_FLOAT: return CMP_FORMAT_RGB_32F;

		case Format::R16G16B16A16_FLOAT: return CMP_FORMAT_ARGB_16F;
		case Format::R16G16B16A16_TYPELESS: return CMP_FORMAT_ARGB_16;
		case Format::R16G16B16A16_UNORM: return CMP_FORMAT_ARGB_16;

		case Format::R32G32_FLOAT: return CMP_FORMAT_RG_32F;

		case Format::R10G10B10A2_TYPELESS: return CMP_FORMAT_ARGB_2101010;
		case Format::R10G10B10A2_UNORM: return CMP_FORMAT_ARGB_2101010;

		case Format::R8G8B8A8_TYPELESS:
		case Format::R8G8B8A8_UNORM:
		case Format::R8G8B8A8_UNORM_SRGB: return CMP_FORMAT_ARGB_8888;
		case Format::R8G8B8A8_SNORM: return CMP_FORMAT_ARGB_8888_S;
		

		case Format::R16G16_FLOAT: return CMP_FORMAT_RG_16F;
		case Format::R16G16_TYPELESS: return CMP_FORMAT_RG_16;
		case Format::R16G16_UNORM: return CMP_FORMAT_RG_16;

		case Format::R32_FLOAT: return CMP_FORMAT_R_32F;

		case Format::R8G8_TYPELESS: return CMP_FORMAT_RG_8;
		case Format::R8G8_UNORM: return CMP_FORMAT_RG_8;
		case Format::R8G8_SNORM: return CMP_FORMAT_RG_8_S;

		case Format::R16_FLOAT: return CMP_FORMAT_R_16F;
		case Format::R16_TYPELESS: return CMP_FORMAT_R_16;
		case Format::R16_UNORM: return CMP_FORMAT_R_16;

		case Format::R8_TYPELESS: return CMP_FORMAT_R_8;
		case Format::R8_UNORM: return CMP_FORMAT_R_8;
		case Format::R8_SNORM: return CMP_FORMAT_R_8_S;

		case Format::B8G8R8A8_TYPELESS:
		case Format::B8G8R8A8_UNORM:
		case Format::B8G8R8A8_UNORM_SRGB: return CMP_FORMAT_BGRA_8888;

		case Format::R8G8B8_TYPELESS: return CMP_FORMAT_RGB_888;
		case Format::R8G8B8_UNORM: return CMP_FORMAT_RGB_888;
		case Format::R8G8B8_SNORM: return CMP_FORMAT_RGB_888_S;
		case Format::R8G8B8_UNORM_SRGB: return CMP_FORMAT_RGB_888;

		case Format::BC1_TYPELESS: return CMP_FORMAT_BC1;
		case Format::BC1_UNORM: return CMP_FORMAT_BC1;
		case Format::BC1_UNORM_SRGB: return CMP_FORMAT_BC1;
		case Format::BC2_TYPELESS: return CMP_FORMAT_BC2;
		case Format::BC2_UNORM: return CMP_FORMAT_BC2;
		case Format::BC2_UNORM_SRGB: return CMP_FORMAT_BC2;
		case Format::BC3_TYPELESS: return CMP_FORMAT_BC3;
		case Format::BC3_UNORM: return CMP_FORMAT_BC3;
		case Format::BC3_UNORM_SRGB: return CMP_FORMAT_BC3;
		case Format::BC4_UNORM: return CMP_FORMAT_BC4;
		case Format::BC4_SNORM: return CMP_FORMAT_BC4_S;
		case Format::BC5_UNORM: return CMP_FORMAT_BC5;
		case Format::BC5_SNORM: return CMP_FORMAT_BC5_S;
		case Format::BC6H_UF16: return CMP_FORMAT_BC6H;
		case Format::BC6H_SF16: return CMP_FORMAT_BC6H_SF;
		case Format::BC7_TYPELESS: return CMP_FORMAT_BC7;
		case Format::BC7_UNORM: return CMP_FORMAT_BC7;
		case Format::BC7_UNORM_SRGB: return CMP_FORMAT_BC7;

		case Format::RGB_ETC1_TYPELESS: return CMP_FORMAT_ETC_RGB;
		case Format::RGB_ETC1_UNORM: return CMP_FORMAT_ETC_RGB;
		case Format::RGB_ETC1_UNORM_SRGB: return CMP_FORMAT_ETC_RGB;
		case Format::RGB_ETC2_TYPELESS: return CMP_FORMAT_ETC2_RGB;
		case Format::RGB_ETC2_UNORM: return CMP_FORMAT_ETC2_RGB;
		case Format::RGB_ETC2_UNORM_SRGB: return CMP_FORMAT_ETC2_SRGB;
		case Format::RGBA_ETC2_TYPELESS: return CMP_FORMAT_ETC2_RGBA;
		case Format::RGBA_ETC2_UNORM: return CMP_FORMAT_ETC2_RGBA;
		case Format::RGBA_ETC2_UNORM_SRGB: return CMP_FORMAT_ETC2_SRGBA;
		case Format::RGBA1_ETC2_TYPELESS: return CMP_FORMAT_ETC2_RGBA1;
		case Format::RGBA1_ETC2_UNORM: return CMP_FORMAT_ETC2_RGBA1;
		case Format::RGBA1_ETC2_UNORM_SRGB: return CMP_FORMAT_ETC2_SRGBA1;

		default:
			return CMP_FORMAT_Unknown;
		}
	}

	RenderCore::TextureDesc BuildTextureDesc(const DirectX::TexMetadata& metadata);

	class CompressonatorTexture
	{
	public:
		CMP_Texture _srcTexture;
		TextureDesc _srcDesc;
		AlignedUniquePtr<uint8_t> _retainedData;

		CompressonatorTexture(BufferUploads::IAsyncDataSource& dataSrc)
		{
			XlZeroMemory(_srcTexture);

			auto descFuture = dataSrc.GetDesc();
			YieldToPool(descFuture);
			auto desc = descFuture.get();
			assert(desc._type == ResourceDesc::Type::Texture && desc._textureDesc._width >= 1 && desc._textureDesc._height >= 1);
			_srcDesc = desc._textureDesc;

			_srcTexture.dwSize     = sizeof(_srcTexture);
			_srcTexture.dwWidth    = desc._textureDesc._width;
			_srcTexture.dwHeight   = desc._textureDesc._height;
			_srcTexture.dwPitch    = 0;		// interpreted as packed
			_srcTexture.format     = AsCompressonatorFormat(desc._textureDesc._format);
			_srcTexture.dwDataSize = ByteCount(desc._textureDesc);
			_retainedData.reset((uint8_t*)XlMemAlign(_srcTexture.dwDataSize, 64));		// use a very large alignment, even if it's not specifically requested by compressonator
			_srcTexture.pData = _retainedData.get();

			auto mipCount = desc._textureDesc._mipCount;
			auto arrayLayerCount = ActualArrayLayerCount(desc._textureDesc);
			VLA_UNSAFE_FORCE(BufferUploads::IAsyncDataSource::SubResource, subres, mipCount*arrayLayerCount);
			for (unsigned a=0; a<arrayLayerCount; ++a)
				for (unsigned m=0; m<mipCount; ++m) {
					auto& sr = subres[m+a*mipCount];
					auto srcOffset = GetSubResourceOffset(desc._textureDesc, m, a);
					sr._id = SubResourceId{m, a};
					sr._destination = {PtrAdd(_srcTexture.pData, srcOffset._offset), PtrAdd(_srcTexture.pData, srcOffset._offset+srcOffset._size)};
					sr._pitches = srcOffset._pitches;
				}

			auto dataFuture = dataSrc.PrepareData(MakeIteratorRange(subres, &subres[mipCount*arrayLayerCount]));
			YieldToPool(dataFuture);
			dataFuture.get();

			// as per compressonator example, swizzle BGRA types
			if (_srcTexture.format == CMP_FORMAT_BGRA_8888) {
				unsigned char blue;
				for (CMP_DWORD i = 0; i < _srcTexture.dwDataSize; i += 4)
				{
					blue = _srcTexture.pData[i];
					_srcTexture.pData[i] = _srcTexture.pData[i + 2];
					_srcTexture.pData[i + 2] = blue;
				}
				_srcTexture.format = CMP_FORMAT_RGBA_8888;
			}
		}

		#if ENABLE_DXTEX
			CompressonatorTexture(const DirectX::ScratchImage& scratchImage)		// scratchImage must outlive this
			{
				XlZeroMemory(_srcTexture);
				_srcDesc = BuildTextureDesc(scratchImage.GetMetadata());

				_srcTexture.dwSize     = sizeof(_srcTexture);
				_srcTexture.dwWidth    = _srcDesc._width;
				_srcTexture.dwHeight   = _srcDesc._height;
				_srcTexture.dwPitch    = 0;		// interpreted as packed
				_srcTexture.format     = AsCompressonatorFormat(_srcDesc._format);
				_srcTexture.dwDataSize = ByteCount(_srcDesc);
				_srcTexture.pData      = scratchImage.GetPixels();
			}
		#endif

		CompressonatorTexture()
		{
			XlZeroMemory(_srcTexture);
		}

		~CompressonatorTexture() {}
		CompressonatorTexture(CompressonatorTexture&&) = default;
		CompressonatorTexture& operator=(CompressonatorTexture&&) = default;
	};

	::Assets::Blob ConvertAndPrepareDDSBlobSync(
		const CompressonatorTexture& input,
		Format dstFmt)
	{

		auto dstDesc = input._srcDesc;
		dstDesc._format = dstFmt;
		size_t ddsHeaderOffset = 0;
		auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

		if (input._srcDesc._format == dstDesc._format) {
			// no format conversion -- copy directly into the output dds
			if (destinationBlob->size() != (ddsHeaderOffset + input._srcTexture.dwDataSize))
				Throw(std::runtime_error("Texture conversion failed because of size mismatch"));
			std::memcpy(PtrAdd(destinationBlob->data(), ddsHeaderOffset), input._srcTexture.pData, input._srcTexture.dwDataSize);
			return destinationBlob;
		}

		// Compressonator doesn't like doing simple conversions (float -> unorm, etc). It'll return CMP_OK indicating success,
		// but will have written nothing to the output. It's a bit frustrating, because there's no indications of what conversions
		// it's going to ignore
		if (GetCompressionType(input._srcDesc._format) == FormatCompressionType::None && GetCompressionType(dstDesc._format) == FormatCompressionType::None) {
			if (GetComponentType(input._srcDesc._format) == FormatComponentType::Float && (GetComponentType(dstDesc._format) == FormatComponentType::UNorm || GetComponentType(dstDesc._format) == FormatComponentType::SNorm || GetComponentType(dstDesc._format) == FormatComponentType::UInt || GetComponentType(dstDesc._format) == FormatComponentType::SInt)) {

				// simple but common conversion -- Float to S/UNorm or S/UInt format
				unsigned sourceComponentCount = GetComponentCount(GetComponents(input._srcDesc._format));
				unsigned dstComponentCount = GetComponentCount(GetComponents(dstDesc._format));
				auto dstComponentType = GetComponentType(dstDesc._format);
				auto dstComponentPrecision = GetComponentPrecision(dstDesc._format);
				auto srcStride = BitsPerPixel(input._srcDesc._format)/8;
				auto dstStride = BitsPerPixel(dstDesc._format)/8;
				void* dstStart = PtrAdd(destinationBlob->data(), ddsHeaderOffset);
				unsigned pixelCount = dstDesc._width*dstDesc._height;

				unsigned component=0;
				for (; component<std::min(sourceComponentCount, dstComponentCount); ++component) {

					// Note UNorm/SNorm conversion rules
					// eg: (https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion)
					// See also VertexUtil.h

					auto* src = (float*)PtrAdd(input._srcTexture.pData, sizeof(float)*component);
					if (dstComponentType == FormatComponentType::UNorm && dstComponentPrecision == 8) {				// UNorm8
						auto* dst = (uint8_t*)PtrAdd(dstStart, sizeof(uint8_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (uint8_t)Clamp<int>(0xff * *src, 0, 0xff);
					} else if (dstComponentType == FormatComponentType::SNorm && dstComponentPrecision == 8) {		// SNorm8
						auto* dst = (int8_t*)PtrAdd(dstStart, sizeof(int8_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (int8_t)Clamp<int>(0x7f * *src, -0x7f, 0x7f);

					} else if (dstComponentType == FormatComponentType::UNorm && dstComponentPrecision == 16) {		// UNorm16
						auto* dst = (uint16_t*)PtrAdd(dstStart, sizeof(uint16_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (uint16_t)Clamp<int>(0xffff * *src, 0, 0xffff);
					} else if (dstComponentType == FormatComponentType::SNorm && dstComponentPrecision == 16) {		// SNorm16
						auto* dst = (int16_t*)PtrAdd(dstStart, sizeof(int16_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (int16_t)Clamp<int>(0x7fff * *src, -0x7fff, 0x7fff);

					} else if (dstComponentType == FormatComponentType::UInt && dstComponentPrecision == 8) {		// UInt8
						auto* dst = (uint8_t*)PtrAdd(dstStart, sizeof(uint8_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (uint8_t)Clamp<int>(*src, 0, 0xff);
					} else if (dstComponentType == FormatComponentType::UInt && dstComponentPrecision == 8) {		// SInt8
						auto* dst = (int8_t*)PtrAdd(dstStart, sizeof(int8_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (int8_t)Clamp<int>(*src, -0x80, 0x7f);

					} else if (dstComponentType == FormatComponentType::UInt && dstComponentPrecision == 16) {		// UInt16
						auto* dst = (uint16_t*)PtrAdd(dstStart, sizeof(uint16_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (uint16_t)Clamp<int>(*src, 0, 0xffff);
					} else if (dstComponentType == FormatComponentType::UInt && dstComponentPrecision == 16) {		// SInt16
						auto* dst = (int16_t*)PtrAdd(dstStart, sizeof(int16_t)*component);
						for (unsigned c=0; c<pixelCount; ++c, src=PtrAdd(src, srcStride), dst=PtrAdd(dst, dstStride))
							*dst = (int16_t)Clamp<int>(*src, -0x8000, 0x7fff);
					} else
						assert(0);
				}

				for (; component<dstComponentCount; ++component) {
					if ((dstComponentType == FormatComponentType::UNorm||dstComponentType == FormatComponentType::UInt) && dstComponentPrecision == 8) {				// UNorm8
						auto* dst = (uint8_t*)PtrAdd(dstStart, sizeof(uint8_t)*component);
						uint8_t fill = (component == 3)?0xff:0x0;
						for (unsigned c=0; c<pixelCount; ++c,  dst=PtrAdd(dst, dstStride)) *dst = fill;
					} else if ((dstComponentType == FormatComponentType::SNorm || dstComponentType == FormatComponentType::SInt) && dstComponentPrecision == 8) {		// SNorm8
						auto* dst = (int8_t*)PtrAdd(dstStart, sizeof(int8_t)*component);
						int8_t fill = (component == 3)?0x7f:0x0;
						for (unsigned c=0; c<pixelCount; ++c, dst=PtrAdd(dst, dstStride)) *dst = fill;

					} else if ((dstComponentType == FormatComponentType::UNorm||dstComponentType == FormatComponentType::UInt) && dstComponentPrecision == 16) {		// UNorm16
						auto* dst = (uint16_t*)PtrAdd(dstStart, sizeof(uint16_t)*component);
						uint16_t fill = (component == 3)?0xffff:0x0;
						for (unsigned c=0; c<pixelCount; ++c, dst=PtrAdd(dst, dstStride)) *dst = fill;
					} else if ((dstComponentType == FormatComponentType::SNorm || dstComponentType == FormatComponentType::SInt) && dstComponentPrecision == 16) {		// SNorm16
						auto* dst = (int16_t*)PtrAdd(dstStart, sizeof(int16_t)*component);
						int16_t fill = (component == 3)?0x7fff:0x0;
						for (unsigned c=0; c<pixelCount; ++c, dst=PtrAdd(dst, dstStride)) *dst = fill;

					} else
						assert(0);
				}

				return destinationBlob;
			}

			Log(Warning) << "Using compressonator to convert between non-compressed formats (" << AsString(input._srcDesc._format) << " to " << AsString(dstDesc._format) << "). This can be unreliable and will sometimes silently fail." << std::endl;
		}

		if (input._srcTexture.format == CMP_FORMAT_Unknown)
			Throw(std::runtime_error(Concatenate("Cannot initialize src texture for format conversion, because source format is not supported: ", AsString(input._srcDesc._format))));

		CMP_CompressOptions options = {0};
		options.dwSize       = sizeof(options);
		options.fquality     = 0.05f;
		// Compressonator seems to have an issue when dwnumThreads is set to 1 (other than running slow). It appears to spin up threads it can never close down
		// let's just set it to "auto" to allow it to adapt to the processor (even if it squeezes our thread pool)
		options.dwnumThreads = 0;
		auto comprDstFormat = AsCompressonatorFormat(dstFmt);
		if (comprDstFormat == CMP_FORMAT_Unknown)
			Throw(std::runtime_error(Concatenate("Cannot write to the request texture pixel format because it is not supported by the compression library: ", AsString(dstFmt))));

		// simple hack because we can't enter Compressonator while it's working
		static Threading::Mutex s_compressonatorLock;
		std::unique_lock l(s_compressonatorLock, std::defer_lock);
		while (!l.try_lock())
			YieldToPoolFor(std::chrono::milliseconds(10));

		auto mipCount = dstDesc._mipCount;
		auto arrayLayerCount = ActualArrayLayerCount(dstDesc);
		for (unsigned a=0; a<arrayLayerCount; ++a)
			for (unsigned m=0; m<mipCount; ++m) {
				auto dstOffset = GetSubResourceOffset(dstDesc, m, a);
				auto dstMipDesc = CalculateMipMapDesc(dstDesc, m);
				auto srcMipDesc = CalculateMipMapDesc(input._srcDesc, m);

				CMP_Texture destTexture = {0};
				destTexture.dwSize     = sizeof(destTexture);
				destTexture.dwWidth    = std::max(1u, (unsigned)srcMipDesc._width);
				destTexture.dwHeight   = std::max(1u, (unsigned)srcMipDesc._height);
				destTexture.dwPitch    = 0;
				destTexture.format     = comprDstFormat;
				destTexture.dwDataSize = (CMP_DWORD)dstOffset._size;
				auto calcSize = CMP_CalculateBufferSize(&destTexture);
				assert(destTexture.dwDataSize == calcSize);
				destTexture.pData = (CMP_BYTE*)PtrAdd(destinationBlob->data(), ddsHeaderOffset + dstOffset._offset);
				assert(PtrAdd(destTexture.pData, destTexture.dwDataSize) <= AsPointer(destinationBlob->end()));

				auto srcOffset = GetSubResourceOffset(input._srcDesc, m, a);
				auto srcTexture = input._srcTexture;
				srcTexture.dwWidth = destTexture.dwWidth;
				srcTexture.dwHeight = destTexture.dwHeight;
				srcTexture.dwDataSize = (CMP_DWORD)srcOffset._size;
				srcTexture.pData = PtrAdd(srcTexture.pData, srcOffset._offset);

				CMP_ERROR cmp_status;
				cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, nullptr);
				if (cmp_status != CMP_OK)
					Throw(std::runtime_error("Compression library failed while processing texture compiler file"));
			}

		l.unlock();

		return destinationBlob;
	}

	::Assets::Blob ConvertAndPrepareDDSBlobSync(
		BufferUploads::IAsyncDataSource& srcPkt,
		Format dstFmt)
	{
		CompressonatorTexture input{srcPkt};
		return ConvertAndPrepareDDSBlobSync(input, dstFmt);
	}
#endif

#if ENABLE_DXTEX
	std::optional<DirectX::ScratchImage> BuildMipmaps(BufferUploads::IAsyncDataSource& srcPkt)
	{
		auto futureDesc = srcPkt.GetDesc();
		YieldToPool(futureDesc);
		auto inputDesc = futureDesc.get();
		assert(inputDesc._type == ResourceDesc::Type::Texture);
		assert(inputDesc._textureDesc._arrayCount <= 1);			// not supporting arrayed textures
		if (inputDesc._textureDesc._mipCount > 1) return {};		// already got mipmaps
		
		std::vector<uint8_t> tempBuffer;
		tempBuffer.resize(ByteCount(inputDesc));
		BufferUploads::IAsyncDataSource::SubResource sr;
		sr._destination = MakeIteratorRange(tempBuffer);
		sr._id = {0,0};
		sr._pitches = MakeTexturePitches(inputDesc._textureDesc);
		auto futureData = srcPkt.PrepareData(MakeIteratorRange(&sr, &sr+1));
		YieldToPool(futureData);
		futureData.get();

		// Build mips needs to know if we're dealing with SRGB or linear data... If it's typeless, we have to assume SRGB, given that we have no specific direction on this
		// todo -- we may have to make this a flag in the PostConvert object -- particularly given that normal maps may come through this path
		auto fmt = inputDesc._textureDesc._format;
		if (GetComponentType(fmt) == FormatComponentType::Typeless) fmt = AsSRGBFormat(fmt);
		if (GetComponentType(fmt) == FormatComponentType::Typeless) fmt = AsLinearFormat(fmt);

		DirectX::Image inputImage;
		inputImage.width = inputDesc._textureDesc._width;
		inputImage.height = inputDesc._textureDesc._height;
		inputImage.format = (DXGI_FORMAT)fmt;
		inputImage.rowPitch = sr._pitches._rowPitch;
		inputImage.slicePitch = sr._pitches._slicePitch;
		inputImage.pixels = tempBuffer.data();

		DirectX::ScratchImage newImage;
		auto mipmapHresult = GenerateMipMaps(inputImage, DirectX::TEX_FILTER_DEFAULT, 0, newImage);
		if (!SUCCEEDED(mipmapHresult))
			Throw(std::runtime_error("Failed while building mip-maps in PostConvert"));

		return newImage;
	}
#endif

	::Assets::Blob PrepareDDSBlobSyncWithoutConvert(
		BufferUploads::IAsyncDataSource& srcPkt)
	{
		auto descFuture = srcPkt.GetDesc();
		YieldToPool(descFuture);
		auto desc = descFuture.get();
		auto srcSize = ByteCount(desc);
		assert(desc._type == ResourceDesc::Type::Texture && desc._textureDesc._width >= 1 && desc._textureDesc._height >= 1);
		auto dstDesc = desc._textureDesc;

		AlignedUniquePtr<uint8_t> data { (uint8_t*)XlMemAlign(srcSize, 64) };

		auto mipCount = dstDesc._mipCount;
		auto arrayLayerCount = ActualArrayLayerCount(dstDesc);
		VLA_UNSAFE_FORCE(BufferUploads::IAsyncDataSource::SubResource, subres, mipCount*arrayLayerCount);
		for (unsigned a=0; a<arrayLayerCount; ++a)
			for (unsigned m=0; m<mipCount; ++m) {
				auto& sr = subres[m+a*mipCount];
				auto srcOffset = GetSubResourceOffset(dstDesc, m, a);
				sr._id = SubResourceId{m, a};
				sr._destination = {PtrAdd(data.get(), srcOffset._offset), PtrAdd(data.get(), srcOffset._offset+srcOffset._size)};
				sr._pitches = srcOffset._pitches;
			}

		auto dataFuture = srcPkt.PrepareData(MakeIteratorRange(subres, &subres[mipCount*arrayLayerCount]));
		YieldToPool(dataFuture);
		dataFuture.get();

		size_t ddsHeaderOffset = 0;
		auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

		// copy directly into the output dds
		if (destinationBlob->size() != (ddsHeaderOffset + srcSize))
			Throw(std::runtime_error("Texture conversion failed because of size mismatch"));
		std::memcpy(PtrAdd(destinationBlob->data(), ddsHeaderOffset), data.get(), srcSize);

		return destinationBlob;
	}	

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class BalancedNoiseTexture : public BufferUploads::IAsyncDataSource
	{
	public:
		virtual std::future<ResourceDesc> GetDesc() override
		{
			std::promise<ResourceDesc> promise;
			promise.set_value(CreateDesc(0, TextureDesc::Plain2D(_width, _height, Format::R32_FLOAT)));
			return promise.get_future();
		}

		virtual StringSection<> GetName() const override { return "balanced-noise"; }

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			assert(subResources.size() == 1);
			assert(subResources[0]._destination.size() == sizeof(float)*_width*_height);
			float* dst = (float*)subResources[0]._destination.begin();
			std::memset(dst, 0xff, subResources[0]._destination.size());		// intentionially writing nans

			// as long as width is an integer cubed and height is an integer squared, we'll get a pattern that visits every pixel
			unsigned subTableWidth = 3, subTableHeight = 2;
			unsigned i = 1;
			while (subTableWidth < _width) { ++i; subTableWidth = std::pow(3.f, i); }
			i = 1;
			while (subTableHeight < _height) { ++i; subTableHeight = std::pow(2.f, i); }

			// We can do this in a smarter way by using the inverse-radical-inverse, and solving some simultaneous
			// equations with modular arithmetic. But since we're building a lookup table anyway, that doesn't seem
			// of any practical purpose
			using namespace XLEMath;
			for (unsigned sampleIdx=0; sampleIdx<subTableWidth*subTableHeight; ++sampleIdx) {
				// "extraScambling" reduces the inherent pattern -- but also adds precision errors, sometimes resulting in pixels not being visited
				const bool extraScambling = false;
				if (extraScambling) {
					auto x = unsigned(subTableWidth * CalculateScrambledHaltonNumber<1>(sampleIdx)), 
						y = unsigned(subTableHeight * CalculateScrambledHaltonNumber<0>(sampleIdx));
					if (x < _width && y < _height)
						dst[x+y*_width] = sampleIdx / float(subTableWidth*subTableHeight);
				} else {
					auto x = unsigned(subTableWidth * CalculateHaltonNumber<3>(sampleIdx)), 
						y = unsigned(subTableHeight * CalculateHaltonNumber<2>(sampleIdx));
					if (x < _width && y < _height)
						dst[x+y*_width] = sampleIdx / float(subTableWidth*subTableHeight);
				}
			}

			for (unsigned c=0; c<_width*_height; ++c)
				assert(((const unsigned*)dst)[c] != 0xffffffffu);			// Inaccuracies in the calculation can result in texels not getting touched for larger textures. Generally it's best to write to a fairly small texture 

			// We can shuffle the rows to add more randomness. The end result is less uniformly distributed, but also has 
			// fewer repeating patterns (since there is a slight pattern to the Halton sampler output)
			// which is better may depend on the application
			std::mt19937_64 rng(153483181236ull);
			for (unsigned y=0; y<_height; ++y)
				std::shuffle(&dst[y*_width], &dst[y*_width+_width], rng);

			std::promise<void> promise;
			promise.set_value();
			return promise.get_future();
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return {};
		}

		unsigned _width, _height;
		BalancedNoiseTexture(unsigned width, unsigned height) : _width(width), _height(height) {}
	};

	class Compiler_BalancedNoise : public ITextureCompiler
	{
	public:
		unsigned _width = 512, _height = 512;
		std::string GetIntermediateName() const override { return (StringMeld<128>() << "balanced-noise-" << _width << "x" << _height).AsString(); }
		std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& context) override { return std::make_shared<BalancedNoiseTexture>(_width, _height); }

		Compiler_BalancedNoise(Formatters::TextInputFormatter<>& fmttr)
		{
			StringSection<> kn;
			while (fmttr.TryKeyedItem(kn)) {
				if (XlEqString(kn, "Width")) _width = Formatters::RequireCastValue<decltype(_width)>(fmttr);
				else if (XlEqString(kn, "Height")) _height = Formatters::RequireCastValue<decltype(_height)>(fmttr);
				else Formatters::SkipValueOrElement(fmttr);
			}
		}
		Compiler_BalancedNoise(unsigned width, unsigned height) : _width(width), _height(height) {}
	};

	template<typename T> decltype(std::declval<T&&>().get()) YieldToPoolAndGet(T&& future) { YieldToPool(future); return future.get(); }

	std::shared_ptr<ITextureCompiler> TextureCompiler_Base(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		auto scaffold = indexer._scaffold.get();

		if (scaffold->HasComponent(indexer._entityNameHash, "BalancedNoise"_h))
			return YieldToPoolAndGet(util->GetFuture<std::shared_ptr<Compiler_BalancedNoise>>("BalancedNoise"_h, indexer)).get();
		
		return nullptr;
	}

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_BalancedNoise(unsigned width, unsigned height)
	{
		return std::make_shared<Compiler_BalancedNoise>(width, height);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void DeserializationOperator(Formatters::TextInputFormatter<>& fmttr, TextureCompilerSource& dst)
	{
		StringSection<> kn;
		while (fmttr.TryKeyedItem(kn)) {
			if (XlEqString(kn, "SourceFile")) {
				dst._srcFile = Formatters::RequireStringValue(fmttr);
				#if defined(_DEBUG)
					if (!MakeFileNameSplitter(dst._srcFile).ParametersWithDivider().IsEmpty())
						Throw(std::runtime_error("Don't include parameters on SourceFile request: " + dst._srcFile + ". These aren't interpreted here."));
				#endif
			} else
				Formatters::SkipValueOrElement(fmttr);
		}
	}

	void DeserializationOperator(Formatters::TextInputFormatter<>& fmttr, PostConvert& dst)
	{
		StringSection<> kn;
		while (fmttr.TryKeyedItem(kn)) {
			if (XlEqString(kn, "Format")) {
				auto mode = Formatters::RequireStringValue(fmttr);
				if (auto fmtOpt = AsFormat(mode)) dst._format = *fmtOpt;
				else Throw(Formatters::FormatException("Unknown 'Format' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
			} else if (XlEqString(kn, "BuildMipmaps")) {
				dst._buildMipmaps = Formatters::RequireCastValue<bool>(fmttr);
			} else Formatters::SkipValueOrElement(fmttr);
		}
	}

	static_assert(::Assets::Internal::AssetMixinTraits<PostConvert>::HasDeserializationOperatorFromFormatter);

	class TextureCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		::Assets::PortableVector<TargetDesc> GetTargets() const
		{
			if (_serializedArtifacts.empty()) return {};
			return {
				TargetDesc { TextureCompilerProcessType, _serializedArtifacts[0]._name.c_str() }
			};
		}
		::Assets::SerializedTarget	SerializeTarget(unsigned idx)
		{
			assert(idx == 0);
			return { _serializedArtifacts };
		}
		::Assets::DependencyValidation GetDependencyValidation() const
		{
			std::vector<::Assets::DependencyValidationMarker> markers;
			markers.insert(markers.end(), _dependencies.begin(), _dependencies.end());
			return ::Assets::GetDepValSys().MakeOrReuse(markers);
		}

		void Initialize(ITextureCompiler& compiler, ::Assets::OperationContextHelper& opHelper, const VariantFunctions& conduit)
		{
			ITextureCompiler::Context ctx { &opHelper, &conduit };
			auto pkt = compiler.ExecuteCompile(ctx);
			auto blob = PrepareDDSBlobSyncWithoutConvert(*pkt);

			_serializedArtifacts.emplace_back(TextureCompilerProcessType, 0, ".dds", blob);
			_dependencies.insert(_dependencies.end(), ctx._dependencies.begin(), ctx._dependencies.end());
			_dependencies.push_back(pkt->GetDependencyValidation());
		}

		void Initialize(ITextureCompiler& compiler, const PostConvert& postConvert, ::Assets::OperationContextHelper& opHelper, const VariantFunctions& conduit)
		{
			assert(postConvert._format != Format::Unknown);
			ITextureCompiler::Context ctx { &opHelper, &conduit };
			auto pkt = compiler.ExecuteCompile(ctx);
			#if XLE_COMPRESSONATOR_ENABLE
				if (opHelper)
					opHelper.SetMessage(Concatenate("Compressing to pixel format ", AsString(postConvert._format)));
				::Assets::Blob blob;
				if (postConvert._buildMipmaps) {
					#if ENABLE_DXTEX
						if (auto mipped = BuildMipmaps(*pkt)) {
							blob = ConvertAndPrepareDDSBlobSync(CompressonatorTexture{*mipped}, postConvert._format);
						} else
							blob = ConvertAndPrepareDDSBlobSync(*pkt, postConvert._format);
					#else
						assert(0);
					#endif
				} else
					blob = ConvertAndPrepareDDSBlobSync(*pkt, postConvert._format);
			#else
				assert(!postConvert._buildMipmaps);
				auto blob = PrepareDDSBlobSyncWithoutConvert(*pkt);
			#endif

			_serializedArtifacts.emplace_back(TextureCompilerProcessType, 0, ".dds", blob);
			_dependencies.insert(_dependencies.end(), ctx._dependencies.begin(), ctx._dependencies.end());
			_dependencies.push_back(pkt->GetDependencyValidation());
		}

		TextureCompileOperation(
			const TextureCompilationRequest& req,
			::Assets::OperationContextHelper&& opHelper, const VariantFunctions& conduit)
		{
			assert(req._subCompiler);
			if (req._postConvert) {
				Initialize(*req._subCompiler, *req._postConvert, opHelper, conduit);
			} else {
				Initialize(*req._subCompiler, opHelper, conduit);
			}
		}

	private:
		std::vector<::Assets::DependencyValidation> _dependencies;
		::Assets::DependencyValidation _cfgFileDepVal;
		::Assets::PortableVector<::Assets::SerializedArtifact> _serializedArtifacts;
	};

	::Assets::CompilerRegistration RegisterTextureCompilerInfrastructure(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"texture-compiler",
			"texture-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](const ::Assets::InitializerPack& initializers, auto&& operationContextHelper, const auto& conduit) {
				return std::make_shared<TextureCompileOperation>(initializers.GetInitializer<TextureCompilationRequest>(0), std::move(operationContextHelper), conduit);
			}};

		uint64_t outputAssetTypes[] = { TextureCompilerProcessType };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		intermediateCompilers.AssociateExtensions(
			result.RegistrationId(),
			"texture");
		return result;
	}

	TextureCompilationRequest MakeTextureCompilationRequestSync(
		TextureCompilerRegistrar& registrar,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		TextureCompilationRequest result;
		result._subCompiler = registrar.TryBeginCompile(util, indexer);
		if (!result._subCompiler) {
			assert(0);		// note that we can hit this during shutdown, because a pending texture compile require can still be queued while the texture compiler is deregistered
			return {};		// invalid compile
		}

		result._intermediateName = result._subCompiler->GetIntermediateName();

		if (indexer._scaffold.get()->HasComponent(indexer._entityNameHash, "PostConvert"_h)) {
			result._postConvert = YieldToPoolAndGet(util->GetFuture<PostConvert>("PostConvert"_h, indexer));
			result._intermediateName = Concatenate(result._intermediateName, "-", AsString(result._postConvert->_format), result._postConvert->_buildMipmaps?"-m":"");
		}

		return result;
	}

	TextureCompilationRequest MakeTextureCompilationRequest(std::shared_ptr<Assets::ITextureCompiler> subCompiler, Format fmt)
	{
		assert(subCompiler);
		TextureCompilationRequest result;
		result._subCompiler = std::move(subCompiler);
		result._intermediateName = Concatenate(result._subCompiler->GetIntermediateName(), "-", AsString(fmt));
		result._postConvert = PostConvert{fmt};
		return result;
	}

	TextureCompilationRequest MakeTextureCompilationRequest(std::shared_ptr<Assets::ITextureCompiler> subCompiler)
	{
		assert(subCompiler);
		TextureCompilationRequest result;
		result._subCompiler = std::move(subCompiler);
		result._intermediateName = result._subCompiler->GetIntermediateName();
		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TextureArtifact::TextureArtifact(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		_artifactFile = chunks[0]._artifactFilename;
	}
	TextureArtifact::TextureArtifact(std::string file) : _artifactFile(file)
	{
		StringSection<> fns[] { MakeStringSection(file) };
		_depVal = ::Assets::GetDepValSys().Make(fns);
	}
	TextureArtifact::TextureArtifact() = default;
	TextureArtifact::~TextureArtifact() = default;
	TextureArtifact::TextureArtifact(TextureArtifact&&) = default;
	TextureArtifact& TextureArtifact::operator=(TextureArtifact&&) = default;
	TextureArtifact::TextureArtifact(const TextureArtifact&) = default;
	TextureArtifact& TextureArtifact::operator=(const TextureArtifact&) = default;

	const ::Assets::ArtifactRequest TextureArtifact::ChunkRequests[1] {
		::Assets::ArtifactRequest{ "main", RenderCore::Assets::TextureCompilerProcessType, 0, ::Assets::ArtifactRequest::DataType::Filename }
	};

	void TextureArtifact::ConstructToPromise(
		std::promise<std::shared_ptr<TextureArtifact>>&& promise,
		const TextureCompilationRequest& request)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[request, promise=std::move(promise)]() mutable {
				TRY {
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{request});
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void TextureArtifact::ConstructToPromise(
		std::promise<std::shared_ptr<TextureArtifact>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		const TextureCompilationRequest& request)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[request, promise=std::move(promise), opContext=std::move(opContext)]() mutable {
				TRY {
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{request}, opContext.get());
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void TextureArtifact::ConstructToPromise(
		std::promise<std::shared_ptr<TextureArtifact>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		const TextureCompilationRequest& request,
		ProgressiveResultFn&& intermediateResultFn)
	{
		if (!intermediateResultFn) {
			ConstructToPromise(std::move(promise), std::move(opContext), request);
			return;
		}
		VariantFunctions conduit;
		conduit.Add(0, std::move(intermediateResultFn));
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[request, promise=std::move(promise), opContext=std::move(opContext), conduit=std::move(conduit)]() mutable {
				TRY {
					::Assets::DefaultCompilerConstructionSynchronously(
						std::move(promise), TextureCompilerProcessType,
						::Assets::InitializerPack{request},
						std::move(conduit),
						opContext.get());
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	auto TextureCompilerRegistrar::Register(std::function<SubCompilerFunctionSig>&& sig) -> RegistrationId
	{
		ScopedModifyLock(_readWriteLock);
		auto result = ++_nextRegistrationId;
		_fns.emplace_back(result, std::move(sig));
		return result;
	}

	void TextureCompilerRegistrar::Deregister(RegistrationId id)
	{
		ScopedModifyLock(_readWriteLock);
		_fns.erase(
			std::remove_if(_fns.begin(), _fns.end(), [id](const auto& q) { return q.first == id; }),
			_fns.end());
	}

	std::shared_ptr<ITextureCompiler> TextureCompilerRegistrar::TryBeginCompile(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		ScopedReadLock(_readWriteLock);
		for (const auto& f:_fns)
			if (auto compiler = f.second(util, indexer))
				return compiler;
		return nullptr;
	}

	TextureCompilerRegistrar::TextureCompilerRegistrar() { _nextRegistrationId = 1; }
	TextureCompilerRegistrar::~TextureCompilerRegistrar() {}

	ITextureCompiler::~ITextureCompiler() = default;

}}


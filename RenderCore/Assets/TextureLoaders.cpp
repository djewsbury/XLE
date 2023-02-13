// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureLoaders.h"
#include "../ResourceDesc.h"
#include "../Format.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/DepVal.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/RawFS.h"
#include "../../OSServices/Log.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Threading/CompletionThreadPool.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/FastParseValue.h"
#include <memory>

#if ENABLE_DXTEX
	#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
		#include "../../OSServices/WinAPI/IncludeWindows.h"	// get in before DirectXTex includes it
	#endif
	#define DeleteFile DeleteFileA
	#include "../../Foreign/DirectXTex/DirectXTex/DirectXTex.h"
	#include "../../Foreign/DirectXTex/DirectXTex/DDS.h"
	#include "../../Foreign/DirectXTex/DirectXTex/DirectXTexP.h"
	#undef DeleteFile
#endif

namespace RenderCore { namespace Assets
{

	static RenderCore::TextureDesc BuildTextureDesc(const DirectX::TexMetadata& metadata)
	{
		using namespace DirectX;
		RenderCore::TextureDesc desc = RenderCore::TextureDesc::Empty();
		
		desc._width = uint32_t(metadata.width);
		desc._height = uint32_t(metadata.height);
		desc._depth = uint32_t(metadata.depth);
		// There's no explicit "array" flag on the input; so we'll consider anything with 1 2d texture
		// as non-array. For a cubemap we'll never set array count to 0 to avoid confusion with a non-array 2d texture
		if (metadata.miscFlags & TEX_MISC_TEXTURECUBE) {
			desc._arrayCount = (metadata.arraySize == 6u) ? 0u : metadata.arraySize;
		} else
			desc._arrayCount = (metadata.arraySize > 1) ? uint16_t(metadata.arraySize) : 0u;
		desc._mipCount = uint8_t(metadata.mipLevels);
		desc._samples = TextureSamples::Create();

			// we need to use a "typeless" format for any pixel formats that can
			// cast to to SRGB or linear versions. This allows the caller to use
			// both SRGB and linear ShaderResourceView(s).
			// But, we don't want to do this for all formats that can become typeless
			// because we need to retain that information on the resource. For example,
			// if we made R32_** formats typeless here, when we create the shader resource
			// view there would be no way to know if it was originally a R32_FLOAT, or a R32_UINT (etc)
		auto srcFormat = (RenderCore::Format)metadata.format;
		if (RenderCore::HasLinearAndSRGBFormats(srcFormat)) {
			desc._format = RenderCore::AsTypelessFormat(srcFormat);
		} else {
			desc._format = srcFormat;
		}

		switch (metadata.dimension) {
		case TEX_DIMENSION_TEXTURE1D: desc._dimensionality = TextureDesc::Dimensionality::T1D; break;
		default:
		case TEX_DIMENSION_TEXTURE2D: 
			if (metadata.miscFlags & TEX_MISC_TEXTURECUBE)
				desc._dimensionality = TextureDesc::Dimensionality::CubeMap; 
			else
				desc._dimensionality = TextureDesc::Dimensionality::T2D; 
			break;
		case TEX_DIMENSION_TEXTURE3D: desc._dimensionality = TextureDesc::Dimensionality::T3D; break;
		}
		if (metadata.IsCubemap())
			desc._dimensionality = TextureDesc::Dimensionality::CubeMap;

		if (desc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
			assert(ActualArrayLayerCount(desc) == 6u);		// arrays of cubemaps not supported; we consider this to be the face count
		}

		return desc;
	}

	static DirectX::TexMetadata BuildTexMetadata(const TextureDesc& srcDesc)
	{
		DirectX::TexMetadata result;
		result.width = srcDesc._width;
		result.height = std::max(1u, (unsigned)srcDesc._height);
		result.depth = std::max(1u, (unsigned)srcDesc._depth);
		result.arraySize = ActualArrayLayerCount(srcDesc);
		result.mipLevels = srcDesc._mipCount;
		result.miscFlags = result.miscFlags2 = 0;
		result.format = (DXGI_FORMAT)srcDesc._format;
		switch (srcDesc._dimensionality) {
		case TextureDesc::Dimensionality::T1D: 
			result.dimension = DirectX::TEX_DIMENSION_TEXTURE1D;
			break;
		default:
		case TextureDesc::Dimensionality::T2D:
			result.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
			break;
		case TextureDesc::Dimensionality::T3D:
			result.dimension = DirectX::TEX_DIMENSION_TEXTURE3D;
			break;
		case TextureDesc::Dimensionality::CubeMap:
			result.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
			result.miscFlags |= DirectX::TEX_MISC_TEXTURECUBE;
			break;
		}
		return result;
	}

	static void PrepareSubresourcesFromDXImage(IteratorRange<const BufferUploads::IAsyncDataSource::SubResource*> subResources, DirectX::ScratchImage& scratchImage)
	{
		for (const auto& sr:subResources) {
			auto* image = scratchImage.GetImage(sr._id._mip, sr._id._arrayLayer, 0);
			if (!image)
				continue;

			assert((unsigned)image->rowPitch == sr._pitches._rowPitch);
			assert((unsigned)image->slicePitch == sr._pitches._slicePitch);
			assert(sr._destination.size() == (size_t)sr._pitches._slicePitch);
			std::memcpy(
				sr._destination.begin(),
				image->pixels,
				std::min(image->slicePitch, sr._destination.size()));
		}
	}

	std::optional<DDSBreakdown> BuildDDSBreakdown(IteratorRange<const void*> data, StringSection<> filename)
	{
		DirectX::TexMetadata texMetadata;
		auto hres = DirectX::GetMetadataFromDDSMemory(data.begin(), data.size(), DirectX::DDS_FLAGS_NO_LEGACY_EXPANSION, texMetadata);
		if (!SUCCEEDED(hres))
			return {};

		DDSBreakdown result;
		result._textureDesc = BuildTextureDesc(texMetadata);

		// We need to get the image data from the file and copy it into the locations requested
		// The normal usage of the DirectXTex library is to use LoadFromDDSMemory() and 
		// construct a series of DirectX::ScatchImage objects. However, that would result in an
		// extra copy (ie, copy mapped file -> ScatchImage -> staging texture output)
		// We can skip that copy if we use the internal DirectXTex library functions directly

		if (texMetadata.dimension == DirectX::TEX_DIMENSION_TEXTURE3D)
			Throw(std::runtime_error("3D DDS textures encountered while reading (" + filename.AsString() + "). Reading this type of texture is not supported."));

		size_t pixelSize, nimages;
		if (!DirectX::_DetermineImageArray(texMetadata, DirectX::CP_FLAGS_NONE, nimages, pixelSize))
			Throw(std::runtime_error("Could not determine image offsets when loading DDS file (" + filename.AsString() + "). This file may be truncated?"));

		size_t offset = sizeof(uint32_t) + sizeof(DirectX::DDS_HEADER);
		auto* pHeader = reinterpret_cast<const DirectX::DDS_HEADER*>(PtrAdd(data.begin(), sizeof(uint32_t)));
		if ((pHeader->ddspf.flags & DDS_FOURCC) && (MAKEFOURCC('D', 'X', '1', '0') == pHeader->ddspf.fourCC))
			offset += sizeof(DirectX::DDS_HEADER_DXT10);
		auto* pixels = PtrAdd(data.begin(), offset);

		if ((pixelSize + offset) > data.size())
			Throw(std::runtime_error("DDS file appears truncating when reading (" + filename.AsString() + ")"));

		VLA(DirectX::Image, dximages, nimages);
		if (!_SetupImageArray(
			(uint8_t*)pixels,
			pixelSize,
			texMetadata,
			DirectX::CP_FLAGS_NONE, dximages, nimages))
			Throw(std::runtime_error("Failure while reading images in DDS file (" + filename.AsString() + ")"));

		result._subresources.reserve(nimages);
		for (unsigned imageIdx=0; imageIdx<nimages; ++imageIdx) {
			auto& image = dximages[imageIdx];
			result._subresources.push_back(
				DDSBreakdown::Subresource {
					image.pixels,
					TexturePitches {
						(unsigned)image.rowPitch,
						(unsigned)image.slicePitch,
						(unsigned)image.slicePitch
					}
				});
		}

		return result;
	}

	class DDSDataSource : public BufferUploads::IAsyncDataSource, public std::enable_shared_from_this<DDSDataSource>
	{
	public:
		virtual std::future<ResourceDesc> GetDesc() override
		{
			struct Captures
			{
				std::promise<ResourceDesc> _promise;
			};
			auto cap = std::make_shared<Captures>();
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures=std::move(cap)]() {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);

						if (!that->_hasReadMetadata) {
							if (!that->_file.IsGood())
								that->_file = ::Assets::MainFileSystem::OpenMemoryMappedFile(that->_filename, 0ull, "r");
						
							auto breakdown = BuildDDSBreakdown(that->_file.GetData(), that->_filename);
							if (!breakdown) {
								// Sometimes we can get here if the file requires some conversion at load in. For example, there are some legacy formats (such as R8G8B8 formats)
								// that are valid in DDS, but aren't supported by modern DX/DXGI. To support these, we need to drop back to a less efficient way of loading
								// the file. But this is much less efficient, and really not recommended
								auto hres = DirectX::GetMetadataFromDDSMemory(that->_file.GetData().begin(), that->_file.GetSize(), DirectX::DDS_FLAGS_NONE, that->_fallbackTexMetadata);
								if (!SUCCEEDED(hres))
									Throw(std::runtime_error("Failed while attempting reading header from DDS file (" + that->_filename + ")"));

								// We succeeded after allowing conversions. Let's use the fallback path
								Log(Warning) << "Falling back to inefficient path for loading DDS file (" << that->_filename << "). This usually means that the file is using a legacy pixel format that isn't natively supported by modern hardware and graphics APIs. This path is not recommended because it can result in slowdowns and memory spikes during loading." << std::endl;
								hres = LoadFromDDSMemory(that->_file.GetData().begin(), that->_file.GetSize(), DirectX::DDS_FLAGS_NONE, &that->_fallbackTexMetadata, that->_fallbackScratchImage);
								if (!SUCCEEDED(hres))
									Throw(std::runtime_error("Failed while attempting reading header from DDS file (" + that->_filename + ") in fallback phase"));
								that->_fallbackTexMetadata = that->_fallbackScratchImage.GetMetadata();
								that->_useFallbackScratchImage = true;
								that->_file = {};
								that->_resourceDesc = CreateDesc(0, BuildTextureDesc(that->_fallbackTexMetadata));
							} else {
								that->_ddsBreakdown = *breakdown;
								that->_resourceDesc = CreateDesc(0, that->_ddsBreakdown._textureDesc);
							}

							that->_hasReadMetadata = true;
						}

						captures->_promise.set_value(that->_resourceDesc);
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});

			return result;
		}

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			struct Captures
			{
				std::promise<void> _promise;
				std::vector<SubResource> _subResources;
				Captures(IteratorRange<const SubResource*> subResources) :_subResources(subResources.begin(), subResources.end()) {}
			};
			auto cap = std::make_shared<Captures>(subResources);
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures = std::move(cap)]() mutable {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);
						assert(that->_hasReadMetadata);

						if (!that->_useFallbackScratchImage) {
							if (!that->_file.IsGood())
								that->_file = ::Assets::MainFileSystem::OpenMemoryMappedFile(that->_filename, 0ull, "r");

							for (const auto& sr:captures->_subResources) {
								auto srcSrIdx = sr._id._arrayLayer * that->_ddsBreakdown._textureDesc._mipCount + sr._id._mip;
								auto& srcSr = that->_ddsBreakdown._subresources[srcSrIdx];
								assert(srcSr._pitches._rowPitch == sr._pitches._rowPitch);
								assert(srcSr._pitches._slicePitch == sr._pitches._slicePitch);
								assert(sr._destination.size() == (size_t)sr._pitches._slicePitch);
								std::memcpy(
									sr._destination.begin(),
									srcSr._data,
									std::min((size_t)srcSr._pitches._arrayPitch, sr._destination.size()));
							}
						} else {
							// This is the inefficient path used when the DirectXTex library needs to do some conversion after loading
							PrepareSubresourcesFromDXImage(captures->_subResources, that->_fallbackScratchImage);
						}
						that->_file = {};		// close the file now, because we're probably done with it
						captures->_promise.set_value();
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});
			return result;
		}

		StringSection<> GetName() const override { return _filename; }

		::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return ::Assets::GetDepValSys().Make(_filename);
		}

		DDSDataSource(const std::string& filename)
		: _filename(filename)
		{
			_hasReadMetadata = false;
			_useFallbackScratchImage = false;
		}
		~DDSDataSource() {}
	private:
		std::string _filename;

		std::mutex _lock;
		OSServices::MemoryMappedFile _file;
		DDSBreakdown _ddsBreakdown;
		RenderCore::ResourceDesc _resourceDesc;
		bool _hasReadMetadata = false;

		DirectX::TexMetadata _fallbackTexMetadata;
		DirectX::ScratchImage _fallbackScratchImage;
		bool _useFallbackScratchImage = false;
	};

	std::function<TextureLoaderSignature> CreateDDSTextureLoader()
	{
		// the DirectXTex library is expecting us to call CoInitializeEx.
		// We need to call this in every thread that uses the DirectXTex library.
		//  ... it should be ok to call it multiple times in the same thread, so
		//      let's just call it every time.
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		return [](StringSection<> filename, TextureLoaderFlags::BitField flags) -> std::shared_ptr<BufferUploads::IAsyncDataSource> {
			// assert(!(flags & TextureLoaderFlags::GenerateMipmaps));
			return std::make_shared<DDSDataSource>(filename.AsString());
		};
	}

	enum class TexFmt
	{
		DDS, TGA, WIC, Unknown
	};

	static TexFmt GetTexFmt(StringSection<> filename)
	{
		auto ext = MakeFileNameSplitter(filename).Extension();
		if (ext.IsEmpty()) return TexFmt::Unknown;

		if (XlEqStringI(ext, "dds")) {
			return TexFmt::DDS;
		} else if (XlEqStringI(ext, "tga")) {
			return TexFmt::TGA;
		} else {
			return TexFmt::WIC;     // try "WIC" for anything else
		}
	}

	class WICDataSource : public BufferUploads::IAsyncDataSource, public std::enable_shared_from_this<WICDataSource>
	{
	public:
		virtual std::future<ResourceDesc> GetDesc() override
		{
			struct Captures
			{
				std::promise<ResourceDesc> _promise;
			};
			auto cap = std::make_shared<Captures>();
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures=std::move(cap)]() {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);

						if (!that->_hasBeenInitialized) {
							auto file = ::Assets::MainFileSystem::OpenMemoryMappedFile(that->_filename, 0ull, "r");

							using namespace DirectX;
							auto fmt = GetTexFmt(that->_filename);
							HRESULT hresult = -1;
							if (fmt == TexFmt::DDS) {
								hresult = LoadFromDDSMemory(file.GetData().begin(), file.GetSize(), DDS_FLAGS_NONE, &that->_texMetadata, that->_image);
							} else if (fmt == TexFmt::TGA) {
								hresult = LoadFromTGAMemory(file.GetData().begin(), file.GetSize(), &that->_texMetadata, that->_image);
							} else {
								assert(fmt == TexFmt::WIC);
								hresult = LoadFromWICMemory(file.GetData().begin(), file.GetSize(), WIC_FLAGS_NONE, &that->_texMetadata, that->_image);
							}

							if (!SUCCEEDED(hresult))
								Throw(std::runtime_error("Failure while reading texture file (" + that->_filename + "). Check for corrupted data."));

							if (   (that->_texMetadata.mipLevels <= 1) && (that->_texMetadata.arraySize <= 1) 
								&& (that->_flags & TextureLoaderFlags::GenerateMipmaps) && fmt != TexFmt::DDS) {

								Log(Verbose) << "Building mipmaps for texture: " << that->_filename << std::endl;
								DirectX::ScratchImage newImage;
								auto mipmapHresult = GenerateMipMaps(*that->_image.GetImage(0,0,0), TEX_FILTER_DEFAULT, 0, newImage);
								if (!SUCCEEDED(mipmapHresult))
									Throw(std::runtime_error("Failed while building mip-maps for texture (" + that->_filename + ")"));

								that->_image = std::move(newImage);
								that->_texMetadata = that->_image.GetMetadata();
							}

							that->_hasBeenInitialized = true;
						}

						auto textureDesc = BuildTextureDesc(that->_texMetadata);
						auto resourceDesc = CreateDesc(0, textureDesc);
						captures->_promise.set_value(resourceDesc);
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});

			return result;
		}

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			struct Captures
			{
				std::promise<void> _promise;
				std::vector<SubResource> _subResources;
				Captures(IteratorRange<const SubResource*> subResources) :_subResources(subResources.begin(), subResources.end()) {}
			};
			auto cap = std::make_shared<Captures>(subResources);
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures = std::move(cap)]() mutable {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);
						assert(that->_hasBeenInitialized);
						PrepareSubresourcesFromDXImage(captures->_subResources, that->_image);
						captures->_promise.set_value();
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});
			return result;
		}

		::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return ::Assets::GetDepValSys().Make(_filename);
		}

		StringSection<> GetName() const override { return _filename; }

		WICDataSource(const std::string& filename, TextureLoaderFlags::BitField flags)
		: _filename(filename), _flags(flags)
		{
			_hasBeenInitialized = false;
		}
		~WICDataSource() {}
	private:
		std::string _filename;
		TextureLoaderFlags::BitField _flags;

		std::mutex _lock;
		DirectX::TexMetadata _texMetadata;
		DirectX::ScratchImage _image;
		bool _hasBeenInitialized = false;
	};

	std::function<TextureLoaderSignature> CreateWICTextureLoader()
	{
		// the DirectXTex library is expecting us to call CoInitializeEx.
		// We need to call this in every thread that uses the DirectXTex library.
		//  ... it should be ok to call it multiple times in the same thread, so
		//      let's just call it every time.
		CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		return [](StringSection<> filename, TextureLoaderFlags::BitField flags) -> std::shared_ptr<BufferUploads::IAsyncDataSource> {
			return std::make_shared<WICDataSource>(filename.AsString(), flags);
		};
	}

	class HDRDataSource : public BufferUploads::IAsyncDataSource, public std::enable_shared_from_this<HDRDataSource>
	{
	public:
		virtual std::future<ResourceDesc> GetDesc() override
		{
			struct Captures
			{
				std::promise<ResourceDesc> _promise;
			};
			auto cap = std::make_shared<Captures>();
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures=std::move(cap)]() {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);
						if (!that->_hasBeenInitialized) {
							that->_file = ::Assets::MainFileSystem::OpenMemoryMappedFile(that->_filename, 0ull, "r");
							auto data = that->_file.GetData();

							const auto headerLen = strlen("#?RADIANCE\n");
							bool radianceHeader = (that->_file.GetSize() >= headerLen) && (std::strncmp((const char*)data.begin(), "#?RADIANCE\n", headerLen) == 0);
							const auto headerLen2 = strlen("#?RGBE\n");
							bool rgbeHeader = (that->_file.GetSize() >= headerLen2) && (std::strncmp((const char*)data.begin(), "#?RGBE\n", headerLen2) == 0);
							if (!radianceHeader && !rgbeHeader)
								Throw(std::runtime_error("Failure while reading texture file (" + that->_filename + "). Check for corrupted data."));

							auto i = (const char*)data.begin();
							while (i != data.end() && *i != '\n') ++i;
							assert(*i == '\n'); ++i;

							unsigned width = 0, height = 0;
							for (;;) {
								while (i != data.end() && *i == '\n') ++i;
								auto fieldBegin = i;
								while (i != data.end() && *i != '\n') ++i;
								if (strncmp(fieldBegin, "FORMAT=", 7) == 0) {
									if (!XlEqString(MakeStringSection(fieldBegin, i), "FORMAT=32-bit_rle_rgbe"))
										Throw(std::runtime_error("Unknown pixel format in HDR file (" + that->_filename + "). Only RGBE data supported."));
								} else if (strncmp(fieldBegin, "-Y ", 3) == 0) {
									auto q = FastParseValue(MakeStringSection(fieldBegin+3, i), height);
									if (strncmp(q, " +X ", 4) != 0)
										Throw(std::runtime_error("Bad header in HDR file (" + that->_filename + ")."));
									q += 4;
									auto end = FastParseValue(MakeStringSection(q, i), width);
									if (end != i)
										Throw(std::runtime_error("Bad header in HDR file (" + that->_filename + ")."));
									break;
								}
							}
							while (i != data.end() && *i == '\n') ++i;
							if (!width || !height)
								Throw(std::runtime_error("Bad header in HDR file (" + that->_filename + ")."));

							// The real file format is R8G8B8E8 (8 bit shared exponent)
							that->_desc = CreateDesc(
								0, TextureDesc::Plain2D(width, height, Format::R32G32B32A32_FLOAT));
							that->_dataBegin = (uint8_t*)i;
							that->_hasBeenInitialized = true;
						}

						captures->_promise.set_value(that->_desc);
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});

			return result;
		}

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			assert(subResources.size() == 1 && !subResources[0]._destination.empty());
			struct Captures
			{
				std::promise<void> _promise;
				std::vector<SubResource> _subResources;
				Captures(IteratorRange<const SubResource*> subResources) :_subResources(subResources.begin(), subResources.end()) {}
			};
			auto cap = std::make_shared<Captures>(subResources);
			auto result = cap->_promise.get_future();
			ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
				[weakThis{weak_from_this()}, captures = std::move(cap)]() {
					try
					{
						auto that = weakThis.lock();
						if (!that)
							Throw(std::runtime_error("Data source has expired"));

						ScopedLock(that->_lock);
						assert(that->_hasBeenInitialized);

						assert(captures->_subResources.size() == 1);
						auto sr = captures->_subResources[0];

						auto width = that->_desc._textureDesc._width, height = that->_desc._textureDesc._height;

						// Referencing STBI (https://github.com/nothings/stb/blob/master/stb_image.h) for the RLE encoding method
						bool isRLE = (that->_dataBegin[0] == 2) && (that->_dataBegin[1] == 2) && !(that->_dataBegin[2]&0x80);
						if (!isRLE) {
							auto expectedByteSize = 4*width*height;
							auto data = that->_file.GetData();
							if (data.size() != (that->_dataBegin-(uint8_t*)data.begin())+expectedByteSize)
								Throw(std::runtime_error("Unexpected file size while reading HDR file (" + that->_filename + ")."));
								
							for (unsigned c=0; c<(that->_desc._textureDesc._width*that->_desc._textureDesc._height); ++c) {
								float* dstBegin = &((float*)sr._destination.begin())[c*4];
								assert((dstBegin+4)<=sr._destination.end());
								uint8_t* src = (uint8_t*)&that->_dataBegin[c*4];
								dstBegin[0] = std::ldexp(src[0], src[3] - int(128 + 8));
								dstBegin[1] = std::ldexp(src[1], src[3] - int(128 + 8));
								dstBegin[2] = std::ldexp(src[2], src[3] - int(128 + 8));
								dstBegin[3] = 1.0f;
							}
						} else {
							
							auto scanLineBuffer = std::make_unique<uint8_t[]>(width*4);

							uint8_t* i = that->_dataBegin;
							for (unsigned y=0; y<height; ++y) {
								auto encodedScanLineWidth = (i[2]<<8)|i[3];
								assert(encodedScanLineWidth == width);
								i+=4;
								auto* scanline = scanLineBuffer.get();
								for (unsigned component=0; component<4; ++component) {
									for (unsigned x=0; x<width;) {
										if (i[0] > 128u) {
											unsigned count = i[0]-128u; 
											for (unsigned q=0; q<count; ++q)
												*scanline++ = i[1];
											i += 2;
											x += count;
										} else {
											auto count = *i++;
											for (unsigned q=0; q<count; ++q)
												*scanline++ = *i++;
											x += count;
										}
									}
								}

								float* dst = &((float*)sr._destination.begin())[y*width*4];
								for (unsigned x=0; x<width; ++x, dst+=4) {
									dst[0] = std::ldexp(scanLineBuffer[x], scanLineBuffer[3*width+x] - int(128 + 8));
									dst[1] = std::ldexp(scanLineBuffer[1*width+x], scanLineBuffer[3*width+x] - int(128 + 8));
									dst[2] = std::ldexp(scanLineBuffer[2*width+x], scanLineBuffer[3*width+x] - int(128 + 8));
									dst[3] = 1.0f;
								}
							}

						}

						captures->_promise.set_value();
					} catch(...) {
						captures->_promise.set_exception(std::current_exception());
					}
				});
			return result;
		}

		::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return ::Assets::GetDepValSys().Make(_filename);
		}

		StringSection<> GetName() const override { return _filename; }

		HDRDataSource(const std::string& filename, TextureLoaderFlags::BitField flags)
		: _filename(filename), _flags(flags)
		{
			if (_flags & TextureLoaderFlags::GenerateMipmaps)
				Throw(std::runtime_error("Mipmap generation not supported by HDR data source"));
			_hasBeenInitialized = false;
		}
		~HDRDataSource() {}
	private:
		std::string _filename;
		TextureLoaderFlags::BitField _flags;

		std::mutex _lock;
		OSServices::MemoryMappedFile _file;
		bool _hasBeenInitialized = false;
		ResourceDesc _desc;
		uint8_t* _dataBegin = nullptr;
	};

	std::function<TextureLoaderSignature> CreateHDRTextureLoader()
	{
		return [](StringSection<> filename, TextureLoaderFlags::BitField flags) -> std::shared_ptr<BufferUploads::IAsyncDataSource> {
			return std::make_shared<HDRDataSource>(filename.AsString(), flags);
		};
	}

	::Assets::Blob PrepareDDSBlob(
		const TextureDesc& tDesc,
		size_t& headerSize)
	{
		auto dstSize = ByteCount(tDesc);
		auto metadata = BuildTexMetadata(tDesc);

		headerSize = 0;
		auto directXFlags = DirectX::DDS_FLAGS_NONE;
		auto hr = DirectX::_EncodeDDSHeader(metadata, directXFlags, nullptr, 0, headerSize);
		assert(SUCCEEDED(hr));

		auto result = std::make_shared<std::vector<uint8_t>>(dstSize+headerSize);
		DirectX::_EncodeDDSHeader(metadata, directXFlags, result->data(), result->size(), headerSize);
		assert(SUCCEEDED(hr));

		return result;
	}

}}


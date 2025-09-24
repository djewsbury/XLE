// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompiler.h"
#include "TextureCompilerRegistrar.h"
#include "../Techniques/Services.h"
#include "../LightingEngine/TextureCompilerUtil.h"
#include "../LightingEngine/BlueNoiseGenerator.h"
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
#include "../../Core/Exceptions.h"

#include "thousandeyes/futures/then.h"

#if XLE_COMPRESSONATOR_ENABLE
	#include "Compressonator.h"
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
		case Format::BC4_UNORM: return CMP_FORMAT_BC4;		// CMP_FORMAT_BC4_S not accessible
		case Format::BC5_UNORM: return CMP_FORMAT_BC5;		// CMP_FORMAT_BC5_S not accessible
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

	class CompressonatorTexture
	{
	public:
		CMP_Texture _srcTexture;
		TextureDesc _srcDesc;

		CompressonatorTexture(BufferUploads::IAsyncDataSource& dataSrc)
		{
			XlZeroMemory(_srcTexture);

			auto descFuture = dataSrc.GetDesc();
			descFuture.wait();
			auto desc = descFuture.get();
			assert(desc._type == ResourceDesc::Type::Texture && desc._textureDesc._width >= 1 && desc._textureDesc._height >= 1);
			_srcDesc = desc._textureDesc;

			_srcTexture.dwSize     = sizeof(_srcTexture);
			_srcTexture.dwWidth    = desc._textureDesc._width;
			_srcTexture.dwHeight   = desc._textureDesc._height;
			_srcTexture.dwPitch    = 0;		// interpreted as packed
			_srcTexture.format     = AsCompressonatorFormat(desc._textureDesc._format);
			_srcTexture.dwDataSize = ByteCount(desc._textureDesc);
			_srcTexture.pData = (CMP_BYTE*)XlMemAlign(_srcTexture.dwDataSize, 64);		// use a very large alignment, even if it's not specifically requested by compressonator

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
			dataFuture.wait();

			// as per compressonator example, swizzle BGRA types
			if (_srcTexture.format == CMP_FORMAT_BGRA_8888) {
				unsigned char blue;
				for (CMP_DWORD i = 0; i < _srcTexture.dwDataSize; i += 4)
				{
					blue                    = _srcTexture.pData[i];
					_srcTexture.pData[i]     = _srcTexture.pData[i + 2];
					_srcTexture.pData[i + 2] = blue;
				}
				_srcTexture.format = CMP_FORMAT_RGBA_8888;
			}
		}

		CompressonatorTexture()
		{
			XlZeroMemory(_srcTexture);
		}

		~CompressonatorTexture()
		{
			XlMemAlignFree(_srcTexture.pData);
		}
		CompressonatorTexture(CompressonatorTexture&&) = default;
		CompressonatorTexture& operator=(CompressonatorTexture&&) = default;
	};

	::Assets::Blob ConvertAndPrepareDDSBlobSync(
		BufferUploads::IAsyncDataSource& srcPkt,
		Format dstFmt)
	{
		CompressonatorTexture input{srcPkt};

		auto dstDesc = input._srcDesc;
		dstDesc._format = dstFmt;
		size_t ddsHeaderOffset = 0;
		auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

		if (input._srcDesc._format != dstDesc._format) {
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
		} else {
			// copy directly into the output dds
			if (destinationBlob->size() != (ddsHeaderOffset + input._srcTexture.dwDataSize))
				Throw(std::runtime_error("Texture conversion failed because of size mismatch"));
			std::memcpy(PtrAdd(destinationBlob->data(), ddsHeaderOffset), input._srcTexture.pData, input._srcTexture.dwDataSize);
		}

		return destinationBlob;
	}
#endif

	::Assets::Blob PrepareDDSBlobSyncWithoutConvert(
		BufferUploads::IAsyncDataSource& srcPkt)
	{
		auto descFuture = srcPkt.GetDesc();
		descFuture.wait();
		auto desc = descFuture.get();
		assert(desc._type == ResourceDesc::Type::Texture && desc._textureDesc._width >= 1 && desc._textureDesc._height >= 1);
		auto srcSize = ByteCount(desc);
		auto dstDesc = desc._textureDesc;

		AlignedUniquePtr<uint8_t> data { (uint8_t*)XlMemAlign(srcSize, 64) };

		auto mipCount = desc._textureDesc._mipCount;
		auto arrayLayerCount = ActualArrayLayerCount(desc._textureDesc);
		VLA_UNSAFE_FORCE(BufferUploads::IAsyncDataSource::SubResource, subres, mipCount*arrayLayerCount);
		for (unsigned a=0; a<arrayLayerCount; ++a)
			for (unsigned m=0; m<mipCount; ++m) {
				auto& sr = subres[m+a*mipCount];
				auto srcOffset = GetSubResourceOffset(desc._textureDesc, m, a);
				sr._id = SubResourceId{m, a};
				sr._destination = {PtrAdd(data.get(), srcOffset._offset), PtrAdd(data.get(), srcOffset._offset+srcOffset._size)};
				sr._pitches = srcOffset._pitches;
			}

		auto dataFuture = srcPkt.PrepareData(MakeIteratorRange(subres, &subres[mipCount*arrayLayerCount]));
		dataFuture.wait();

		size_t ddsHeaderOffset = 0;
		auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

		// copy directly into the output dds
		if (destinationBlob->size() != (ddsHeaderOffset + srcSize))
			Throw(std::runtime_error("Texture conversion failed because of size mismatch"));
		std::memcpy(PtrAdd(destinationBlob->data(), ddsHeaderOffset), data.get(), srcSize);

		return destinationBlob;
	}

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
			std::memset(dst, 0, subResources[0]._destination.size());

			// as long as width is an integer cubed and height is an integer squared, we'll get a pattern that visits every pixel
			unsigned subTableWidth = 3, subTableHeight = 2;
			unsigned i = 1;
			while (subTableWidth < _width) { ++i; subTableWidth = i*i*i; }
			i = 1;
			while (subTableHeight < _height) { ++i; subTableHeight = i*i; }

			// We can do this in a smarter way by using the inverse-radical-inverse, and solving some simultaneous
			// equations with modular arithmetic. But since we're building a lookup table anyway, that doesn't seem
			// of any practical purpose
			using namespace XLEMath;
			for (unsigned sampleIdx=0; sampleIdx<subTableWidth*subTableHeight; ++sampleIdx) {
				const bool extraScambling = true;
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

			// We can shuffle the rows to add more randomness. The end result is less uniformly distributed, but also has 
			// fewer repeating patterns (since there is a slight pattern to the Halton sampler output)
			// which is better may depend on the application
			// std::mt19937_64 rng(153483181236ull);
			// for (unsigned y=0; y<_height; ++y)
			// 	std::shuffle(&dst[y*_width], &dst[y*_width+_width], rng);

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

	class HaltonSamplerTexture : public BufferUploads::IAsyncDataSource
	{
	public:
		virtual std::future<ResourceDesc> GetDesc() override
		{
			std::promise<ResourceDesc> promise;
			promise.set_value(CreateDesc(0, TextureDesc::Plain2D(_width, _height, Format::R32_UINT)));
			return promise.get_future();
		}

		virtual StringSection<> GetName() const override { return "halton-sampler"; }

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			assert(subResources.size() == 1);
			assert(subResources[0]._destination.size() == sizeof(uint32_t)*_width*_height);
			uint32_t* dst = (uint32_t*)subResources[0]._destination.begin();
			std::memset(dst, 0, subResources[0]._destination.size());

			uint32_t repeatingStride = LightingEngine::HaltonSamplerHelper::WriteHaltonSamplerIndices(MakeIteratorRange(subResources[0]._destination).Cast<uint32_t*>(), _width, _height);
			(void)repeatingStride;

			std::promise<void> promise;
			promise.set_value();
			return promise.get_future();
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return {};
		}

		unsigned _width, _height;
		HaltonSamplerTexture(unsigned width, unsigned height) : _width(width), _height(height) {}
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

	std::shared_ptr<ITextureCompiler> TextureCompiler_Base(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		auto scaffold = indexer._scaffold.get();

		if (scaffold->HasComponent(indexer._entityNameHash, "BalancedNoise"_h))
			return util->GetFuture<std::shared_ptr<Compiler_BalancedNoise>>("BalancedNoise"_h, indexer).get().get();

		class Compiler_HaltonSampler : public ITextureCompiler
		{
		public:
			unsigned _width = 512, _height = 512;
			std::string GetIntermediateName() const override { return (StringMeld<128>() << "halton-sampler-" << _width << "x" << _height).AsString(); }
			std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& context) override { return std::make_shared<HaltonSamplerTexture>(_width, _height); }

			Compiler_HaltonSampler(Formatters::TextInputFormatter<>& fmttr)
			{
				StringSection<> kn;
				while (fmttr.TryKeyedItem(kn)) {
					if (XlEqString(kn, "Width")) _width = Formatters::RequireCastValue<decltype(_width)>(fmttr);
					else if (XlEqString(kn, "Height")) _height = Formatters::RequireCastValue<decltype(_height)>(fmttr);
					else Formatters::SkipValueOrElement(fmttr);
				}
			}
		};

		if (scaffold->HasComponent(indexer._entityNameHash, "HaltonSampler"_h))
			return util->GetFuture<std::shared_ptr<Compiler_HaltonSampler>>("HaltonSampler"_h, indexer).get().get();
		
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
			} else Formatters::SkipValueOrElement(fmttr);
		}
	}

	static_assert(::Assets::Internal::AssetMixinTraits<PostConvert>::HasDeserializationOperatorFromFormatter);

	class TextureCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		std::vector<TargetDesc> GetTargets() const
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
				auto blob = ConvertAndPrepareDDSBlobSync(*pkt, postConvert._format);
			#else
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
		std::vector<::Assets::SerializedArtifact> _serializedArtifacts;
	};

	::Assets::CompilerRegistration RegisterTextureCompiler(
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
		if (!result._subCompiler)
			return {};		// invalid compile

		result._intermediateName = result._subCompiler->GetIntermediateName();

		if (indexer._scaffold.get()->HasComponent(indexer._entityNameHash, "PostConvert"_h)) {
			result._postConvert = util->GetFuture<PostConvert>("PostConvert"_h, indexer).get();
			result._intermediateName = Concatenate(result._intermediateName, "-", AsString(result._postConvert->_format));
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<BufferUploads::IAsyncDataSource> TextureArtifact::BeginDataSource(TextureLoaderFlags::BitField loadedFlags) const
	{
		return Techniques::Services::GetInstance().CreateTextureDataSource(_artifactFile, loadedFlags);
	}

	auto TextureArtifact::BeginLoadRawData(TextureLoaderFlags::BitField loadedFlags) const -> std::future<RawData>
	{
		auto pkt = Techniques::Services::GetInstance().CreateTextureDataSource(_artifactFile, 0);
		if (!pkt) {
			std::promise<RawData> promise;
			promise.set_exception(std::make_exception_ptr(
				::Assets::Exceptions::ConstructionError(
					::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
					GetDependencyValidation(),
					StringMeld<256>() << "Could not find matching texture loader for file: " << _artifactFile)));
			return promise.get_future();
		}

		auto futureDesc = pkt->GetDesc();
		return thousandeyes::futures::then(
			ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
			std::move(futureDesc),
			[pkt](auto descFuture) {
				auto desc = descFuture.get();
				assert(desc._type == ResourceDesc::Type::Texture);
				auto mipCount = (unsigned)desc._textureDesc._mipCount, elementCount = ActualArrayLayerCount(desc._textureDesc);
				std::vector<uint8_t> data;
				data.resize(ByteCount(desc._textureDesc));
				VLA_UNSAFE_FORCE(BufferUploads::IAsyncDataSource::SubResource, srs, mipCount*elementCount);
				for (unsigned e=0; e<elementCount; ++e)
					for (unsigned m=0; m<mipCount; ++m) {
						auto srOffset = GetSubResourceOffset(desc._textureDesc, m, e);
						auto& sr = srs[e*mipCount+m];
						sr._id = {m,e};
						assert((srOffset._offset+srOffset._size) <= data.size());
						sr._destination = MakeIteratorRange(PtrAdd(data.data(), srOffset._offset), PtrAdd(data.data(), srOffset._offset+srOffset._size));
						sr._pitches = srOffset._pitches;
					}
				return thousandeyes::futures::then(
					ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
					pkt->PrepareData(MakeIteratorRange(srs, &srs[mipCount*elementCount])),
					[tDesc=desc._textureDesc, data=std::move(data), pkt](auto) {		// need to retain "pkt" as load as PrepareData() is working
						RawData result;
						result._data = std::move(data);
						result._desc = tDesc;
						return result;
					});
			});
	}

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
		ScopedLock(_mutex);
		auto result = ++_nextRegistrationId;
		_fns.emplace_back(result, std::move(sig));
		return result;
	}

	void TextureCompilerRegistrar::Deregister(RegistrationId id)
	{
		ScopedLock(_mutex);
		_fns.erase(
			std::remove_if(_fns.begin(), _fns.end(), [id](const auto& q) { return q.first == id; }),
			_fns.end());
	}

	std::shared_ptr<ITextureCompiler> TextureCompilerRegistrar::TryBeginCompile(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		ScopedLock(_mutex);
		for (const auto& f:_fns)
			if (auto compiler = f.second(util, indexer))
				return compiler;
		return nullptr;
	}

	TextureCompilerRegistrar::TextureCompilerRegistrar() { _nextRegistrationId = 1; }
	TextureCompilerRegistrar::~TextureCompilerRegistrar() {}

	ITextureCompiler::~ITextureCompiler() = default;

}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompiler.h"
#include "../Techniques/Services.h"
#include "../Techniques/TextureCompilerUtil.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediatesStore.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/InitializerPack.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/BitUtils.h"
#include "../../Core/Exceptions.h"

#include "Compressonator.h"

namespace RenderCore { namespace Assets
{
	::Assets::Blob PrepareDDSBlob(const TextureDesc& tDesc, size_t& headerSize);

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

		case Format::R8G8B8A8_TYPELESS: return CMP_FORMAT_ARGB_8888;
		case Format::R8G8B8A8_UNORM: return CMP_FORMAT_ARGB_8888;
		case Format::R8G8B8A8_SNORM: return CMP_FORMAT_ARGB_8888_S;
		case Format::R8G8B8A8_UNORM_SRGB: return CMP_FORMAT_ARGB_8888;

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

		case Format::B8G8R8A8_TYPELESS: return CMP_FORMAT_ABGR_8888;
		case Format::B8G8R8A8_UNORM: return CMP_FORMAT_ABGR_8888;
		case Format::B8G8R8A8_UNORM_SRGB: return CMP_FORMAT_ABGR_8888;

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
		case Format::BC4_UNORM: return CMP_FORMAT_BC4;		// CMP_FORMAT_BC4_S not accessable
		case Format::BC5_UNORM: return CMP_FORMAT_BC5;		// CMP_FORMAT_BC5_S not accessable
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
			_srcTexture.dwPitch    = 0;		// interpretted as packed
			_srcTexture.format     = AsCompressonatorFormat(desc._textureDesc._format);
			_srcTexture.dwDataSize = ByteCount(desc._textureDesc);
			_srcTexture.pData = (CMP_BYTE*)malloc(_srcTexture.dwDataSize);

			assert(_srcTexture.format != CMP_FORMAT_Unknown);

			auto mipCount = std::max(1u, (unsigned)desc._textureDesc._mipCount);
			auto arrayLayerCount = std::max(1u, (unsigned)desc._textureDesc._arrayCount);
			BufferUploads::IAsyncDataSource::SubResource subres[mipCount*arrayLayerCount];
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
		}

		CompressonatorTexture()
		{
			XlZeroMemory(_srcTexture);
		}

		~CompressonatorTexture()
		{
			free(_srcTexture.pData);
		}
		CompressonatorTexture(CompressonatorTexture&&) = default;
		CompressonatorTexture& operator=(CompressonatorTexture&&) = default;;
	};

	class TextureCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		std::vector<TargetDesc> GetTargets() const
		{
			if (_compilationException)
				return { 
					TargetDesc { TextureCompilerProcessType, "compilation-exception" }
				};
			if (_serializedArtifacts.empty()) return {};
			return {
				TargetDesc { TextureCompilerProcessType, _serializedArtifacts[0]._name.c_str() }
			};
		}
		std::vector<SerializedArtifact>	SerializeTarget(unsigned idx)
		{
			assert(idx == 0);
			if (_compilationException)
				std::rethrow_exception(_compilationException);
			return _serializedArtifacts;
		}
		std::vector<::Assets::DependentFileState> GetDependencies() const { return _dependencies; }

		TextureCompileOperation(const ::Assets::InitializerPack& initializers)
		{
			TRY
			{
				// load the given file and perform texture processing operations 
				auto srcFN = initializers.GetInitializer<std::string>(0);
				size_t inputBlockSize = 0;
				::Assets::DependentFileState fileState;
				auto inputBlock = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(srcFN, &inputBlockSize, &fileState);
				_dependencies.push_back(fileState);
				if (!inputBlockSize)
					Throw(std::runtime_error("Empty or missing file while loading: " + srcFN));
				InputStreamFormatter<> inputFormatter{MakeStringSection((const char*)inputBlock.get(), (const char*)PtrAdd(inputBlock.get(), inputBlockSize))};
				StreamDOM<InputStreamFormatter<>> dom(inputFormatter);
				auto type = dom.RootElement().Attribute("Operation");
				if (!type || (!XlEqStringI(type.Value(), "Convert") && !XlEqStringI(type.Value(), "EquRectToCubeMap") && !XlEqStringI(type.Value(), "EquiRectFilterGlossySpecular")  && !XlEqStringI(type.Value(), "ComputeShader")))
					Throw(std::runtime_error("Unknown operation in texture compiler file: " + srcFN + ", (" + type.Value().AsString() + ")"));

				auto dstFormatName = dom.RootElement().Attribute("Format");
				if (!dstFormatName)
					Throw(std::runtime_error("Expecting 'Format' field in texture compiler file: " + srcFN));
				auto dstFormat = AsFormat(dstFormatName.Value());
				if (dstFormat == Format::Unknown)
					Throw(std::runtime_error("Unknown 'Format' field in texture compiler file: " + srcFN));

				std::shared_ptr<BufferUploads::IAsyncDataSource> srcPkt;
				if (!XlEqString(type.Value(), "ComputeShader")) {
					auto srcFile = dom.RootElement().Attribute("SourceFile");
					if (!srcFile)
						Throw(std::runtime_error("Expecting 'SourceFile' fields in texture compiler file: " + srcFN));
					srcPkt = Techniques::Services::GetInstance().CreateTextureDataSource(srcFile.Value(), 0);
					_dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(srcFile.Value()));
				}

				if (XlEqString(type.Value(), "EquRectToCubeMap")) {
					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					bool mipMapsFromSource = XlEqStringI(dom.RootElement().Attribute("MipMapFilter").Value(), "FromSource");
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = dom.RootElement().Attribute("FaceDim", 512);
					targetDesc._height = dom.RootElement().Attribute("FaceDim", 512);
					targetDesc._depth = 1;
					targetDesc._arrayCount = 6;
					targetDesc._mipCount = mipMapsFromSource ? IntegerLog2(targetDesc._width)+1 : 1;
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					auto processed = Techniques::EquRectFilter(*srcPkt, targetDesc, Techniques::EquRectFilterMode::ToCubeMap);
					srcPkt = processed._newDataSource;
					_dependencies.insert(_dependencies.end(), processed._depFileStates.begin(), processed._depFileStates.end());
				} else if (XlEqString(type.Value(), "EquiRectFilterGlossySpecular")) {
					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = dom.RootElement().Attribute("FaceDim", 512);
					targetDesc._height = dom.RootElement().Attribute("FaceDim", 512);
					targetDesc._depth = 1;
					targetDesc._arrayCount = 6;
					targetDesc._mipCount = IntegerLog2(targetDesc._width)+1;
					targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					auto processed = Techniques::EquRectFilter(*srcPkt, targetDesc, Techniques::EquRectFilterMode::ToGlossySpecular);
					srcPkt = processed._newDataSource;
					_dependencies.insert(_dependencies.end(), processed._depFileStates.begin(), processed._depFileStates.end());
				} else if (XlEqString(type.Value(), "ComputeShader")) {
					auto targetDesc = TextureDesc::Plain2D(
						dom.RootElement().Attribute("Width", 512),
						dom.RootElement().Attribute("Height", 512),
						Format::R32G32B32A32_FLOAT); // use full float precision for the pre-compression format
					auto shader = dom.RootElement().Attribute("Shader");
					if (!shader)
						Throw(std::runtime_error("Expecting 'Shader' field in texture compiler file: " + srcFN));
					auto processed = Techniques::GenerateFromComputeShader(shader.Value(), targetDesc);
					srcPkt = processed._newDataSource;
					_dependencies.insert(_dependencies.end(), processed._depFileStates.begin(), processed._depFileStates.end());
				}
				CompressonatorTexture input{*srcPkt};

				auto dstDesc = input._srcDesc;
				dstDesc._format = dstFormat;
				size_t ddsHeaderOffset = 0;
				auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

				CMP_CompressOptions options = {0};
				options.dwSize       = sizeof(options);
				options.fquality     = 0.05f;
				options.dwnumThreads = 1;
				auto comprDstFormat = AsCompressonatorFormat(dstFormat);

				auto mipCount = std::max(1u, (unsigned)dstDesc._mipCount);
				auto arrayLayerCount = std::max(1u, (unsigned)dstDesc._arrayCount);
				for (unsigned a=0; a<arrayLayerCount; ++a)
					for (unsigned m=0; m<mipCount; ++m) {
						auto dstOffset = GetSubResourceOffset(dstDesc, m, a);
						auto dstMipDesc = CalculateMipMapDesc(dstDesc, m);
						auto srcMipDesc = CalculateMipMapDesc(input._srcDesc, m);

						CMP_Texture destTexture = {0};
						destTexture.dwSize     = sizeof(destTexture);
						destTexture.dwWidth    = srcMipDesc._width;
						destTexture.dwHeight   = std::max(1u, (unsigned)srcMipDesc._height);
						destTexture.dwPitch    = 0;
						destTexture.format     = comprDstFormat;
						destTexture.dwDataSize = dstOffset._size;
						auto calcSize = CMP_CalculateBufferSize(&destTexture);
						assert(destTexture.dwDataSize == calcSize);
						destTexture.pData = (CMP_BYTE*)PtrAdd(destinationBlob->data(), ddsHeaderOffset + dstOffset._offset);
						assert(PtrAdd(destTexture.pData, destTexture.dwDataSize) <= AsPointer(destinationBlob->end()));

						auto srcOffset = GetSubResourceOffset(input._srcDesc, m, a);
						auto srcTexture = input._srcTexture;
						srcTexture.dwWidth = destTexture.dwWidth;
						srcTexture.dwHeight = destTexture.dwHeight;
						srcTexture.dwDataSize = srcOffset._size;
						srcTexture.pData = PtrAdd(srcTexture.pData, srcOffset._offset);

						CMP_ERROR cmp_status;
						cmp_status = CMP_ConvertTexture(&srcTexture, &destTexture, &options, nullptr);
						if (cmp_status != CMP_OK)
							Throw(std::runtime_error("Compression library failed while processing texture compiler file: " + srcFN));
					}

				_serializedArtifacts = std::vector<SerializedArtifact>{
					{
						TextureCompilerProcessType, 0, MakeFileNameSplitter(srcFN).File().AsString() + ".dds", destinationBlob
					}
				};

			} CATCH(...) {
				_compilationException = std::current_exception();
			} CATCH_END
		}

	private:
		std::vector<::Assets::DependentFileState> _dependencies;
		std::vector<SerializedArtifact> _serializedArtifacts;
		std::exception_ptr _compilationException;
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
			[](auto initializers) {
				return std::make_shared<TextureCompileOperation>(initializers);
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
}}


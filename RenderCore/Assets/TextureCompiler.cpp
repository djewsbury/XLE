// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompiler.h"
#include "../Techniques/Services.h"
#include "../LightingEngine/TextureCompilerUtil.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/InitializerPack.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/BitUtils.h"
#include "../../Core/Exceptions.h"

#include "thousandeyes/futures/then.h"
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
			_srcTexture.dwPitch    = 0;		// interpretted as packed
			_srcTexture.format     = AsCompressonatorFormat(desc._textureDesc._format);
			_srcTexture.dwDataSize = ByteCount(desc._textureDesc);
			_srcTexture.pData = (CMP_BYTE*)malloc(_srcTexture.dwDataSize);

			assert(_srcTexture.format != CMP_FORMAT_Unknown);

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

	TextureCompilationRequest MakeTextureCompilationRequest(StreamDOMElement<InputStreamFormatter<>>& operationElement, std::string srcFN)
	{
		TextureCompilationRequest result;
		auto type = operationElement.Name();
		if (XlEqStringI(type, "Convert")) result._operation = TextureCompilationRequest::Operation::Convert; 
		else if (XlEqStringI(type, "EquirectToCubeMap")) result._operation = TextureCompilationRequest::Operation::EquirectToCubeMap;
		else if (XlEqStringI(type, "EquirectFilterGlossySpecular")) result._operation = TextureCompilationRequest::Operation::EquirectFilterGlossySpecular;
		else if (XlEqStringI(type, "EquirectFilterGlossySpecularReference")) result._operation = TextureCompilationRequest::Operation::EquirectFilterGlossySpecularReference;
		else if (XlEqStringI(type, "ProjectToSphericalHarmonic")) result._operation = TextureCompilationRequest::Operation::ProjectToSphericalHarmonic;
		else if (XlEqStringI(type, "EquirectFilterDiffuseReference")) result._operation = TextureCompilationRequest::Operation::EquirectFilterDiffuseReference;
		else if (XlEqStringI(type, "ComputeShader")) result._operation = TextureCompilationRequest::Operation::ComputeShader;
		else Throw(std::runtime_error("Unknown operation in texture compiler file: " + srcFN + ", (" + type.AsString() + ")"));

		auto dstFormatName = operationElement.Attribute("Format");
		if (!dstFormatName)
			Throw(std::runtime_error("Expecting 'Format' field in texture compiler file: " + srcFN));
		auto fmtOpt = AsFormat(dstFormatName.Value());
		if (!fmtOpt.has_value())
			Throw(std::runtime_error("Unknown 'Format' field in texture compiler file: " + srcFN));
		result._format = fmtOpt.value();

		result._faceDim = operationElement.Attribute("FaceDim", result._faceDim);
		result._width = operationElement.Attribute("Width", result._width);
		result._height = operationElement.Attribute("Height", result._height);
		result._coefficientCount = operationElement.Attribute("CoefficientCount", result._coefficientCount);
		result._sampleCount = operationElement.Attribute("SampleCount", result._sampleCount);

		auto mipFilter = operationElement.Attribute("MipMapFilter");
		if (mipFilter && XlEqStringI(mipFilter.Value(), "FromSource")) result._mipMapFilter = TextureCompilationRequest::MipMapFilter::FromSource;

		result._shader = operationElement.Attribute("Shader").Value().AsString();
		result._srcFile = operationElement.Attribute("SourceFile").Value().AsString();
		return result;
	}

	std::ostream& SerializationOperator(std::ostream& str, const TextureCompilationRequest& request)
	{
		switch (request._operation) {
		case TextureCompilationRequest::Operation::Convert: str << request._srcFile << "-" << AsString(request._format); break;
		case TextureCompilationRequest::Operation::EquirectToCubeMap: str << request._srcFile << "-EquirectToCubeMap-" << request._faceDim << "-" << AsString(request._format); break;
		case TextureCompilationRequest::Operation::EquirectFilterGlossySpecular: str << request._srcFile << "-spec-" << request._faceDim << "-" << AsString(request._format); break;
		case TextureCompilationRequest::Operation::EquirectFilterGlossySpecularReference: str << request._srcFile << "-refspec-" << request._faceDim << "-" << AsString(request._format); break;
		case TextureCompilationRequest::Operation::EquirectFilterDiffuseReference: str << request._srcFile << "-refdiffuse-" << request._faceDim << "-" << AsString(request._format); break;
		case TextureCompilationRequest::Operation::ProjectToSphericalHarmonic: str << request._srcFile << "-sh-" << request._coefficientCount; break;
		case TextureCompilationRequest::Operation::ComputeShader: str << request._shader << "-" << request._width << "-" << request._height << "-" << AsString(request._format); break;
		default: assert(0);
		}
		return str;
	}

	uint64_t TextureCompilationRequest::GetHash(uint64_t seed) const
	{
		std::stringstream str;
		str << *this;
		return Hash64(str.str(), seed);
	}

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
		std::vector<SerializedArtifact>	SerializeTarget(unsigned idx)
		{
			assert(idx == 0);
			return _serializedArtifacts;
		}
		::Assets::DependencyValidation GetDependencyValidation() const
		{
			std::vector<::Assets::DependencyValidationMarker> markers;
			markers.insert(markers.end(), _dependencies.begin(), _dependencies.end());
			return ::Assets::GetDepValSys().MakeOrReuse(markers);
		}

		void Initialize(TextureCompilationRequest request, std::string srcFN, const VariantFunctions& conduit)
		{
			std::shared_ptr<BufferUploads::IAsyncDataSource> srcPkt;
			if (request._operation != TextureCompilationRequest::Operation::ComputeShader) {
				if (request._srcFile.empty())
					Throw(::Assets::Exceptions::ConstructionError(_cfgFileDepVal, "Expecting 'SourceFile' fields in texture compiler file: " + srcFN));
				srcPkt = Techniques::Services::GetInstance().CreateTextureDataSource(request._srcFile, 0);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			}

			LightingEngine::ProgressiveTextureFn dummyIntermediateFn;
			LightingEngine::ProgressiveTextureFn* intermediateFunction = &dummyIntermediateFn;
			if (conduit.Has<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>(0))
				intermediateFunction = &conduit.Get<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>(0);

			if (request._operation == TextureCompilationRequest::Operation::EquirectToCubeMap) {
				auto srcDst = srcPkt->GetDesc();
				srcDst.wait();
				auto targetDesc = srcDst.get()._textureDesc;
				targetDesc._width = request._faceDim;
				targetDesc._height = request._faceDim;
				targetDesc._depth = 1;
				targetDesc._arrayCount = 0u;
				targetDesc._mipCount = (request._mipMapFilter == TextureCompilationRequest::MipMapFilter::FromSource) ? IntegerLog2(targetDesc._width)+1 : 1;
				targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
				srcPkt = LightingEngine::EquirectFilter(*srcPkt, targetDesc, LightingEngine::EquirectFilterMode::ToCubeMap, {}, *intermediateFunction);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			} else if (request._operation == TextureCompilationRequest::Operation::EquirectFilterGlossySpecular) {
				auto srcDst = srcPkt->GetDesc();
				srcDst.wait();
				auto targetDesc = srcDst.get()._textureDesc;
				targetDesc._width = request._faceDim;
				targetDesc._height = request._faceDim;
				targetDesc._depth = 1;
				targetDesc._arrayCount = 0u;
				targetDesc._mipCount = IntegerLog2(targetDesc._width)+1;
				targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
				targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
				LightingEngine::EquirectFilterParams params;
				params._sampleCount = request._sampleCount;
				params._idealCmdListCostMS = request._commandListIntervalMS;
				srcPkt = LightingEngine::EquirectFilter(*srcPkt, targetDesc, LightingEngine::EquirectFilterMode::ToGlossySpecular, params, *intermediateFunction);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			} else if (request._operation == TextureCompilationRequest::Operation::EquirectFilterGlossySpecularReference) {
				auto srcDst = srcPkt->GetDesc();
				srcDst.wait();
				auto targetDesc = srcDst.get()._textureDesc;
				targetDesc._width = request._faceDim;
				targetDesc._height = request._faceDim;
				targetDesc._depth = 1;
				targetDesc._arrayCount = 0u;
				targetDesc._mipCount = IntegerLog2(targetDesc._width)+1;
				targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
				targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
				srcPkt = LightingEngine::EquirectFilter(*srcPkt, targetDesc, LightingEngine::EquirectFilterMode::ToGlossySpecularReference, {}, *intermediateFunction);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			} else if (request._operation == TextureCompilationRequest::Operation::EquirectFilterDiffuseReference) {
				auto srcDst = srcPkt->GetDesc();
				srcDst.wait();
				auto targetDesc = srcDst.get()._textureDesc;
				targetDesc._width = request._faceDim;
				targetDesc._height = request._faceDim;
				targetDesc._depth = 1;
				targetDesc._arrayCount = 0u;
				targetDesc._mipCount = 1;
				targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
				targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
				srcPkt = LightingEngine::EquirectFilter(*srcPkt, targetDesc, LightingEngine::EquirectFilterMode::ToDiffuseReference, {}, *intermediateFunction);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			} else if (request._operation == TextureCompilationRequest::Operation::ProjectToSphericalHarmonic) {
				auto targetDesc = TextureDesc::Plain2D(request._coefficientCount, 1, Format::R32G32B32A32_FLOAT);
				srcPkt = LightingEngine::EquirectFilter(*srcPkt, targetDesc, LightingEngine::EquirectFilterMode::ProjectToSphericalHarmonic, {}, *intermediateFunction);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			} else if (request._operation == TextureCompilationRequest::Operation::ComputeShader) {
				auto targetDesc = TextureDesc::Plain2D(
					request._width,
					request._height,
					Format::R32G32B32A32_FLOAT); // use full float precision for the pre-compression format
				auto shader = request._shader;
				if (shader.empty())
					Throw(::Assets::Exceptions::ConstructionError(_cfgFileDepVal, "Expecting 'Shader' field in texture compiler file: " + srcFN));
				srcPkt = LightingEngine::GenerateFromSamplingComputeShader(shader, targetDesc, request._sampleCount);
				_dependencies.push_back(srcPkt->GetDependencyValidation());
			}
			CompressonatorTexture input{*srcPkt};

			auto dstDesc = input._srcDesc;
			dstDesc._format = request._format;
			size_t ddsHeaderOffset = 0;
			auto destinationBlob = PrepareDDSBlob(dstDesc, ddsHeaderOffset);

			CMP_CompressOptions options = {0};
			options.dwSize       = sizeof(options);
			options.fquality     = 0.05f;
			options.dwnumThreads = 1;
			auto comprDstFormat = AsCompressonatorFormat(request._format);
			if (comprDstFormat == CMP_FORMAT_Unknown)
				Throw(std::runtime_error("Cannot write to the request texture pixel format because it is not supported by the compression library: " + srcFN));

			auto mipCount = dstDesc._mipCount;
			auto arrayLayerCount = ActualArrayLayerCount(dstDesc);
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
						Throw(std::runtime_error("Compression library failed while processing texture compiler file: " + srcFN));
				}

			_serializedArtifacts = std::vector<SerializedArtifact>{
				{
					TextureCompilerProcessType, 0, MakeFileNameSplitter(srcFN).File().AsString() + ".dds", destinationBlob
				}
			};
		}

		TextureCompileOperation(std::string srcFN, const VariantFunctions& conduit)
		{
			// load the given file and perform texture processing operations
			size_t inputBlockSize = 0;
			::Assets::FileSnapshot snapshot;
			auto dataHolder = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(srcFN, &inputBlockSize, &snapshot);
			_cfgFileDepVal = ::Assets::GetDepValSys().Make(::Assets::DependentFileState{srcFN, snapshot});
			_dependencies.push_back(_cfgFileDepVal);
			if (!inputBlockSize)
				Throw(::Assets::Exceptions::ConstructionError(_cfgFileDepVal, "Empty or missing file while loading: " + srcFN));
			auto inputData = MakeStringSection((const char*)dataHolder.get(), (const char*)PtrAdd(dataHolder.get(), inputBlockSize));
			InputStreamFormatter<> inputFormatter{inputData};
			StreamDOM<InputStreamFormatter<>> dom(inputFormatter);
			if (dom.RootElement().children().empty())
				Throw(std::runtime_error("Missing compilation operation in file: " + srcFN));

			auto operationElement = *dom.RootElement().children().begin();
			auto request = MakeTextureCompilationRequest(operationElement, srcFN);

			Initialize(request, srcFN, conduit);
		}

		TextureCompileOperation(TextureCompilationRequest request, const VariantFunctions& conduit)
		{
			std::stringstream str;
			str << request;
			Initialize(request, str.str(), conduit);
		}

	private:
		std::vector<::Assets::DependencyValidation> _dependencies;
		::Assets::DependencyValidation _cfgFileDepVal;
		std::vector<SerializedArtifact> _serializedArtifacts;
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
			[](const ::Assets::InitializerPack& initializers, const auto& conduit) {
				auto paramType = initializers.GetInitializer<unsigned>(0);
				if (paramType == 0) {
					return std::make_shared<TextureCompileOperation>(initializers.GetInitializer<std::string>(1), conduit);
				} else {
					return std::make_shared<TextureCompileOperation>(initializers.GetInitializer<TextureCompilationRequest>(1), conduit);
				}
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
		StringSection<> initializer)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString()]() mutable {
				auto splitter = MakeFileNameSplitter(init);
				if (XlEqStringI(splitter.Extension(), "texture")) {
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{0u, init});
				} else {
					TRY {
						promise.set_value(std::make_shared<TextureArtifact>(init));
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				}
			});
	}

	void TextureArtifact::ConstructToPromise(
		std::promise<std::shared_ptr<TextureArtifact>>&& promise,
		const TextureCompilationRequest& request)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[request, promise=std::move(promise)]() mutable {
				TRY {
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{1u, request});
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void TextureArtifact::ConstructToPromise(
		std::promise<std::shared_ptr<TextureArtifact>>&& promise,
		std::shared_ptr<::Assets::OperationContext> opContext,
		StringSection<> initializer)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString(), opContext=std::move(opContext)]() mutable {
				auto splitter = MakeFileNameSplitter(init);
				if (XlEqStringI(splitter.Extension(), "texture")) {
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{0u, init}, opContext.get());
				} else {
					TRY {
						promise.set_value(std::make_shared<TextureArtifact>(init));
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				}
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
					::Assets::DefaultCompilerConstructionSynchronously(std::move(promise), TextureCompilerProcessType, ::Assets::InitializerPack{1u, request}, opContext.get());
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
						::Assets::InitializerPack{1u, request},
						std::move(conduit),
						opContext.get());
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}
}}


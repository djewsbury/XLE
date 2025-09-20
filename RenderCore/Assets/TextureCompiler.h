// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureLoaders.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"

namespace Assets { class OperationContext; class DependencyValidation; class ArtifactRequestResult; class ArtifactRequest; }
namespace std { template <typename T> class function; }

namespace RenderCore { namespace Assets
{
	constexpr auto TextureCompilerProcessType = ConstHash64Legacy<'Text', 'ure'>::Value;

#if 0
	class TextureCompilationRequest
	{
	public:
		enum class Operation
		{
			Convert,
			EquirectToCubeMap,
			EquirectToCubeMapBokeh,
			EquirectFilterGlossySpecular,
			EquirectFilterGlossySpecularReference,
			EquirectFilterDiffuseReference,
			ComputeShader,
			ConversionComputeShader,
			ProjectToSphericalHarmonic,
			BalancedNoise,
			HaltonSampler
		};
		Operation _operation = Operation::Convert;
		Format _format = Format::Unknown;
		unsigned _faceDim = 512;
		unsigned _width = 512, _height = 512;
		unsigned _arrayLayerCount = 0;
		unsigned _coefficientCount = 9;
		unsigned _sampleCount = 1;

		enum class MipMapFilter { None, FromSource };
		MipMapFilter _mipMapFilter = MipMapFilter::None;

		// Compilers that operate on the GPU will attempt to split command lists so that
		// each individual one takes less than the give number of milliseconds to process
		// on the GPU.
		// This can be useful for avoiding driver timeouts; and also so that operations
		// that given progressive results don't hog the GPU to heavily
		unsigned _commandListIntervalMS = 1500;

		// For compilations that use BalancedSamplingShaderHelper, this can limit the maximum
		// number of samples that are calculated for a given cmd list. This can sometimes
		// reduce the occurrence of infinities (depending on the algorithm)
		unsigned _maxSamplesPerCmdList = ~0u;

		enum class CoordinateSystem
		{
			// A Y-up coordinate system compatible with (for example) Substance Painter
			YUp,

			// A Z-up coordinate system compatible with (for example) Blender
			ZUp
		};
		CoordinateSystem _coordinateSystem = CoordinateSystem::ZUp;

		std::string _shader;
		std::string _srcFile;

		uint64_t GetHash(uint64_t seed=DefaultSeed64) const;
	};

	std::ostream& operator<<(std::ostream&, const TextureCompilationRequest&);		// (note, on clang Initializer pack won't find this if it's called SerializationOperator)
	const char* AsString(TextureCompilationRequest::CoordinateSystem);
	std::optional<TextureCompilationRequest::CoordinateSystem> AsCoordinateSystem(StringSection<> name);

#else

	struct PostConvert
	{
		Format _format = Format::Unknown;

		friend void DeserializationOperator(Formatters::TextInputFormatter<>&, PostConvert&);
	};

	class ITextureCompiler;

	class TextureCompilationRequest
	{
	public:
		std::string _intermediateName;
		std::shared_ptr<ITextureCompiler> _subCompiler;
		std::optional<PostConvert> _postConvert;

		uint64_t CalculateHash(uint64_t seed = DefaultSeed64) const { return Hash64(_intermediateName, seed); }
		friend std::ostream& operator<<(std::ostream& str, const TextureCompilationRequest& req) { return str << req._intermediateName; }
	};

	

	/*
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> _util;
		::AssetsNew::ScaffoldIndexer indexer;
	};*/

	struct TextureCompilerSource
	{
		std::string _srcFile;

		friend void DeserializationOperator(Formatters::TextInputFormatter<>&, TextureCompilerSource&);
	};

#endif

	::Assets::Blob ConvertAndPrepareDDSBlobSync(
		BufferUploads::IAsyncDataSource& src,
		Format dstFmt);

	class TextureArtifact
	{
	public:
		const ::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }
		std::shared_ptr<BufferUploads::IAsyncDataSource> BeginDataSource(TextureLoaderFlags::BitField loadedFlags = 0) const;
		struct RawData
		{
			std::vector<uint8_t> _data;
			TextureDesc _desc;
		};
		std::future<RawData> BeginLoadRawData(TextureLoaderFlags::BitField loadedFlags = 0) const;

#if 0
		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			StringSection<> initializer);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			std::shared_ptr<::Assets::OperationContext> opContext,
			StringSection<> initializer);
#endif

		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			const TextureCompilationRequest& request);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			std::shared_ptr<::Assets::OperationContext> opContext,
			const TextureCompilationRequest& request);

		using ProgressiveResultFn = std::function<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			std::shared_ptr<::Assets::OperationContext> opContext,
			const TextureCompilationRequest& request,
			ProgressiveResultFn&& intermediateResultFn);

		TextureArtifact();
		~TextureArtifact();
		TextureArtifact(TextureArtifact&&);
		TextureArtifact& operator=(TextureArtifact&&);
		TextureArtifact(const TextureArtifact&);
		TextureArtifact& operator=(const TextureArtifact&);
		TextureArtifact(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		TextureArtifact(std::string file);
		static const ::Assets::ArtifactRequest ChunkRequests[1];
	private:
		std::string _artifactFile;
		::Assets::DependencyValidation _depVal;
	};

	static void ConstructViaTextureCompiler(
		std::promise<std::shared_ptr<TextureArtifact>>&&,
		std::shared_ptr<::Assets::OperationContext>,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
		const ::AssetsNew::ScaffoldIndexer&);

	class TextureCompilerRegistrar;
	::Assets::CompilerRegistration RegisterTextureCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);

	class ITextureCompiler;
	std::shared_ptr<ITextureCompiler> TextureCompiler_Base(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer);

	TextureCompilationRequest MakeTextureCompilationRequestSync(
		TextureCompilerRegistrar& registrar,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer);

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_BalancedNoise(unsigned width, unsigned height);
	TextureCompilationRequest MakeTextureCompilationRequest(std::shared_ptr<Assets::ITextureCompiler>, Format fmt);
}}


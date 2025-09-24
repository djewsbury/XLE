// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureLoaders.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Utility/MemoryUtils.h"

namespace Assets { class OperationContext; class DependencyValidation; class ArtifactRequestResult; class ArtifactRequest; }
namespace AssetsNew { struct ScaffoldAndEntityName; class CompoundAssetUtil; }
namespace Formatters { template<typename T> class TextInputFormatter; }
namespace std { template <typename T> class function; }

namespace RenderCore { namespace Assets
{
	constexpr auto TextureCompilerProcessType = ConstHash64Legacy<'Text', 'ure'>::Value;

	struct PostConvert
	{
		Format _format = Format::Unknown;

		friend void DeserializationOperator(Formatters::TextInputFormatter<char>&, PostConvert&);
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

	
	struct TextureCompilerSource
	{
		std::string _srcFile;

		friend void DeserializationOperator(Formatters::TextInputFormatter<char>&, TextureCompilerSource&);
	};

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


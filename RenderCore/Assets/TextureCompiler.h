// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureLoaders.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include <future>
#include <functional>

namespace RenderCore::BufferUploads { class IAsyncDataSource; }
namespace Assets { class OperationContext; class DependencyValidation; class ArtifactRequestResult; class ArtifactRequest; struct OperationContextHelper; }
namespace AssetsNew { struct ScaffoldAndEntityName; class CompoundAssetUtil; }
namespace Formatters { template<typename T> class TextInputFormatter; }
namespace Utility { class VariantFunctions; }
namespace std { template <typename T> class function; }

namespace RenderCore { namespace Assets
{
	constexpr auto TextureCompilerProcessType = ConstHash64Legacy<'Text', 'ure'>::Value;
	class TextureCompilationRequest;

	class TextureArtifact
	{
	public:
		const ::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }
		const std::string& GetArtifactFile() const { return _artifactFile; }

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ITextureCompiler;
	struct PostConvert
	{
		Format _format = Format::Unknown;
		friend void DeserializationOperator(Formatters::TextInputFormatter<char>&, PostConvert&);
	};

	struct TextureCompilerSource
	{
		std::string _srcFile;
		friend void DeserializationOperator(Formatters::TextInputFormatter<char>&, TextureCompilerSource&);
	};

	class TextureCompilationRequest
	{
	public:
		std::string _intermediateName;
		std::shared_ptr<ITextureCompiler> _subCompiler;
		std::optional<PostConvert> _postConvert;

		uint64_t CalculateHash(uint64_t seed = DefaultSeed64) const { return Hash64(_intermediateName, seed); }
		friend std::ostream& operator<<(std::ostream& str, const TextureCompilationRequest& req) { return str << req._intermediateName; }
	};

	TextureCompilationRequest MakeTextureCompilationRequest(std::shared_ptr<Assets::ITextureCompiler>, Format fmt);

	class TextureCompilerRegistrar;
	TextureCompilationRequest MakeTextureCompilationRequestSync(
		TextureCompilerRegistrar& registrar,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	::Assets::Blob ConvertAndPrepareDDSBlobSync(
		BufferUploads::IAsyncDataSource& src,
		Format dstFmt);

	class ITextureCompiler;
	std::shared_ptr<ITextureCompiler> TextureCompiler_Base(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer);

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_BalancedNoise(unsigned width, unsigned height);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ITextureCompiler
	{
	public:
		struct Context
		{
			::Assets::OperationContextHelper* _opContext = nullptr;
			const VariantFunctions* _conduit = nullptr;
			std::vector<::Assets::DependencyValidation> _dependencies;
		};

		virtual std::string GetIntermediateName() const = 0;
		virtual std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& ctx) = 0;
		virtual ~ITextureCompiler();
	};

	class TextureCompilerRegistrar
	{
	public:
		using SubCompilerFunctionSig = std::shared_ptr<ITextureCompiler>(
			std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
			const ::AssetsNew::ScaffoldAndEntityName&);

		using RegistrationId = unsigned;
		RegistrationId Register(std::function<SubCompilerFunctionSig>&&);
		void Deregister(RegistrationId);

		std::shared_ptr<ITextureCompiler> TryBeginCompile(
			std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
			const ::AssetsNew::ScaffoldAndEntityName&);

		TextureCompilerRegistrar();
		~TextureCompilerRegistrar();

	protected:
		Threading::Mutex _mutex;
		std::vector<std::pair<RegistrationId, std::function<SubCompilerFunctionSig>>> _fns;
		RegistrationId _nextRegistrationId;
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	::Assets::CompilerRegistration RegisterTextureCompilerInfrastructure(
		::Assets::IIntermediateCompilers& intermediateCompilers);
}}


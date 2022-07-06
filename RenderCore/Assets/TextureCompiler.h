// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TextureLoaders.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../Assets/IArtifact.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace RenderCore { namespace Assets
{
	static const auto TextureCompilerProcessType = ConstHash64<'Text', 'ure'>::Value;

	class TextureCompilationRequest
	{
	public:
		enum class Operation
		{
			Convert,
			EquRectToCubeMap,
			EquiRectFilterGlossySpecular,
			ComputeShader,
			ProjectToSphericalHarmonic
		};
		Operation _operation = Operation::Convert;
		Format _format = Format::Unknown;
		unsigned _faceDim = 512;
		unsigned _width = 512, _height = 512;
		unsigned _coefficientCount = 9;

		enum class MipMapFilter { None, FromSource };
		MipMapFilter _mipMapFilter = MipMapFilter::None;

		std::string _shader;
		std::string _srcFile;

		uint64_t GetHash(uint64_t seed=DefaultSeed64) const;
	};

	std::ostream& SerializationOperator(std::ostream&, const TextureCompilationRequest&);

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
			StringSection<> initializer);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<TextureArtifact>>&&,
			const TextureCompilationRequest& request);

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

	::Assets::CompilerRegistration RegisterTextureCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers);
}}


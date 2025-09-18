// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SignatureAsset.h"
#include "ParseHLSL.h"
#include "GraphSyntax.h"
#include "Assets/BlockSerializer.h"
#include "Assets/ICompileOperation.h"
#include "Assets/NascentChunk.h"
#include "Utility/MemoryUtils.h"

using namespace Utility::Literals;

namespace ShaderSourceParser
{
	const ::Assets::ArtifactRequest SignatureAsset::ChunkRequests[1]
	{
		::Assets::ArtifactRequest { "Scaffold", "shader-signature"_h, 1u, ::Assets::ArtifactRequest::DataType::BlockSerializer },
	};

	struct SignatureAssetData
	{
		GraphLanguage::ShaderFragmentSignature _signature;
		uint32_t _isGraphSyntaxFile;
	};

	auto SignatureAsset::GetSignature() const -> const GraphLanguage::ShaderFragmentSignature&
	{
		return ((const SignatureAssetData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get()))->_signature;
	}

	bool SignatureAsset::IsGraphSyntaxFile() const
	{
		return !!((const SignatureAssetData*)::Assets::Block_GetFirstObject(_rawMemoryBlock.get()))->_isGraphSyntaxFile;
	}

	SignatureAsset::SignatureAsset(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		assert(chunks.size() == 1);
		_rawMemoryBlock = std::move(chunks[0]._buffer);
		_rawMemoryBlockSize = chunks[0]._bufferSize;
	}

	SignatureAsset::~SignatureAsset() {}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	::Assets::SimpleCompilerResult CompileShaderSignatureResource(const ::Assets::InitializerPack& initializers)
	{
		auto fn = initializers.GetInitializer<std::string>(0);
		size_t fileSize;
		::Assets::FileSnapshot fileState;
		auto f = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(fn, &fileSize, &fileState);
		auto depVal = ::Assets::GetDepValSys().Make(::Assets::DependentFileState{fn, fileState});
		if (!f || !fileSize)
			Throw(::Assets::Exceptions::ConstructionError(depVal, "Missing or empty source file while generating signature: " + fn));

		auto srcFile = MakeStringSection((const char*)f.get(), (const char*)PtrAdd(f.get(), fileSize));

		bool isGraphSyntaxFile = false;
		GraphLanguage::ShaderFragmentSignature signature;
		TRY {
			if (XlEqStringI(MakeFileNameSplitter(fn).Extension(), "graph")) {
				auto graphSyntax = GraphLanguage::ParseGraphSyntax(srcFile);
				for (auto& subGraph:graphSyntax._subGraphs)
					signature._functions.emplace_back(std::make_pair(subGraph.first, std::move(subGraph.second._signature)));
				isGraphSyntaxFile = true;
			} else
				signature = ParseHLSL(srcFile);
		} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, depVal));
		} CATCH(const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, depVal));
		} CATCH_END
	
		// write the processed version to a blockSerializer
		::Assets::BlockSerializer blockSerializer;
		blockSerializer << signature;
		blockSerializer << (uint32_t)isGraphSyntaxFile;

		return {
			{
				std::vector<::Assets::SerializedArtifact>{
					{ "shader-signature"_h, 1, fn, ::Assets::AsBlob(blockSerializer) }
				},
				depVal
			},
			"shader-signature"_h
		};
	}

	::Assets::CompilerRegistration RegisterSignatureAssetCompiler(::Assets::IIntermediateCompilers& compilers)
	{
		auto result = ::Assets::RegisterSimpleCompiler(
			compilers,
			"shader-signature-compiler",
			"shader-signature-compiler",
			CompileShaderSignatureResource,
			[](::Assets::ArtifactTargetCode targetCode, const ::Assets::InitializerPack& initializers) {
				::Assets::IIntermediateCompilers::SplitArchiveName result;
				auto fn = initializers.GetInitializer<std::string>(0);
				result._entryId = Hash64(fn);
				result._archive = "signature";
				result._descriptiveName = fn;
				return result;
			});

		uint64_t outputAssetTypes[] = { GetCompileProcessType((SignatureAsset*)nullptr) };
		compilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

}

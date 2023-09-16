// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialCompiler.h"
#include "RawMaterial.h"
#include "MaterialScaffold.h"
#include "MaterialMachine.h"
#include "AssetUtils.h"
#include "ModelCompilationConfiguration.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/ChunkFileWriter.h"
#include "../../Assets/Assets.h"
#include "../../Assets/NascentChunk.h"
#include "../../Assets/MemoryFile.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/AssetMixins.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Formatters/StreamDOM.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/Conversion.h"

namespace RenderCore
{
	::Assets::BlockSerializer& SerializationOperator(::Assets::BlockSerializer& str, const SamplerDesc& sampler)
	{
		str << (uint32_t)sampler._filter;
		str << (uint32_t)sampler._addressU;
		str << (uint32_t)sampler._addressV;
		str << (uint32_t)sampler._addressW;
		str << (uint32_t)sampler._comparison;
		str << sampler._flags;
		return str;
	}
}

namespace RenderCore { namespace Assets
{
	class ModelCompilationConfiguration;

	MaterialGuid MakeMaterialGuid(StringSection<> name)
	{
		//  If the material name is just a number, then we will use that
		//  as the guid. Otherwise we hash the name.
        MaterialGuid result = 0;
		const char* parseEnd = FastParseValue(name, result);
		if (parseEnd != name.end()) { result = Hash64(name.begin(), name.end()); }
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Type>
		static std::pair<std::unique_ptr<uint8_t[], PODAlignedDeletor>, size_t> SerializeViaStreamFormatterToBuffer(const Type& type)
	{
		MemoryOutputStream<utf8> strm;
		{ Formatters::TextOutputFormatter fmtter{strm}; fmtter << type; }
		auto strmBuffer = MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End());
		std::unique_ptr<uint8_t[], PODAlignedDeletor> result { (uint8_t*)XlMemAlign(strmBuffer.size(), sizeof(uint64_t)) };
		std::memcpy(result.get(), strmBuffer.begin(), strmBuffer.size());
		return { std::move(result), strmBuffer.size() };
	}
	
	static ::Assets::SimpleCompilerResult MaterialCompileOperation(const ::Assets::InitializerPack& initializers)
	{
		std::string sourceMaterial, sourceModel;
		std::shared_ptr<ModelCompilationConfiguration> sourceModelConfiguration;
		sourceMaterial = initializers.GetInitializer<std::string>(0);
		if (initializers.GetCount() >= 2)
			sourceModel = initializers.GetInitializer<std::string>(1);
		if (initializers.GetCount() >= 3 && initializers.GetInitializerType(2).hash_code() == typeid(decltype(sourceModelConfiguration)).hash_code())
			sourceModelConfiguration = initializers.GetInitializer<decltype(sourceModelConfiguration)>(2);

		if (sourceModel.empty())
			Throw(::Exceptions::BasicLabel{"Empty source model in MaterialCompileOperation"});

		if (sourceMaterial == sourceModel)
			sourceMaterial = {};

		char buffer[3*MaxPath];

			// Ensure we strip off parameters from the source model filename before we get here.
			// the parameters are irrelevant to the compiler -- so if they stay on the request
			// name, will we end up with multiple assets that are equivalent
		{
			auto splitter = MakeFileNameSplitter(sourceModel);
			if (!splitter.ParametersWithDivider().IsEmpty())
				sourceModel = splitter.AllExceptParameters().AsString();
		}

		auto modelMatFuture = ::Assets::MakeAssetMarker<RawMatConfigurations>(sourceModel, sourceModelConfiguration);
		auto modelMatState = modelMatFuture->StallWhilePending();
		if (modelMatState == ::Assets::AssetState::Invalid)
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
				modelMatFuture->GetDependencyValidation(),
				StringMeldInPlace(buffer) << "Failed while loading material information from source model (" << sourceModel << ") with msg (" << ::Assets::AsString(modelMatFuture->GetActualizationLog()) << ")"));

		const auto& modelMat = modelMatFuture->Actualize();

		std::vector<::Assets::DependencyValidationMarker> depVals;
		depVals.emplace_back(modelMat.GetDependencyValidation());

			//  for each configuration, we want to build a resolved material
		struct PendingAssets
		{
			SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>> _resolvedNames;
			using MaterialMarker = std::shared_ptr<::Assets::Marker<ResolvedMaterial>>;
			std::vector<std::pair<MaterialGuid, MaterialMarker>> _materials;
		} pendingAssets;
		pendingAssets._resolvedNames.reserve(modelMat._configurations.size());
		pendingAssets._materials.reserve(modelMat._configurations.size());

		for (const auto& cfg:modelMat._configurations) {
			ShaderPatchCollection patchCollection;
			std::basic_stringstream<::Assets::ResChar> resName;
			auto guid = MakeMaterialGuid(MakeStringSection(cfg));

				// Our resolved material comes from 3 separate inputs:
				//  1) model:configuration
				//  2) material:*
				//  3) material:configuration
				//
				// Some material information is actually stored in the model
				// source data. This is just for art-pipeline convenience --
				// generally texture assignments (and other settings) are 
				// set in the model authoring tool (eg, 3DS Max). The .material
				// files actually only provide overrides for settings that can't
				// be set within 3rd party tools.
				// 
				// We don't combine the model and material information until
				// this step -- this gives us some flexibility to use the same
				// model with different material files. The material files can
				// also override settings from 3DS Max (eg, change texture assignments
				// etc). This provides a path for reusing the same model with
				// different material settings (eg, when we want one thing to have
				// a red version and a blue version)

			std::vector<::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>> partialMaterials;
		
				// resolve in model:configuration
				// This is a little different, because we have to pass the "sourceModelConfiguration" down the chain
			{
				auto meld = StringMeldInPlace(buffer);
				meld << sourceModel << ":" << cfg;
				auto partialMaterial = ::Assets::MakeAssetMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>(meld.AsStringSection(), sourceModelConfiguration);
				partialMaterials.emplace_back(std::move(partialMaterial));
			}

			if (!sourceMaterial.empty()) {
					// resolve in material:*
				{
					auto meld = StringMeldInPlace(buffer);
					meld << sourceMaterial << ":*";
					auto partialMaterial = ::Assets::MakeAssetMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>(meld.AsStringSection());
					partialMaterials.emplace_back(std::move(partialMaterial));
				}

					// resolve in the main material:cfg
				{
					auto meld = StringMeldInPlace(buffer);
					meld << sourceMaterial << ":" << cfg;
					auto partialMaterial = ::Assets::MakeAssetMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>(meld.AsStringSection());
					partialMaterials.emplace_back(std::move(partialMaterial));
				}
			}

			auto marker = std::make_shared<::Assets::Marker<ResolvedMaterial>>();
			ResolvedMaterial::ConstructToPromise(marker->AdoptPromise(), partialMaterials);

			pendingAssets._materials.push_back(std::make_pair(guid, std::move(marker)));

			SerializableVector<char> resNameVec(cfg.begin(), cfg.end());
			pendingAssets._resolvedNames.push_back(std::make_pair(guid, std::move(resNameVec)));
		}

		struct SerializedBlock1
		{
			uint64_t _hash = 0;
			size_t _dataSize = 0;
			std::unique_ptr<uint8_t[], PODAlignedDeletor> _data;
		};
		struct SerializedBlock2
		{
			uint64_t _hash = 0;
			::Assets::BlockSerializer _subBlock;
		};
		std::vector<SerializedBlock2> resolved;
		std::vector<SerializedBlock1> patchCollections;
		resolved.reserve(pendingAssets._materials.size());
		patchCollections.reserve(pendingAssets._materials.size());

		for (const auto&m:pendingAssets._materials) {
			auto state = m.second->StallWhilePending();
			assert(state.value() == ::Assets::AssetState::Ready);
			auto& resolvedMat = m.second->Actualize();

			::Assets::BlockSerializer tempBlock;

			if (resolvedMat._resources.GetCount())
				tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachShaderResourceBindings, resolvedMat._resources);
			if (resolvedMat._selectors.GetCount())
				tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachSelectors, resolvedMat._selectors);
			if (resolvedMat._uniforms.GetCount())
				tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachConstants, resolvedMat._uniforms);
			if (!resolvedMat._samplers.empty()) {
				tempBlock << (uint32_t)MaterialCommand::AttachSamplerBindings;
				auto recall = tempBlock.CreateRecall(sizeof(uint32_t));
				for (auto& s:resolvedMat._samplers) {
					tempBlock << Hash64(s.first);
					tempBlock << s.second;
				}
				tempBlock.PushSizeValueAtRecall(recall);
			}
			tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachStateSet, resolvedMat._stateSet.GetHash());

			if (resolvedMat._patchCollection.GetHash() != 0) {
				tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachPatchCollectionId, resolvedMat._patchCollection.GetHash());

				bool gotExisting = false;
				for (const auto&p:patchCollections)
					gotExisting |= p._hash == resolvedMat._patchCollection.GetHash();

				if (!gotExisting) {
					// ShaderPatchCollection is mostly strings; so we just serialize it as a text block
					auto buffer = SerializeViaStreamFormatterToBuffer(resolvedMat._patchCollection);
					patchCollections.emplace_back(SerializedBlock1{resolvedMat._patchCollection.GetHash(), buffer.second, std::move(buffer.first)});
				}
			}

			resolved.emplace_back(SerializedBlock2{m.first, std::move(tempBlock)});
			depVals.emplace_back(resolvedMat.GetDependencyValidation());
		}

		std::sort(resolved.begin(), resolved.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
		std::sort(patchCollections.begin(), patchCollections.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
		std::sort(pendingAssets._resolvedNames.begin(), pendingAssets._resolvedNames.end(), CompareFirst<MaterialGuid, SerializableVector<char>>());

			// "resolved" is now actually the data we want to write out
		::Assets::BlockSerializer blockSerializer;
		auto outerRecall = blockSerializer.CreateRecall(sizeof(uint32_t));
		for (const auto& m:resolved) {
			blockSerializer << (uint32_t)ScaffoldCommand::Material;
			blockSerializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			blockSerializer << m._hash;
			blockSerializer << m._subBlock.SizePrimaryBlock();
			blockSerializer.SerializeSubBlock(m._subBlock);
		}
		for (const auto& pc:patchCollections) {
			blockSerializer << (uint32_t)ScaffoldCommand::ShaderPatchCollection;
			blockSerializer << (uint32_t)(sizeof(size_t) + sizeof(size_t) + sizeof(uint64_t));
			blockSerializer << pc._hash;
			blockSerializer << pc._dataSize;
			blockSerializer.SerializeSubBlock(MakeIteratorRange(pc._data.get(), PtrAdd(pc._data.get(), pc._dataSize)));
		}
		blockSerializer << MakeCmdAndSerializable(ScaffoldCommand::MaterialNameDehash, pendingAssets._resolvedNames);
		blockSerializer.PushSizeValueAtRecall(outerRecall);

		return {
			std::vector<::Assets::SerializedArtifact>{
				{
					ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
					(StringMeld<256>() << sourceModel << "&" << sourceMaterial).AsString(),
					::Assets::AsBlob(blockSerializer)
				}
			},
			::Assets::GetDepValSys().MakeOrReuse(depVals),
			MaterialScaffold::CompileProcessType
		};
	}

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		auto result = ::Assets::RegisterSimpleCompiler(intermediateCompilers, "material-scaffold-compiler", "material-scaffold-compiler", MaterialCompileOperation);
		uint64_t outputAssetTypes[] = { MaterialScaffold::CompileProcessType };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

}}


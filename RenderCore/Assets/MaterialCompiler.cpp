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
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/ChunkFile.h"
#include "../../Assets/Assets.h"
#include "../../Assets/NascentChunk.h"
#include "../../Assets/IntermediatesStore.h"
#include "../../Assets/MemoryFile.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Assets
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	static void AddDep(
		std::vector<::Assets::DependentFileState>& deps,
		const ::Assets::DependentFileState& newDep)
	{
		auto existing = std::find_if(
			deps.cbegin(), deps.cend(),
			[&](const ::Assets::DependentFileState& test) { return test._filename == newDep._filename; });
		if (existing == deps.cend())
			deps.push_back(newDep);
	}
	
	static void AddDep(
		std::vector<::Assets::DependentFileState>& deps,
		StringSection<> newDep)
	{
			// we need to call "GetDependentFileState" first, because this can change the
			// format of the filename. String compares alone aren't working well for us here
		AddDep(deps, ::Assets::IntermediatesStore::GetDependentFileState(newDep));
	}

	template<typename Type>
		static std::pair<std::unique_ptr<uint8_t[], PODAlignedDeletor>, size_t> SerializeViaStreamFormatterToBuffer(const Type& type)
	{
		MemoryOutputStream<utf8> strm;
		{ OutputStreamFormatter fmtter{strm}; fmtter << type; }
		auto strmBuffer = MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End());
		std::unique_ptr<uint8_t[], PODAlignedDeletor> result { (uint8_t*)XlMemAlign(strmBuffer.size(), sizeof(uint64_t)) };
		std::memcpy(result.get(), strmBuffer.begin(), strmBuffer.size());
		return { std::move(result), strmBuffer.size() };
	}

	class MaterialCompileOperation : public ::Assets::ICompileOperation
	{
	public:

		std::vector<TargetDesc> GetTargets() const
		{
			if (_compilationException)
				return { 
					TargetDesc { MaterialScaffold::CompileProcessType, "compilation-exception" }
				};
			if (_serializedArtifacts.empty()) return {};
			return {
				TargetDesc { MaterialScaffold::CompileProcessType, _serializedArtifacts[0]._name.c_str() }
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

		MaterialCompileOperation(const ::Assets::InitializerPack& initializers)
		{
			TRY
			{
				std::string sourceMaterial, sourceModel;
				sourceMaterial = initializers.GetInitializer<std::string>(0);
				if (initializers.GetCount() >= 2)
					sourceModel = initializers.GetInitializer<std::string>(1);

				if (sourceModel.empty())
					Throw(::Exceptions::BasicLabel{"Empty source model in MaterialCompileOperation"});

				if (sourceMaterial == sourceModel)
					sourceMaterial = {};

					// Ensure we strip off parameters from the source model filename before we get here.
					// the parameters are irrelevant to the compiler -- so if they stay on the request
					// name, will we end up with multiple assets that are equivalent
				{
					auto splitter = MakeFileNameSplitter(sourceModel);
					if (!splitter.ParametersWithDivider().IsEmpty())
						sourceModel = splitter.AllExceptParameters().AsString();
				}

				AddDep(_dependencies, sourceModel);        // we need need a dependency (even if it's a missing file)

				auto modelMatFuture = ::Assets::MakeAsset<RawMatConfigurations>(sourceModel);
				auto modelMatState = modelMatFuture->StallWhilePending();
				if (modelMatState == ::Assets::AssetState::Invalid) {
					Throw(::Assets::Exceptions::ConstructionError(
						::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
						modelMatFuture->GetDependencyValidation(),
						"Failed while loading material information from source model (%s) with msg (%s)", sourceModel.c_str(), 
							::Assets::AsString(modelMatFuture->GetActualizationLog()).c_str()));
				}

				const auto& modelMat = modelMatFuture->Actualize();

					//  for each configuration, we want to build a resolved material
					//  Note that this is a bit crazy, because we're going to be loading
					//  and re-parsing the same files over and over again!
				SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>> resolvedNames;
				std::vector<std::pair<MaterialGuid, std::shared_ptr<::Assets::Marker<ResolvedMaterial>>>> materialFutures;
				resolvedNames.reserve(modelMat._configurations.size());
				materialFutures.reserve(modelMat._configurations.size());

				for (const auto& cfg:modelMat._configurations) {
					MaterialScaffold::Material resMat;
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
				
						// resolve in model:configuration
					StringMeld<3*MaxPath, ::Assets::ResChar> meld; 
					meld << sourceModel << ":" << Conversion::Convert<::Assets::rstring>(cfg);

					if (!sourceMaterial.empty()) {
							// resolve in material:*
						meld << ";" << sourceMaterial << ":*";
						meld << ";" << sourceMaterial << ":" << Conversion::Convert<::Assets::rstring>(cfg);
					}

					auto futureMaterial = ::Assets::MakeAsset<ResolvedMaterial>(meld.AsStringSection());
					materialFutures.push_back(std::make_pair(guid, std::move(futureMaterial)));

					auto resNameStr = meld.AsString();
					SerializableVector<char> resNameVec(resNameStr.begin(), resNameStr.end());
					resolvedNames.push_back(std::make_pair(guid, std::move(resNameVec)));
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
				resolved.reserve(materialFutures.size());
				patchCollections.reserve(materialFutures.size());

				for (const auto&m:materialFutures) {
					auto state = m.second->StallWhilePending();
					assert(state.value() == ::Assets::AssetState::Ready);
					auto& resolvedMat = m.second->Actualize();

					::Assets::BlockSerializer tempBlock;

					if (resolvedMat._resourceBindings.GetCount())
						tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachShaderResourceBindings, resolvedMat._resourceBindings);
					if (resolvedMat._matParamBox.GetCount())
						tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachSelectors, resolvedMat._matParamBox);
					if (resolvedMat._constants.GetCount())
						tempBlock << MakeCmdAndSerializable(MaterialCommand::AttachConstants, resolvedMat._constants);
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

					for (const auto& d:resolvedMat._depFileStates)
						AddDep(_dependencies, d);
				}

				std::sort(resolved.begin(), resolved.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
				std::sort(patchCollections.begin(), patchCollections.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
				std::sort(resolvedNames.begin(), resolvedNames.end(), CompareFirst<MaterialGuid, SerializableVector<char>>());

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
				blockSerializer << MakeCmdAndSerializable(ScaffoldCommand::MaterialNameDehash, resolvedNames);
				blockSerializer.PushSizeValueAtRecall(outerRecall);

				_serializedArtifacts = std::vector<SerializedArtifact>{
					{
						ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
						(StringMeld<256>() << sourceModel << "&" << sourceMaterial).AsString(),
						::Assets::AsBlob(blockSerializer)
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

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"material-scaffold-compiler",
			"material-scaffold-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				return std::make_shared<MaterialCompileOperation>(initializers);
			}};

		uint64_t outputAssetTypes[] = { MaterialScaffold::CompileProcessType };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

}}


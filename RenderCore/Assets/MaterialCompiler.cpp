// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialCompiler.h"
#include "RawMaterial.h"
#include "MaterialScaffold.h"
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
		StringSection<> newDep)
	{
			// we need to call "GetDependentFileState" first, because this can change the
			// format of the filename. String compares alone aren't working well for us here
		auto depState = ::Assets::IntermediatesStore::GetDependentFileState(newDep);
		auto existing = std::find_if(
			deps.cbegin(), deps.cend(),
			[&](const ::Assets::DependentFileState& test) { return test._filename == depState._filename; });
		if (existing == deps.cend())
			deps.push_back(depState);
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

					// Parameters must be stripped off the source model filename before we get here.
					// the parameters are irrelevant to the compiler -- so if they stay on the request
					// name, will we end up with multiple assets that are equivalent
				assert(MakeFileNameSplitter(sourceModel).ParametersWithDivider().IsEmpty());

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

				auto modelMat = modelMatFuture->Actualize();

					//  for each configuration, we want to build a resolved material
					//  Note that this is a bit crazy, because we're going to be loading
					//  and re-parsing the same files over and over again!
				SerializableVector<std::pair<MaterialGuid, MaterialScaffold::Material>> resolved;
				SerializableVector<std::pair<MaterialGuid, SerializableVector<char>>> resolvedNames;
				std::vector<ShaderPatchCollection> patchCollections;
				resolved.reserve(modelMat->_configurations.size());
				patchCollections.reserve(modelMat->_configurations.size());

				auto searchRules = ::Assets::DefaultDirectorySearchRules(sourceModel);
				::Assets::ResChar resolvedSourceMaterial[MaxPath];
				ResolveMaterialFilename(resolvedSourceMaterial, dimof(resolvedSourceMaterial), searchRules, sourceMaterial);
				searchRules.AddSearchDirectoryFromFilename(resolvedSourceMaterial);

				using Meld = StringMeld<MaxPath, ::Assets::ResChar>;
				for (const auto& cfg:modelMat->_configurations) {

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
					Meld meld; meld << sourceModel << ":" << Conversion::Convert<::Assets::rstring>(cfg);
					resName << meld;

					MergeIn_Stall(resMat, patchCollection, meld.AsStringSection(), searchRules, _dependencies);

					if (resolvedSourceMaterial[0] != '\0') {
							// resolve in material:*
						Meld starInit; starInit << resolvedSourceMaterial << ":*";
						Meld configInit; configInit << resolvedSourceMaterial << ":" << Conversion::Convert<::Assets::rstring>(cfg);
					
						MergeIn_Stall(resMat, patchCollection, starInit.AsStringSection(), searchRules, _dependencies);
						MergeIn_Stall(resMat, patchCollection, configInit.AsStringSection(), searchRules, _dependencies);

						resName << ";" << starInit << ";" << configInit;
					}

					resolved.push_back(std::make_pair(guid, std::move(resMat)));

					auto resNameStr = resName.str();
					SerializableVector<char> resNameVec(resNameStr.begin(), resNameStr.end());
					resolvedNames.push_back(std::make_pair(guid, std::move(resNameVec)));

					bool gotExisting = false;
					for (const auto&p:patchCollections)
						gotExisting |= p.GetHash() == patchCollection.GetHash();

					if (!gotExisting)
						patchCollections.emplace_back(std::move(patchCollection));
				}

				std::sort(resolved.begin(), resolved.end(), CompareFirst<MaterialGuid, MaterialScaffold::Material>());
				std::sort(resolvedNames.begin(), resolvedNames.end(), CompareFirst<MaterialGuid, SerializableVector<char>>());

					// "resolved" is now actually the data we want to write out
				::Assets::NascentBlockSerializer blockSerializer;
				SerializationOperator(blockSerializer, resolved);
				SerializationOperator(blockSerializer, resolvedNames);

				MemoryOutputStream<utf8> patchCollectionStrm;
				{
					OutputStreamFormatter fmtter(patchCollectionStrm);
					SerializeShaderPatchCollectionSet(fmtter, MakeIteratorRange(patchCollections));
				}

				_serializedArtifacts = std::vector<SerializedArtifact>{
					{
						ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
						(Meld() << sourceModel << "&" << sourceMaterial).AsString(),
						::Assets::AsBlob(blockSerializer)
					},
					{
						ChunkType_PatchCollections, ResolvedMat_ExpectedVersion, 
						(Meld() << sourceModel << "&" << sourceMaterial).AsString(),
						::Assets::AsBlob(MakeIteratorRange(patchCollectionStrm.GetBuffer().Begin(), patchCollectionStrm.GetBuffer().End()))
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

	::Assets::IIntermediateCompilers::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		auto result = intermediateCompilers.RegisterCompiler(
			"material-scaffold-compiler",
			"material-scaffold-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				return std::make_shared<MaterialCompileOperation>(initializers);
			});

		uint64_t outputAssetTypes[] = { MaterialScaffold::CompileProcessType };
		intermediateCompilers.AssociateRequest(
			result._registrationId,
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

}}


// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MaterialCompiler.h"
#include "RawMaterial.h"
#include "CompiledMaterialSet.h"
#include "MaterialMachine.h"
#include "AssetUtils.h"
#include "ModelCompilationConfiguration.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/ChunkFileWriter.h"
#include "../../Assets/Assets.h"
#include "../../Assets/NascentChunk.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Formatters/StreamDOM.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/FastParseValue.h"

using namespace Utility::Literals;

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

	using ResolvedMaterial = ::Assets::AssetWrapper<RawMaterial>;
	using CompoundAssetScaffold = ::AssetsNew::CompoundAssetScaffold;
	using ContextImbuedMaterialSet = ::Assets::ContextImbuedAsset<sp<CompoundAssetScaffold>>;

	namespace Internal
	{
		struct PendingAssets
		{
			SerializableVector<std::pair<MaterialGuid, SerializableString>> _resolvedNames;
			using MaterialMarker = sp<::Assets::Marker<ResolvedMaterial>>;
			std::vector<std::pair<MaterialGuid, MaterialMarker>> _materials;
		};

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

		void Serialize(
			::Assets::BlockSerializer& blockSerializer,
			PendingAssets& pendingAssets,
			std::vector<::Assets::DependencyValidationMarker>& depVals)
		{
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
				auto& actualized = m.second->Actualize();
				auto& resolvedMat = std::get<0>(actualized);

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
				depVals.emplace_back(std::get<::Assets::DependencyValidation>(actualized));
			}

			std::sort(resolved.begin(), resolved.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
			std::sort(patchCollections.begin(), patchCollections.end(), [](const auto& lhs, const auto& rhs) { return lhs._hash < rhs._hash; });
			std::sort(pendingAssets._resolvedNames.begin(), pendingAssets._resolvedNames.end(), CompareFirst2{});

				// "resolved" is now actually the data we want to write out
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
		}

		static sp<::Assets::Marker<ContextImbuedMaterialSet>> MakeModelMatFuture(std::string sourceModel, sp<ModelCompilationConfiguration> sourceModelConfiguration)
		{
			if (sourceModelConfiguration) return ::Assets::GetAssetMarker<ContextImbuedMaterialSet>(sourceModel, sourceModelConfiguration);
			else return ::Assets::GetAssetMarker<ContextImbuedMaterialSet>(sourceModel);
		}


		struct SourceModelHelper
		{
			std::string _sourceModel;
			sp<ModelCompilationConfiguration> _sourceModelConfiguration;
			::Assets::ContextImbuedAsset<sp<CompoundAssetScaffold>> _modelMat;
			std::vector<std::string> _modelMatConfigs;

			IteratorRange<const std::string*> GetModelMatConfigs() const { return _modelMatConfigs; }
			::Assets::DependencyValidation GetModelMatDepVal() const { return _modelMat.GetDependencyValidation(); }

			::AssetsNew::ScaffoldAndEntityName GetMaterialMarkerPtr(const std::string& cfg) const
			{
				return ::AssetsNew::ScaffoldAndEntityName{ _modelMat, Hash64(cfg) DEBUG_ONLY(, cfg) };
			}

			SourceModelHelper(
				std::string sourceModel,
				sp<ModelCompilationConfiguration> sourceModelConfiguration)
			: _sourceModel(std::move(sourceModel)), _sourceModelConfiguration(std::move(sourceModelConfiguration))
			{
					// Ensure we strip off parameters from the source model filename before we get here.
					// the parameters are irrelevant to the compiler -- so if they stay on the request
					// name, will we end up with multiple assets that are equivalent
				{
					auto splitter = MakeFileNameSplitter(_sourceModel);
					if (!splitter.ParametersWithDivider().IsEmpty())
						_sourceModel = splitter.AllExceptParameters().AsString();
				}

				auto modelMatFuture = MakeModelMatFuture(_sourceModel, _sourceModelConfiguration);
				auto modelMatState = modelMatFuture->StallWhilePending();
				if (modelMatState == ::Assets::AssetState::Invalid)
					Throw(::Assets::Exceptions::ConstructionError(
						::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
						modelMatFuture->GetDependencyValidation(),
						StringMeld<3*MaxPath>() << "Failed while loading material information from source model (" << _sourceModel << ") with msg (" << ::Assets::AsString(modelMatFuture->GetActualizationLog()) << ")"));
				_modelMat = modelMatFuture->Actualize();

				// todo -- add all entities with a RawMaterial (etc) component to _sourceModelConfiguration
			}

			SourceModelHelper(::Assets::ContextImbuedAsset<sp<CompoundAssetScaffold>> modelMat) : _modelMat(std::move(modelMat)) {}
			SourceModelHelper() = default;
		};
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	using MaterialFuture = std::shared_future<ResolvedMaterial>;
	static sp<::Assets::Marker<ResolvedMaterial>> MergePartialMaterials(const std::vector<MaterialFuture>& partialMaterials)
	{
		auto marker = std::make_shared<::Assets::Marker<ResolvedMaterial>>();
		::Assets::PollToPromise(
			marker->AdoptPromise(),
			[partialMaterials](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:partialMaterials)
					if (f.wait_until(timeoutTime) == std::future_status::timeout)
						return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[partialMaterials]() {
				if (partialMaterials.size() == 1)
					return partialMaterials.front().get();

				std::vector<::Assets::DependencyValidationMarker> dvs; dvs.reserve(partialMaterials.size());
				auto& actualized = partialMaterials.front().get();
				RawMaterial mergedResult { actualized.get() };
				dvs.emplace_back(actualized.GetDependencyValidation());

				for (auto i=partialMaterials.begin()+1; i!=partialMaterials.end(); ++i) {
					auto& actualized = (*i).get();
					mergedResult.MergeInWithFilenameResolve(actualized.get(), {});
					dvs.emplace_back(actualized.GetDependencyValidation());
				}

				return ResolvedMaterial { std::move(mergedResult), ::Assets::GetDepValSys().MakeOrReuse(dvs) };
			});
		return marker;
	}
	
	static ::Assets::SimpleCompilerResult MaterialCompileOperation(const ::Assets::InitializerPack& initializers)
	{
		std::string sourceMaterialName, sourceModelName;
		sp<ModelCompilationConfiguration> sourceModelConfiguration;
		sourceMaterialName = initializers.GetInitializer<std::string>(0);
		if (initializers.GetCount() >= 2)
			sourceModelName = initializers.GetInitializer<std::string>(1);
		if (initializers.GetCount() >= 3 && initializers.GetInitializerType(2).hash_code() == typeid(decltype(sourceModelConfiguration)).hash_code())
			sourceModelConfiguration = initializers.GetInitializer<decltype(sourceModelConfiguration)>(2);

		if (sourceModelName.empty())
			Throw(::Exceptions::BasicLabel{"Empty source model in MaterialCompileOperation"});

		if (sourceMaterialName == sourceModelName)
			sourceMaterialName = {};

		auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();

		Internal::SourceModelHelper sourceModelHelper { sourceModelName, std::move(sourceModelConfiguration) };
		auto modelMat = sourceModelHelper.GetModelMatConfigs();

		ContextImbuedMaterialSet sourceMaterial;
		if (!sourceMaterialName.empty())
			sourceMaterial = ::Assets::ActualizeAsset<ContextImbuedMaterialSet>(sourceMaterialName);

			//  for each configuration, we want to build a resolved material
		Internal::PendingAssets pendingAssets;
		pendingAssets._resolvedNames.reserve(modelMat.size());
		pendingAssets._materials.reserve(modelMat.size());

		char buffer[3*MaxPath];
		for (const auto& cfg:modelMat) {
			std::basic_stringstream<::Assets::ResChar> resName;
			auto guid = MakeMaterialGuid(MakeStringSection(cfg));

				// Our resolved material comes from 3 separate inputs:
				//  1) model:configuration
				//  2) material:configuration
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

			std::vector<std::shared_future<ResolvedMaterial>> partialMaterials;
		
				// resolve in model:configuration
				// This is a little different, because we have to pass the "sourceModelConfiguration" down the chain
			partialMaterials.emplace_back(
				util->GetCachedAssetFuture<RawMaterial>("RawMaterial"_h, sourceModelHelper.GetMaterialMarkerPtr(cfg)));

			if (!sourceMaterial.get()) {
					// resolve in the main material:cfg
				partialMaterials.emplace_back(
					util->GetCachedAssetFuture<RawMaterial>("RawMaterial"_h, ::AssetsNew::ScaffoldAndEntityName{sourceMaterial, Hash64(cfg) DEBUG_ONLY(, cfg)}));
			}

			pendingAssets._materials.emplace_back(guid, MergePartialMaterials(partialMaterials));
			pendingAssets._resolvedNames.emplace_back(guid, cfg);
		}

		std::vector<::Assets::DependencyValidationMarker> depVals;
		depVals.emplace_back(sourceModelHelper.GetModelMatDepVal());
		::Assets::BlockSerializer blockSerializer;
		Internal::Serialize(blockSerializer, pendingAssets, depVals);

		return {
			std::vector<::Assets::SerializedArtifact>{
				{
					ChunkType_ResolvedMat, ResolvedMat_ExpectedVersion,
					(StringMeld<256>() << sourceModelName << "&" << sourceMaterialName).AsString(),
					::Assets::AsBlob(blockSerializer)
				}
			},
			::Assets::GetDepValSys().MakeOrReuse(depVals),
			GetCompileProcessType((CompiledMaterialSet*)nullptr)
		};
	}

	::Assets::CompilerRegistration RegisterMaterialCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		auto result = ::Assets::RegisterSimpleCompiler(intermediateCompilers, "material-scaffold-compiler", "material-scaffold-compiler", MaterialCompileOperation);
		uint64_t outputAssetTypes[] = { GetCompileProcessType((CompiledMaterialSet*)nullptr) };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static sp<CompiledMaterialSet> ConstructMaterialSetSync(
		sp<MaterialSetConstruction> construction,
		const ::Assets::ContextImbuedAsset<sp<CompoundAssetScaffold>>& baseMaterials,
		std::vector<std::string> materialsToInstantiate)
	{
		assert(materialsToInstantiate.empty() ^ (std::get<0>(baseMaterials) == nullptr));		// one or the other, not both

		Internal::SourceModelHelper sourceModelHelper;
		std::vector<::Assets::DependencyValidationMarker> depVals;
		bool useRawMaterialSet = materialsToInstantiate.empty();
		if (useRawMaterialSet) {
			sourceModelHelper = baseMaterials;
			const auto& modelMat = sourceModelHelper.GetModelMatConfigs();
			materialsToInstantiate.reserve(modelMat.size());
			for (auto& m:modelMat) materialsToInstantiate.push_back(m);
			depVals.emplace_back(sourceModelHelper.GetModelMatDepVal());		// record a dependency here incase it's empty
		}

		auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();

			//  for each configuration, we want to build a resolved material
		Internal::PendingAssets pendingAssets;
		pendingAssets._resolvedNames.reserve(materialsToInstantiate.size());
		pendingAssets._materials.reserve(materialsToInstantiate.size());

		char buffer[3*MaxPath];
		for (const auto& cfg:materialsToInstantiate) {
			ShaderPatchCollection patchCollection;
			std::basic_stringstream<::Assets::ResChar> resName;
			auto guid = MakeMaterialGuid(MakeStringSection(cfg));

			std::vector<std::shared_future<ResolvedMaterial>> partialMaterials;
			if (useRawMaterialSet)
				partialMaterials.emplace_back(util->GetCachedAssetFuture<RawMaterial>("RawMaterial"_h, sourceModelHelper.GetMaterialMarkerPtr(cfg)));

			auto o0 = construction->_inlineMaterialOverrides.begin();
			auto o1 = construction->_materialFileOverrides.begin();
			auto o2 = construction->_futureMaterialOverrides.begin();
			auto o3 = construction->_futureMaterialSetOverrides.begin();
			for (unsigned overrideIdx=0; overrideIdx<construction->_nextOverrideIdx; ++overrideIdx) {

				if (o0 != construction->_inlineMaterialOverrides.end() && o0->first._overrideIdx == overrideIdx) {

					if (o0->first._application == 0 || o0->first._application == guid) {
						auto marker = std::make_shared<::Assets::Marker<ResolvedMaterial>>();
						marker->SetAssetForeground({o0->second, ::Assets::DependencyValidation{}});
						partialMaterials.emplace_back(std::move(marker->ShareFuture()));
					}
					++o0;

				} else if (o1 != construction->_materialFileOverrides.end() && o1->first._overrideIdx == overrideIdx) {

					if (o1->first._application == 0 || o1->first._application == guid) {
						auto indexer = ::AssetsNew::ContextAndIdentifier{ (StringMeldInPlace(buffer) << o1->second << ":" << cfg).AsString() };
						partialMaterials.emplace_back(util->GetCachedAssetFuture<RawMaterial>("RawMaterial"_h, indexer));
					}
					++o1;

				} else if (o2 != construction->_futureMaterialOverrides.end() && o2->first._overrideIdx == overrideIdx) {
					if (o2->first._application == 0 || o2->first._application == guid)
						partialMaterials.push_back(o2->second->ShareFuture());
					++o2;
				} else if (o3 != construction->_futureMaterialSetOverrides.end() && o3->first._overrideIdx == overrideIdx) {
					if (o3->first._application == 0 || o3->first._application == guid) {
						// We have to go via a ::Assets::PtrToMarkerPtr<CompilableMaterialAssetMixin<RawMaterial>>
						// in order to put this in "partialMaterials"
						std::promise<ResolvedMaterial> promisedMaterial;
						auto futureMaterial = promisedMaterial.get_future();
						::Assets::WhenAll(o3->second).ThenConstructToPromise(
							std::move(promisedMaterial),
							[cfg, util](const ::Assets::ContextImbuedAsset<sp<::AssetsNew::CompoundAssetScaffold>>& scaffold) {
								auto indexer = ::AssetsNew::ScaffoldAndEntityName{ scaffold, Hash64(cfg) DEBUG_ONLY(, cfg) };
								return util->GetCachedAssetFuture<RawMaterial>("RawMaterial"_h, indexer).get();	// note -- stall
							});
						partialMaterials.emplace_back(std::move(futureMaterial));
					}
					++o3;
				}

			}

			pendingAssets._materials.emplace_back(guid, MergePartialMaterials(partialMaterials));
			pendingAssets._resolvedNames.emplace_back(guid, cfg);
		}

		::Assets::BlockSerializer blockSerializer;
		Internal::Serialize(blockSerializer, pendingAssets, depVals);
		auto memBlock = blockSerializer.AsMemoryBlock();
		::Assets::Block_Initialize(memBlock.get());

		return std::make_shared<CompiledMaterialSet>(
			std::move(memBlock), blockSerializer.Size(),
			::Assets::GetDepValSys().MakeOrReuse(depVals));
	}

	void ConstructMaterialSet(
		std::promise<sp<CompiledMaterialSet>>&& promise,
		sp<MaterialSetConstruction> construction)
	{
		if (auto* marker = std::get_if<PtrToMarkerToMaterialSet>(&construction->_baseMaterials)) {
			::Assets::WhenAll(*marker).ThenConstructToPromise(
				std::move(promise),
				[construction=std::move(construction)](const auto& sourceModelConfiguration) {
					return ConstructMaterialSetSync(construction, sourceModelConfiguration, {});
				});
		} else if (auto* strs = std::get_if<std::vector<std::string>>(&construction->_baseMaterials)) {
			if (strs->empty()) {
				promise.set_exception(std::make_exception_ptr(std::runtime_error("Bad ConstructMaterialSet call because there are no materials to instantiate specified")));
				return;
			}

			::ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
				[promise=std::move(promise), construction=std::move(construction), cfgs=*strs]() mutable {
					TRY {
						promise.set_value(ConstructMaterialSetSync(construction, {}, std::move(cfgs)));
					} CATCH(...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		} else if (auto* modelFileIdentifier = std::get_if<std::string>(&construction->_baseMaterials)) {
			::Assets::WhenAll(Internal::MakeModelMatFuture(*modelFileIdentifier, nullptr)).ThenConstructToPromise(
				std::move(promise),
				[construction=std::move(construction)](const auto& sourceModelConfiguration) {
					return ConstructMaterialSetSync(construction, sourceModelConfiguration, {});
				});
		} else {
			promise.set_exception(std::make_exception_ptr(std::runtime_error("Bad ConstructMaterialSet call because base materials have not been set")));
		}
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void MaterialSetConstruction::SetBaseMaterials(PtrToMarkerToMaterialSet&& futureBaseMaterials)
	{
		_baseMaterials = std::move(futureBaseMaterials);
		_disableHash = true;
		_hash = 0;
	}
	void MaterialSetConstruction::SetBaseMaterials(IteratorRange<const std::string*> cfgs)
	{
		_baseMaterials = std::vector<std::string>{cfgs.begin(), cfgs.end()};
		_hash = 0;
	}
	void MaterialSetConstruction::SetBaseMaterials(std::string modelFileIdentifier)
	{
		_baseMaterials = std::move(modelFileIdentifier);
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(StringSection<> application, RawMaterial&& mat)
	{
		_inlineMaterialOverrides.emplace_back(Override{MakeMaterialGuid(application), _nextOverrideIdx++}, std::move(mat));
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(StringSection<> application, PtrToMarkerToMaterial&& mat)
	{
		_futureMaterialOverrides.emplace_back(Override{MakeMaterialGuid(application), _nextOverrideIdx++}, std::move(mat));
		_disableHash = true;
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(StringSection<> application, std::string materialFileIdentifier)
	{
		_materialFileOverrides.emplace_back(Override{MakeMaterialGuid(application), _nextOverrideIdx++}, std::move(materialFileIdentifier));
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(RawMaterial&& mat)
	{
		_inlineMaterialOverrides.emplace_back(Override{0, _nextOverrideIdx++}, std::move(mat));
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(PtrToMarkerToMaterial&& mat)
	{
		_futureMaterialOverrides.emplace_back(Override{0, _nextOverrideIdx++}, std::move(mat));
		_disableHash = true;
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(PtrToMarkerToMaterialSet&& mat)
	{
		_futureMaterialSetOverrides.emplace_back(Override{0, _nextOverrideIdx++}, std::move(mat));
		_disableHash = true;
		_hash = 0;
	}

	void MaterialSetConstruction::AddOverride(std::string materialFileIdentifier)
	{
		_materialFileOverrides.emplace_back(Override{0, _nextOverrideIdx++}, std::move(materialFileIdentifier));
		_hash = 0;
	}

	bool MaterialSetConstruction::CanBeHashed() const { return !_disableHash; }
	uint64_t MaterialSetConstruction::GetHash() const
	{
		assert(CanBeHashed());
		if (_hash == 0) {
			_hash = DefaultSeed64;
			auto i = _inlineMaterialOverrides.begin();
			auto i2 = _materialFileOverrides.begin();
			auto i3 = _futureMaterialOverrides.begin();
			for (unsigned c=0; c<_nextOverrideIdx; ++c) {
				if (i != _inlineMaterialOverrides.end() && i->first._overrideIdx == c) {
					_hash = i->second.CalculateHash(_hash) + i->first._application;
					++i;
				} else if (i2 != _materialFileOverrides.end() && i2->first._overrideIdx == c) {
					_hash = Hash64(i2->second, _hash) + i->first._application;
					++i2;
				} else 
					Throw(std::runtime_error("Attempting to create a hash for a MaterialSetConstruction which cannot be hashed"));
			}
			if (auto* str = std::get_if<std::string>(&_baseMaterials)) {
				_hash = Hash64(*str, _hash);
			} else if (auto* v = std::get_if<std::vector<std::string>>(&_baseMaterials)) {
				for (const auto& str:*v)
					_hash = Hash64(str, _hash);
			} else {
				assert(std::holds_alternative<std::monostate>(_baseMaterials));
			}
		}
		return _hash;
	}

	MaterialSetConstruction::MaterialSetConstruction() = default;
	MaterialSetConstruction::~MaterialSetConstruction() = default;

}}


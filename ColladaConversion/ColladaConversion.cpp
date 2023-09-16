// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ColladaConversion.h"
#include "Scaffold.h"
#include "ConversionConfig.h"

#include "SEffect.h"
#include "SCommandStream.h"
#include "SAnimation.h"
#include "SCommandStream.h"
#include "SRawGeometry.h"

#include "../RenderCore/GeoProc/NascentCommandStream.h"
#include "../RenderCore/GeoProc/NascentAnimController.h"
#include "../RenderCore/GeoProc/NascentObjectsSerialize.h"
#include "../RenderCore/GeoProc/NascentModel.h"
#include "../RenderCore/GeoProc/MeshDatabase.h"

#include "../RenderCore/Assets/AssetUtils.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../Assets/ICompileOperation.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/InitializerPack.h"
#include "../Assets/IntermediatesStore.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/ConfigFileContainer.h"

#include "../OSServices/Log.h"
#include "../Utility/Streams/StreamTypes.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Formatters/TextOutputFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Core/SelectConfiguration.h"

#include "../RenderCore/Assets/ModelRendererConstruction.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetMixins.h"

#include <memory>
#include <map>
#include <set>

#pragma warning(disable:4505) // unreferenced local function has been removed

using namespace RenderCore::Assets::GeoProc;

namespace ColladaConversion
{
	static const char* s_cfgName = "rawos/colladaimport.dat";
	using ModelCompilationConfiguration = RenderCore::Assets::ModelCompilationConfiguration;

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ColladaCompileOp : public ::Assets::ICompileOperation
    {
    public:
        std::string _name;
		ImportConfiguration _cfg;
        OSServices::MemoryMappedFile _fileData;
        std::shared_ptr<DocumentScaffold> _doc;
        ::ColladaConversion::URIResolveContext _resolveContext;
		std::vector<TargetDesc> _targets;
		::Assets::DependencyValidation _depVal;

		::Assets::rstring _rootNode;
		std::shared_ptr<ModelCompilationConfiguration> _modelCompilationConfiguration;

		std::vector<TargetDesc> GetTargets() const override;
		std::vector<::Assets::SerializedArtifact> SerializeTarget(unsigned idx) override;
		::Assets::DependencyValidation GetDependencyValidation() const override;

		ColladaCompileOp();
		~ColladaCompileOp();
    };

	static NascentSkeleton ConvertSkeleton(
		const ColladaCompileOp& input, const VisualScene& scene,
		IteratorRange<const std::string*> skinningSkeletons,
		IteratorRange<const Node*> roots);

	static NascentObjectGuid ConvertGeometryBlock(
		NascentModel& model,
		std::map<NascentObjectGuid, std::vector<uint64_t>>& geoBlockMatBindings,
		Section reference,
		const ::ColladaConversion::URIResolveContext& resolveContext,
		const ImportConfiguration& cfg)
	{
		GuidReference refGuid(reference);

		auto* scaffoldGeo = FindElement(refGuid, resolveContext, &IDocScopeIdResolver::FindMeshGeometry);
		if (!scaffoldGeo) {
				// look for a skin controller instead... We will use the geometry object that is referenced
				// by the controller
			auto* scaffoldController = FindElement(refGuid, resolveContext, &IDocScopeIdResolver::FindSkinController);
			if (scaffoldController) {
				refGuid = GuidReference{scaffoldController->GetBaseMesh()};
				scaffoldGeo = FindElement(refGuid, resolveContext, &IDocScopeIdResolver::FindMeshGeometry);
			}
		}

		if (!scaffoldGeo)
			Throw(::Exceptions::BasicLabel("Could not found geometry object to instantiate (%s)",
				reference.AsString().c_str()));

		NascentObjectGuid geoId { refGuid._id, refGuid._fileHash };
		auto* existingGeometry = model.FindGeometryBlock(geoId);
		if (!existingGeometry) {
			auto convertedMesh = Convert(*scaffoldGeo, resolveContext, cfg);
			if (convertedMesh._geoBlock._drawCalls.empty()) {
                    
					// everything else should be empty as well...
				assert(!convertedMesh._geoBlock._mesh || convertedMesh._geoBlock._mesh->GetUnifiedVertexCount() == 0);
				assert(convertedMesh._geoBlock._indices.empty());
				assert(convertedMesh._geoBlock._meshVertexIndexToSrcIndex.empty());
                
				Throw(::Exceptions::BasicLabel(
					"Geometry object is empty (%s)", reference.AsString().c_str()));
			}
			assert(convertedMesh._geoBlock._drawCalls.size() == convertedMesh._matBindingSymbols.size());

			model.Add(geoId, std::move(convertedMesh._geoBlock));
			model.AttachNameToGeometryBlock(geoId, scaffoldGeo->GetName().AsString());
			geoBlockMatBindings.insert(std::make_pair(geoId, std::move(convertedMesh._matBindingSymbols)));
		}

		return geoId;
	}

	static void ConvertCommand(
		NascentModel& model,
		std::map<NascentObjectGuid, std::vector<uint64_t>>& geoBlockMatBindings,
		const Node& attachedNode,
		NascentObjectGuid geoId, NascentObjectGuid controllerId,
		const std::string& localToModelBinding,
		IteratorRange<const InstanceGeometry::MaterialBinding*> materialBindings,
		const ::ColladaConversion::URIResolveContext& resolveContext)
	{
		auto mati = geoBlockMatBindings.find(geoId);
		assert(mati != geoBlockMatBindings.end());

		auto materials = BuildMaterialTableStrings(
			materialBindings,
			mati->second, resolveContext);

		std::vector<NascentObjectGuid> skinControllers;
		if (controllerId != NascentObjectGuid{})
			skinControllers.push_back(controllerId);

		model.Add(
			{ attachedNode.GetId().GetHash() },
			NascentModel::Command {
				geoId, std::move(skinControllers),
				localToModelBinding,
				materials, 
				0
			});
		model.AttachNameToCommand({ attachedNode.GetId().GetHash() }, attachedNode.GetName().AsString());
	}

	static bool IsAncestorOf(const Node& node, IteratorRange<const Node*> roots) 
	{
		if (std::find(roots.begin(), roots.end(), node))
			return true;

		if (!node.GetParent())
			return false;

		return IsAncestorOf(node.GetParent(), roots);
	}

	static std::string GetSkeletonName(const InstanceController& instController, const ::ColladaConversion::URIResolveContext& resolveContext)
	{
		auto node = FindElement(
			GuidReference(instController.GetSkeleton()),
			resolveContext,
			&IDocScopeIdResolver::FindNode);
		if (node)
			return SkeletonBindingName(node);

		auto skeleId = instController.GetSkeleton().AsString();
		if (!skeleId.empty() && *skeleId.begin() == '#')
			skeleId.erase(skeleId.begin());
		return skeleId;
	}

	static NascentModel ConvertModel(const ColladaCompileOp& input, const VisualScene& scene, IteratorRange<const Node*> roots)
	{
		NascentModel model;
		std::map<NascentObjectGuid, std::vector<uint64_t>> geoBlockMatBindings;

		///////////////////
		for (unsigned instGeoIndex=0; instGeoIndex<scene.GetInstanceGeometryCount(); ++instGeoIndex) {
            const auto& instGeo = scene.GetInstanceGeometry(instGeoIndex);
			auto attachNode = scene.GetInstanceGeometry_Attach(instGeoIndex);
			if (!IsAncestorOf(attachNode, roots))
				continue;

			TRY {
				auto geoId = ConvertGeometryBlock(
					model, geoBlockMatBindings,
					instGeo._reference,
					input._resolveContext, input._cfg);

				ConvertCommand(
					model, geoBlockMatBindings,
					attachNode,
					geoId, {},
					SkeletonBindingName(attachNode), MakeIteratorRange(instGeo._matBindings),
					input._resolveContext);
            } CATCH(const std::exception& e) {
                Log(Warning) << "Got exception while instantiating geometry (" << instGeo._reference.AsString().c_str() << "). Exception details:" << std::endl;
                Log(Warning) << e.what() << std::endl;
            } CATCH(...) {
                Log(Warning) << "Got unknown exception while instantiating geometry (" << instGeo._reference.AsString().c_str() << ")." << std::endl;
            } CATCH_END
        }

		///////////////////
		for (unsigned instSkinControllerIndex=0; instSkinControllerIndex<scene.GetInstanceControllerCount(); ++instSkinControllerIndex) {
            const auto& instController = scene.GetInstanceController(instSkinControllerIndex);
			auto attachNode = scene.GetInstanceController_Attach(instSkinControllerIndex);
			if (!IsAncestorOf(attachNode, roots))
				continue;

			NascentObjectGuid geoId;

			bool skinSuccessful = false;
            TRY {
				geoId = ConvertGeometryBlock(
					model, geoBlockMatBindings,
					instController._reference,
					input._resolveContext, input._cfg);

				//////////////////

				GuidReference controllerRef(instController._reference);
				NascentObjectGuid controllerId { controllerRef._id, controllerRef._fileHash };
				auto* scaffoldController = FindElement(controllerRef, input._resolveContext, &IDocScopeIdResolver::FindSkinController);
				if (!scaffoldController)
					Throw(::Exceptions::BasicLabel("Could not find controller object to instantiate (%s)",
						instController._reference.AsString().c_str()));

				auto controller = Convert(*scaffoldController, input._resolveContext, input._cfg);
				auto skeleName = GetSkeletonName(instController, input._resolveContext);

				model.Add(
					controllerId, 
					NascentModel::SkinControllerBlock {
						std::make_shared<UnboundSkinController>(std::move(controller)),
						skeleName
					});
				model.AttachNameToSkinControllerBlock(controllerId, scaffoldController->GetName().AsString());

				ConvertCommand(
					model, geoBlockMatBindings,
					attachNode,
					geoId, controllerId,
					SkeletonBindingName(attachNode), MakeIteratorRange(instController._matBindings),
					input._resolveContext);

                skinSuccessful = true;
            } CATCH(const std::exception& e) {
                Log(Warning) << "Got exception while instantiating controller (" << instController._reference.AsString().c_str() << "). Exception details:" << std::endl;
                Log(Warning) << e.what() << std::endl;
            } CATCH(...) {
                Log(Warning) << "Got unknown exception while instantiating controller (" << instController._reference.AsString().c_str() << ")." << std::endl;
            } CATCH_END

            if (!skinSuccessful) {
                    // if we failed to instantiate this object as a skinned controller,
                    // we can try to fall back to a static geometry object. This fallback
                    // can be required for some controller objects that use rigid animation
                    //  -- they can have a skin controller with no joints (meaning at the 
                    //      only transform that can affect them is the parent node -- or maybe the skeleton root?)
                Log(Warning) << "Could not instantiate controller as a skinned object. Falling back to rigid object." << std::endl;
                TRY {
					ConvertCommand(
						model, geoBlockMatBindings,
						attachNode,
						geoId, {},
						SkeletonBindingName(attachNode), MakeIteratorRange(instController._matBindings),
						input._resolveContext);
                } CATCH(const std::exception& e) {
                    Log(Warning) << "Got exception while instantiating geometry (after controller failed) (" << instController._reference.AsString().c_str() << "). Exception details:" << std::endl;
                    Log(Warning) << e.what() << std::endl;
                } CATCH(...) {
                    Log(Warning) << "Got unknown exception while instantiating geometry (after controller failed) (" << instController._reference.AsString().c_str() << ")." << std::endl;
                } CATCH_END
            }
        }

		return model;
	}

	static std::vector<Node> FindRoots(const VisualScene& scene, StringSection<utf8> rootNodeName)
	{
		std::vector<Node> roots;
		if (rootNodeName.IsEmpty()) {
			roots.push_back(scene.GetRootNode());
		} else {
			roots = scene.GetRootNode().FindAllBreadthFirst(
				[rootNodeName](const Node& n)
				{
					if (XlEqString(n.GetName(), rootNodeName)) return true;
					auto desc = GetLevelOfDetail(n);
					return desc._isLODRoot && XlEqString(desc._remainingName, rootNodeName);
				});
		}
		return roots;
	}

	static std::vector<std::pair<std::string, ModelCompilationConfiguration::SkeletonRules>> CollateSkeletonRoots(const ColladaCompileOp& input, const VisualScene& scene, IteratorRange<const Node*> roots, const ModelCompilationConfiguration& cfg)
	{
		std::vector<std::pair<std::string, ModelCompilationConfiguration::SkeletonRules>> skinningSkeletons;
		for (unsigned instSkinControllerIndex=0; instSkinControllerIndex<scene.GetInstanceControllerCount(); ++instSkinControllerIndex) {
            const auto& instController = scene.GetInstanceController(instSkinControllerIndex);
			auto attachNode = scene.GetInstanceController_Attach(instSkinControllerIndex);
			if (!IsAncestorOf(attachNode, roots))
				continue;

			auto name = GetSkeletonName(instController, input._resolveContext);
			auto i = std::find_if(skinningSkeletons.begin(), skinningSkeletons.end(), [name](const auto& q) { return q.first == name; });
			if (i == skinningSkeletons.end()) continue;

			skinningSkeletons.emplace_back(name, cfg.MatchSkeletonRules(name));
		}
		skinningSkeletons.emplace_back(std::string{}, cfg.MatchSkeletonRules({}));
		return skinningSkeletons;
	}

    std::vector<::Assets::SerializedArtifact> SerializeSkin(const ColladaCompileOp& input, StringSection<utf8> rootNodeName, const ModelCompilationConfiguration& configuration)
    {
        const auto* scene = input._doc->FindVisualScene(
            GuidReference(input._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

		auto roots = FindRoots(*scene, rootNodeName);
		if (roots.empty())
			Throw(::Exceptions::BasicLabel("Not root nodes found"));

		auto model = ConvertModel(input, *scene, MakeIteratorRange(roots));
		auto skinningSkeletons = CollateSkeletonRoots(input, *scene, roots, configuration);
		std::vector<std::string> skeletonNames;
		for (const auto&q:skinningSkeletons) skeletonNames.push_back(q.first);
		auto embeddedSkeleton = ConvertSkeleton(input, *scene, skeletonNames, MakeIteratorRange(roots));
		if (skinningSkeletons.size() != 1)
			Throw(std::runtime_error("Optimization for multiple skeletons not supported"));
		OptimizeSkeleton(embeddedSkeleton, model, skinningSkeletons.front().second);

		return model.SerializeToChunks("skin", embeddedSkeleton, configuration);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	static Float4x4 BuildCoordinateTransform(const AssetDesc& assetDesc)
    {
        // When the "up" vector, or the units specified in the collada header
        // don't match our expectations, we can apply an extra transform.
        // This should transform some given input coordinates into our expected
        // final output.
        // Here, we will convert to 1.f unit == 1.f meter, with +Z being up.
        float scale = assetDesc._metersPerUnit;
        Float3x3 axisTransform;
        switch (assetDesc._upAxis) {
        case AssetDesc::UpAxis::X:
                // -Y --> +X
                // +Z --> -Y
                // +X --> +Z
            axisTransform = Float3x3(
                 0.f, -1.f,  0.f,
                 0.f,  0.f, -1.f,
                 1.f,  0.f,  0.f);
            break;

        case AssetDesc::UpAxis::Y:
                // +X --> +X
                // +Z --> -Y
                // +Y --> +Z
                //  hmm... winding flip...?
            axisTransform = Float3x3(
                 1.f,  0.f,   0.f,
                 0.f,  0.f,  -1.f,
                 0.f,  1.f,   0.f);
            break;

        default:
        case AssetDesc::UpAxis::Z:
            axisTransform = Identity<Float3x3>();
            break;
        }

        return AsFloat4x4(Float3x3(scale * axisTransform));
    }

	static NascentSkeleton ConvertSkeleton(
		const ColladaCompileOp& input, const VisualScene& scene,
		IteratorRange<const std::string*> skinningSkeletons,
		IteratorRange<const Node*> roots)
	{
		NascentSkeleton result;
		result.WriteOutputMarker("", "identity");

		for (const auto&skeleton:skinningSkeletons) {
			if (skeleton.empty()) {
				unsigned topLevelPops = 0;
				auto coordinateTransform = BuildCoordinateTransform(input._doc->GetAssetDesc());
				if (!Equivalent(coordinateTransform, Identity<Float4x4>(), 1e-5f)) {
						// Push on the coordinate transform (if there is one)
						// This should be optimised into other matrices (or even into
						// the geometry) when we perform the skeleton optimisation steps.
					result.WritePushLocalToWorld();
					++topLevelPops;
					result.WriteStaticTransform(coordinateTransform);
				}

				for (const auto&root:roots)
					BuildSkeleton(result, root);
				result.WritePopLocalToWorld(topLevelPops);
			} else {
				auto node = scene.GetRootNode().FindBreadthFirst(
					[skeleton](const Node& node) {
						return skeleton == SkeletonBindingName(node);
					});
				if (!node)
					Throw(::Exceptions::BasicLabel("Could not find node for skeleton with binding name (%s)", skeleton.c_str()));

				// Note that we include this skeleton, even if it isn't strictly an ancestor of the nodes
				// in roots. This is so skin controllers can reference skeletons in arbitrary parts of the scene
				BuildSkeleton(result, node, skeleton);
			}
		}
		
		return result;
	}

    NascentSkeleton ConvertSkeleton(const ColladaCompileOp& input, const VisualScene& scene, StringSection<utf8> rootNodeName, const ModelCompilationConfiguration& cfg)
    {
		auto roots = FindRoots(scene, rootNodeName);
		if (roots.empty()) return {};

		auto skinningSkeletons = CollateSkeletonRoots(input, scene, roots, cfg);
		std::vector<std::string> skeletonNames;
		for (const auto&q:skinningSkeletons) skeletonNames.push_back(q.first);
		auto result = ConvertSkeleton(input, scene, skeletonNames, MakeIteratorRange(roots));
		if (skinningSkeletons.size() != 1)
			Throw(std::runtime_error("Optimization for multiple skeletons not supported"));
		OptimizeSkeleton(result, skinningSkeletons.front().second);

		return result;
    }

    std::vector<::Assets::SerializedArtifact> SerializeSkeleton(const ColladaCompileOp& input, StringSection<utf8> rootNodeName, const ModelCompilationConfiguration& cfg)
    {
		const auto* scene = input._doc->FindVisualScene(
            GuidReference(input._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

        return SerializeSkeletonToChunks("skeleton", ConvertSkeleton(input, *scene, rootNodeName, cfg));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void SerializeMatTable(OutputStream& stream, const ColladaCompileOp& model)
    {
        Formatters::TextOutputFormatter formatter(stream);

        std::vector<std::pair<NascentObjectGuid, RenderCore::Assets::RawMaterial>> compiledEffects;

        const auto& effects = model._doc->_effects;
        for (auto i=effects.cbegin(); i!=effects.cend(); ++i) {
            TRY
            {
				NascentObjectGuid id { i->GetId().GetHash() };
                compiledEffects.insert(
                    LowerBound(compiledEffects, id), 
                    std::make_pair(id, Convert(*i, model._resolveContext, model._cfg)));
            } CATCH (...) {
            } CATCH_END
        }

        const auto& mats = model._doc->_materials;
        for (auto m=mats.cbegin(); m!=mats.cend(); ++m) {
            GuidReference effect(m->_effectReference);
			auto i = LowerBound(compiledEffects, NascentObjectGuid { effect._id, effect._fileHash });
			if (i == compiledEffects.end() || !(i->first == NascentObjectGuid { effect._id, effect._fileHash }))
                continue;

            auto ele = formatter.BeginKeyedElement(m->_name.AsString());
            SerializationOperator(formatter, i->second);
            formatter.EndElement(ele);
        }
    }

    std::vector<::Assets::SerializedArtifact> SerializeMaterials(const ColladaCompileOp& model, StringSection<utf8> rootNodeName)
    { 
        MemoryOutputStream<char> strm;
        SerializeMatTable(strm, model);
        return {
			::Assets::SerializedArtifact{
				Type_RawMat, 0, model._name,
				::Assets::AsBlob(MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End()))}
		};
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static NascentAnimationSet ConvertAnimationSet(const ColladaCompileOp& input)
    {
		NascentAnimationSet result;

		const auto& animations = input._doc->_animations;
        for (auto i=animations.cbegin(); i!=animations.cend(); ++i) {
            TRY {
				const float framesPerSecond = 120.f;
                auto anim = Convert(*i, input._resolveContext, framesPerSecond); 
				if (!anim._curves.empty()) {
					unsigned maxFrame = 0, minFrame = std::numeric_limits<unsigned>::max();
					for (const auto& c:anim._curves) {
						minFrame = std::min(minFrame, (unsigned)c._curve.TimeAtFirstKeyframe());
						maxFrame = std::max(maxFrame, (unsigned)c._curve.TimeAtLastKeyframe());
					}
					NascentAnimationSet::BlockSpan blocks[1] {
						{minFrame, maxFrame+1}
					};
					auto nascentBlocks = result.AddAnimation("main", MakeIteratorRange(blocks), framesPerSecond);
					for (auto c=anim._curves.begin(); c!=anim._curves.end(); ++c) {
						unsigned curveIndex = result.AddCurve(std::move(c->_curve));
						nascentBlocks[0].AddAnimationDriver(
							c->_parameterName, c->_parameterComponent, c->_samplerType, curveIndex,
							c->_interpolationType);
					}
				}
            } CATCH (...) {
            } CATCH_END
        }

		return result;
    }

	std::vector<::Assets::SerializedArtifact> SerializeAnimations(const ColladaCompileOp& model, StringSection<utf8> rootNodeName)
	{
		return SerializeAnimationsToChunks(model._name, ConvertAnimationSet(model));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	auto ColladaCompileOp::GetTargets() const -> std::vector<TargetDesc> 
	{
		return _targets;
	}

	auto	ColladaCompileOp::SerializeTarget(unsigned idx) -> std::vector<::Assets::SerializedArtifact>
	{
		if (idx >= _targets.size()) return {};

		TRY
		{
			switch (_targets[idx]._targetCode) {
			case Type_Model:			return SerializeSkin(*this, _rootNode, *_modelCompilationConfiguration);
			case Type_Skeleton:			return SerializeSkeleton(*this, _rootNode, *_modelCompilationConfiguration);
			case Type_RawMat:			return SerializeMaterials(*this, _rootNode);
			case Type_AnimationSet:		return SerializeAnimations(*this, _rootNode);
			default:
				Throw(::Exceptions::BasicLabel("Cannot serialize target (%s)", _targets[idx]._name));
			}
		} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, GetDependencyValidation()));
		} CATCH(const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, GetDependencyValidation()));
		} CATCH_END
	}

	::Assets::DependencyValidation ColladaCompileOp::GetDependencyValidation() const
	{
		return _depVal;
	}

	ColladaCompileOp::ColladaCompileOp() {}
	ColladaCompileOp::~ColladaCompileOp() {}

	static std::shared_ptr<::Assets::ICompileOperation> CreateNormalCompileOperation(
		StringSection<::Assets::ResChar> identifier,
		std::shared_ptr<ModelCompilationConfiguration> configuration)
	{
		std::shared_ptr<ColladaCompileOp> result = std::make_shared<ColladaCompileOp>();

		auto split = MakeFileNameSplitter(identifier);
		auto filePath = split.AllExceptParameters().AsString();

		{
			// don't throw an error when the cfg file is missing; just drop back to the defaults
			::Assets::FileSnapshot snapshot;
			auto cfgBlob = ::Assets::MainFileSystem::TryLoadFileAsBlob_TolerateSharingErrors(s_cfgName, &snapshot);
			::Assets::DependentFileState depFileState { s_cfgName, snapshot };
			auto cfgDepVal = ::Assets::GetDepValSys().Make(MakeIteratorRange(&depFileState, &depFileState+1));
			result->_cfg = ::Assets::AutoConstructAsset<ImportConfiguration>(cfgBlob, std::move(cfgDepVal));
		}

		// Always load a .model file next to the input file -- this might contain addition configuration options
		::Assets::DependencyValidation modelCfgFileDepVal;
		{
			auto cfgFileName = split.DrivePathAndFilename().AsString() + ".model";
			auto cfgFile = ::Assets::MakeAssetMarker<std::shared_ptr<::Assets::ResolvedAssetMixin<ModelCompilationConfiguration>>>(cfgFileName);
			cfgFile->StallWhilePending();
			if (cfgFile->GetAssetState() == ::Assets::AssetState::Ready) {
				auto newCfg = std::make_shared<ModelCompilationConfiguration>();
				newCfg->MergeInWithFilenameResolve(*cfgFile->Actualize(), {});
				if (configuration)
					newCfg->MergeInWithFilenameResolve(*configuration, {});
				configuration = std::move(newCfg);
			}
			modelCfgFileDepVal = cfgFile->GetDependencyValidation();		// ensure we get a "does not exist" dep val if the file isn't there
		}

		auto mainFileDepVal = ::Assets::GetDepValSys().Make(filePath);
		result->_fileData = ::Assets::MainFileSystem::OpenMemoryMappedFile(filePath, 0, "r", OSServices::FileShareMode::Read);
		Formatters::XmlInputFormatter<utf8> formatter { result->_fileData.GetData(), mainFileDepVal };
		formatter._allowCharacterData = true;

		result->_name = identifier.AsString();
		result->_rootNode = split.Parameters().AsString();
		result->_modelCompilationConfiguration = std::move(configuration);
		if (!result->_modelCompilationConfiguration)
			result->_modelCompilationConfiguration = std::make_shared<ModelCompilationConfiguration>();
		result->_doc = std::make_shared<ColladaConversion::DocumentScaffold>();
		result->_doc->Parse(formatter);

		result->_resolveContext = ::ColladaConversion::URIResolveContext(result->_doc);

		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Model, "Model"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_RawMat, "RawMat"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Skeleton, "Skeleton"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_AnimationSet, "Animations"});

		::Assets::DependencyValidationMarker depVals[] { mainFileDepVal, result->_cfg.GetDependencyValidation(), modelCfgFileDepVal };
		result->_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);

		return std::move(result);
	}

#if COMPILER_ACTIVE == COMPILER_TYPE_MSVC
	std::shared_ptr<::Assets::ICompileOperation> CreateCompileOperation(const ::Assets::InitializerPack& initPack)
	{
#pragma comment(linker, "/EXPORT:CreateCompileOperation=" __FUNCDNAME__)
#else
	dll_export std::shared_ptr<::Assets::ICompileOperation> CreateCompileOperation(const ::Assets::InitializerPack&) asm("CreateCompileOperation");
	std::shared_ptr<::Assets::ICompileOperation> CreateCompileOperation(const ::Assets::InitializerPack& initPack)
	{
#endif
		auto identifier = initPack.GetInitializer<std::string>(0);
		std::shared_ptr<ModelCompilationConfiguration> configuration;
		if (initPack.GetCount() >= 2 && initPack.GetInitializerType(1).hash_code() == typeid(decltype(configuration)).hash_code())
			configuration = initPack.GetInitializer<decltype(configuration)>(1);
		return CreateNormalCompileOperation(identifier, std::move(configuration));
	}
}


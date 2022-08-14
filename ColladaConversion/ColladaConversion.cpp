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

#include "../OSServices/Log.h"
#include "../Utility/Streams/StreamTypes.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/OutputStreamFormatter.h"
#include "../Utility/Streams/FormatterUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Core/SelectConfiguration.h"

#include <memory>
#include <map>
#include <set>

#pragma warning(disable:4505) // unreferenced local function has been removed

using namespace RenderCore::Assets::GeoProc;

namespace ColladaConversion
{
	static const char* s_cfgName = "rawos/colladaimport.dat";

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ColladaCompileOp : public ::Assets::ICompileOperation
    {
    public:
        std::string _name;
		::Assets::rstring _rootNode;
		ImportConfiguration _cfg;
        OSServices::MemoryMappedFile _fileData;
        std::shared_ptr<DocumentScaffold> _doc;
        ::ColladaConversion::URIResolveContext _resolveContext;
		std::vector<TargetDesc> _targets;
		std::vector<::Assets::DependentFileState> _dependencies;

		std::vector<TargetDesc> GetTargets() const;
		std::vector<SerializedArtifact> SerializeTarget(unsigned idx);
		std::vector<::Assets::DependentFileState> GetDependencies() const;

		ColladaCompileOp();
		~ColladaCompileOp();
    };

	static NascentSkeleton ConvertSkeleton(
		const ColladaCompileOp& input, const VisualScene& scene, const std::set<std::string>& skinningSkeletons, IteratorRange<const Node*> roots);

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

	static std::set<std::string> GetSkeletons(NascentModel& model)
	{
		std::set<std::string> skinningSkeletons;
		for (const auto& skinController:model.GetSkinControllerBlocks())
			skinningSkeletons.insert(skinController.second._skeleton);
		skinningSkeletons.insert(std::string{});
		return skinningSkeletons;
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

    std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeSkin(const ColladaCompileOp& input, StringSection<utf8> rootNodeName)
    {
        const auto* scene = input._doc->FindVisualScene(
            GuidReference(input._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

		auto roots = FindRoots(*scene, rootNodeName);
		if (roots.empty())
			Throw(::Exceptions::BasicLabel("Not root nodes found"));

		auto model = ConvertModel(input, *scene, MakeIteratorRange(roots));
		auto embeddedSkeleton = ConvertSkeleton(input, *scene, GetSkeletons(model), MakeIteratorRange(roots));
		OptimizeSkeleton(embeddedSkeleton, model);
		return model.SerializeToChunks("skin", embeddedSkeleton, NativeVBSettings { true });
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
		const ColladaCompileOp& input, const VisualScene& scene, const std::set<std::string>& skinningSkeletons, IteratorRange<const Node*> roots)
	{
		NascentSkeleton result;
		result.WriteOutputMarker("", "identity");

		for (const auto&skeletonName:skinningSkeletons) {
			if (skeletonName.empty()) {
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
					[skeletonName](const Node& node) {
						return skeletonName == SkeletonBindingName(node);
					});
				if (!node)
					Throw(::Exceptions::BasicLabel("Could not find node for skeleton with binding name (%s)", skeletonName.c_str()));

				// Note that we include this skeleton, even if it isn't strictly an ancestor of the nodes
				// in roots. This is so skin controllers can reference skeletons in arbitrary parts of the scene
				BuildSkeleton(result, node, skeletonName);
			}
		}
		
		return result;
	}

    NascentSkeleton ConvertSkeleton(const ColladaCompileOp& input, const VisualScene& scene, StringSection<utf8> rootNodeName)
    {
		auto roots = FindRoots(scene, rootNodeName);
		if (roots.empty()) return {};

		std::set<std::string> skinningSkeletons;
		for (unsigned instSkinControllerIndex=0; instSkinControllerIndex<scene.GetInstanceControllerCount(); ++instSkinControllerIndex) {
            const auto& instController = scene.GetInstanceController(instSkinControllerIndex);
			auto attachNode = scene.GetInstanceController_Attach(instSkinControllerIndex);
			if (!IsAncestorOf(attachNode, MakeIteratorRange(roots)))
				continue;

			skinningSkeletons.insert(GetSkeletonName(instController, input._resolveContext));
		}
		skinningSkeletons.insert(std::string{});

		auto result = ConvertSkeleton(input, scene, skinningSkeletons, MakeIteratorRange(roots));
        RenderCore::Assets::TransformationMachineOptimizer_Null optimizer;
        result.GetSkeletonMachine().Optimize(optimizer);

		return result;
    }

    std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeSkeleton(const ColladaCompileOp& input, StringSection<utf8> rootNodeName)
    {
		const auto* scene = input._doc->FindVisualScene(
            GuidReference(input._doc->_visualScene)._id);
        if (!scene)
            Throw(::Exceptions::BasicLabel("No visual scene found"));

        return SerializeSkeletonToChunks("skeleton", ConvertSkeleton(input, *scene, rootNodeName));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void SerializeMatTable(OutputStream& stream, const ColladaCompileOp& model)
    {
        OutputStreamFormatter formatter(stream);

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

    std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeMaterials(const ColladaCompileOp& model, StringSection<utf8> rootNodeName)
    { 
        MemoryOutputStream<char> strm;
        SerializeMatTable(strm, model);
        return {
			::Assets::ICompileOperation::SerializedArtifact{
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

	std::vector<::Assets::ICompileOperation::SerializedArtifact> SerializeAnimations(const ColladaCompileOp& model, StringSection<utf8> rootNodeName)
	{
		return SerializeAnimationsToChunks(model._name, ConvertAnimationSet(model));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	auto ColladaCompileOp::GetTargets() const -> std::vector<TargetDesc> 
	{
		return _targets;
	}

	auto	ColladaCompileOp::SerializeTarget(unsigned idx) -> std::vector<SerializedArtifact>
	{
		if (idx >= _targets.size()) return {};

		switch (_targets[idx]._targetCode) {
		case Type_Model:			return SerializeSkin(*this, MakeStringSection(_rootNode).Cast<utf8>());
		case Type_Skeleton:			return SerializeSkeleton(*this, MakeStringSection(_rootNode).Cast<utf8>());
		case Type_RawMat:			return SerializeMaterials(*this, MakeStringSection(_rootNode).Cast<utf8>());
		case Type_AnimationSet:		return SerializeAnimations(*this, MakeStringSection(_rootNode).Cast<utf8>());
		default:
			Throw(::Exceptions::BasicLabel("Cannot serialize target (%s)", _targets[idx]._name));
		}
	}

	std::vector<::Assets::DependentFileState> ColladaCompileOp::GetDependencies() const
	{
		return _dependencies;
	}

	ColladaCompileOp::ColladaCompileOp() {}
	ColladaCompileOp::~ColladaCompileOp() {}

	static std::shared_ptr<::Assets::ICompileOperation> CreateNormalCompileOperation(StringSection<::Assets::ResChar> identifier)
	{
		std::shared_ptr<ColladaCompileOp> result = std::make_shared<ColladaCompileOp>();

		auto split = MakeFileNameSplitter(identifier);
		auto filePath = split.AllExceptParameters().AsString();

		result->_dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(s_cfgName));
		result->_dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(filePath));

		result->_cfg = ImportConfiguration(s_cfgName);
		result->_fileData = ::Assets::MainFileSystem::OpenMemoryMappedFile(MakeStringSection(filePath), 0, "r", OSServices::FileShareMode::Read);
		XmlInputStreamFormatter<utf8> formatter(
			MakeStringSection((const char*)result->_fileData.GetData().begin(), (const char*)result->_fileData.GetData().end()));
		formatter._allowCharacterData = true;

		result->_name = identifier.AsString();
		result->_rootNode = split.Parameters().AsString();
		result->_doc = std::make_shared<ColladaConversion::DocumentScaffold>();
		result->_doc->Parse(formatter);

		result->_resolveContext = ::ColladaConversion::URIResolveContext(result->_doc);

		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Model, "Model"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_RawMat, "RawMat"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_Skeleton, "Skeleton"});
		result->_targets.push_back(ColladaCompileOp::TargetDesc{Type_AnimationSet, "Animations"});

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
		return CreateNormalCompileOperation(identifier);
	}
}


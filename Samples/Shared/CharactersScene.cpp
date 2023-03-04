// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CharactersScene.h"
#include "Character.h"
#include "SampleGlobals.h"
#include "../../RenderCore/Assets/ModelRunTime.h"
#include "../../RenderCore/Assets/SharedStateSet.h"
#include "../../RenderCore/Assets/AssetUtils.h"
#include "../../RenderCore/Assets/Services.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/IAnnotator.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../Tools/EntityInterface/RetainedEntities.h"
#include "../../ConsoleRig/Console.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/Profiling/CPUProfiler.h"
#include "../../Utility/Meta/AccessorSerialize.h"

namespace Sample
{
    class StateBin
    {
    public:
        const CharacterModel*       _model;
        float                       _time;
        uint64                      _animation;
        std::vector<Float4x4>       _instances;
    
        StateBin();
        StateBin(StateBin&& moveFrom);
        StateBin& operator=(StateBin&& moveFrom);
    };

    StateBin::StateBin()
    {
        _model = nullptr; _time = 0.f; _animation = 0;
    }

    StateBin::StateBin(StateBin&& moveFrom)
    :       _model    (std::move(moveFrom._model))
    ,       _time     (moveFrom._time)
    ,       _animation(moveFrom._animation)
    ,       _instances(std::move(moveFrom._instances))
    {
    }

    StateBin& StateBin::operator=(StateBin&& moveFrom)
    {
        _model       = std::move(moveFrom._model);
        _time        = moveFrom._time;
        _animation   = moveFrom._animation;
        _instances   = std::move(moveFrom._instances);
        return *this;
    }

//////////////////////////////////////////////////////////////////////////////////////////////

    class PlayerCharacterInterf;

    class CharactersScene::Pimpl
    {
    public:
        typedef std::unique_ptr<
            RenderCore::Assets::PreparedAnimation,
            void(*)(RenderCore::Assets::PreparedAnimation*)> PreparedAnimation;

        using AnimPtr = std::shared_ptr<AnimationDecisionTree>;
        using ModelPtr = std::shared_ptr<CharacterModel>;
        std::vector<std::pair<uint64, AnimPtr>>     _animDecisionTrees;
        std::vector<std::pair<uint64, ModelPtr>>    _characterModels;

        mutable std::vector<PreparedAnimation>      _preallocatedState;
        mutable RenderCore::Assets::SharedStateSet  _charactersSharedStateSet;

        std::shared_ptr<PlayerCharacter>    _playerCharacter;
        std::vector<NPCCharacter>           _characters;
        std::vector<NetworkCharacter>       _networkCharacters;
        std::vector<StateBin>               _stateCache;

        std::shared_ptr<PlayerCharacterInterf> _playerCharacterInterf;

        Pimpl()
        : _charactersSharedStateSet(RenderCore::Assets::Services::GetTechniqueConfigDirs()) {}
    };

//////////////////////////////////////////////////////////////////////////////////////////////

    static const std::basic_string<utf8> StringAutoCotangent((const utf8*)"AUTO_COTANGENT");

    void CharactersScene::Render(
        RenderCore::IThreadContext& context,
        SceneEngine::LightingParserContext& parserContext,
        int techniqueIndex) const
    {
        using RenderCore::Metal::ConstantBuffer;
        using namespace SceneEngine;

		RenderCore::GPUAnnotation anno(context, "Characters");
        CPUProfileEvent pEvnt2("CharactersSceneRender", g_cpuProfiler);

            //  Turn on auto cotangents for character rendering
            //  This prevents us from having to transform the tangent frame through the skinning
            //  transforms (and avoids having to store those tangents in the prepared animation buffer)
        auto& techEnv = parserContext.GetTechniqueContext()._globalEnvironmentState;
        techEnv.SetParameter(StringAutoCotangent.c_str(), 1);

		auto captureMarker = _pimpl->_charactersSharedStateSet.CaptureState(context, parserContext.GetRenderStateDelegate(), {});

        RenderCore::Assets::ModelRendererContext modelContext(
            context, parserContext, techniqueIndex);

        if (!_pimpl->_preallocatedState.empty()) {

            const bool interleavedRender = Tweakable("InterleavedRender", false);
            if (interleavedRender) {

                auto si = _pimpl->_preallocatedState.begin();
                for (auto i=_pimpl->_stateCache.begin(); i!=_pimpl->_stateCache.end(); ++i, ++si) {
                    CATCH_ASSETS_BEGIN
                        const auto& model = *i->_model;

                        model.GetPrepareMachine().PrepareAnimation(
                            context, **si, 
                            RenderCore::Assets::AnimationState(i->_time, i->_animation));
                        model.GetRenderer().PrepareAnimation(
                            context, **si, model.GetPrepareMachine().GetSkeletonBinding());

                        for (auto i2=i->_instances.cbegin(); i2!=i->_instances.cend(); ++i2) {
                            RenderCore::Assets::MeshToModel meshToModel(
                                **si, &model.GetPrepareMachine().GetSkeletonBinding());
                            model.GetRenderer().Render(modelContext, _pimpl->_charactersSharedStateSet, *i2, meshToModel, si->get());
                        }
                    CATCH_ASSETS_END(parserContext)
                }

            } else {

                    //
                    //  Use the skinned characters information calculated in PrepareCharacters
                    //  This way, we can do the skinning once, but re-use the results while
                    //  rendering the characters multiple times (for example, for shadows or
                    //  other passes).
                    //

                auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
                metalContext->Bind(RenderCore::Techniques::CommonResources()._dssReadWrite);
                metalContext->Bind(RenderCore::Techniques::CommonResources()._blendOpaque);

				RenderCore::GPUProfilerBlock profileBlock(context, "RenderCharacters");

                auto si = _pimpl->_preallocatedState.begin();
                for (auto i=_pimpl->_stateCache.begin(); i!=_pimpl->_stateCache.end(); ++i, ++si) {
                    CATCH_ASSETS_BEGIN
                        const auto& model = *i->_model;
                        for (auto i2=i->_instances.cbegin(); i2!=i->_instances.cend(); ++i2) {
                            CPUProfileEvent pEvnt("CharacterModelRender", g_cpuProfiler);
                            model.GetRenderer().Render(
                                modelContext, _pimpl->_charactersSharedStateSet, *i2, 
                                RenderCore::Assets::MeshToModel(model.GetModelScaffold()), si->get());
                        }
                    CATCH_ASSETS_END(parserContext)
                }

            }

        } else {

                //
                //      In some cases we can't allocate preallocated state... This will happen
                //      on hardware without geometry shader support. Here, we have to render
                //      without geometry shaders / stream output
                //
                //      (this will currently render without any animation applied)
                //

            for (auto i=_pimpl->_stateCache.begin(); i!=_pimpl->_stateCache.end(); ++i) {
                CATCH_ASSETS_BEGIN
                    const auto& model = *i->_model;
                    for (auto i2=i->_instances.cbegin(); i2!=i->_instances.cend(); ++i2) {
                        model.GetRenderer().Render(modelContext, _pimpl->_charactersSharedStateSet, *i2);
                    }
                CATCH_ASSETS_END(parserContext)
            }

        }

        techEnv.SetParameter(StringAutoCotangent.c_str(), 0);
    }

    void CharactersScene::Prepare(RenderCore::IThreadContext& context)
    {
        CPUProfileEvent pEvnt("CharactersScenePrepare", g_cpuProfiler);

            //  We need to prepare the animation state for all of the visible characters for
            //  this frame. Build the animation state before we do any rendering -- so 
            //  usually this is one of the first steps in rendering any given frame.

        if (RenderCore::Assets::ModelRenderer::CanDoPrepareAnimation(context)) {
            while (_pimpl->_preallocatedState.size() < _pimpl->_stateCache.size()) {
                _pimpl->_preallocatedState.emplace_back(
                    _pimpl->_stateCache[0]._model->GetRenderer().CreatePreparedAnimation());
            }
        }

        const bool interleavedRender = Tweakable("InterleavedRender", false);
        if (interleavedRender) 
            return;

		RenderCore::GPUProfilerBlock profileBlock(context, "PrepareAnimation");
		RenderCore::GPUAnnotation anno(context, "PrepareCharacters");

            //
            //      Separate state preparation from rendering, so we can profile
            //      them both separately
            //  
        auto si = _pimpl->_preallocatedState.begin();
        for (auto i=_pimpl->_stateCache.begin(); i!=_pimpl->_stateCache.end(); ++i, ++si) {
            TRY {
                const auto& model = *i->_model;
                    // 2 prepare steps
                    //      * first, we need to generate the transform matrices
                    //      * second, we generate the animated vertex positions
                model.GetPrepareMachine().PrepareAnimation(
                    context, **si,
                    RenderCore::Assets::AnimationState(i->_time, i->_animation));
                model.GetRenderer().PrepareAnimation(context, **si, model.GetPrepareMachine().GetSkeletonBinding());
            } CATCH(const ::Assets::Exceptions::RetrievalError&) {
            } CATCH_END
        }
    }

    void CharactersScene::Cull(const Float4x4& worldToProjection)
    {
        CPUProfileEvent pEvnt("CharactersSceneCull", g_cpuProfiler);

            //  Prepare the list of visible characters
            //  Here we do culling against the edge of the screen. We could do occlusion
            //  and/or distance culling also... This implementation is very primitive. 
            //  There is a lot of excessive allocations and some inefficiency. 
            //  But it's ok for a sample.
            //
            //  We actually need to do the culling for the main scene and shadows at the same time.
            //  This is because we need to collect a list of all of the characters that need to
            //  have their animation prepared for a frame. The animation needs to be built if
            //  that character is visible in the main scene, or in the shadows -- and only once
            //  if it is visible in both.
            //  
            //  So an ideal implementation of this would check each character against multiple
            //  frustums, and also calculate the correct shadow frustum flags required.
        auto roughBoundingBox = std::make_pair(Float3(-2.f, -2.f, -2.f), Float3(2.f, 2.f, 2.f));
        _pimpl->_stateCache.clear();

        std::sort(_pimpl->_characters.begin(), _pimpl->_characters.end());
        if (!_pimpl->_characters.empty() && Tweakable("DrawNPCs", true)) {
            const float epsilon = 1.0f / (1.5f*60.f);
            auto blockStart = _pimpl->_characters.begin();
            for (auto i=_pimpl->_characters.begin();;++i) {
                if  (   i==_pimpl->_characters.end() 
                    ||  i->_model != blockStart->_model 
                    ||  i->_animState._animation != blockStart->_animState._animation 
                    || (i->_animState._time - blockStart->_animState._time) > epsilon) {

                    StateBin newState;
                    newState._model     =  blockStart->_model;
                    newState._animation =  blockStart->_animState._animation;
                    newState._time      = (blockStart->_animState._time + (i-1)->_animState._time) * .5f;
                    for (auto i2=blockStart; i2<i; ++i2) {
                        Float4x4 final = i2->_localToWorld;
                        Combine_IntoRHS(RotationZ((float)M_PI), final);     // compensate for flip in the sample art
                        Combine_IntoRHS(Float3(i2->_animState._motionCompensation * newState._time), final);

                        __declspec(align(16)) auto localToCulling = Combine(i2->_localToWorld, worldToProjection);
                        if (!CullAABB_Aligned(localToCulling, roughBoundingBox.first, roughBoundingBox.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                            newState._instances.push_back(final);
                        }
                    }

                    if (!newState._instances.empty()) {
                        _pimpl->_stateCache.push_back(std::move(newState));
                    }

                    if (i==_pimpl->_characters.end()) break;
                    blockStart = i;

                }
            }
        }

        for (auto i=_pimpl->_networkCharacters.cbegin();i!=_pimpl->_networkCharacters.cend();++i) {
            StateBin newState;
            newState._model     = i->_model;
            newState._animation = i->_animState._animation;
            newState._time      = i->_animState._time;
    
            Float4x4 final      = i->_localToWorld;
            Combine_IntoRHS(RotationZ((float)M_PI), final);     // compensate for flip in the sample art
            Combine_IntoRHS(Float3(i->_animState._motionCompensation * i->_animState._time), final);

            __declspec(align(16)) auto localToCulling = Combine(i->_localToWorld, worldToProjection);
            if (!CullAABB_Aligned(localToCulling, roughBoundingBox.first, roughBoundingBox.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                newState._instances.push_back(final);
                _pimpl->_stateCache.push_back(std::move(newState));
            }
        }

        if (_pimpl->_playerCharacter) {
            StateBin newState;
            newState._model = _pimpl->_playerCharacter->_model;
            newState._animation = _pimpl->_playerCharacter->_animState._animation;
            newState._time = _pimpl->_playerCharacter->_animState._time;
    
            Float4x4 final = _pimpl->_playerCharacter->_localToWorld;
            Combine_IntoRHS(RotationZ((float)M_PI), final);     // compensate for flip in the sample art
            Combine_IntoRHS(Float3(_pimpl->_playerCharacter->_animState._motionCompensation * _pimpl->_playerCharacter->_animState._time), final);
    
            __declspec(align(16)) auto localToCulling = Combine(_pimpl->_playerCharacter->_localToWorld, worldToProjection);
            if (!CullAABB_Aligned(localToCulling, roughBoundingBox.first, roughBoundingBox.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                newState._instances.push_back(final);
                _pimpl->_stateCache.push_back(std::move(newState));
            }
        }
    }

    void CharactersScene::Update(float deltaTime)
    {
        CPUProfileEvent pEvnt("CharactersSceneUpdate", g_cpuProfiler);

            // update the simulations state for all characters
        for (auto i = _pimpl->_characters.begin(); i!=_pimpl->_characters.end(); ++i) {
            i->Update(deltaTime);
        }
        for (auto i = _pimpl->_networkCharacters.begin(); i!=_pimpl->_networkCharacters.end(); ++i) {
            i->Update(deltaTime);
        }
        if (_pimpl->_playerCharacter)
            _pimpl->_playerCharacter->Update(deltaTime);
    }

    class PlayerCharacterInterf : public IPlayerCharacter
    {
    public:
        bool OnInputEvent(const RenderOverlays::DebuggingDisplay::InputContext& context, const OSServices::InputSnapshot& evnt)
        {
            if (_scene->_playerCharacter)
                _scene->_playerCharacter->Accumulate(evnt);
            return false;
        }

        const Float4x4& GetLocalToWorld() const
        {
            if (_scene->_playerCharacter)
                return _scene->_playerCharacter->GetLocalToWorld();
            return Identity<Float4x4>();
        }

        void SetLocalToWorld(const Float4x4& newTransform)
        {
            if (_scene->_playerCharacter)
                _scene->_playerCharacter->SetLocalToWorld(newTransform);
        }

        bool IsPresent() const
        {
            return _scene->_playerCharacter != nullptr;
        }

        PlayerCharacterInterf(std::shared_ptr<CharactersScene::Pimpl> scene)
            : _scene(std::move(scene)) {}
        ~PlayerCharacterInterf() {}
    private:
        std::shared_ptr<CharactersScene::Pimpl> _scene;
    };

    std::shared_ptr<IPlayerCharacter>  CharactersScene::GetPlayerCharacter()
    {
        return _pimpl->_playerCharacterInterf;
    }

    Float4x4 CharactersScene::DefaultCameraToWorld() const
    {
        if (!_pimpl->_playerCharacter) return Identity<Float4x4>();

        auto boundingBox = std::make_pair<Float3, Float3>(Float3(0.f, 0.f, 0.f), Float3(0.f, 0.f, 0.f));
        if (_pimpl->_playerCharacter->_model)
            boundingBox = _pimpl->_playerCharacter->_model->GetModelScaffold().GetStaticBoundingBox();
        auto cameraFocusPoint = LinearInterpolate(boundingBox.first, boundingBox.second, 0.5f);

        const Float3 defaultForwardVector(0.f, -1.f, 0.f);
        const Float3 defaultUpVector(0.f, 0.f, 1.f);
        const float boundingBoxDimension = Magnitude(boundingBox.second - boundingBox.first);
        return MakeCameraToWorld(
            defaultForwardVector, defaultUpVector,
            cameraFocusPoint - boundingBoxDimension * defaultForwardVector);
    }

    void CharactersScene::Clear()
    {
        _pimpl->_characters.clear();
        _pimpl->_playerCharacter = nullptr;
    }

    void CharactersScene::CreateCharacter(
        uint64 id,
        const CharacterInputFiles& inputFiles, const AnimationNames& animNames,
        bool player, const Float4x4& localToWorld)
    {
        uint64 modelHash = inputFiles.MakeHash();
        auto model = LowerBound(_pimpl->_characterModels, modelHash);
        if (model == _pimpl->_characterModels.end() || model->first != modelHash) {
            model = _pimpl->_characterModels.insert(
                model,
                std::make_pair(modelHash, 
                    std::make_shared<CharacterModel>(
                        inputFiles, std::ref(_pimpl->_charactersSharedStateSet))));
        }

        uint64 animHash = HashCombine(animNames.MakeHash(), modelHash);
        auto anim = LowerBound(_pimpl->_animDecisionTrees, animHash);
        if (anim == _pimpl->_animDecisionTrees.end() || anim->first != animHash) {
            anim = _pimpl->_animDecisionTrees.insert(
                anim,
                std::make_pair(modelHash, 
                    std::make_shared<AnimationDecisionTree>(
                        animNames, model->second->GetAnimationData(), CharactersScale)));
        }

        if (player) {
            _pimpl->_playerCharacter = std::make_shared<PlayerCharacter>(id, std::ref(*model->second), anim->second);
            _pimpl->_playerCharacter->SetLocalToWorld(localToWorld);
        } else {
            NPCCharacter chr(id, std::ref(*model->second), anim->second);
            chr.SetLocalToWorld(localToWorld);
            _pimpl->_characters.push_back(chr);
        }
    }

    void CharactersScene::DeleteCharacter(uint64 id)
    {
        if (_pimpl->_playerCharacter && _pimpl->_playerCharacter->_id == id) {
            _pimpl->_playerCharacter = nullptr;
        } else {
            for (auto i=_pimpl->_characters.begin(); i!=_pimpl->_characters.end(); ++i)
                if (i->_id == id) {
                    _pimpl->_characters.erase(i);
                    return;
                }
            for (auto i=_pimpl->_networkCharacters.begin(); i!=_pimpl->_networkCharacters.end(); ++i)
                if (i->_id == id) {
                    _pimpl->_networkCharacters.erase(i);
                    return;
                }
        }
    }

    CharactersScene::CharactersScene()
    {
        _pimpl = std::make_shared<Pimpl>();
        _pimpl->_playerCharacterInterf = std::make_shared<PlayerCharacterInterf>(_pimpl);
    }

    CharactersScene::~CharactersScene() {}



///////////////////////////////////////////////////////////////////////////////////////////////////

    void RegisterEntityInterface(
        EntityInterface::RetainedEntities& flexSys,
        const std::shared_ptr<CharactersScene>& sys)
    {
        using namespace EntityInterface;
        std::weak_ptr<CharactersScene> weakPtrToThis = sys;
        const utf8* types[] = { (const utf8*)"CharacterSpawn" };
        for (unsigned c=0; c<dimof(types); ++c) {
            flexSys.RegisterCallback(
                flexSys.GetTypeId(types[c]),
                [weakPtrToThis](
                    const RetainedEntities& entities, const Identifier& id, 
                    RetainedEntities::ChangeType changeType)
                {
                    auto scene = weakPtrToThis.lock();
                    if (!scene) return;

                    if (changeType == RetainedEntities::ChangeType::Delete) {
                        scene->DeleteCharacter(id.Object());
                    } else if (changeType == RetainedEntities::ChangeType::Create) {
                        auto* ent = entities.GetEntity(id);
                        if (!ent) return;

                        auto model = CreateFromParameters<CharacterInputFiles>(ent->_properties);
                        auto anim = CreateFromParameters<AnimationNames>(ent->_properties);
                        bool isPlayer = ent->_properties.GetParameter<int>(MakeStringSection("CharacterType"), 1) == 0;
                        auto localToWorld = Transpose(ent->_properties.GetParameter<Float4x4>(MakeStringSection("Transform"), Identity<Float4x4>()));
                        scene->CreateCharacter(id.Object(), model, anim, isPlayer, localToWorld);
                    }
                });
        }
    }
}


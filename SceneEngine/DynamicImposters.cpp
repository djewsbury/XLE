// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DynamicImposters.h"

#if 0

#include "GestaltResource.h"
#include "SceneEngineUtils.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/QueryPool.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/RenderStateResolver.h"
#include "../RenderCore/Techniques/CompiledRenderStateSet.h"
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../FixedFunctionModel/ModelRunTime.h"
#include "../FixedFunctionModel/DelayedDrawCall.h"
#include "../FixedFunctionModel/SharedStateSet.h"
#include "../FixedFunctionModel/PreboundShaders.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/IAnnotator.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"
#include "../Math/Transformations.h"
#include "../Math/RectanglePacking.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/FunctionUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/HeapUtils.h"
#include "../Utility/Meta/ClassAccessors.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"
#include "../Utility/Meta/AccessorSerialize.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
    using namespace RenderCore;

    using Packer = RectanglePacker_MaxRects;

    static unsigned GetXYAngle(
        const RenderCore::Assets::ModelScaffold& scaffold,
        const Float3x4& localToWorld, const Float3& cameraPosition, 
        unsigned angleQuant)
    {
            // We're going to get a rough XY angle value from 
            // this matrix... We just want an angle based on the 
            // direction between the center of the object and the
            // camera.
            // Note that we could just use the origin of local space
            // to skip some steps here for a faster result.
        const auto& bb = scaffold.GetStaticBoundingBox();
        Float3 bbCenter = (bb.first + bb.second) * 0.5f;

            // we need to transform the camera position into local space in
            // order to take into account an XY transforms on the local-to-world
        Float3 offset = TransformPointByOrthonormalInverse(localToWorld, cameraPosition);
        float angle = XlATan2(offset[1], offset[0]);        // note -- z ignored.

            // assuming out atan implementation returns a value between
            // -pi and pi...
            // Note the offset by half our range to make sure that 0.f
            // is right in the middle of one quantized range.
        float range = 2.f * gPI / float(angleQuant);
        auto result = (unsigned)XlFloor((angle + gPI + .5f * range) / range);
        result = result % angleQuant;
        assert(result >= 0 && result < angleQuant);
        return result;
    }

    static Float2 XYAngleAsVector(unsigned xyAngle, unsigned angleQuant)
    {
            // get a "forward" vector that corresponds to the given xy angle.
        const auto factor = float(2.f * gPI) / float(angleQuant);
        const auto angle = float(xyAngle) * factor - gPI;
        return Float2(XlCos(angle), XlSin(angle));
    }

    static UInt2 MipMapDims(UInt2 dims, unsigned mipMapLevel)
    {
        return UInt2(
            std::max(dims[0] >> mipMapLevel, 1u),
            std::max(dims[1] >> mipMapLevel, 1u));
    }

    class ImposterSpriteAtlas
    {
    public:
            // We have 2 options for our altas
            //      we can store a post-lighting image of the object
            //      Or we can store the deferred shading parameters 
            //      (like diffuse, normal, specular parameters, etc).
            // Lets' build in some flexibility so we can select which
            // method to use.

        class Layer
        {
        public:
            GestaltTypes::RTVSRV    _atlas;
            GestaltTypes::RTVSRV    _tempRTV;

            Layer() {}
            ~Layer() {}
            Layer(Layer&& moveFrom) never_throws : _atlas(std::move(moveFrom._atlas)), _tempRTV(std::move(moveFrom._tempRTV)) {}
            Layer& operator=(Layer&& moveFrom) never_throws
            {
                _atlas = std::move(moveFrom._atlas);
                _tempRTV = std::move(moveFrom._tempRTV);
                return *this;
            }
            Layer(GestaltTypes::RTVSRV&& atlas, GestaltTypes::RTVSRV&& tempRTV)
                : _atlas(std::move(atlas)), _tempRTV(std::move(tempRTV)) {}
        };
        std::vector<Layer>  _layers;
        GestaltTypes::DSV   _tempDSV;

        ImposterSpriteAtlas(UInt2 dims, UInt2 tempSurfaceDims, std::initializer_list<Format> layers);
        ImposterSpriteAtlas(ImposterSpriteAtlas&& moveFrom) never_throws;
        ImposterSpriteAtlas& operator=(ImposterSpriteAtlas&& moveFrom) never_throws;
        ImposterSpriteAtlas();
        ~ImposterSpriteAtlas();
    };

    ImposterSpriteAtlas::ImposterSpriteAtlas(UInt2 dims, UInt2 tempSurfaceDims, std::initializer_list<Format> layers)
    {
        _tempDSV = GestaltTypes::DSV(
            TextureDesc::Plain2D(tempSurfaceDims[0], tempSurfaceDims[1], Format::D16_UNORM),
            "SpriteAtlasTemp");
        _layers.reserve(layers.size());
        for (const auto& l:layers)
            _layers.emplace_back(Layer(
                GestaltTypes::RTVSRV(TextureDesc::Plain2D(dims[0], dims[1], l), "SpriteAtlas"),
                GestaltTypes::RTVSRV(TextureDesc::Plain2D(tempSurfaceDims[0], tempSurfaceDims[1], l), "SpriteAtlasTemp")));
    }

    ImposterSpriteAtlas::ImposterSpriteAtlas(ImposterSpriteAtlas&& moveFrom) never_throws
    : _layers(std::move(moveFrom._layers))
    , _tempDSV(std::move(moveFrom._tempDSV))
    {}

    ImposterSpriteAtlas& ImposterSpriteAtlas::operator=(ImposterSpriteAtlas&& moveFrom) never_throws
    {
        _layers = std::move(moveFrom._layers);
        _tempDSV = std::move(moveFrom._tempDSV);
        return *this;
    }

    ImposterSpriteAtlas::ImposterSpriteAtlas() {}
    ImposterSpriteAtlas::~ImposterSpriteAtlas() {}

    static const InputElementDesc s_inputLayout[] = 
    {
        InputElementDesc("POSITION", 0, Format::R32G32B32_FLOAT),
        InputElementDesc("XAXIS", 0, Format::R32G32B32_FLOAT),
        InputElementDesc("YAXIS", 0, Format::R32G32B32_FLOAT),
        InputElementDesc("SIZE", 0, Format::R32G32_FLOAT),
        InputElementDesc("SPRITEINDEX", 0, Format::R32_UINT)
    };

    class DynamicImposters::Pimpl
    {
    public:
        Config _config;

        using SpriteHash = uint64_t;

        class QueuedObject
        {
        public:
            const ModelRenderer*    _renderer;
            const ModelScaffold*    _scaffold;
            unsigned                _XYangle;

            SpriteHash MakeHash() const { return HashCombine(IntegerHash64(size_t(_renderer)), _XYangle); }
        };
        std::vector<std::pair<SpriteHash, QueuedObject>>    _queuedObjects;
        std::vector<std::pair<SpriteHash, Float4>>          _queuedInstances;

        static const unsigned MipMapCount = 5;

        using Rectangle = std::pair<UInt2, UInt2>;
        class PreparedSprite
        {
        public:
            Rectangle   _rect[MipMapCount];
            Float3      _projectionCentre;
            Float2      _worldSpaceHalfSize;
            unsigned    _createFrame;
            unsigned    _usageFrame;

            PreparedSprite()
            {
                XlZeroMemory(_rect);
                _projectionCentre = Zero<Float3>();
                _worldSpaceHalfSize = Zero<Float2>();
                _createFrame = 0;
                _usageFrame = 0;
            }
        };

            //// //// //// //// Prepared Sprites Table //// //// //// ////
        LRUQueue                        _lruQueue;
        std::vector<PreparedSprite>     _preparedSprites;
        std::vector<std::pair<SpriteHash, unsigned>> _preparedSpritesLookup;
        SpanningHeap<uint16>            _preparedSpritesHeap;

            //// //// //// //// Atlas //// //// //// ////
        Packer                          _packer;
        ImposterSpriteAtlas             _atlas;

            //// //// //// //// Rendering //// //// //// ////
        FixedFunctionModel::SimpleShaderVariationManager   _material;
        Metal::ConstantBuffer           _spriteTableCB;
        SharedStateSet*                 _sharedStateSet;
        std::shared_ptr<Techniques::IRenderStateDelegate> _stateRes;

            //// //// //// //// Metrics //// //// //// ////
        unsigned    _overflowCounter;
        unsigned    _overflowMaxSpritesCounter;
        unsigned    _pendingCounter;
        unsigned    _copyCounter;
        unsigned    _evictionCounter;

        unsigned    _frameCounter;

        void BuildNewSprites(
            Metal::DeviceContext& context,
            Techniques::ParsingContext& parserContext);
        PreparedSprite BuildSprite(
            Metal::DeviceContext& context,
            Techniques::ParsingContext& parserContext,
            const QueuedObject& ob);
        bool AttemptEviction();
        void RenderObject(
            Metal::DeviceContext& context,
            Techniques::ParsingContext& parserContext,
            const QueuedObject& ob, 
            const Float4x4& cameraToWorld,
            const ViewportDesc& viewport,
            const Float4x4& projMatrix) const;
        void CopyToAltas(
            Metal::DeviceContext& context,
            const Rectangle& destination,
            const Rectangle& source);
    };

    static Utility::Internal::StringMeldInPlace<char> QuickMetrics(
        Techniques::ParsingContext& parserContext)
    {
        return StringMeldAppend(parserContext._stringHelpers->_quickMetrics);
    }

    static bool IsGood(const std::pair<UInt2, UInt2>& rect)
    {
        return rect.second[0] > rect.first[0]
            && rect.second[1] > rect.first[1];
    }

    static const unsigned MaxPreparePerFrame = 3;
    static const unsigned MaxEvictionPerFrame = 3;
    static const unsigned EvictionCreateGracePeriod = 60;   // don't evict sprites within this many frames from their creation
    static const unsigned EvictionUsageGracePeriod = 5;     // don't evict sprites within this many frames of being used

    void DynamicImposters::Pimpl::BuildNewSprites(
        Metal::DeviceContext& context,
        Techniques::ParsingContext& parserContext)
    {
        ProtectState protectState;
        const ProtectState::States::BitField volatileStates =
              ProtectState::States::RenderTargets
            | ProtectState::States::Viewports
            | ProtectState::States::DepthStencilState
            | ProtectState::States::BlendState;
        bool initProtectState = false;

        const unsigned maxPreparePerFrame = Tweakable("ImpostersReset", false) ? INT_MAX : MaxPreparePerFrame;
        unsigned preparedThisFrame = 0;

        _overflowCounter = 0;
        _overflowMaxSpritesCounter = 0;
        _pendingCounter = 0;
        _evictionCounter = 0;

        auto preparedSpritesI = _preparedSpritesLookup.begin();
        for (const auto& o:_queuedObjects) {
            uint64_t hash = o.first;
            preparedSpritesI = std::lower_bound(preparedSpritesI, _preparedSpritesLookup.end(), hash, CompareFirst<Pimpl::SpriteHash, unsigned>());
            if (preparedSpritesI != _preparedSpritesLookup.end() && preparedSpritesI->first == hash) {
            } else {
                if (preparedThisFrame++ >= maxPreparePerFrame) {
                    ++_pendingCounter;
                    continue;
                }

                unsigned newIndex = _preparedSpritesHeap.Allocate(1<<4)>>4;
                if (newIndex == (~0x0u)>>4) {
                    ++_overflowMaxSpritesCounter;
                    continue;
                }

                if (!initProtectState) {
                    protectState = ProtectState(context, volatileStates);
                    initProtectState = true;
                }
                
                TRY {
                    auto& newSprite = _preparedSprites[newIndex];
                    newSprite = BuildSprite(context, parserContext, o.second);
                    if (!IsGood(newSprite._rect[0])) {
                        if (AttemptEviction()) {
                                // reset preparedSpritesI, because _preparedSpritesLookup
                                // may have changed in AttemptEviction!
                                // (note that we have to do this before BuildSprite, because BuildSprite can throw)
                            preparedSpritesI = std::lower_bound(
                                _preparedSpritesLookup.begin(), _preparedSpritesLookup.end(), 
                                hash, CompareFirst<Pimpl::SpriteHash, unsigned>());

                            newSprite = BuildSprite(context, parserContext, o.second);
                        }
                    }

                    if (IsGood(newSprite._rect[0])) {
                        preparedSpritesI = _preparedSpritesLookup.insert(preparedSpritesI, std::make_pair(hash, newIndex));
                        _lruQueue.BringToFront(newIndex);
                    } else {
                        ++_overflowCounter;
                        _preparedSpritesHeap.Deallocate(newIndex<<4, 1<<4);
                    }
                } CATCH(const ::Assets::Exceptions::RetrievalError&e) {
                    parserContext.Process(e);
                    ++_pendingCounter;
                    _preparedSpritesHeap.Deallocate(newIndex<<4, 1<<4);
                } CATCH(...) {
                    _preparedSpritesHeap.Deallocate(newIndex<<4, 1<<4);
                    throw;
                } CATCH_END
            }
        }
    }

    bool DynamicImposters::Pimpl::AttemptEviction()
    {
            // Attempt to evict the oldest sprites from the list to free
            // up some more space in the altas.
            // Note that the oldest sprites are not guaranteed to be 
            // contiguous, nor are they guaranteed to be large (relative
            // to the new sprite we want to insert).
            //
            // So it's quite possible that evicting some sprites may not
            // free up enough room to insert the new sprite we want.
            //
            // It may take a few frames of eviction before we can insert
            // the particular new sprite we want.
        if (_evictionCounter != 0) return false;

        const unsigned evictionCount = MaxEvictionPerFrame;
        unsigned evicted=0;
        for (evicted; evicted<evictionCount; evicted++) {
            if (_preparedSpritesHeap.IsEmpty()) break;

            auto oldest = _lruQueue.GetOldestValue();

            auto& sprite = _preparedSprites[oldest];

                // Check the grace period... prevent sprites from being evicted too
                // soon after being created or used. We will tend to hit these breaks
                // when the atlas is saturated and we can't fit in any more sprites...
            if ((_frameCounter - sprite._createFrame) < EvictionCreateGracePeriod) break;
            if ((_frameCounter - sprite._usageFrame) < EvictionUsageGracePeriod) break;

            for (unsigned c=0; c<dimof(sprite._rect); ++c) {
                _packer.Deallocate(sprite._rect[c]);        // return rectangle to the packer
                sprite._rect[c] = std::make_pair(UInt2(0,0), UInt2(0,0));
            }

            _preparedSpritesHeap.Deallocate(oldest<<4, 1<<4);
            auto i = std::find_if(
                _preparedSpritesLookup.cbegin(), _preparedSpritesLookup.cend(),
                [oldest](const std::pair<SpriteHash, unsigned>& p) { return p.second == oldest; });
            assert(i!=_preparedSpritesLookup.cend());
            _preparedSpritesLookup.erase(i);

                // we have to bring it to the front in order to
                // get a new oldest value
            _lruQueue.DisconnectOldest();
        }

        _evictionCounter = evicted;
        return evicted > 0;
    }

    void DynamicImposters::Render(
        RenderCore::Metal::DeviceContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        unsigned techniqueIndex)
    {
        ++_pimpl->_frameCounter;

        if (_pimpl->_queuedInstances.empty() || _pimpl->_queuedObjects.empty())
            return;

        auto shader = _pimpl->_material.FindVariation(
            parserContext, techniqueIndex, SCENE_ENGINE_RES "/Vegetation/Vegetation.tech");
        if (!shader._shader._shaderProgram) return;

            // For each object here, we should look to see if we have a prepared
            // imposter already available. If not, we have to build the imposter.
        _pimpl->BuildNewSprites(context, parserContext);

        class Vertex
        {
        public:
            Float3 _pos; 
            Float3 _xAxis, _uAxis;
            Float2 _size;
            unsigned _spriteIndex;
            float _sortingDistance;
        };
        std::vector<Vertex> vertices;
        vertices.reserve(_pimpl->_queuedInstances.size());

        auto& projDesc = parserContext.GetProjectionDesc();
        auto cameraRight = ExtractRight_Cam(projDesc._cameraToWorld);
        auto cameraUp = Float3(0.f, 0.f, 1.f); // ExtractUp_Cam(projDesc._cameraToWorld);
        auto cameraPos = ExtractTranslation(projDesc._cameraToWorld);

            // Now just render a sprite for each instance requested
            // Here, we can choose to sort the instances by sprite id,
            // or just leave them in their original order. All of the
            // sprite textures at in the same atlas; so it's ok to mix
            // the instances up together.
            // In some cases we might want to sort the sprites back-to-front
            // (since they are close to camera facing, we should get correct
            // sorting when sorting on a per-sprite basis)
        std::sort(_pimpl->_queuedInstances.begin(), _pimpl->_queuedInstances.end(), CompareFirst<uint64_t, Float4>());

        auto preparedSpritesI = _pimpl->_preparedSpritesLookup.begin();
        for (auto i=_pimpl->_queuedInstances.cbegin(); i!=_pimpl->_queuedInstances.cend();) {
            uint64_t hash = i->first;
            auto start = i++;
            while (i < _pimpl->_queuedInstances.cend() && i->first == hash) ++i;

            preparedSpritesI = std::lower_bound(preparedSpritesI, _pimpl->_preparedSpritesLookup.end(), hash, CompareFirst<Pimpl::SpriteHash, unsigned>());
            if (preparedSpritesI == _pimpl->_preparedSpritesLookup.end() || preparedSpritesI->first != hash)
                continue;

            unsigned spriteIndex = preparedSpritesI->second;
            auto& sprite = _pimpl->_preparedSprites[spriteIndex];
            assert(sprite._rect[0].second[0] > sprite._rect[0].first[0]
                && sprite._rect[0].second[1] > sprite._rect[0].first[1]);

                // We should have a number of sprites of the same instance. Expand the sprite into 
                // triangles here, on the CPU. 
                // We'll do the projection here because we need to use the parameters from how the
                // sprite was originally projected.

            Float3 projectionCenter = sprite._projectionCentre;
            Float2 projectionSize = sprite._worldSpaceHalfSize;

            for (auto s=start; s<i; ++s) {
                Float3 center = Truncate(s->second);
                float scale = s->second[3];
                float sortingDistance = MagnitudeSquared(center + scale * projectionCenter - cameraPos);
                vertices.push_back(Vertex {center + scale * projectionCenter, cameraRight, cameraUp, scale * projectionSize, spriteIndex, sortingDistance});
            }

            _pimpl->_lruQueue.BringToFront(spriteIndex);
            sprite._usageFrame = _pimpl->_frameCounter;
        }

        if (vertices.empty()) return;

            // sort back-to-front
            // Since the sprites are mostly camera-aligned, this should give us a good
            // approximation of pixel-accurate sorting
        std::sort(vertices.begin(), vertices.end(),
            [](const Vertex& lhs, const Vertex& rhs) { return lhs._sortingDistance > rhs._sortingDistance; });

            // We don't really have to rebuild this every time. We should only need to 
            // rebuild this table when the prepared sprites array changes.
        class SpriteTableElement
        {
        public:
            UInt4 _coords[Pimpl::MipMapCount];
        };
        const unsigned maxSprites = 2048 / Pimpl::MipMapCount;
        SpriteTableElement buffer[maxSprites];
        assert(unsigned(_pimpl->_preparedSprites.size()) <= maxSprites);
        for (unsigned c=0; c<std::min(unsigned(_pimpl->_preparedSprites.size()), maxSprites); ++c) {
            auto& e = buffer[c];
            for (unsigned m=0; m<Pimpl::MipMapCount; ++m) {
                const auto &r = _pimpl->_preparedSprites[c]._rect[m];
                e._coords[m] = UInt4(r.first[0], r.first[1], r.second[0], r.second[1]);
            }
        }
        _pimpl->_spriteTableCB.Update(context, buffer, sizeof(buffer));

            // bind the atlas textures to the find slots (eg, over DiffuseTexture, NormalsTexture, etc)
        for (unsigned c=0; c<_pimpl->_atlas._layers.size(); ++c)
            context.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(c, _pimpl->_atlas._layers[c]._atlas.SRV()));

        // shader.Apply(context, parserContext, {std::move(spriteTable)});
        ConstantBufferView cbs[] = {&_pimpl->_spriteTableCB};
        shader._shader._boundUniforms->Apply(context, 0, parserContext.GetGlobalUniformsStream());
		shader._shader._boundUniforms->Apply(context, 1, UniformsStream{MakeIteratorRange(cbs)});
        context.Bind(*shader._shader._shaderProgram);
        
        auto tempvb = MakeMetalVB(AsPointer(vertices.begin()), vertices.size()*sizeof(Vertex));
		VertexBufferView vbv[] = { &tempvb };
		shader._shader._boundLayout->Apply(context, MakeIteratorRange(vbv));
        context.Bind(Topology::PointList);
        context.Bind(Techniques::CommonResources()._blendOneSrcAlpha);
        context.Bind(Techniques::CommonResources()._dssReadWrite);
        context.Bind(Techniques::CommonResources()._defaultRasterizer);
        context.Draw((unsigned)vertices.size());

        QuickMetrics(parserContext) << "Imposters: Drawing (" << vertices.size() << ") instances from (" << _pimpl->_queuedObjects.size() << ") unique types.\n";
    }

    auto DynamicImposters::Pimpl::BuildSprite(
        RenderCore::Metal::DeviceContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        const QueuedObject& ob) -> PreparedSprite
    {
        Metal::GPUAnnotation annon(context, "Imposter-Prepare");

            // First we need to calculate the correct size for the sprite required
            // for this object. Because we're using a generic rectangle packing
            // algorithm, we can choose any size.
            // We want to try to get a 1:1 ratio at a certain distance from the
            // camera. So let's create an imaginary camera at a given distance from the
            // object & find the approximate screen space dimensions (based on the 
            // bounding box).
            //
            // We can perhaps optimise this by analysing the render target after rendering
            // -- though it would require a CPU/GPU sync (or some very complex method to 
            // delay moving the render target into the atlas).
            //
            // We're also going to need mip-maps for the sprites. That gives us two options:
            //      1) use power of 2 dimensions (or close to it) and then use the standard
            //          hardware support for mipmaps
            //      2) use unconstrained dimensions, and allocate each mipmap using a separate
            //          packing operation.

        const auto& cfg = _config;
        auto bb = ob._scaffold->GetStaticBoundingBox();
        Float3 centre = .5f * (bb.first + bb.second);
        Float2 cameraForward = -XYAngleAsVector(ob._XYangle, cfg._angleQuant);
        Float3 camera = centre + cfg._calibrationDistance * -Expand(cameraForward, 0.f);
        
            // Find the vertical and horizontal angles for each corner of the bounding box.
        Float3 corners[] = {
            Float3( bb.first[0],  bb.first[1],  bb.first[2]),
            Float3(bb.second[0],  bb.first[1],  bb.first[2]),
            Float3( bb.first[0], bb.second[1],  bb.first[2]),
            Float3(bb.second[0], bb.second[1],  bb.first[2]),
            Float3( bb.first[0],  bb.first[1], bb.second[2]),
            Float3(bb.second[0],  bb.first[1], bb.second[2]),
            Float3( bb.first[0], bb.second[1], bb.second[2]),
            Float3(bb.second[0], bb.second[1], bb.second[2])
        };

        const auto camToWorld = MakeCameraToWorld(Expand(cameraForward, 0.f), Float3(0.f, 0.f, 1.f), camera);
        const auto worldToCam = InvertOrthonormalTransform(camToWorld);

        const float pixelRatio = 16.f/9.f;    // Adjust the pixel ratio for standard 16:9
        auto virtualProj = PerspectiveProjection(
            cfg._calibrationFov, pixelRatio,
            0.05f, 2000.f, GeometricCoordinateSpace::RightHanded,
            Techniques::GetDefaultClipSpaceType());
        Float2 screenSpaceMin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), screenSpaceMax(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max());
        for (unsigned c=0; c<dimof(corners); ++c) {
            Float4 proj = virtualProj * worldToCam * Expand(corners[c], 1.f);
            Float2 d(proj[0] / proj[3], proj[1] / proj[3]);

            screenSpaceMin[0] = std::min(screenSpaceMin[0], d[0]);
            screenSpaceMin[1] = std::min(screenSpaceMin[1], d[1]);
            screenSpaceMax[0] = std::max(screenSpaceMax[0], d[0]);
            screenSpaceMax[1] = std::max(screenSpaceMax[1], d[1]);
        }

        Float2 a(.5f * (screenSpaceMax[0] + screenSpaceMin[0]), .5f * (screenSpaceMax[1] + screenSpaceMin[1]));
        Float4x4 adjustmentMatrix(
            2.f / (screenSpaceMax[0] - screenSpaceMin[0]), 0.f, 0.f, (2.f * a[0]) / (screenSpaceMax[0] - screenSpaceMin[0]),
            0.f, 2.f / (screenSpaceMax[1] - screenSpaceMin[1]), 0.f, (2.f * a[1]) / (screenSpaceMax[1] - screenSpaceMin[1]),
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f);

        Int2 minCoords(
            (int)XlFloor((screenSpaceMin[0] * .5f + .5f) * cfg._calibrationPixels),
            (int)XlFloor((screenSpaceMin[1] * .5f + .5f) * cfg._calibrationPixels));
        Int2 maxCoords(
            (int)XlFloor((screenSpaceMax[0] * .5f + .5f) * cfg._calibrationPixels),
            (int)XlFloor((screenSpaceMax[1] * .5f + .5f) * cfg._calibrationPixels));

            // If the object is too big, we need to constrain the dimensions
            // note that we should adjust the projection matrix as well, to
            // match this.
        UInt2 dims = maxCoords - minCoords;
        if (dims[0] > cfg._maxDims[0]) {
            minCoords[0] += (dims[0] - cfg._maxDims[0]) / 2;
            maxCoords[0] -= (dims[0] - cfg._maxDims[0]) / 2 + 1;
            dims[0] = maxCoords[0] - minCoords[0];
            assert(dims[0] <= cfg._maxDims[0]);
        }
        if (dims[0] < cfg._minDims[0]) {
            minCoords[0] -= (cfg._minDims[0] - dims[0]) / 2;
            maxCoords[0] += (cfg._minDims[0] - dims[0]) / 2 + 1;
            dims[0] = maxCoords[0] - minCoords[0];
            assert(dims[0] >= cfg._minDims[0]);
        }
        if (dims[1] > cfg._maxDims[1]) {
            minCoords[1] += (dims[1] - cfg._maxDims[1]) / 2;
            maxCoords[1] -= (dims[1] - cfg._maxDims[1]) / 2 + 1;
            dims[1] = maxCoords[1] - minCoords[1];
            assert(dims[1] <= cfg._maxDims[1]);
        }
        if (dims[1] < cfg._minDims[1]) {
            minCoords[1] -= (cfg._minDims[1] - dims[1]) / 2;
            maxCoords[1] += (cfg._minDims[1] - dims[1]) / 2 + 1;
            dims[1] = maxCoords[1] - minCoords[1];
            assert(dims[1] >= cfg._minDims[1]);
        }

            // Reserve some space before we render
            // Note that we have to be careful here, because this operation must
            // be reversable. If there is an exception during rendering (eg, pending
            // asset) or a failure while allocating space for mip-maps, then we want
            // to revert to our previous state.
            // Note that copying the packer here results in an allocation... It would
            // be good if we could avoid this allocation!
        Packer newPacker = _packer;

        Rectangle reservedSpace[MipMapCount];
        for (unsigned c=0; c<MipMapCount; ++c) {
            reservedSpace[c] = newPacker.Allocate(MipMapDims(dims, c));
            if (    reservedSpace[c].first[0] >= reservedSpace[c].second[0]
                ||  reservedSpace[c].first[1] >= reservedSpace[c].second[1]) {
                return PreparedSprite();
            }
        }

            // Render the object to our temporary buffer with the given camera
            // and focus points (and the viewport we've calculated)
            // Is it best to render with a perspective camera? Or orthogonal?
            // Because the sprite is used at different distances, the perspective
            // is not fixed... But a fixed perspective camera might still be a better
            // approximation than an orthogonal camera.
        Float4x4 finalProj = adjustmentMatrix * virtualProj;
        RenderObject(
            context, parserContext, ob,
            camToWorld, ViewportDesc(0.f, 0.f, float(maxCoords[0] - minCoords[0]), float(maxCoords[1] - minCoords[1])),
            finalProj);

            // Now we want to copy the rendered sprite into the atlas
            // We should generate the mip-maps in this step as well.
        context.Bind(Techniques::CommonResources()._blendOpaque);
        for (unsigned c=0; c<MipMapCount; ++c) {
            CopyToAltas(
                context, reservedSpace[c],
                Rectangle(UInt2(0, 0), maxCoords - minCoords));
        }

            // once everything is complete (and there is no further possibility of an exception,
            // we should commit our changes to the rectangle packer)
        _packer = std::move(newPacker);

        for (unsigned c=0; c<MipMapCount; ++c)
            _copyCounter += 
                    (reservedSpace[c].second[0] - reservedSpace[c].first[0])
                *   (reservedSpace[c].second[1] - reservedSpace[c].first[1])
                ;

        PreparedSprite result;
        for (unsigned c=0; c<MipMapCount; ++c)
            result._rect[c] = reservedSpace[c];
        result._projectionCentre = centre;

            // Get the world space sprite rectangle for the plane through a point exactly
            // cfg._calibrationDistance in front of the camera
            // Note that with a bit more math, we could calculate the intersection of an
            // arbitrary plane with the projection... This would allow us to generate a 
            // sprite that is not exactly camera-aligned (so we could rotate the sprite 
            // geometry slightly in 3D as the camera spins around it)
        {
            float fov, aspect;
            std::tie(fov, aspect) = CalculateFov(ExtractMinimalProjection(finalProj), Techniques::GetDefaultClipSpaceType());
            result._worldSpaceHalfSize[1] = cfg._calibrationDistance * XlTan(.5f * fov);
            result._worldSpaceHalfSize[0] = aspect * result._worldSpaceHalfSize[1];
        }

        result._createFrame = _frameCounter;

        return result;
    }

    void DynamicImposters::Pimpl::CopyToAltas(
        Metal::DeviceContext& context,
        const Rectangle& destination,
        const Rectangle& source)
    {
            // Copy from the temporary render surface of the altas into
            // the final sprite destination. This will sometimes require
            // re-sampling (for mipmap generation).
            //
            // Let's use a shader-based copy.
            //
            // In an ideal world we would do compression in this step.
            // nVidia has some cuda-based texture compressors that could
            // presumedly compress the texture on the GPU (without needing
            // CPU involvement)
        for (unsigned c=0; c<unsigned(_atlas._layers.size()); ++c) {
            const auto& l = _atlas._layers[c];
            ShaderBasedCopy(
                context,
                l._atlas.RTV(), l._tempRTV.SRV(),
                destination, source, CopyFilter::BoxFilterAlphaComplementWeight, 0);
        }
    }

    class CustomStateResolver : public Techniques::IRenderStateDelegate
    {
    public:
        auto Resolve(
            const RenderCore::Assets::RenderStateSet& states, 
            unsigned techniqueIndex) -> Techniques::CompiledRenderStateSet
        {
            return Techniques::CompiledRenderStateSet{
                Metal::BlendState(_blendState), 
				Techniques::BuildDefaultRastizerState(states)};
        }

        virtual uint64_t GetHash() { return typeid(CustomStateResolver).hash_code(); }

        CustomStateResolver()
        {
            _blendState = Metal::BlendState(
                BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha,
                BlendOp::Add, Blend::Zero, Blend::InvSrcAlpha);
        }
    private:
        Metal::BlendState _blendState;
    };

    void DynamicImposters::Pimpl::RenderObject(
        Metal::DeviceContext& context,
        Techniques::ParsingContext& parserContext,
        const QueuedObject& ob, 
        const Float4x4& cameraToWorld,
        const ViewportDesc& viewport,
        const Float4x4& cameraToProjection) const
    {
        StringMeldAppend(parserContext._stringHelpers->_errorString)
            << "Building dynamic imposter: (" << /*ob._scaffold->Filename() <<*/ ") angle (" << ob._XYangle << ")\n";

        context.Clear(_atlas._tempDSV.DSV(), Metal::DeviceContext::ClearFilter::Depth|Metal::DeviceContext::ClearFilter::Stencil, 1.f, 0u);

        Metal::RenderTargetView rtvs[3];
        auto layerCount = std::min(dimof(rtvs), _atlas._layers.size());
        for (unsigned c=0; c<layerCount; ++c) {
            const auto& l = _atlas._layers[c];
                // note --  alpha starts out as 1.f
                //          With the _blendOneSrcAlpha blend, this how no effect
            context.Clear(l._tempRTV.RTV(), {0.f, 0.f, 0.f, 1.f});  
            rtvs[c] = l._tempRTV.RTV();
        }
        
        context.Bind(
            ResourceList<Metal::RenderTargetView, dimof(rtvs)>(
                std::initializer_list<Metal::RenderTargetView>(rtvs, &rtvs[(unsigned)layerCount])), 
            &_atlas._tempDSV.DSV());
        context.Bind(viewport);
        context.Bind(Techniques::CommonResources()._blendOpaque);
        context.Bind(Techniques::CommonResources()._dssReadWrite);

            // We have to adjust the projection desc and the
            // main transform constant buffer...
            // We will squish the FOV to align the object perfectly within our
            // perspective frustum
        auto oldProjDesc = parserContext.GetProjectionDesc();

        auto& projDesc = parserContext.GetProjectionDesc();
        std::tie(projDesc._verticalFov, projDesc._aspectRatio) = 
            CalculateFov(ExtractMinimalProjection(cameraToProjection), Techniques::GetDefaultClipSpaceType());

        Float4x4 modelToWorld = Identity<Float4x4>();
        const bool viewSpaceNormals = true;

            // Normally the deferred shader writes out world space normals.
            // However, we can cause it to write view space normals by
            // defining world space to be the same as view space.
            // That is, model->world actually transforms into camera space.
            // So, worldToProjection becomes just cameraToProjection
        if (constant_expression<viewSpaceNormals>::result()) {
            projDesc._worldToProjection = cameraToProjection;
            projDesc._cameraToProjection = cameraToProjection;
            projDesc._cameraToWorld = Identity<Float4x4>();
            modelToWorld = InvertOrthonormalTransform(cameraToWorld);
        } else {
            projDesc._worldToProjection = Combine(InvertOrthonormalTransform(cameraToWorld), cameraToProjection);
            projDesc._cameraToProjection = cameraToProjection;
            projDesc._cameraToWorld = cameraToWorld;
        }

        auto cleanup = MakeAutoCleanup(
            [&projDesc, &oldProjDesc, &context, &parserContext]() 
            { 
                projDesc = oldProjDesc;
                auto globalTransform = BuildGlobalTransformConstants(projDesc);
                parserContext.SetGlobalCB(
                    context, Techniques::TechniqueContext::CB_GlobalTransform,
                    &globalTransform, sizeof(globalTransform));
                parserContext.GetSubframeShaderSelectors().SetParameter("DECAL_BLEND", 0u);
            });

        auto globalTransform = BuildGlobalTransformConstants(projDesc);
        parserContext.SetGlobalCB(
            context, Techniques::TechniqueContext::CB_GlobalTransform,
            &globalTransform, sizeof(globalTransform));

            // We need to use the "DECAL_BLEND" mode.
            // This writes the blending alpha to the destination target
        parserContext.GetSubframeShaderSelectors().SetParameter("DECAL_BLEND", 1u);

            // Now we can just render the object.
            // Let's use a temporary DelayedDrawCallSet
            // todo -- it might be ideal to use an MSAA target for this step

        FixedFunctionModel::DelayedDrawCallSet drawCalls(typeid(FixedFunctionModel::ModelRenderer).hash_code());
        ob._renderer->Prepare(
            drawCalls, *_sharedStateSet,
            modelToWorld, FixedFunctionModel::MeshToModel(*ob._scaffold));
        FixedFunctionModel::ModelRenderer::Sort(drawCalls);

        auto marker = _sharedStateSet->CaptureState(context, _stateRes, nullptr);
        FixedFunctionModel::ModelRenderer::RenderPrepared(
            FixedFunctionModel::ModelRendererContext(context, parserContext, Techniques::TechniqueIndex::Deferred),
            *_sharedStateSet, drawCalls, FixedFunctionModel::DelayStep::OpaqueRender);

            // We also have to render the rest of the geometry (using the same technique)
            // otherwise this geometry will never be rendered.
            // Since the imposters are mostly used in the distance, we're going to use
            // unsorted translucency as a rough approximation of sorted blending.

        context.Bind(Techniques::CommonResources()._dssReadOnly);

        FixedFunctionModel::ModelRenderer::RenderPrepared(
            FixedFunctionModel::ModelRendererContext(context, parserContext, Techniques::TechniqueIndex::Deferred),
            *_sharedStateSet, drawCalls, FixedFunctionModel::DelayStep::PostDeferred);
        FixedFunctionModel::ModelRenderer::RenderPrepared(
            FixedFunctionModel::ModelRendererContext(context, parserContext, Techniques::TechniqueIndex::Deferred),
            *_sharedStateSet, drawCalls, FixedFunctionModel::DelayStep::SortedBlending);
    }

    void DynamicImposters::Reset()
    {
        _pimpl->_queuedObjects.clear();
        _pimpl->_queuedInstances.clear();

        if (Tweakable("ImpostersReset", false)) {
            _pimpl->_packer = Packer(Truncate(_pimpl->_config._altasSize));
            _pimpl->_copyCounter = 0;

            _pimpl->_preparedSprites.clear();
            _pimpl->_preparedSpritesLookup.clear();
            _pimpl->_lruQueue = LRUQueue(_pimpl->_config._maxSpriteCount);
            _pimpl->_preparedSprites.resize(_pimpl->_config._maxSpriteCount);
            _pimpl->_preparedSpritesLookup.reserve(_pimpl->_config._maxSpriteCount);
            _pimpl->_preparedSpritesHeap = SpanningHeap<uint16>(_pimpl->_config._maxSpriteCount<<4);
        }
    }

    void DynamicImposters::Queue(
        const ModelRenderer& renderer, 
        const ModelScaffold& scaffold,
        const Float3x4& localToWorld,
        const Float3& cameraPosition)
    {
            // Note that we're ignoring most of the rotation information in the local-to-world
            // For objects that are incorrectly exported (eg wrong up vector), we will sometimes 
            // rotate all instances to compenstate. But that will be ignored when we get to this 
            // point!
        auto qo = Pimpl::QueuedObject { &renderer, &scaffold, GetXYAngle(scaffold, localToWorld, cameraPosition, _pimpl->_config._angleQuant) };
        auto hash = qo.MakeHash();
        auto existing = LowerBound(_pimpl->_queuedObjects, hash);

        if (existing == _pimpl->_queuedObjects.end() || existing->first != hash)
            _pimpl->_queuedObjects.insert(existing, std::make_pair(hash, qo));
                
        _pimpl->_queuedInstances.push_back(
            std::make_pair(hash, Expand(
                ExtractTranslation(localToWorld),
                ExtractUniformScaleFast(localToWorld))));
    }

    void DynamicImposters::Load(const Config& config)
    {
        Reset();

        _pimpl->_config = config;

        auto atlasSize = Truncate(config._altasSize);
        _pimpl->_packer = Packer(atlasSize);
        _pimpl->_copyCounter = 0;

            // the formats we initialize for the atlas really depend on whether we're going
            // to be writing pre-lighting or post-lighting parameters to the sprites.
        _pimpl->_atlas = ImposterSpriteAtlas(
            atlasSize, config._maxDims, { Format::R8G8B8A8_UNORM_SRGB, Format::R8G8B8A8_SNORM});

            // allocate the sprite table 
        _pimpl->_preparedSprites.clear();
        _pimpl->_preparedSpritesLookup.clear();
        _pimpl->_lruQueue = LRUQueue(config._maxSpriteCount);
        _pimpl->_preparedSprites.resize(config._maxSpriteCount);
        _pimpl->_preparedSpritesLookup.reserve(config._maxSpriteCount);
        _pimpl->_preparedSpritesHeap = SpanningHeap<uint16>(_pimpl->_config._maxSpriteCount<<4);
    }

    void DynamicImposters::Disable()
    {
        Reset();
        _pimpl->_preparedSprites.clear();
        _pimpl->_config = Config();
        _pimpl->_packer = Packer();
        _pimpl->_atlas = ImposterSpriteAtlas();
        _pimpl->_lruQueue = LRUQueue();
        _pimpl->_preparedSprites = decltype(_pimpl->_preparedSprites)();
        _pimpl->_preparedSpritesLookup = decltype(_pimpl->_preparedSpritesLookup)();
        _pimpl->_preparedSpritesHeap = decltype(_pimpl->_preparedSpritesHeap)();
    }

    float DynamicImposters::GetThresholdDistance() const { return _pimpl->_config._thresholdDistance; }
    bool DynamicImposters::IsEnabled() const { return !_pimpl->_atlas._layers.empty(); }

    auto DynamicImposters::GetMetrics() const -> Metrics
    {
        Metrics result;
        result._spriteCount = _pimpl->_preparedSpritesHeap.CalculateAllocatedSpace() >> 4;
        result._maxSpriteCount = (unsigned)_pimpl->_preparedSprites.size();
        result._pixelsAllocated = 0;
        for (const auto&i:_pimpl->_preparedSprites) {
            for (unsigned m=0; m<dimof(i._rect); ++m) {
                const auto& r = i._rect[m];
                result._pixelsAllocated += (r.second[0] - r.first[0]) * (r.second[1] - r.first[1]);
            }
        }
        result._pixelsTotal = 
              _pimpl->_config._altasSize[0]
            * _pimpl->_config._altasSize[1]
            * _pimpl->_config._altasSize[2]
            ;
        result._overflowCounter = _pimpl->_overflowCounter + _pimpl->_overflowMaxSpritesCounter;
        result._pendingCounter = _pimpl->_pendingCounter;
        result._evictionCounter = _pimpl->_evictionCounter;
        std::tie(result._largestFreeBlockArea, result._largestFreeBlockSide) 
            = _pimpl->_packer.LargestFreeBlock();
        
        result._bytesPerPixel = 
            (BitsPerPixel(Format::R8G8B8A8_UNORM_SRGB) + BitsPerPixel(Format::R8G8B8A8_SNORM)) / 8;
        result._layerCount = (unsigned)_pimpl->_atlas._layers.size();

        auto oldest = _pimpl->_lruQueue.GetOldestValue();
        if (oldest != unsigned(0x0)) {
            result._mostStaleCounter = _pimpl->_frameCounter - _pimpl->_preparedSprites[oldest]._usageFrame;
        } else {
            result._mostStaleCounter = 0;
        }
        return result;
    }

    Metal::ShaderResourceView DynamicImposters::GetAtlasResource(unsigned layer)
    {
        return _pimpl->_atlas._layers[layer]._atlas.SRV();
    }

    auto DynamicImposters::GetSpriteMetrics(unsigned spriteIndex) -> SpriteMetrics
    {
        const auto& sprite = _pimpl->_preparedSprites[spriteIndex];
        SpriteMetrics result;
        for (unsigned c=0; c<dimof(result._mipMaps); ++c)
            result._mipMaps[c] = sprite._rect[c];
        result._age = _pimpl->_frameCounter - sprite._createFrame;
        result._timeSinceUsage = _pimpl->_frameCounter - sprite._usageFrame;
        return result;
    }

    DynamicImposters::DynamicImposters(SharedStateSet& sharedStateSet)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_sharedStateSet = &sharedStateSet;
        _pimpl->_overflowCounter = 0;
        _pimpl->_overflowMaxSpritesCounter = 0;
        _pimpl->_pendingCounter = 0;
        _pimpl->_copyCounter = 0;
        _pimpl->_evictionCounter = 0;
        _pimpl->_frameCounter = 0;

        _pimpl->_material = FixedFunctionModel::SimpleShaderVariationManager(
            MakeIteratorRange(s_inputLayout),
            {Hash64("SpriteTable")}, ParameterBox());
        _pimpl->_stateRes = std::make_shared<CustomStateResolver>();

        _pimpl->_spriteTableCB = MakeMetalCB(nullptr, sizeof(UInt4)*2048);
    }

    DynamicImposters::~DynamicImposters() {}

    DynamicImposters::Config::Config()
    {
        _thresholdDistance = 650.f;
        _angleQuant = 8;
        _calibrationDistance = 1000.f;
        _calibrationFov = Deg2Rad(45.f);
        _calibrationPixels = 1000;
        _minDims = UInt2(32, 32);
        _maxDims = UInt2(128, 128);
        _altasSize = UInt3(1024, 256, 1); // UInt3(4096, 2048, 1);
        _maxSpriteCount = 256;
    }

    DynamicImposters::Metrics::Metrics()
    {
        _spriteCount = 0;
        _pixelsAllocated = 0;
        _pixelsTotal = 0;
        _largestFreeBlockArea = UInt2(0,0);
        _largestFreeBlockSide = UInt2(0,0);
        _overflowCounter = 0;
        _pendingCounter = 0;
        _bytesPerPixel = 0;
        _layerCount = 0;
    }
}


template<> const ClassAccessors& Legacy_GetAccessors<SceneEngine::DynamicImposters::Config>()
{
    using Obj = SceneEngine::DynamicImposters::Config;
    static ClassAccessors props(typeid(Obj).hash_code());
    static bool init = false;
    if (!init) {
        props.Add("ThresholdDistance", DefaultGet(Obj, _thresholdDistance),  DefaultSet(Obj, _thresholdDistance));
        props.Add("AngleQuant", DefaultGet(Obj, _angleQuant),  DefaultSet(Obj, _angleQuant));
        props.Add("CalibrationDistance", DefaultGet(Obj, _calibrationDistance),  DefaultSet(Obj, _calibrationDistance));
        props.Add("CalibrationFov", 
            [](const Obj& obj) { return Rad2Deg(obj._calibrationFov); }, 
            [](Obj& obj, float value) { obj._calibrationFov = Deg2Rad(value); });
        props.Add("CalibrationPixels", DefaultGet(Obj, _calibrationPixels),  DefaultSet(Obj, _calibrationPixels));
        props.Add("MinDims", DefaultGet(Obj, _minDims),  DefaultSet(Obj, _minDims));
        props.Add("MaxDims", DefaultGet(Obj, _maxDims),  DefaultSet(Obj, _maxDims));
        props.Add("AltasSize", DefaultGet(Obj, _altasSize),  DefaultSet(Obj, _altasSize));
        props.Add("MaxSpriteCount", DefaultGet(Obj, _maxSpriteCount),  DefaultSet(Obj, _maxSpriteCount));

        init = true;
    }
    return props;
}

#else

#include "../Utility/Meta/ClassAccessors.h"
#include "../Utility/Meta/ClassAccessorsImpl.h"
#include "../Utility/Meta/AccessorSerialize.h"

namespace SceneEngine
{
    class DynamicImposters::Pimpl {};
    void DynamicImposters::Render(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        unsigned techniqueIndex) 
    {}
    void DynamicImposters::Queue(
        const ModelRenderer& renderer,
        const Float3x4& localToWorld, const Float3& cameraPosition)
    {}
    void DynamicImposters::Reset() {}

    void DynamicImposters::Load(const Config& config) {}
    void DynamicImposters::Disable() {}

    float DynamicImposters::GetThresholdDistance() const { return 1e32f; }
    bool DynamicImposters::IsEnabled() const { return false; }

    auto DynamicImposters::GetMetrics() const -> Metrics { return {}; }
    const RenderCore::IResourceView* DynamicImposters::GetAtlasResource(unsigned layer) { return nullptr; }
    auto DynamicImposters::GetSpriteMetrics(unsigned spriteIndex) -> SpriteMetrics { return {}; }

    DynamicImposters::DynamicImposters() {}
    DynamicImposters::~DynamicImposters() {}

    DynamicImposters::Config::Config() {}
    DynamicImposters::Metrics::Metrics() {}
}

template<> const ClassAccessors& Legacy_GetAccessors<SceneEngine::DynamicImposters::Config>()
{
    using Obj = SceneEngine::DynamicImposters::Config;
    static ClassAccessors props(typeid(Obj).hash_code());
    return props;
}

#endif

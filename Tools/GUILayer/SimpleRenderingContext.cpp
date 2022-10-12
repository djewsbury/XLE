// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleRenderingContext.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "EditorInterfaceUtils.h"
#include "GUILayerUtil.h"
#include "IOverlaySystem.h"
#include "UITypesBinding.h"
#include "MathLayer.h"
#include "ExportedNativeTypes.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/ManipulatorsRender.h"
#include "../../RenderCore/Types.h"
#include "../../RenderCore/ResourceDesc.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/Drawables.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../Math/Transformations.h"
#include <vector>

#include "../../SceneEngine/PlacementsManager.h"        // For some of the code in RenderingUtil below

namespace GUILayer
{
    class VertexFormatRecord
    {
    public:
        std::vector<RenderCore::InputElementDesc> _inputLayout;
    };

    class SavedRenderResourcesPimpl
    {
    public:
        std::vector<std::pair<uint64, RenderCore::IResourcePtr>> _vertexBuffers;
        std::vector<std::pair<uint64, unsigned>> _vbFormat;
        std::vector<std::pair<uint64, RenderCore::IResourcePtr>> _indexBuffers;
        uint64 _nextBufferID;
        RenderCore::IDevice* _device;

        VertexFormatRecord _vfRecord[8];
        SavedRenderResourcesPimpl(RenderCore::IDevice& device);
    };

    SavedRenderResourcesPimpl::SavedRenderResourcesPimpl(RenderCore::IDevice& device)
    : _nextBufferID(1)
    , _device(&device)
    {
            //  These are the vertex formats defined by the Sony editor
        using namespace RenderCore;
        _vfRecord[0]._inputLayout = {GlobalInputLayouts::P.begin(), GlobalInputLayouts::P.end()};
        _vfRecord[1]._inputLayout = {GlobalInputLayouts::PC.begin(), GlobalInputLayouts::PC.end()};
        _vfRecord[2]._inputLayout = {GlobalInputLayouts::PN.begin(), GlobalInputLayouts::PN.end()};
        _vfRecord[3]._inputLayout = {GlobalInputLayouts::PT.begin(), GlobalInputLayouts::PT.end()};
		_vfRecord[4]._inputLayout = {};
        _vfRecord[5]._inputLayout = {GlobalInputLayouts::PNT.begin(), GlobalInputLayouts::PNT.end()};
        _vfRecord[6]._inputLayout = {GlobalInputLayouts::PNTT.begin(), GlobalInputLayouts::PNTT.end()};
		_vfRecord[7]._inputLayout = {};

        // formats without a "color" input get color in a per-instance secondary vertex stream
        _vfRecord[0]._inputLayout.emplace_back("COLOR", 0, Format::R32G32B32A32_FLOAT, 1, 0, InputDataRate::PerInstance);
        _vfRecord[2]._inputLayout.emplace_back("COLOR", 0, Format::R32G32B32A32_FLOAT, 1, 0, InputDataRate::PerInstance);
        _vfRecord[3]._inputLayout.emplace_back("COLOR", 0, Format::R32G32B32A32_FLOAT, 1, 0, InputDataRate::PerInstance);
        _vfRecord[5]._inputLayout.emplace_back("COLOR", 0, Format::R32G32B32A32_FLOAT, 1, 0, InputDataRate::PerInstance);
        _vfRecord[6]._inputLayout.emplace_back("COLOR", 0, Format::R32G32B32A32_FLOAT, 1, 0, InputDataRate::PerInstance);
    }

    /// <summary>Create and maintain rendering resources for SimpleRenderingContext</summary>
    /// Create & maintain vertex and index buffers. Intended for use when linking to
    /// C# GUI apps.
    public ref class RetainedRenderResources
    {
    public:
        uint64  CreateVertexBuffer(void* data, size_t size, unsigned format);
        uint64  CreateIndexBuffer(void* data, size_t size);
        bool    DeleteBuffer(uint64 id);

        std::shared_ptr<RenderCore::IResource> GetVertexBuffer(uint64 id);
        std::shared_ptr<RenderCore::IResource> GetIndexBuffer(uint64 id);
        const VertexFormatRecord* GetVertexBufferFormat(uint64 id);

        RetainedRenderResources(EngineDevice^ engineDevice);
        ~RetainedRenderResources();
        !RetainedRenderResources();
    protected:
        clix::auto_ptr<SavedRenderResourcesPimpl> _pimpl;
    };

#if defined(GUILAYER_SCENEENGINE)
    static bool SetupState(
        RenderCore::Metal::DeviceContext& devContext, 
        RenderCore::Techniques::ParsingContext& parsingContext,
        const float color[], const float xform[],
        const VertexFormatRecord& vf,
		RenderCore::IResource* vb,
		RenderCore::IResource* ib = nullptr)
    {
        CATCH_ASSETS_BEGIN
            using namespace RenderCore;
            const auto techniqueIndex = 0u;

            FixedFunctionModel::SimpleShaderVariationManager material(
                vf._inputLayout,
                {Techniques::ObjectCB::LocalTransform, Techniques::ObjectCB::BasicMaterialConstants},
				{});

            auto variation = material.FindVariation(parsingContext, techniqueIndex, UNLIT_TECH);
            if (variation._shader._shaderProgram == nullptr) {
                return false; // we can't render because we couldn't resolve a good shader variation
            }

			if (ib) {
				auto* res = (RenderCore::Metal::Resource*)ib->QueryInterface(typeid(RenderCore::Metal::Resource).hash_code());
				assert(res);
				if (res)
					devContext.Bind(*res, RenderCore::Format::R32_UINT);
			}

            ParameterBox matConstants;
            matConstants.SetParameter((const utf8*)"MaterialDiffuse", Float3(color[0], color[1], color[2]));

			ConstantBufferView cbvs[] = {
                Techniques::MakeLocalTransformPacket(Transpose(*(Float4x4*)xform), Float3(0.f, 0.f, 0.f)),
                variation._cbLayout->BuildCBDataAsPkt(matConstants, RenderCore::Techniques::GetDefaultShaderLanguage()) };
			variation._shader._boundUniforms->Apply(devContext, 1, { MakeIteratorRange(cbvs) });
            variation._shader.Apply(devContext, parsingContext, { vb });
            return true;
        CATCH_ASSETS_END(parsingContext)
        return false;
    }
#endif

    void SimpleRenderingContext::DrawPrimitive(
        unsigned primitiveType,
        uint64 vb,
        unsigned startVertex,
        unsigned vertexCount,
        const float color[], const float xform[])
    {
        auto* vfFormat = _retainedRes->GetVertexBufferFormat(vb);
        if (!vfFormat) return;

        auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
        geo->_vertexStreams[0]._resource = _retainedRes->GetVertexBuffer(vb);
        geo->_vertexStreamCount = 1;

        if (!geo->_vertexStreams[0]._resource) return;

        // Color is stored into a per-instance secondary vertex stream
        auto colorStorage = _immediateDrawables->GetDrawablesPacket()->AllocateStorage(RenderCore::Techniques::DrawablesPacket::Storage::Vertex, sizeof(Float4));
        geo->_vertexStreams[1]._vbOffset = colorStorage._startOffset;
        geo->_vertexStreams[1]._type = RenderCore::Techniques::DrawableGeo::StreamType::PacketStorage;
        ++geo->_vertexStreamCount;
        *(Float4*)colorStorage._data.begin() = color;

        RenderCore::Techniques::ImmediateDrawableMaterial currentState;
        currentState._uniformStreamInterface = _mainUniformsInterface.get();
        currentState._uniforms._immediateData.push_back(
            RenderCore::Techniques::MakeLocalTransformPacket(Transpose(Float4x4(xform)), ExtractTranslation(_parsingContext->GetProjectionDesc()._cameraToWorld)));
        // We're repurposing this flag for depth write disable
        currentState._stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::WriteMask;
        currentState._stateSet._writeMask = (unsigned(_depthTestEnable)<<1u) | unsigned(_depthWriteEnable);
        _immediateDrawables->QueueDraw(
            vertexCount, startVertex,
            geo,
            vfFormat->_inputLayout,
            currentState,
            (RenderCore::Topology)primitiveType);
    }

    void SimpleRenderingContext::DrawIndexedPrimitive(
        unsigned primitiveType,
        uint64 vb, uint64 ib,
        unsigned startIndex,
        unsigned indexCount,
        unsigned startVertex,
        const float color[], const float xform[]) 
    {
        assert(startVertex == 0);

        auto* vfFormat = _retainedRes->GetVertexBufferFormat(vb);
        if (!vfFormat) return;

        auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
        geo->_vertexStreams[0]._resource = _retainedRes->GetVertexBuffer(vb);
        geo->_vertexStreamCount = 1;
        geo->_ib = _retainedRes->GetIndexBuffer(ib);
        geo->_ibFormat = RenderCore::Format::R32_UINT;

        if (!geo->_vertexStreams[0]._resource || !geo->_ib) return;

        // Color is stored into a per-instance secondary vertex stream
        auto colorStorage = _immediateDrawables->GetDrawablesPacket()->AllocateStorage(RenderCore::Techniques::DrawablesPacket::Storage::Vertex, sizeof(Float4));
        geo->_vertexStreams[1]._vbOffset = colorStorage._startOffset;
        ++geo->_vertexStreamCount;
        *(Float4*)colorStorage._data.begin() = color;

        RenderCore::Techniques::ImmediateDrawableMaterial currentState;
        currentState._uniformStreamInterface = _mainUniformsInterface.get();
        currentState._uniforms._immediateData.push_back(
            RenderCore::Techniques::MakeLocalTransformPacket(Transpose(Float4x4(xform)), ExtractTranslation(_parsingContext->GetProjectionDesc()._cameraToWorld)));
        // We're repurposing this flag for depth write disable
        currentState._stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::WriteMask;
        currentState._stateSet._writeMask = (unsigned(_depthTestEnable)<<1u) | unsigned(_depthWriteEnable);
        _immediateDrawables->QueueDraw(
            indexCount, startIndex,
            geo,
            vfFormat->_inputLayout,
            currentState,
            (RenderCore::Topology)primitiveType);
    }

    void SimpleRenderingContext::InitState(bool depthTest, bool depthWrite)
    {
        _depthTestEnable = !!depthTest;
        _depthWriteEnable = !!depthWrite;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    uint64  RetainedRenderResources::CreateVertexBuffer(void* data, size_t size, unsigned format)
    {
        using namespace RenderCore;
        auto desc = CreateDesc(BindFlag::VertexBuffer, LinearBufferDesc::Create((unsigned)size), "retained-render-resources");
		auto newBuffer = _pimpl->_device->CreateResource(
            desc,
            SubResourceInitData{MakeIteratorRange(data, PtrAdd(data, size))});
        _pimpl->_vertexBuffers.push_back(std::make_pair(_pimpl->_nextBufferID, std::move(newBuffer)));
        _pimpl->_vbFormat.push_back(std::make_pair(_pimpl->_nextBufferID, format));
        return _pimpl->_nextBufferID++;
    }

    uint64  RetainedRenderResources::CreateIndexBuffer(void* data, size_t size)
    {
		using namespace RenderCore;
        auto desc = CreateDesc(BindFlag::IndexBuffer, LinearBufferDesc::Create((unsigned)size), "retained-render-resources");
		auto newBuffer = _pimpl->_device->CreateResource(
            desc,
            SubResourceInitData{MakeIteratorRange(data, PtrAdd(data, size))});
        _pimpl->_indexBuffers.push_back(std::make_pair(_pimpl->_nextBufferID, std::move(newBuffer)));
        return _pimpl->_nextBufferID++;
    }

    std::shared_ptr<RenderCore::IResource> RetainedRenderResources::GetVertexBuffer(uint64 id)
    {
        for (auto i = _pimpl->_vertexBuffers.cbegin(); i != _pimpl->_vertexBuffers.cend(); ++i)
            if (i->first == id) return i->second;
        return nullptr;
    }

    const VertexFormatRecord* RetainedRenderResources::GetVertexBufferFormat(uint64 id)
    {
        for (auto i = _pimpl->_vbFormat.cbegin(); i != _pimpl->_vbFormat.cend(); ++i)
            if (i->first == id && i->second < dimof(_pimpl->_vfRecord)) {
                return &_pimpl->_vfRecord[i->second];
            }
        return nullptr;
    }

    std::shared_ptr<RenderCore::IResource> RetainedRenderResources::GetIndexBuffer(uint64 id)
    {
        for (auto i = _pimpl->_indexBuffers.cbegin(); i != _pimpl->_indexBuffers.cend(); ++i)
            if (i->first == id) return i->second;
        return nullptr;
    }

    bool RetainedRenderResources::DeleteBuffer(uint64 id)
    {
        auto vi = LowerBound(_pimpl->_vertexBuffers, id);
        if (vi != _pimpl->_vertexBuffers.end() && vi->first == id) {
            _pimpl->_vertexBuffers.erase(vi);
            return true;
        }

        auto ii = LowerBound(_pimpl->_indexBuffers, id);
        if (ii != _pimpl->_indexBuffers.end() && ii->first == id) {
            _pimpl->_indexBuffers.erase(ii);
            return true;
        }

        return false;
    }

    RetainedRenderResources::RetainedRenderResources(EngineDevice^ engineDevice) 
    {
        _pimpl.reset(
            new SavedRenderResourcesPimpl(
                *engineDevice->GetNative().GetRenderDevice()));
    }

    RetainedRenderResources::~RetainedRenderResources() { _pimpl.reset(); }
    RetainedRenderResources::!RetainedRenderResources() { _pimpl.reset(); }

////////////////////////////////////////////////////////////////////////////////////////////////?//

    SimpleRenderingContext::SimpleRenderingContext(
        RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
        RetainedRenderResources^ savedRes,
        RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
        void* parsingContext)
    : _retainedRes(savedRes)
    , _immediateDrawables(&immediateDrawables), _parsingContext((RenderCore::Techniques::ParsingContext*)parsingContext)
    , _pipelineAccelerators(&pipelineAccelerators)
    {
        _depthWriteEnable = true;
        _depthTestEnable = true;

        _mainUniformsInterface = std::make_shared<RenderCore::UniformsStreamInterface>();
        _mainUniformsInterface->BindImmediateData(0, Hash64("LocalTransform"));
    }
    SimpleRenderingContext::~SimpleRenderingContext() {}
    SimpleRenderingContext::!SimpleRenderingContext() {}

////////////////////////////////////////////////////////////////////////////////////////////////?//

    public ref class RenderingUtil
    {
    public:
        static void RenderCylinderHighlight(
            SimpleRenderingContext^ renderingContext,
            Vector3 centre, float radius)
        {
            ToolsRig::RenderCylinderHighlight(
                renderingContext->GetParsingContext(), renderingContext->GetPipelineAccelerators(),
                AsFloat3(centre), radius);
        }

        static void RenderHighlight(
            SimpleRenderingContext^ context,
            PlacementsEditorWrapper^ placements,
            PlacementsRendererWrapper^ renderer,
            ObjectSet^ highlight, uint64 materialGuid)
        {
            if (highlight == nullptr) {

				ToolsRig::Placements_RenderHighlight(
                    context->GetParsingContext(), context->GetPipelineAccelerators(),
                    renderer->GetNative(), placements->GetNative().GetCellSet(),
					nullptr, nullptr,
                    materialGuid);

            } else {
                if (highlight->IsEmpty()) return;

                ToolsRig::Placements_RenderHighlight(
                    context->GetParsingContext(), context->GetPipelineAccelerators(),
                    renderer->GetNative(), placements->GetNative().GetCellSet(),
                    (const SceneEngine::PlacementGUID*)AsPointer(highlight->_nativePlacements->cbegin()),
                    (const SceneEngine::PlacementGUID*)AsPointer(highlight->_nativePlacements->cend()),
                    materialGuid);
            }
        }

        static void ClearDepthBuffer(SimpleRenderingContext^ context)
        {
#if defined(GUILAYER_SCENEENGINE)
            auto& metalContext = context->GetDevContext();
            RenderCore::Metal::DepthStencilView dsv(metalContext);
            using ClearFilter = RenderCore::Metal::DeviceContext::ClearFilter;
            metalContext.Clear(dsv, ClearFilter::Depth|ClearFilter::Stencil, 1.f, 0u);
#endif
        }
    };

	public delegate void RenderCallback(GUILayer::SimpleRenderingContext^ context);

    public ref class SimpleRenderingContextOverlaySystem : public GUILayer::IOverlaySystem
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override
        {
            ToolsRig::ConfigureParsingContext(parserContext, *_visCameraSettings.get());
            
            auto& immediateDrawables = *EngineDevice::GetInstance()->GetNative().GetImmediateDrawingApparatus()->_immediateDrawables;
            auto& pipelineAccelerators = *EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_pipelineAccelerators;
			auto context = gcnew GUILayer::SimpleRenderingContext(immediateDrawables, RetainedResources, pipelineAccelerators, &parserContext);
			try
			{
                OnRender(context);

				{
					auto rpi = RenderCore::Techniques::RenderPassToPresentationTargetWithDepthStencil(parserContext);
					immediateDrawables.ExecuteDraws(parserContext, rpi);
				}
				OnRenderPostProcess(context);
			}
			finally
			{
				delete context;
			}
        }

        property VisCameraSettings^ CameraSettings {
            void set(VisCameraSettings^ cameraSettings)
            {
                _visCameraSettings = cameraSettings->GetUnderlying();
            }
            VisCameraSettings^ get()
            {
                return gcnew VisCameraSettings(_visCameraSettings);
            }
        }

        event RenderCallback^ OnRender;
		event RenderCallback^ OnRenderPostProcess;
        property GUILayer::RetainedRenderResources^ RetainedResources;
        clix::shared_ptr<ToolsRig::VisCameraSettings> _visCameraSettings;
    };

}


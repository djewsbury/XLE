// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IterativeSystemDebugger.h"
#include "IOverlaySystem.h"
#include "ManipulatorUtils.h"       // for IGetAndSetProperties
#include "MarshalString.h"
#include "EngineDevice.h"
#include "NativeEngineDevice.h"
#include "ExportedNativeTypes.h"
#include "../../SceneEngine/Erosion.h"
#include "../../SceneEngine/LightingParser.h"
#include "../../SceneEngine/TerrainUberSurface.h"
#include "../../SceneEngine/Fluid.h"
#include "../../SceneEngine/CloudsForm.h"
#include "../../SceneEngine/RenderStepUtils.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/RenderPassUtils.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../BufferUploads/ResourceLocator.h"
#include "../../Math/Transformations.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/Meta/ClassAccessors.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>

#pragma warning(disable:4793) // 'GUILayer::`anonymous namespace'::SetGlobalTransform' : function compiled as native

namespace GUILayer
{
    using ErosionSettings = SceneEngine::ErosionSimulation::Settings;

///////////////////////////////////////////////////////////////////////////////////////////////////

    ref class ErosionOverlay : public IOverlaySystem
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        ErosionOverlay(
            std::shared_ptr<SceneEngine::ErosionSimulation> sim,
            ErosionIterativeSystem::Settings^ previewSettings);
        !ErosionOverlay();
        ~ErosionOverlay();
    private:
        clix::shared_ptr<SceneEngine::ErosionSimulation> _sim;
        ErosionIterativeSystem::Settings^ _previewSettings;
    };

    static SceneEngine::ErosionSimulation::RenderDebugMode AsDebugMode(ErosionIterativeSystem::Settings::Preview input)
    {
        using P = ErosionIterativeSystem::Settings::Preview;
        switch (input) {
        default:
        case P::WaterVelocity: return SceneEngine::ErosionSimulation::RenderDebugMode::WaterVelocity3D;
        case P::HardMaterials: return SceneEngine::ErosionSimulation::RenderDebugMode::HardMaterials;
        case P::SoftMaterials: return SceneEngine::ErosionSimulation::RenderDebugMode::SoftMaterials;
        }
    }

    static void SetGlobalTransform(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        Float2 worldDims)
    {
        auto camToWorld = MakeCameraToWorld(
            Float3(0.f, 0.f, -1.f),
            Float3(0.f, 1.f, 0.f),
            Float3(0.f, 0.f, 0.f));
        SceneEngine::LightingParser_SetGlobalTransform(
            context, parserContext, 
            RenderCore::Techniques::BuildOrthogonalProjectionDesc(
                camToWorld, 
                0.f, worldDims[1], worldDims[0], 0.f, 
                -4096.f, 4096.f));
        SceneEngine::SetFrameGlobalStates(*RenderCore::Metal::DeviceContext::Get(context));
    }

    void ErosionOverlay::Render(
        RenderCore::IThreadContext& threadContext,
		const RenderTargetWrapper& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, renderTarget._renderTarget, parserContext);
        Float2 worldDims = _sim->GetDimensions() * _sim->GetWorldSpaceSpacing();
        SetGlobalTransform(threadContext, parserContext, worldDims);

		auto metalContext = RenderCore::Metal::DeviceContext::Get(threadContext);
        _sim->RenderDebugging(*metalContext, parserContext, AsDebugMode(_previewSettings->ActivePreview));
    }

    ErosionOverlay::ErosionOverlay(
        std::shared_ptr<SceneEngine::ErosionSimulation> sim,
        ErosionIterativeSystem::Settings^ previewSettings)
    : _sim(sim), _previewSettings(previewSettings)
    {}

    ErosionOverlay::!ErosionOverlay() { _sim.reset(); }
    ErosionOverlay::~ErosionOverlay() { _sim.reset(); }

////////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class ClassAccessors_GetAndSet : public IGetAndSetProperties
    {
    public:
        virtual bool TryGetMember(System::String^ name, bool caseInsensitive, System::Type^ type, Object^% result);
        virtual bool TrySetMember(System::String^ name, bool caseInsensitive, Object^ value);

        template<typename Type>
            explicit ClassAccessors_GetAndSet(std::shared_ptr<Type> type);
        !ClassAccessors_GetAndSet();
        ~ClassAccessors_GetAndSet();

    protected:
        const ClassAccessors* _accessors;
        clix::shared_ptr<void> _type;
    };

    bool ClassAccessors_GetAndSet::TryGetMember(System::String^ name, bool caseInsensitive, System::Type^ type, Object^% result)
    {
        auto nativeString = clix::marshalString<clix::E_UTF8>(name);
        if (type == System::Single::typeid) {
            float f = 0.f;
            bool success = _accessors->TryOpaqueGet(
                &f, sizeof(f), ImpliedTyping::TypeOf<float>(),
                _type.get(), Hash64(nativeString.c_str()));
            if (success)
                result = gcnew System::Single(f);
            return success;
        } 
        else if (type == System::UInt32::typeid) {
            uint32 f = 0;
            bool success = _accessors->TryOpaqueGet(
                &f, sizeof(f), ImpliedTyping::TypeOf<uint32>(),
                _type.get(), Hash64(nativeString.c_str()));
            if (success)
                result = gcnew System::UInt32(f);
            return success;
        }
        else if (type == System::Int32::typeid) {
            int32 f = 0;
            bool success = _accessors->TryOpaqueGet(
                &f, sizeof(f), ImpliedTyping::TypeOf<uint32>(),
                _type.get(), Hash64(nativeString.c_str()));
            if (success)
                result = gcnew System::Int32(f);
            return success;
        }
        return false;
    }

    bool ClassAccessors_GetAndSet::TrySetMember(System::String^ name, bool caseInsensitive, Object^ value)
    {
        auto nativeString = clix::marshalString<clix::E_UTF8>(name);
        if (value->GetType() == System::Single::typeid) {
            auto v = *dynamic_cast<System::Single^>(value);
            return _accessors->TryOpaqueSet(
                _type.get(), Hash64(nativeString.c_str()),
                MakeOpaqueIteratorRange(v), ImpliedTyping::TypeOf<decltype(v)>());
        } else if (value->GetType() == System::UInt32::typeid) {
            auto v = *dynamic_cast<System::UInt32^>(value);
            return _accessors->TryOpaqueSet(
                _type.get(), Hash64(nativeString.c_str()),
                MakeOpaqueIteratorRange(v), ImpliedTyping::TypeOf<decltype(v)>());
        } else if (value->GetType() == System::Int32::typeid) {
            auto v = *dynamic_cast<System::Int32^>(value);
            return _accessors->TryOpaqueSet(
                _type.get(), Hash64(nativeString.c_str()),
                MakeOpaqueIteratorRange(v), ImpliedTyping::TypeOf<decltype(v)>());
        }
        return false;
    }

    template<typename Type>
        ClassAccessors_GetAndSet::ClassAccessors_GetAndSet(std::shared_ptr<Type> type)
        : _type(type)
    {
        _accessors = &Legacy_GetAccessors<Type>();
    }

    ClassAccessors_GetAndSet::!ClassAccessors_GetAndSet() { _type.reset(); }
    ClassAccessors_GetAndSet::~ClassAccessors_GetAndSet() { _type.reset(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class ErosionIterativeSystemPimpl
    {
    public:
        std::shared_ptr<SceneEngine::ErosionSimulation> _sim;
        std::shared_ptr<ErosionSettings> _settings;
    };

    static std::shared_ptr<RenderCore::Metal::DeviceContext> GetImmediateContext()
    {
        auto immContext = EngineDevice::GetInstance()->GetNative().GetRenderDevice()->GetImmediateContext();
        return RenderCore::Metal::DeviceContext::Get(*immContext);
    }

    void ErosionIterativeSystem::Tick()
    {
        TRY {
            _pimpl->_sim->Tick(*GetImmediateContext(), *_pimpl->_settings);
        } CATCH (const ::Assets::Exceptions::PendingAsset&) {
        } CATCH_END
    }

    ErosionIterativeSystem::ErosionIterativeSystem(System::String^ sourceHeights)
    {
        using namespace SceneEngine;
        _pimpl.reset(new ErosionIterativeSystemPimpl);
        _pimpl->_settings = std::make_shared<ErosionSettings>();
        _settings = gcnew ErosionIterativeSystem::Settings();

        _getAndSetProperties = gcnew ClassAccessors_GetAndSet(_pimpl->_settings);

        {
            TerrainUberSurfaceGeneric uberSurface(
                clix::marshalString<clix::E_UTF8>(sourceHeights).c_str());

            auto maxSize = 4096u;
            UInt2 dims(
                std::min(uberSurface.GetWidth(), maxSize),
                std::min(uberSurface.GetHeight(), maxSize));
            _pimpl->_sim = std::make_shared<ErosionSimulation>(dims, 1.f);

                // We can use the an ubersurface interface to get the 
                // heights data onto the GPU (in the form of a resource locator)
                // Note that we're limited by the maximum texture size supported
                // by the GPU here. If we want to deal with a very large area, we
                // have to split it up into multiple related simulations.
            intrusive_ptr<BufferUploads::ResourceLocator> resLoc;
            {
                GenericUberSurfaceInterface interf(uberSurface);
                resLoc = interf.CopyToGPU(UInt2(0,0), dims);
            }

            RenderCore::Metal::ShaderResourceView srv(resLoc->GetUnderlying());
            _pimpl->_sim->InitHeights(
                *GetImmediateContext(), srv,
                UInt2(0,0), dims);
        }

        _overlay = gcnew ErosionOverlay(_pimpl->_sim, _settings);
    }

    ErosionIterativeSystem::!ErosionIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    ErosionIterativeSystem::~ErosionIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    ErosionIterativeSystem::Settings::Settings()
    {
        ActivePreview = Preview::HardMaterials;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

    ref class CFDOverlay : public IOverlaySystem
    {
    public:
        virtual void Render(
            RenderCore::Techniques::ParsingContext& parserContext) override;

        using RenderFn = std::function<void(
            RenderCore::IThreadContext&,
            RenderCore::Techniques::ParsingContext&,
            void*)>;

        CFDOverlay(
            std::shared_ptr<void> sim, 
            RenderFn&& renderFn,
            Float2 dims);
        !CFDOverlay();
        ~CFDOverlay();
    private:
        clix::shared_ptr<void> _sim;
        clix::auto_ptr<RenderFn> _renderFn;
        array<float>^ _worldDims;
    };

    static SceneEngine::FluidDebuggingMode AsDebugMode(CFDPreviewSettings::Preview input)
    {
        using P = CFDPreviewSettings::Preview;
        switch (input) {
        default:
        case P::Density: return SceneEngine::FluidDebuggingMode::Density;
        case P::Velocity: return SceneEngine::FluidDebuggingMode::Velocity;
        case P::Temperature: return SceneEngine::FluidDebuggingMode::Temperature;
        case P::Vapor: return SceneEngine::FluidDebuggingMode::Vapor;
        case P::Divergence: return SceneEngine::FluidDebuggingMode::Divergence;
        }
    }

    void CFDOverlay::Render(
        RenderCore::IThreadContext& threadContext,
		const RenderTargetWrapper& renderTarget,
        RenderCore::Techniques::ParsingContext& parserContext)
    {
        if (!(*_renderFn)) return;

		auto rpi = RenderCore::Techniques::RenderPassToPresentationTarget(threadContext, renderTarget._renderTarget, parserContext);
		Float2 worldDims { (float)_worldDims[0], (float)_worldDims[1] };
        SetGlobalTransform(threadContext, parserContext, worldDims);

        (*_renderFn)(
            threadContext, parserContext,
            _sim.get());
    }

    CFDOverlay::CFDOverlay(
        std::shared_ptr<void> sim,
        RenderFn&& renderFn,
        Float2 dims)
    : _sim(sim)
    {
        _renderFn.reset(new RenderFn(std::move(renderFn)));
        _worldDims = gcnew array<float>(2);
        _worldDims[0] = dims[0];
        _worldDims[1] = dims[1];
    }

    CFDOverlay::!CFDOverlay() { _sim.reset(); }
    CFDOverlay::~CFDOverlay() { _sim.reset(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename SimObject>
        static CFDOverlay::RenderFn MakeRenderFn(
            msclr::gcroot<CFDPreviewSettings^> settings)
    {
        return [settings](
                RenderCore::IThreadContext& device,
                RenderCore::Techniques::ParsingContext& parserContext,
                void* sim)
            {
                ((SimObject*)sim)->RenderDebugging(
                    device, parserContext, 
                    AsDebugMode(settings->ActivePreview));
            };
    }

    template<typename SimObject>
        static CFDOverlay::RenderFn MakeRenderWidgetsFn()
    {
        return [](
                RenderCore::IThreadContext& device,
                RenderCore::Techniques::ParsingContext& parsingContext,
                void* sim)
            {
                ((SimObject*)sim)->RenderWidgets(device, parsingContext);
            };
    }

    CFDPreviewSettings::CFDPreviewSettings()
    {
        ActivePreview = Preview::Density;
        DeltaTime = 1.0f / 60.f;
        AddDensity = 0.25f;
        AddTemperature = AddDensity * 0.25f;
        AddVelocity = 1.f;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using FluidSolver = SceneEngine::FluidSolver2D;

    class CFDIterativeSystemPimpl
    {
    public:
        std::shared_ptr<FluidSolver> _sim;
        std::shared_ptr<FluidSolver::Settings> _settings;
    };

    void CFDIterativeSystem::Tick()
    {
        _pimpl->_sim->Tick(_settings->DeltaTime, *_pimpl->_settings);
    }

    void CFDIterativeSystem::OnMouseMove(float px, float py, float velX, float velY, unsigned mouseButton)
    {
        if (!mouseButton) return;

        Float2 coords(
            _pimpl->_sim->GetDimensions()[0] * px,
            _pimpl->_sim->GetDimensions()[1] * (1.f-py));

        auto radius = 4.f;
        auto radiusSq = radius*radius;
        for (float y = XlFloor(coords[1] - radius); y <= XlCeil(coords[1] + radius); ++y) {
            for (float x = XlFloor(coords[0] - radius); x <= XlCeil(coords[0] + radius); ++x) {
                Float2 fo = Float2(x, y) - coords;
                if (MagnitudeSquared(fo) <= radiusSq && x >= 0.f && y >= 0.f) {

                    auto c = UInt2(unsigned(x), unsigned(y));
                    if (mouseButton & (1<<0)) {
                        _pimpl->_sim->AddDensity(c, _settings->AddDensity);
                        _pimpl->_sim->AddTemperature(c, _settings->AddTemperature);
                    } else if (mouseButton & (1<<1)) {
                        _pimpl->_sim->AddDensity(c, -_settings->AddDensity);
                        _pimpl->_sim->AddTemperature(c, -_settings->AddTemperature);
                    } else if (mouseButton & (1<<2)) {
                        _pimpl->_sim->AddVelocity(c, 1.f * Float2(velX, -velY));
                    }
                }
            }
        }
    }
    
    CFDIterativeSystem::CFDIterativeSystem(unsigned size)
    {
        using namespace SceneEngine;
        _pimpl.reset(new CFDIterativeSystemPimpl);
        _pimpl->_settings = std::make_shared<FluidSolver::Settings>();
        _pimpl->_sim = std::make_shared<FluidSolver>(UInt2(size, size));
        _settings = gcnew CFDPreviewSettings();

        _getAndSetProperties = gcnew ClassAccessors_GetAndSet(_pimpl->_settings);

        _overlay = gcnew CFDOverlay(
            _pimpl->_sim, 
            MakeRenderFn<FluidSolver>(_settings),
            _pimpl->_sim->GetDimensions());
    }

    CFDIterativeSystem::!CFDIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    CFDIterativeSystem::~CFDIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using CloudsForm2D = SceneEngine::CloudsForm2D;

    class CloudsIterativeSystemPimpl
    {
    public:
        std::shared_ptr<CloudsForm2D> _sim;
        std::shared_ptr<CloudsForm2D::Settings> _settings;
    };

    void CloudsIterativeSystem::Tick()
    {
        _pimpl->_sim->Tick(_settings->DeltaTime, *_pimpl->_settings);
    }

    void CloudsIterativeSystem::OnMouseMove(float px, float py, float velX, float velY, unsigned mouseButton)
    {
        Float2 coords(
            _pimpl->_sim->GetDimensions()[0] * px,
            _pimpl->_sim->GetDimensions()[1] * (1.f-py));

        if (mouseButton == 0) {
            _pimpl->_sim->OnMouseMove(coords);
            return;
        }

        auto radius = 4.f;
        auto radiusSq = radius*radius;
        for (float y = XlFloor(coords[1] - radius); y <= XlCeil(coords[1] + radius); ++y) {
            for (float x = XlFloor(coords[0] - radius); x <= XlCeil(coords[0] + radius); ++x) {
                Float2 fo = Float2(x, y) - coords;
                if (MagnitudeSquared(fo) <= radiusSq && x >= 0.f && y >= 0.f) {

                    auto c = UInt2(unsigned(x), unsigned(y));
                    if (mouseButton&(1<<0)) {
                        _pimpl->_sim->AddVapor(c, _settings->AddDensity);
                    } else if (mouseButton&(1<<1)) {
                        _pimpl->_sim->AddVelocity(c, _settings->AddVelocity * Float2(velX, -velY));
                    }
                }
            }
        }
    }
    
    CloudsIterativeSystem::CloudsIterativeSystem(unsigned size)
    {
        using namespace SceneEngine;
        _pimpl.reset(new CloudsIterativeSystemPimpl);
        _pimpl->_settings = std::make_shared<CloudsForm2D::Settings>();
        _pimpl->_sim = std::make_shared<CloudsForm2D>(UInt2(size, size));
        _settings = gcnew CFDPreviewSettings();

        _getAndSetProperties = gcnew ClassAccessors_GetAndSet(_pimpl->_settings);

        _overlay = gcnew CFDOverlay(
            _pimpl->_sim, 
            MakeRenderFn<CloudsForm2D>(_settings),
            _pimpl->_sim->GetDimensions());

		_widgetsOverlay = gcnew CFDOverlay(
            _pimpl->_sim, 
            MakeRenderWidgetsFn<CloudsForm2D>(),
            _pimpl->_sim->GetDimensions());
    }

    CloudsIterativeSystem::!CloudsIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
		delete _widgetsOverlay; _widgetsOverlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    CloudsIterativeSystem::~CloudsIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
		delete _widgetsOverlay; _widgetsOverlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using FluidSolver3D = SceneEngine::FluidSolver3D;

    class CFD3DIterativeSystemPimpl
    {
    public:
        std::shared_ptr<FluidSolver3D> _sim;
        std::shared_ptr<FluidSolver3D::Settings> _settings;
    };

    void CFD3DIterativeSystem::Tick()
    {
        _pimpl->_sim->Tick(_settings->DeltaTime, *_pimpl->_settings);
    }

    void CFD3DIterativeSystem::OnMouseMove(float px, float py, float velX, float velY, unsigned mouseButton)
    {
        if (mouseButton&(1<<0)) {
            auto dims = _pimpl->_sim->GetDimensions();
            UInt2 coords((dims[0]*3/8)+rand()%(dims[0]/4), (dims[1]/4)+rand()%(dims[1]*3/8));
            auto radius = 3.f;
            auto radiusSq = radius*radius;
            for (float y = XlFloor(coords[1] - radius); y <= XlCeil(coords[1] + radius); ++y) {
                for (float x = XlFloor(coords[0] - radius); x <= XlCeil(coords[0] + radius); ++x) {
                    Float2 fo = Float2(x, y) - coords;
                    if (MagnitudeSquared(fo) <= radiusSq && x >= 0.f && y >= 0.f) {

                        auto c = UInt2(unsigned(x), unsigned(y));
                        if (mouseButton == 0) {
                            _pimpl->_sim->AddDensity(Expand(c, 1u), 10.f);
                        }
                    }
                }
            }

        }
    }
    
    CFD3DIterativeSystem::CFD3DIterativeSystem(unsigned width, unsigned height, unsigned depth)
    {
        using namespace SceneEngine;
        _pimpl.reset(new CFD3DIterativeSystemPimpl);
        _pimpl->_settings = std::make_shared<FluidSolver3D::Settings>();
        _pimpl->_sim = std::make_shared<FluidSolver3D>(UInt3(width, height, depth));
        _settings = gcnew CFDPreviewSettings();

        _getAndSetProperties = gcnew ClassAccessors_GetAndSet(_pimpl->_settings);

        _overlay = gcnew CFDOverlay(
            _pimpl->_sim, 
            MakeRenderFn<FluidSolver3D>(_settings),
            Truncate(_pimpl->_sim->GetDimensions()));
    }

    CFD3DIterativeSystem::!CFD3DIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    CFD3DIterativeSystem::~CFD3DIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using RefFluidSolver = SceneEngine::ReferenceFluidSolver2D;

    class CFDRefIterativeSystemPimpl
    {
    public:
        std::shared_ptr<RefFluidSolver> _sim;
        std::shared_ptr<RefFluidSolver::Settings> _settings;
    };

    void CFDRefIterativeSystem::Tick()
    {
        _pimpl->_sim->Tick(*_pimpl->_settings);
    }

    void CFDRefIterativeSystem::OnMouseMove(float px, float py, float velX, float velY, unsigned mouseButton)
    {
        if (!mouseButton) return;

        Float2 coords(
            _pimpl->_sim->GetDimensions()[0] * px,
            _pimpl->_sim->GetDimensions()[1] * (1.f-py));

        auto radius = 10.f;
        auto radiusSq = radius*radius;
        for (float y = XlFloor(coords[1] - radius); y <= XlCeil(coords[1] + radius); ++y) {
            for (float x = XlFloor(coords[0] - radius); x <= XlCeil(coords[0] + radius); ++x) {
                Float2 fo = Float2(x, y) - coords;
                if (MagnitudeSquared(fo) <= radiusSq && x >= 0.f && y >= 0.f) {

                    auto c = UInt2(unsigned(x), unsigned(y));
                    if (mouseButton & (1<<0)) {
                        _pimpl->_sim->AddDensity(c, 1.f);
                    } else if (mouseButton & (1<<1)) {
                        _pimpl->_sim->AddDensity(c, -1.f);
                    } else if (mouseButton & (1<<2)) {
                        _pimpl->_sim->AddVelocity(c, 1.f * Float2(velX, -velY));
                    }
                }
            }
        }
    }
    
    CFDRefIterativeSystem::CFDRefIterativeSystem(unsigned size)
    {
        using namespace SceneEngine;
        _pimpl.reset(new CFDRefIterativeSystemPimpl);
        _pimpl->_settings = std::make_shared<RefFluidSolver::Settings>();
        _pimpl->_sim = std::make_shared<RefFluidSolver>(UInt2(size, size));
        _settings = gcnew CFDPreviewSettings();

        _getAndSetProperties = gcnew ClassAccessors_GetAndSet(_pimpl->_settings);

        _overlay = gcnew CFDOverlay(
            _pimpl->_sim, 
            MakeRenderFn<RefFluidSolver>(_settings),
            _pimpl->_sim->GetDimensions());
    }

    CFDRefIterativeSystem::!CFDRefIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }

    CFDRefIterativeSystem::~CFDRefIterativeSystem()
    {
        _pimpl.reset();
        delete _overlay; _overlay = nullptr;
        delete _getAndSetProperties; _getAndSetProperties = nullptr;
    }
    
}


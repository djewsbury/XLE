// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EngineForward.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include <memory>
#include <msclr\auto_gcroot.h>

namespace RenderCore { namespace Assets { class Services; } }
namespace ToolsRig { class DivergentAssetManager; class IPreviewSceneRegistry; }
namespace EntityInterface { class IEntityMountingTree; }
namespace ConsoleRig { class GlobalServices; class CrossModule; }
namespace RenderCore { namespace Techniques 
{ 
    class IPipelineAcceleratorPool;
    class IImmediateDrawables;
    class Services;
    class DrawingApparatus;
    class ImmediateDrawingApparatus;
    class PrimaryResourcesApparatus;
    class FrameRenderingApparatus;
    class TechniqueContext;
}}
namespace RenderCore { namespace LightingEngine { 
    class LightingEngineApparatus;
}}

namespace GUILayer
{
    class NativeEngineDevice
    {
    public:
        const std::shared_ptr<RenderCore::IDevice>&        GetRenderDevice() { return _renderDevice; }
        ::Assets::Services*         GetAssetServices() { return _assetServices.get(); }
        RenderCore::IThreadContext* GetImmediateContext();
        ConsoleRig::GlobalServices* GetGlobalServices() { return _services.get(); }
        int                         GetCreationThreadId() { return _creationThreadId; }

        const std::shared_ptr<RenderCore::Techniques::DrawingApparatus>& GetDrawingApparatus();
        const std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus>& GetImmediateDrawingApparatus();
        const std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus>& GetPrimaryResourcesApparatus();
        const std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus>& GetFrameRenderingApparatus();
        const std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus>& GetLightingEngineApparatus();

		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& GetMainPipelineAcceleratorPool();
        const std::shared_ptr<RenderCore::Techniques::IImmediateDrawables>& GetImmediateDrawables();

        void ResetFrameBufferPool();

        NativeEngineDevice();
        ~NativeEngineDevice();

    protected:
        ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _services;
		ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;
        ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniquesServices;
        std::shared_ptr<RenderCore::IDevice> _renderDevice;
        std::shared_ptr<RenderCore::IThreadContext> _immediateContext;

        std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
        std::shared_ptr<RenderCore::Techniques::ImmediateDrawingApparatus> _immediateDrawingApparatus;
        std::shared_ptr<RenderCore::Techniques::PrimaryResourcesApparatus> _primaryResourcesApparatus;
        std::shared_ptr<RenderCore::Techniques::FrameRenderingApparatus> _frameRenderingApparatus;
        std::shared_ptr<RenderCore::LightingEngine::LightingEngineApparatus> _lightingEngineApparatus;

        uint32_t _mountId0 = ~0u;
        uint32_t _mountId1 = ~0u;
        uint32_t _mountId2 = ~0u;
        uint64_t _defaultEnvMount = ~0ull;
        
        ConsoleRig::AttachablePtr<ToolsRig::IPreviewSceneRegistry> _previewSceneRegistry;
        ConsoleRig::AttachablePtr<EntityInterface::IEntityMountingTree> _entityMountingTree;
        std::unique_ptr<ToolsRig::DivergentAssetManager> _divAssets;

        int _creationThreadId;
		msclr::auto_gcroot<System::Windows::Forms::IMessageFilter^> _messageFilter;
    };

	class RenderTargetWrapper
	{
	public:
		std::shared_ptr<RenderCore::IResource> _renderTarget;
	};
}

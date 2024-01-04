// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TechniqueUtils.h"
#include "../FrameBufferDesc.h"
#include "../StateDesc.h"
#include <vector>
#include <memory>
#include <functional>

namespace Assets { namespace Exceptions { class RetrievalError; }}
namespace Utility { class ParameterBox; }
namespace RenderCore { class IResource; class IThreadContext; class ViewportDesc; class IDescriptorSet; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace RenderCore { namespace BindFlag { using BitField = unsigned; }}

namespace RenderCore { namespace Techniques 
{
    class TechniqueContext;
	class IUniformBufferDelegate;
    class IShaderResourceDelegate;
    class SystemUniformsDelegate;
    class FragmentStitchingContext;
    class AttachmentReservation;
    class RenderPassInstance;
    class IUniformDelegateManager;
    class IPipelineAcceleratorPool;
    using VisibilityMarkerId = uint32_t;

    /// <summary>Manages critical shader state</summary>
    /// Certain system variables are bound to the shaders, and managed by higher
    /// level code. The simplest example is the global transform; but there are
    /// other global resources required by many shaders.
    ///
    /// Technique selection also involves some state information -- called the
    /// run-time technique state and the global technique state.
    ///
    /// This context object manages this kind of global state information.
    /// It also captures error information (such as invalid assets), which can
    /// be reported to the user after parsing.
    class ParsingContext
    {
    public:
            //  ----------------- Active projection context -----------------
        ProjectionDesc&         GetProjectionDesc();
        const ProjectionDesc&   GetProjectionDesc() const;
        ProjectionDesc&         GetPrevProjectionDesc();
        const ProjectionDesc&   GetPrevProjectionDesc() const;
        bool&                   GetEnablePrevProjectionDesc();
        bool                    GetEnablePrevProjectionDesc() const;
        ViewportDesc&           GetViewport()                       { return _viewportDesc; }
        const ViewportDesc&     GetViewport() const                 { return _viewportDesc; }

            //  ----------------- Working technique context -----------------
        TechniqueContext&		GetTechniqueContext()               { return *_techniqueContext; }
		ParameterBox&			GetSubframeShaderSelectors()		{ return _subframeShaderSelectors; }

        /// <summary>Clone a new ParsingContext that can be modified without effecting the original "this"</summary>
        /// Returns a new ParsingContext, with cloned members.
        ///
        /// This is typically used when beginning a render to an offscreen target (or perhaps some compute shader
        /// operation separate from the main rendering operation). In these cases, we need an isolated AttachmentReservation,
        /// which can be helpful to manage attachment lifetimes separately, or to just rebind attachment semantics.
        ///
        /// "this" must outlive the forked ParsingContext. Any changes made to the forked parsing context will *not*
        /// propagate back to "this".
        ///
        /// The members of "TechniqueContext" are global pools, and are not shared (ie, they do not get cloned).
        ParsingContext Fork();

        const std::shared_ptr<IUniformDelegateManager>& GetUniformDelegateManager() { return _uniformDelegateManager; }
        void SetUniformDelegateManager(std::shared_ptr<IUniformDelegateManager> newMan) { _uniformDelegateManager = std::move(newMan); }

        IThreadContext& GetThreadContext() { return *_threadContext; }
        IPipelineAcceleratorPool& GetPipelineAccelerators() { return *_pipelineAccelerators; }

        RenderPassInstance* _rpi = nullptr;

        BufferUploads::CommandListID _requiredBufferUploadsCommandList = 0;
        void RequireCommandList(BufferUploads::CommandListID);

        VisibilityMarkerId GetPipelineAcceleratorsVisibility() const;
        void SetPipelineAcceleratorsVisibility(VisibilityMarkerId newMarkerId);

			//  ----------------- Frame buffer / render pass state -----------------
        FragmentStitchingContext& GetFragmentStitchingContext();
        AttachmentReservation& GetAttachmentReservation();
        FrameBufferProperties& GetFrameBufferProperties();
        void BindAttachment(uint64_t semantic, std::shared_ptr<IResource>, bool isInitialized, BindFlag::BitField currentLayout=~0u, const TextureViewDesc& defaultView = {});      // set initialLayout=~0u for never initialized
        void BindAttachment(uint64_t semantic, std::shared_ptr<IPresentationChain>, BindFlag::BitField currentLayout=~0u, const TextureViewDesc& defaultView = {});
        AttachmentReservation SwapAttachmentReservation(AttachmentReservation&&);

			//  ----------------- Overlays for late rendering -----------------
        typedef std::function<void(ParsingContext&)> PendingOverlay;
        std::vector<PendingOverlay> _pendingOverlays;

            //  ----------------- Exception reporting -----------------
        class StringHelpers
        {
        public:
            char _errorString[1024];
            char _pendingAssets[1024];
            char _invalidAssets[1024];
            char _quickMetrics[4096];
            unsigned _bottomOfScreenErrorMsgTracker = 0;

            StringHelpers();
        };
        std::unique_ptr<StringHelpers> _stringHelpers;
        void Process(const ::Assets::Exceptions::RetrievalError& e);
        bool HasPendingAssets() const { return _stringHelpers->_pendingAssets[0] != '\0'; }
        bool HasInvalidAssets() const { return _stringHelpers->_invalidAssets[0] != '\0'; }
        bool HasErrorString() const { return _stringHelpers->_errorString[0] != '\0'; }

        ParsingContext(
            TechniqueContext& techniqueContext,
            IThreadContext& threadContext);
        ~ParsingContext();

        ParsingContext& operator=(const ParsingContext&) = delete;
        ParsingContext(const ParsingContext&) = delete;
        ParsingContext& operator=(ParsingContext&&);
        ParsingContext(ParsingContext&&);

    protected:
        TechniqueContext*       _techniqueContext;
        IThreadContext*         _threadContext;
        std::shared_ptr<IUniformDelegateManager> _uniformDelegateManager;
        IPipelineAcceleratorPool*   _pipelineAccelerators;

        class Internal;
        std::unique_ptr<Internal>           _internal;
        ViewportDesc                        _viewportDesc;

		ParameterBox                        _subframeShaderSelectors;
        VisibilityMarkerId                  _pipelineAcceleratorsVisibility;

        ParsingContext();
    };

    /// <summary>Utility macros for catching asset exceptions</summary>
    /// Invalid and pending assets are common exceptions during rendering.
    /// This macros assist in creating firewalls for these exceptions
    /// (by passing them along to a ParsingContext to be recorded).
    /// 
    /// <example>
    ///     <code>\code
    ///     CATCH_ASSETS_BEGIN
    ///         DoRenderOperation(parserContext);
    ///     CATCH_ASSETS_END(parserContext)
    ///
    ///     // or:
    ///     TRY { DoRenderOperation(parserContext); } 
    ///     CATCH_ASSETS(parserContext)
    ///     CATCH (...) { HandleOtherException(); }
    ///     CATCH_END
    ///     \endcode</code>
    /// </example>
    /// @{
    #define CATCH_ASSETS(parserContext)                                                             \
        CATCH(const ::Assets::Exceptions::RetrievalError& e) { (parserContext).Process(e); }        \
        /**/

    #define CATCH_ASSETS_BEGIN TRY {
    #define CATCH_ASSETS_END(parserContext) } CATCH_ASSETS(parserContext) CATCH_END
    /// @}


    inline void ParsingContext::RequireCommandList(BufferUploads::CommandListID cmdList)
    {
        assert(cmdList != ~0u);
        _requiredBufferUploadsCommandList = std::max(cmdList, _requiredBufferUploadsCommandList);
        assert(_requiredBufferUploadsCommandList != ~0u);
    }

    inline VisibilityMarkerId ParsingContext::GetPipelineAcceleratorsVisibility() const { return _pipelineAcceleratorsVisibility; }
    inline void ParsingContext::SetPipelineAcceleratorsVisibility(VisibilityMarkerId newMarkerId) { _pipelineAcceleratorsVisibility = newMarkerId; }

}}


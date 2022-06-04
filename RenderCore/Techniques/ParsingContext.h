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
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace Techniques 
{
    class TechniqueContext;
	class IUniformBufferDelegate;
    class IShaderResourceDelegate;
    class SystemUniformsDelegate;
    class FragmentStitchingContext;
    class RenderPassInstance;
    class IUniformDelegateManager;
    using VisibilityMarkerId = uint32_t;
        
    /// <summary>Manages critical shader state</summary>
    /// Certain system variables are bound to the shaders, and managed by higher
    /// level code. The simpliest example is the global transform; but there are
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
        ProjectionDesc&         GetProjectionDesc()                 { return _internal->_projectionDesc; }
        const ProjectionDesc&   GetProjectionDesc() const           { return _internal->_projectionDesc; }
        ProjectionDesc&         GetPrevProjectionDesc()             { return _internal->_prevProjectionDesc; }
        const ProjectionDesc&   GetPrevProjectionDesc() const       { return _internal->_prevProjectionDesc; }
        bool&                   GetEnablePrevProjectionDesc()       { return _internal->_enablePrevProjectionDesc; }
        bool                    GetEnablePrevProjectionDesc() const { return _internal->_enablePrevProjectionDesc; }
        ViewportDesc&           GetViewport()                       { return _viewportDesc; }
        const ViewportDesc&     GetViewport() const                 { return _viewportDesc; }

            //  ----------------- Working technique context -----------------
        TechniqueContext&		GetTechniqueContext()               { return *_techniqueContext; }
		ParameterBox&			GetSubframeShaderSelectors()		{ return _subframeShaderSelectors; }

        const std::shared_ptr<IUniformDelegateManager>& GetUniformDelegateManager() { return _uniformDelegateManager; }
        void SetUniformDelegateManager(std::shared_ptr<IUniformDelegateManager> newMan) { _uniformDelegateManager = newMan; }

        IThreadContext& GetThreadContext() { return *_threadContext; }

        std::pair<uint64_t, const IDescriptorSet*> _extraSequencerDescriptorSet = {0ull, nullptr};
        RenderPassInstance* _rpi = nullptr;

        BufferUploads::CommandListID _requiredBufferUploadsCommandList = 0;
        void RequireCommandList(BufferUploads::CommandListID);

        VisibilityMarkerId GetPipelineAcceleratorsVisibility() const;
        void SetPipelineAcceleratorsVisibility(VisibilityMarkerId newMarkerId);

			//  ----------------- Frame buffer / render pass state -----------------
        FragmentStitchingContext& GetFragmentStitchingContext();

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
        ParsingContext& operator=(ParsingContext&&) = default;
        ParsingContext(ParsingContext&&) = default;

    protected:
        TechniqueContext*       _techniqueContext;
        IThreadContext*         _threadContext;
        std::shared_ptr<IUniformDelegateManager> _uniformDelegateManager;

        struct Internal 
        {
            ProjectionDesc _projectionDesc;
            ProjectionDesc _prevProjectionDesc;
            bool _enablePrevProjectionDesc = false;
        };
        std::unique_ptr<Internal>           _internal;
        ViewportDesc                        _viewportDesc;

		ParameterBox                        _subframeShaderSelectors;
        std::unique_ptr<FragmentStitchingContext> _stitchingContext;
        VisibilityMarkerId                  _pipelineAcceleratorsVisibility;
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
        if (cmdList != ~0u)
            _requiredBufferUploadsCommandList = std::max(cmdList, _requiredBufferUploadsCommandList);
        assert(_requiredBufferUploadsCommandList != ~0u);
    }

    inline VisibilityMarkerId ParsingContext::GetPipelineAcceleratorsVisibility() const { return _pipelineAcceleratorsVisibility; }
    inline void ParsingContext::SetPipelineAcceleratorsVisibility(VisibilityMarkerId newMarkerId) { _pipelineAcceleratorsVisibility = newMarkerId; }

}}


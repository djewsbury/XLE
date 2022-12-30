// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ParsingContext.h"
#include "Techniques.h"
#include "SystemUniformsDelegate.h"
#include "RenderPass.h"
#include "../IDevice.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/ArithmeticUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
    class ParsingContext::Internal
    {
    public:
        ProjectionDesc _projectionDesc;
        ProjectionDesc _prevProjectionDesc;
        bool _enablePrevProjectionDesc = false;

        FragmentStitchingContext _stitchingContext;
        AttachmentReservation _attachmentReservation;
    };

    void ParsingContext::Process(const ::Assets::Exceptions::RetrievalError& e)
    {
            //  Handle a "invalid asset" and "pending asset" exception that 
            //  occurred during rendering. Normally this will just mean
            //  reporting the assert to the screen.
            //
            // These happen fairly often -- particularly when just starting up, or
            //  when changing rendering settings.
            //  at the moment, this will result in a bunch of allocations -- that's not
            //  ideal during error processing.
        auto* id = e.Initializer();
        
        auto* bufferStart = _stringHelpers->_pendingAssets;
        if (e.State() == ::Assets::AssetState::Invalid)
            bufferStart = _stringHelpers->_invalidAssets;

        static_assert(
            dimof(_stringHelpers->_pendingAssets) == dimof(_stringHelpers->_invalidAssets),
            "Assuming pending and invalid asset buffers are the same length");

        if (!XlFindStringI(bufferStart, id)) {
            StringMeldAppend(bufferStart, bufferStart + dimof(_stringHelpers->_pendingAssets)) << "," << id;

			if (e.State() == ::Assets::AssetState::Invalid) {
				// Writing the exception string into "_errorString" here can help to pass shader error message 
				// back to the PreviewRenderManager for the material tool
				StringMeldAppend(_stringHelpers->_errorString, ArrayEnd(_stringHelpers->_errorString)) << e.what() << "\n";
			}
		}
    }

    void ParsingContext::BindAttachment(uint64_t semantic, std::shared_ptr<IResource> resource, bool isInitialized, BindFlags::BitField currentLayout)
    {
        auto semanticName = AttachmentSemantics::TryDehash(semantic);
        _internal->_stitchingContext.DefineAttachment(
            semantic,
            resource->GetDesc(),
            semanticName ? semanticName : "<<unknown>>",
            isInitialized ? RenderCore::Techniques::PreregisteredAttachment::State::Initialized : RenderCore::Techniques::PreregisteredAttachment::State::Uninitialized,
            currentLayout);
        _internal->_attachmentReservation.Bind(semantic, std::move(resource), currentLayout);
    }

    void ParsingContext::BindAttachment(uint64_t semantic, std::shared_ptr<IPresentationChain> presChain, BindFlags::BitField currentLayout)
    {
        assert(presChain);
        auto semanticName = AttachmentSemantics::TryDehash(semantic);
        auto presChainDesc = presChain->GetDesc();
        auto imageDesc = CreateDesc(
            presChainDesc._bindFlags,
            AllocationRules::ResizeableRenderTarget,
            TextureDesc::Plain2D(presChainDesc._width, presChainDesc._height, presChainDesc._format, 1, 0, presChainDesc._samples));
        _internal->_stitchingContext.DefineAttachment(
            semantic,
            imageDesc,
            semanticName ? semanticName : "<<unknown>>",
            RenderCore::Techniques::PreregisteredAttachment::State::Uninitialized,
            currentLayout);
        _internal->_attachmentReservation.Bind(semantic, std::move(presChain), imageDesc, currentLayout);
    }

    ParsingContext::ParsingContext(TechniqueContext& techniqueContext, IThreadContext& threadContext)
	: _techniqueContext(&techniqueContext)
    , _threadContext(&threadContext)
    {
		assert(_techniqueContext);
        _stringHelpers = std::make_unique<StringHelpers>();

        _internal = std::make_unique<Internal>();
        assert(size_t(_internal.get()) % 16 == 0);
        _internal->_attachmentReservation = AttachmentReservation{*techniqueContext._attachmentPool};

		_uniformDelegateManager = _techniqueContext->_uniformDelegateManager;
        _pipelineAcceleratorsVisibility = 0;

        _internal->_stitchingContext = FragmentStitchingContext{
            IteratorRange<const PreregisteredAttachment*>{},
            FrameBufferProperties{},
            _techniqueContext->_systemAttachmentFormats};
    }

    ParsingContext::~ParsingContext() {}

    ParsingContext& ParsingContext::operator=(ParsingContext&&) = default;
    ParsingContext::ParsingContext(ParsingContext&&) = default;

    ParsingContext::StringHelpers::StringHelpers()
    {
        _errorString[0] = _pendingAssets[0] = _invalidAssets[0] = _quickMetrics[0] = '\0';
    }

    ProjectionDesc&         ParsingContext::GetProjectionDesc()                 { return _internal->_projectionDesc; }
    const ProjectionDesc&   ParsingContext::GetProjectionDesc() const           { return _internal->_projectionDesc; }
    ProjectionDesc&         ParsingContext::GetPrevProjectionDesc()             { return _internal->_prevProjectionDesc; }
    const ProjectionDesc&   ParsingContext::GetPrevProjectionDesc() const       { return _internal->_prevProjectionDesc; }
    bool&                   ParsingContext::GetEnablePrevProjectionDesc()       { return _internal->_enablePrevProjectionDesc; }
    bool                    ParsingContext::GetEnablePrevProjectionDesc() const { return _internal->_enablePrevProjectionDesc; }
    FragmentStitchingContext& ParsingContext::GetFragmentStitchingContext()     { return _internal->_stitchingContext; }
    AttachmentReservation& ParsingContext::GetAttachmentReservation()           { return _internal->_attachmentReservation; }
}}


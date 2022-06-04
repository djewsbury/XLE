// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ParsingContext.h"
#include "Techniques.h"
#include "SystemUniformsDelegate.h"
#include "RenderPass.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/ArithmeticUtils.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
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

	FragmentStitchingContext& ParsingContext::GetFragmentStitchingContext()
	{
		if (!_stitchingContext)
			_stitchingContext = std::make_unique<FragmentStitchingContext>(
                IteratorRange<const PreregisteredAttachment*>{},
                FrameBufferProperties{},
                _techniqueContext->_systemAttachmentFormats);
		return *_stitchingContext;
	}

    ParsingContext::ParsingContext(TechniqueContext& techniqueContext, IThreadContext& threadContext)
	: _techniqueContext(&techniqueContext)
    , _threadContext(&threadContext)
    {
		assert(_techniqueContext);
        _stringHelpers = std::make_unique<StringHelpers>();

        _internal = std::make_unique<Internal>();
        assert(size_t(_internal.get()) % 16 == 0);

		_uniformDelegateManager = _techniqueContext->_uniformDelegateManager;
        _pipelineAcceleratorsVisibility = 0;
    }

    ParsingContext::~ParsingContext() {}

    ParsingContext::StringHelpers::StringHelpers()
    {
        _errorString[0] = _pendingAssets[0] = _invalidAssets[0] = _quickMetrics[0] = '\0';
    }
}}


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

	void ParsingContext::AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>& dele)
	{
		for (auto&d:_uniformDelegates)
			if (d.first == binding) {
				d.second = dele;
				return;
			}
		_uniformDelegates.push_back(std::make_pair(binding, dele));
	}

	void ParsingContext::RemoveUniformDelegate(uint64_t binding)
	{
		_uniformDelegates.erase(
			std::remove_if(
				_uniformDelegates.begin(), _uniformDelegates.end(),
				[binding](const std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>& p) { return p.first == binding; }),
			_uniformDelegates.end());
	}

	void ParsingContext::RemoveUniformDelegate(IUniformBufferDelegate& dele)
	{
		_uniformDelegates.erase(
			std::remove_if(
				_uniformDelegates.begin(), _uniformDelegates.end(),
				[&dele](const std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>& p) { return p.second.get() == &dele; }),
			_uniformDelegates.end());
	}

	void ParsingContext::AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>& dele)
	{
		#if defined(_DEBUG)
			auto i = std::find_if(
				_shaderResourceDelegates.begin(), _shaderResourceDelegates.end(),
				[&dele](const std::shared_ptr<IShaderResourceDelegate>& p) { return p.get() == dele.get(); });
			assert(i == _shaderResourceDelegates.end());
		#endif
		_shaderResourceDelegates.push_back(dele);
	}

	void ParsingContext::RemoveShaderResourceDelegate(IShaderResourceDelegate& dele)
	{
		_shaderResourceDelegates.erase(
			std::remove_if(
				_shaderResourceDelegates.begin(), _shaderResourceDelegates.end(),
				[&dele](const std::shared_ptr<IShaderResourceDelegate>& p) { return p.get() == &dele; }),
			_shaderResourceDelegates.end());
	}

	SystemUniformsDelegate& ParsingContext::GetSystemUniformsDelegate() const
	{
		return *_techniqueContext->_systemUniformsDelegate;
	}

	FragmentStitchingContext& ParsingContext::GetFragmentStitchingContext()
	{
		if (!_stitchingContext)
			_stitchingContext = std::make_unique<FragmentStitchingContext>();
		return *_stitchingContext;
	}

    ParsingContext::ParsingContext(TechniqueContext& techniqueContext)
	: _techniqueContext(&techniqueContext)
    {
		assert(_techniqueContext);
        _stringHelpers = std::make_unique<StringHelpers>();

        _internal = std::make_unique<Internal>();
        assert(size_t(_internal.get()) % 16 == 0);

		if (_techniqueContext->_systemUniformsDelegate)
			_shaderResourceDelegates.push_back(_techniqueContext->_systemUniformsDelegate);
    }

    ParsingContext::~ParsingContext() {}

    ParsingContext::StringHelpers::StringHelpers()
    {
        _errorString[0] = _pendingAssets[0] = _invalidAssets[0] = _quickMetrics[0] = '\0';
    }
}}


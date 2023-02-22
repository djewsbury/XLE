// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class IResourceView; }
namespace RenderCore { namespace Techniques { class ParsingContext; class IComputeShaderOperator; }}

namespace RenderOverlays
{

	class BlurryBackgroundEffect
	{
	public:
		std::shared_ptr<RenderCore::IResourceView> GetResourceView();

		BlurryBackgroundEffect(RenderCore::Techniques::ParsingContext& parsingContext);
		~BlurryBackgroundEffect();
	private:
		RenderCore::Techniques::ParsingContext* _parsingContext;
		std::shared_ptr<RenderCore::IResourceView> _backgroundResource;
		::Assets::PtrToMarkerPtr<RenderCore::Techniques::IComputeShaderOperator> _pipelineOperator;
	};
}

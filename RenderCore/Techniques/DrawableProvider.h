// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Assets/ScaffoldCmdStream.h"
#include <memory>

namespace RenderCore { namespace Assets { class RendererConstruction; }}
namespace RenderCore { namespace Techniques
{
	class IPipelineAcceleratorPool;

	class DrawableProvider
	{
	public:
		void Add(Assets::RendererConstruction&);

		DrawableProvider(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators);
		~DrawableProvider();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};
}}

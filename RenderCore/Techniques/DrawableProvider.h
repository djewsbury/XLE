// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Assets/ScaffoldCmdStream.h"
#include "../../BufferUploads/IBufferUploads.h"
#include <memory>

namespace RenderCore { namespace Assets { class RendererConstruction; }}
namespace BufferUploads { class IManager; }
namespace std { template<typename T> class promise; }
namespace RenderCore { namespace Techniques
{
	class IPipelineAcceleratorPool;

	class DrawableProvider : public std::enable_shared_from_this<DrawableProvider>
	{
	public:
		struct FulFilledProvider
		{
			std::shared_ptr<DrawableProvider> _provider;
			BufferUploads::CommandListID _completionCmdList;
		};
		void FulfillWhenNotPending(std::promise<FulFilledProvider>&& promise);
		::Assets::AssetState GetAssetState() const;

		DrawableProvider(
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<BufferUploads::IManager> bufferUploads,
			const Assets::RendererConstruction&);
		~DrawableProvider();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		void Add(const Assets::RendererConstruction&);
	};
}}

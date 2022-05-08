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
	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class DrawableGeo;

	class DrawableProvider : public std::enable_shared_from_this<DrawableProvider>
	{
	public:
		std::vector<std::shared_ptr<DrawableGeo>> _drawableGeos;
		std::vector<std::shared_ptr<PipelineAccelerator>> _pipelineAccelerators;
		std::vector<std::shared_ptr<DescriptorSetAccelerator>> _descriptorSetAccelerators;
		std::vector<Float4x4> _geoSpaceToNodeSpaces;
		struct DrawCall
		{
			unsigned _geoIdx = ~0u;
			unsigned _pipelineAcceleratorIdx = ~0u;
			unsigned _descriptorSetAcceleratorIdx = ~0u;
			unsigned _geoSpaceToNodeSpaceIdx = ~0u;
			unsigned _batchFilter = 0;
			uint64_t _materialGuid = ~0ull;
			unsigned _firstIndex = 0, _indexCount =0;
			unsigned _firstVertex = 0;
		};
		std::vector<DrawCall> _drawCalls;
		unsigned _drawCallCounts[2] = {0};		// per batch

		struct FulFilledProvider
		{
			std::shared_ptr<DrawableProvider> _provider;
			BufferUploads::CommandListID _completionCmdList;
		};
		void FulfillWhenNotPending(std::promise<FulFilledProvider>&& promise);

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

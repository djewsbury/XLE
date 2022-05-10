// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../BufferUploads/IBufferUploads.h"
#include "../../Math/Matrix.h"
#include <memory>

namespace RenderCore { namespace Assets { class RendererConstruction; class ScaffoldCmdIterator; }}
namespace BufferUploads { class IManager; }
namespace std { template<typename T> class promise; }
namespace RenderCore { namespace Techniques
{
	class IPipelineAcceleratorPool;
	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class DrawableGeo;
	class DrawableInputAssembly;

	class DrawableConstructor : public std::enable_shared_from_this<DrawableConstructor>
	{
	public:
		std::vector<std::shared_ptr<DrawableGeo>> _drawableGeos;
		std::vector<std::shared_ptr<PipelineAccelerator>> _pipelineAccelerators;
		std::vector<std::shared_ptr<DescriptorSetAccelerator>> _descriptorSetAccelerators;
		std::vector<std::shared_ptr<DrawableInputAssembly>> _drawableInputAssemblies;
		struct DrawCall
		{
			unsigned _drawableGeoIdx = ~0u;					// index into _drawableGeos
			unsigned _pipelineAcceleratorIdx = ~0u;			// index into _pipelineAccelerators
			unsigned _descriptorSetAcceleratorIdx = ~0u;	// index into _descriptorSetAccelerators
			unsigned _iaIdx = ~0u;							// index into _drawableInputAssemblies
			unsigned _batchFilter = 0;
			unsigned _firstIndex = 0;
			unsigned _indexCount = 0;
			unsigned _firstVertex = 0;
		};
		std::vector<DrawCall> _drawCalls;
		unsigned _drawCallCounts[2] = {0};		// per batch

		enum class Command : uint32_t
		{
			BeginElement = 0x3000,		// must equal s_scaffoldCmdBegin_DrawableConstructor
			ExecuteDrawCalls,
			SetGeoSpaceToNodeSpace
		};
		std::vector<uint8_t> _translatedCmdStream;
		IteratorRange<Assets::ScaffoldCmdIterator> GetCmdStream() const;

		BufferUploads::CommandListID _completionCommandList;

		void FulfillWhenNotPending(std::promise<std::shared_ptr<DrawableConstructor>>&& promise);

		DrawableConstructor(
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
			BufferUploads::IManager& bufferUploads,
			const Assets::RendererConstruction&);
		~DrawableConstructor();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		void Add(const Assets::RendererConstruction&);
	};
}}

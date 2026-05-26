// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Assets/ScaffoldCmdStream.h"		// for MakeScaffoldCmdRange
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/DepVal.h"
#include "../../Math/Matrix.h"
#include <memory>

namespace RenderCore { namespace Assets { class ScaffoldCmdIterator; class ModelRendererConstruction; }}
namespace RenderCore { namespace BufferUploads { class IManager; class IResourcePool; }}
namespace RenderCore { class InputElementDesc; }
namespace std { template<typename T> class promise; }
namespace RenderCore { namespace Techniques
{
	class IDrawablesPool;
	class IPipelineAcceleratorPool;
	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class DrawableGeo;
	class DrawableInputAssembly;
	class IDeformAcceleratorPool;
	class DeformAccelerator;
	class ResourceConstructionContext;

	struct CustomDrawableConstructorRules
	{
		/// Specify additional input elements, which are added to all input assemblies. Typically used
		/// for instancing. All extra elements must be in stream 1. When not empty, the created DrawableGeos will have an
		/// uninitialized space reserved for a stream 1 VB binding.
		std::vector<InputElementDesc> _additionalInputElements;

		/// When enabled, created VBs and IBs will have the UnorderedAccess bind flag enabled
		bool _enableUnorderedAccessBinding = false;

		/// When enabled, won't instantiate the descriptor set accelerators
		bool _disableDescriptorSetAccelerators = false;
	};

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
			uint64_t _materialGuid = ~0ull;
		};
		enum class Command : uint32_t
		{
			BeginElement = 0x3000,		// must equal s_scaffoldCmdBegin_DrawableConstructor
			ExecuteDrawCalls,
			SetGeoSpaceToNodeSpace
		};
		struct CommandStream
		{
			uint64_t _guid;
			std::vector<DrawCall> _drawCalls;
			std::vector<unsigned> _drawCallCounts;		// per batch
			std::vector<uint8_t> _translatedCmdStream;

			IteratorRange<Assets::ScaffoldCmdIterator> GetCmdStream() const;
		};
		std::vector<CommandStream> _cmdStreams;

		const CommandStream& GetCmdStream() const;
		const CommandStream* FindCmdStream(uint64_t guid) const;

		std::vector<Float4x4> _baseTransforms;
		std::vector<std::pair<unsigned, unsigned>> _elementBaseTransformRanges;		// for each element, start and end in the _baseTransforms array

		BufferUploads::CommandListID _completionCommandList;

		void FulfillWhenNotPending(std::promise<std::shared_ptr<DrawableConstructor>>&& promise);
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		DrawableConstructor(
			std::shared_ptr<IDrawablesPool> drawablesPool,
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<ResourceConstructionContext> constructionContext,
			const Assets::ModelRendererConstruction&,
			CustomDrawableConstructorRules&& ={},
			const std::shared_ptr<IDeformAcceleratorPool>& =nullptr,
			const std::shared_ptr<DeformAccelerator>& =nullptr);
		~DrawableConstructor();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
		::Assets::DependencyValidation _depVal;

		void Add(
			const Assets::ModelRendererConstruction&, 
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator);
	};

	inline auto DrawableConstructor::GetCmdStream() const -> const CommandStream&
	{
		assert(!_cmdStreams.empty());
		return _cmdStreams.front();
	}

	inline auto DrawableConstructor::FindCmdStream(uint64_t guid) const -> const CommandStream*
	{
		for (const auto& q:_cmdStreams)
			if (q._guid == guid)
				return &q;
		return nullptr;
	}

	inline IteratorRange<Assets::ScaffoldCmdIterator> DrawableConstructor::CommandStream::GetCmdStream() const
	{
		return Assets::MakeScaffoldCmdRange(MakeIteratorRange(_translatedCmdStream));
	}

	std::future<std::shared_ptr<DrawableConstructor>> ToFuture(DrawableConstructor& construction);

}}

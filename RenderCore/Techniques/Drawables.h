// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice.h"
#include "../StateDesc.h"
#include "../../Math/Matrix.h"
#include "../../Utility/VariantUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include <vector>
#include <memory>
#include <string>

namespace Utility { class ParameterBox; }
namespace RenderCore { class IThreadContext; class MiniInputElementDesc; class InputElementDesc; class UniformsStreamInterface; class UniformsStream; class DescriptorSetSignature; }
namespace RenderCore { namespace Assets { class ShaderPatchCollection; class PredefinedDescriptorSetLayout; } }
namespace Assets { class IAsyncMarker; }
namespace BufferUploads { using CommandListID = uint32_t; class IResourcePool; class IBatchedResources; class ResourceLocator; class Event_ResourceReposition; }
namespace std { template<typename Type> class promise; }

namespace RenderCore { namespace Techniques
{
	class ParsingContext;
	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class DeformAccelerator;
	class RepositionableGeometryConduit;
	namespace Internal { class DrawableGeoHeap; }

	class DrawableGeo
	{
	public:
		enum class StreamType { Resource, PacketStorage, Deform };
		struct VertexStream
		{
			IResourcePtr	_resource;
			unsigned		_vbOffset = 0u;
			StreamType		_type = StreamType::Resource;
		};
		VertexStream        _vertexStreams[4];
		unsigned            _vertexStreamCount = 0;

		IResourcePtr		_ib;
		Format				_ibFormat = Format(0);
		unsigned			_ibOffset = 0u;
		StreamType			_ibStreamType = StreamType::Resource;

		struct Flags
		{
			enum Enum { Temporary       = 1 << 0 };
			using BitField = unsigned;
		};
		Flags::BitField     _flags = 0u;

		std::shared_ptr<DeformAccelerator> _deformAccelerator;
		BufferUploads::CommandListID _completionCmdList = 0;

		// avoid constructing directly -- prefer IDrawablesPool::CreateGeo() or DrawablesPacket::CreateTemporaryGeo()
		DrawableGeo();
		~DrawableGeo();
	protected:
		friend class DrawablesPool;
		friend class Internal::DrawableGeoHeap;
		friend class RepositionableGeometryConduit;
		std::shared_ptr<RepositionableGeometryConduit> _repositionalGeometry;
	};

	class ExecuteDrawableContext
	{
	public:
		void		ApplyLooseUniforms(const UniformsStream&) const;
		void 		ApplyDescriptorSets(IteratorRange<const IDescriptorSet* const*>) const;
		void		SetStencilRef(unsigned frontFaceStencil, unsigned backFaceStencil) const;

		uint64_t 	GetBoundLooseImmediateDatas() const;
		uint64_t 	GetBoundLooseResources() const;
		uint64_t 	GetBoundLooseSamplers() const;
		bool		AtLeastOneBoundLooseUniform() const;

		void        Draw(unsigned vertexCount, unsigned startVertexLocation=0) const;
		void        DrawIndexed(unsigned indexCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0) const;
		void		DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation=0) const;
		void		DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation=0, unsigned baseVertexLocation=0) const;
		void        DrawAuto() const;
		void        DrawIndirect(const IResource& res, unsigned offset=0) const;
		void        DrawIndexedIndirect(const IResource& res, unsigned offset=0) const;
	};

	struct Drawable;
	using ExecuteDrawableFn = void(ParsingContext&, const ExecuteDrawableContext&, const Drawable&);

	struct Drawable
	{
		PipelineAccelerator*				_pipeline;
		DescriptorSetAccelerator*			_descriptorSet;
		const DrawableGeo*					_geo;
		const UniformsStreamInterface*		_looseUniformsInterface;
		ExecuteDrawableFn*					_drawFn;
		unsigned							_deformInstanceIdx;
	};

	class DrawableInputAssembly
	{
	public:
		IteratorRange<const InputElementDesc*> GetInputElements() const { return MakeIteratorRange(_inputElements); }
		IteratorRange<const unsigned*> GetStrides() const { return MakeIteratorRange(_strides); }
		Topology GetTopology() const { return _topology; }
		uint64_t GetHash() const { return _hash; }

		// avoid constructing directly -- prefer IDrawablesPool::CreateInputAssembly()
		DrawableInputAssembly(
			IteratorRange<const InputElementDesc*> inputElements,
			Topology topology);
		~DrawableInputAssembly();
	private:
		std::vector<InputElementDesc> _inputElements;
		std::vector<unsigned> _strides;
		uint64_t _hash;
		Topology _topology;
	};

	class GeometryProcable
	{
	public:
		const DrawableGeo* _geo;
		const DrawableInputAssembly* _inputAssembly;
		Float4x4 _localToWorld;
		unsigned _indexCount = 0;
		unsigned _startIndexLocation = 0;
	};

	class IDrawablesPool;

	class DrawablesPacket
	{
	public:
		VariantArray _drawables;

		enum class Storage { Vertex, Index, Uniform, CPU };
		struct AllocateStorageResult { IteratorRange<void*> _data; unsigned _startOffset; };
		AllocateStorageResult AllocateStorage(Storage storageType, size_t size);
		DrawableGeo* CreateTemporaryGeo();

		void Reset();

		IteratorRange<const void*> GetStorage(Storage storageType) const;

		DrawablesPacket();
		~DrawablesPacket();
		DrawablesPacket(DrawablesPacket&& moveFrom) never_throws;
		DrawablesPacket&operator=(DrawablesPacket&& moveFrom) never_throws;
	private:
		std::vector<uint8_t>	_vbStorage;
		std::vector<uint8_t>	_ibStorage;
		std::vector<uint8_t>	_ubStorage;
		struct CPUStoragePage
		{
			std::unique_ptr<uint8_t[]> _memory;
			size_t _allocated = 0, _used = 0;
		};
		std::vector<CPUStoragePage>	_cpuStoragePages;
		unsigned				_storageAlignment = 0u;
		unsigned				_ubStorageAlignment = 0u;
		IDrawablesPool*	_pool = nullptr;
		unsigned _poolMarker = ~0u;
		std::unique_ptr<Internal::DrawableGeoHeap> _geoHeap;

		friend class IDrawablesPool;
		friend class DrawablesPool;
		DrawablesPacket(IDrawablesPool&, unsigned);
	};

	class IDrawablesPool
	{
	public:
		virtual DrawablesPacket CreatePacket() = 0;
		virtual std::shared_ptr<DrawableGeo> CreateGeo() = 0;
		virtual std::shared_ptr<DrawableInputAssembly> CreateInputAssembly(IteratorRange<const InputElementDesc*>, Topology) = 0;
		virtual std::shared_ptr<UniformsStreamInterface> CreateProtectedLifetime(UniformsStreamInterface&& input) = 0;
		
		virtual void IncreaseAliveCount() = 0;
		using DestructionFunctionSig = void(void*);
		virtual void ProtectedDestroy(void* object, DestructionFunctionSig* destructionFunction) = 0;
		virtual int EstimateAliveClientObjectsCount() const = 0;

		template<typename Type, typename... Args>
			std::shared_ptr<Type> MakeProtectedPtr(Args...);

		~IDrawablesPool();
		unsigned GetGUID() const { return _guid; }
	protected:
		virtual void ReturnToPool(DrawablesPacket&&, unsigned) = 0;
		friend class DrawablesPacket;
		unsigned _guid;
	};

	std::shared_ptr<IDrawablesPool> CreateDrawablesPool();

	class IPipelineAcceleratorPool;
	class SequencerConfig;
	using VisibilityMarkerId = uint32_t;

	struct DrawOptions
	{
		std::optional<VisibilityMarkerId> _pipelineAcceleratorsVisibility;	// when empty, the marker in the ParsingContext is used
	};
		
	void Draw(
		ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const DrawOptions& drawOptions = {});

	struct PreparedResourcesVisibility
	{
		VisibilityMarkerId _pipelineAcceleratorsVisibility = 0;
		BufferUploads::CommandListID _bufferUploadsVisibility = 0;
	};
	void PrepareResources(
		std::promise<PreparedResourcesVisibility>&& promise,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt);

	enum class Batch
	{
		Opaque, Blending, Topological, Max
	};
	
	namespace BatchFlags
	{
		enum Flags
		{
			Opaque = 1u<<unsigned(Batch::Opaque),
			Blending = 1u<<unsigned(Batch::Blending),
			Topological = 1u<<unsigned(Batch::Topological)
		};
		using BitField = unsigned;
	}

	/// <summary>Associate drawable geos with resource source so they can be updated after reposition operations</summary>
	class RepositionableGeometryConduit : public std::enable_shared_from_this<RepositionableGeometryConduit>
	{
	public:
		std::shared_ptr<BufferUploads::IResourcePool> GetIBResourcePool();
		std::shared_ptr<BufferUploads::IResourcePool> GetVBResourcePool();

		void Attach(DrawableGeo&, IteratorRange<BufferUploads::ResourceLocator*> attachedLocators);

		RepositionableGeometryConduit(std::shared_ptr<BufferUploads::IBatchedResources> vb, std::shared_ptr<BufferUploads::IBatchedResources> ib);
		~RepositionableGeometryConduit();
	protected:
		Threading::Mutex _lock;
		std::shared_ptr<BufferUploads::IBatchedResources> _vb, _ib;
		struct AttachedRange { DrawableGeo* _geo; IResource* _batchResource; unsigned _rangeBegin, _rangeSize; };
		std::vector<AttachedRange> _attachedRanges;
		unsigned _frameBarrierMarker = ~0u;
		unsigned _lastProcessedVB = 0, _lastProcessedIB = 0;
		void HandleRepositions(IteratorRange<const BufferUploads::Event_ResourceReposition*>);

		friend class DrawableGeo;
		void Remove(DrawableGeo& geo);
	};

	namespace Internal
	{
		template<typename Type>
			static void DestructionFunction(void* ptr) { delete (Type*)ptr; }
	}

	template<typename Type, typename... Args>
		std::shared_ptr<Type> IDrawablesPool::MakeProtectedPtr(Args... args)
	{
		IncreaseAliveCount();
		return std::shared_ptr<Type>(
			new Type(std::forward<Args>(args)...),
			[this](auto* obj) { this->ProtectedDestroy(obj, &Internal::DestructionFunction<Type>); });
	}
}}

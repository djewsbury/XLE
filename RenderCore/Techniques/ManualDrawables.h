// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Drawables.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <future>

namespace RenderCore { namespace BufferUploads { class IDataPacket; class IAsyncDataSource; class IManager; using CommandListID = uint32_t; }}
namespace RenderCore { namespace Assets { class RawMaterial; class ScaffoldCmdIterator; }}

namespace RenderCore { namespace Techniques 
{
	class DrawableGeo;

	/// <summary>Utility for constructing a DrawableGeo</summary>
	/// This utility can be useful when we want to construct some geometry that will be used on multiple
	/// frames (ie, it's not a subframe temporary) and will be used with the Drawable system.
	///
	/// We can queue upload from data from a variety of sources -- and the underlying device resources
	/// and upload process will occur asynchronously. Multiple geos can be constructed at the same time,
	/// which may be useful if you want to batch a lot of uploads at once.
	///
	/// Construct geos by calling BeginGeo(), and then calling the Set... functions to fill in the geo
	/// Call BeginGeo() again to start another geo -- the result will be the index of this geo.
	///
	/// Setters that take "DrawablesPacket::AllocateStorageResult" are expecting that storage was allocated
	/// through this same object (ie, with ManualDrawableGeoConstructor::AllocateStorage). Don't attempt
	/// to use storage from another ManualDrawableGeoConstructor or a DrawablesPacket.
	///
	/// Call FulfillWhenNotPending() when finished. This promises to return the completed geos sometime
	/// in the future. Remember that the promise is fulfilled when the upload is written to a command list,
	/// not when the command list is queued on the device queue.
	/// The caller is reponsible for respecting the completion command list given by the Promise object.
	///
	/// The ManualDrawableGeoConstructor can't be used after FulfillWhenNotPending() is called.
	class ManualDrawableGeoConstructor
	{
		struct Pimpl;
	public:
		enum DrawableStream { IB, Vertex0, Vertex1, Vertex2, Vertex3 };
		unsigned BeginGeo();
		void SetStreamData(DrawableStream, DrawablesPacket::AllocateStorageResult);
		void SetStreamData(DrawableStream, std::shared_ptr<IResource> resource, size_t offset=0);
		void SetStreamData(DrawableStream, std::vector<uint8_t>&& sourceData);
		void SetStreamData(DrawableStream, std::shared_ptr<BufferUploads::IDataPacket>&& sourceData);
		void SetStreamData(DrawableStream, std::shared_ptr<BufferUploads::IAsyncDataSource>&& sourceData, size_t size);
		void SetIndexFormat(Format);

		DrawablesPacket::AllocateStorageResult AllocateStorage(DrawablesPacket::Storage, size_t byteCount);

		struct Promise
		{
			IteratorRange<const std::shared_ptr<DrawableGeo>*> GetInstantiatedGeos();
			BufferUploads::CommandListID GetCompletionCommandList() const;
			~Promise() = default;
			Promise(Promise&&) = default;
			Promise& operator=(Promise&&) = default;
			Promise() = default;
		private:
			std::shared_ptr<Pimpl> _pimpl;
			Promise(std::shared_ptr<Pimpl> pimpl);
			friend class ManualDrawableGeoConstructor;
		};
		void FulfillWhenNotPending(std::promise<Promise>&& promise);
		Promise ImmediateFulfill();

		ManualDrawableGeoConstructor(std::shared_ptr<IDrawablesPool> pool, std::shared_ptr<BufferUploads::IManager> bufferUploads);
		~ManualDrawableGeoConstructor();
	private:
		std::shared_ptr<Pimpl> _pimpl;
	};


	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class IPipelineAcceleratorPool;

	/// <summary>Construct pipeline & descriptor set accelerators for use when queuing Drawables</summary>
	/// Accelerators are the mechanism for selecting pipelines and descriptor sets for the Drawables system
	///
	/// This utility enables construction of these objects directly from their basic configuration components.
	std::pair<std::shared_ptr<PipelineAccelerator>, std::shared_ptr<DescriptorSetAccelerator>> CreateAccelerators(
		IPipelineAcceleratorPool& pool,
		const RenderCore::Assets::RawMaterial& material,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology = Topology::TriangleList);

	// Create a material machine that can be passed to ConstructDescriptorSetHelper::Construct
	class ManualMaterialMachine
	{
	public:
		IteratorRange<Assets::ScaffoldCmdIterator> GetMaterialMachine() const;
		ManualMaterialMachine(
			const ParameterBox& constantBindings,
			const ParameterBox& resourceBindings,
			IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings = {});
	private:
		std::unique_ptr<uint8_t[], PODAlignedDeletor> _dataBlock;
		size_t _primaryBlockSize;
	};

}}


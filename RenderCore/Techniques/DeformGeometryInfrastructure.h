// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "DeformOperationFactory.h"		// for DeformOperationFactorySet::Deformer

namespace RenderCore { namespace Assets { class RendererConstruction; }}
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace Techniques
{
	struct DeformerToRendererBinding;
	struct DeformerInputBinding;
	class DeformerConstruction;
	class IGeoDeformerInfrastructure;
	class IGeoDeformer;

	class IGeoDeformerInfrastructure : public IDeformAttachment
	{
	public:
		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding() const = 0;
		virtual std::vector<std::shared_ptr<IGeoDeformer>> GetOperations(size_t typeId) = 0;
		virtual std::future<BufferUploads::CommandListID> GetCompletionCommandList() const = 0;
	};

	std::shared_ptr<IGeoDeformerInfrastructure> CreateDeformGeometryInfrastructure(
		IDevice& device,
		const Assets::RendererConstruction&,
		const DeformerConstruction&);

	class IGeoDeformer
	{
	public:
		struct Metrics
		{
			unsigned _dispatchCount = 0;
			unsigned _vertexCount = 0;
			unsigned _descriptorSetWrites = 0;
			unsigned _constantDataSize = 0;
			unsigned _inputStaticDataSize = 0;
		};

		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			const IResourceView& srcVB,
			const IResourceView& deformTemporariesVB,
			const IResourceView& dstVB,
			Metrics& metrics) const;

		using VertexElementRange = IteratorRange<RenderCore::VertexElementIterator>;
		virtual void ExecuteCPU(
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			IteratorRange<const void*> srcVB,
			IteratorRange<const void*> deformTemporariesVB,
			IteratorRange<const void*> dstVB) const;

		virtual void ExecuteCB(
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			IteratorRange<const void*> dstCB) const;

		virtual void Bind(const DeformerInputBinding& binding) = 0;
		virtual bool IsCPUDeformer() const = 0;

		virtual void* QueryInterface(size_t) = 0;
		virtual ~IGeoDeformer();
	};

	struct DeformerInputBinding
	{
		struct GeoBinding
		{
			std::vector<InputElementDesc> _inputElements;		// use _inputSlot to indicate which buffer each element is within
			std::vector<InputElementDesc> _outputElements;		// use _inputSlot to indicate which buffer each element is within
			unsigned _bufferStrides[5];
			unsigned _bufferOffsets[5];
		};
		using ElementAndGeoIdx = std::pair<unsigned, unsigned>;
		std::vector<std::pair<ElementAndGeoIdx, GeoBinding>> _geoBindings;	// geoId, GeoBinding
	};
	
	struct DeformerToRendererBinding
	{
		struct GeoBinding
		{
			std::vector<InputElementDesc> _generatedElements;
			std::vector<uint64_t> _suppressedElements;
			unsigned _postDeformBufferOffset = 0;
		};
		using ElementAndGeoIdx = std::pair<unsigned, unsigned>;
		std::vector<std::pair<ElementAndGeoIdx, GeoBinding>> _geoBindings;	// geoId, GeoBinding
	};
}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "DeformOperationFactory.h"		// for DeformOperationFactorySet::Deformer

namespace RenderCore { namespace Assets { class ModelScaffold; }}

namespace RenderCore { namespace Techniques
{
	struct DeformerToRendererBinding;
	class IGeoDeformerInfrastructure;
	class IGeoDeformer;

	class IGeoDeformerInfrastructure : public IDeformAttachment
	{
	public:
		virtual const DeformerToRendererBinding& GetDeformerToRendererBinding() const = 0;
		virtual std::vector<std::shared_ptr<IGeoDeformer>> GetOperations(size_t typeId) = 0;
	};

	std::shared_ptr<IGeoDeformerInfrastructure> CreateDeformGeometryInfrastructure(
		IDevice& device,
		IteratorRange<const DeformOperationFactorySet::Deformer*> deformers,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName = {});

	std::shared_ptr<IGeoDeformerInfrastructure> CreateDeformGeometryInfrastructure(
		IDevice& device,
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName = {});

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

		virtual void* QueryInterface(size_t) = 0;
		virtual ~IGeoDeformer();
	};

	struct DeformerToRendererBinding
	{
		struct GeoBinding
		{
			unsigned _geoId = ~0u;
			std::vector<InputElementDesc> _generatedElements;
			std::vector<uint64_t> _suppressedElements;
			unsigned _postDeformBufferOffset = 0;
		};
		std::vector<GeoBinding> _geoBindings;
	};
}}


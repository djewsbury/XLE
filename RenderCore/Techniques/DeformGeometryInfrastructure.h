// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"
#include "../Types.h"
#include "../../Assets/AssetsCore.h"
#include <future>

namespace RenderCore { namespace Assets { class ModelRendererConstruction; }}
namespace RenderCore { namespace Techniques
{
	class DeformerConstruction;
	struct DeformerInputBinding;

	std::shared_ptr<IGeoDeformerConductor> CreateGeoDeformerConductor(
		IDevice& device,
		const RenderCore::Assets::ModelRendererConstruction&,
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

		virtual void ExecuteCPU(
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			IteratorRange<const void*> srcVB,
			IteratorRange<const void*> deformTemporariesVB,
			IteratorRange<const void*> dstVB) const;

		virtual void Bind(const DeformerInputBinding& binding) = 0;
		virtual bool IsCPUDeformer() const = 0;
		virtual std::future<void> GetInitializationFuture() const = 0;
		virtual BufferUploads::CommandListID GetCompletionCmdList() const;

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
			friend bool operator==(const GeoBinding& lhs, const GeoBinding& rhs);
		};
		using ElementAndGeoIdx = std::pair<unsigned, unsigned>;
		std::vector<std::pair<ElementAndGeoIdx, GeoBinding>> _geoBindings;	// geoId, GeoBinding

		friend bool operator==(const DeformerToRendererBinding& lhs, const DeformerToRendererBinding& rhs);
	};

	T1(Type) inline bool CompareRange(IteratorRange<const Type*> lhs, IteratorRange<const Type*> rhs)
	{
		if (lhs.size() != rhs.size()) return false;
		for (unsigned c=0; c<lhs.size(); ++c)
			if (!(lhs[c] == rhs[c])) return false;
		return true;
	}

	inline bool operator==(const DeformerToRendererBinding::GeoBinding& lhs, const DeformerToRendererBinding::GeoBinding& rhs)
	{
		return CompareRange<InputElementDesc>(lhs._generatedElements, rhs._generatedElements) && CompareRange<uint64_t>(lhs._suppressedElements, rhs._suppressedElements) && lhs._postDeformBufferOffset == rhs._postDeformBufferOffset;
	}
	
	inline bool operator==(const DeformerToRendererBinding& lhs, const DeformerToRendererBinding& rhs)
	{
		return CompareRange<std::pair<DeformerToRendererBinding::ElementAndGeoIdx, DeformerToRendererBinding::GeoBinding>>(lhs._geoBindings, rhs._geoBindings);
	}
}}


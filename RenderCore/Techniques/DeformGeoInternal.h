// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformOperationFactory.h"
#include "DeformGeometryInfrastructure.h"
#include "CommonBindings.h"
#include "PipelineCollection.h"
#include "CommonUtils.h"
#include "../Assets/ModelMachine.h"
#include "../Metal/InputLayout.h"
#include "../Format.h"
#include "../Types.h"
#include "../UniformsStream.h"
#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../Assets/Marker.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"
#include <memory>
#include <vector>
#include <future>
#include <optional>

namespace RenderCore { class IDevice; class ICompiledPipelineLayout; class UniformsStreamInterface; }
namespace RenderCore { namespace Techniques 
{
	class ComputePipelineAndLayout;
	class CompiledShaderPatchCollection;
	class PipelineCollection;
}}

namespace RenderCore { namespace Techniques 
{
	namespace Internal
	{
		static constexpr unsigned VB_CPUStaticData = 0;
		static constexpr unsigned VB_GPUStaticData = 1;
		static constexpr unsigned VB_CPUDeformTemporaries = 2;
		static constexpr unsigned VB_GPUDeformTemporaries = 3;
		static constexpr unsigned VB_PostDeform = 4;
		static constexpr unsigned VB_Count = 5;

		class DeformerInputBindingHelper
		{
		public:
			DeformerInputBinding _inputBinding;

			using VertexElementRange = IteratorRange<RenderCore::VertexElementIterator>;
			const DeformerInputBinding::GeoBinding* CalculateRanges(
				IteratorRange<VertexElementRange*> sourceElements,
				IteratorRange<VertexElementRange*> destinationElements,
				unsigned geoIdx,
				IteratorRange<const void*> srcVB,
				IteratorRange<const void*> deformTemporariesVB,
				IteratorRange<const void*> dstVB) const;
		};

		struct SourceDataTransform
		{
			const Assets::ModelScaffold* _modelScaffold;
			unsigned	_geoIdx;
			uint64_t	_sourceStream;
			Format		_targetFormat;
			unsigned	_targetOffset;
			unsigned	_targetStride;
			unsigned	_vertexCount;
		};

		struct DeformBufferIterators
		{
			unsigned _bufferIterators[VB_Count] = {0,0,0,0,0};
			std::vector<SourceDataTransform> _cpuStaticDataLoadRequests;
			std::vector<ModelScaffoldLoadRequest> _gpuStaticDataLoadRequests;
		};

		DeformerToRendererBinding::GeoBinding CreateDeformBindings(
			IteratorRange<DeformerInputBinding::GeoBinding*> resultDeformerBindings,
			IteratorRange<const DeformOperationInstantiation*> instantiations,
			DeformBufferIterators& bufferIterators,
			bool isCPUDeformer,
			unsigned geoIdx,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		static const RenderCore::Assets::VertexElement* FindElement(IteratorRange<const RenderCore::Assets::VertexElement*> ele, uint64_t semanticHash)
		{
			return std::find_if(
				ele.begin(), ele.end(),
				[semanticHash](const RenderCore::Assets::VertexElement& ele) {
					return (Hash64(ele._semanticName) + ele._semanticIndex) == semanticHash;
				});
		}

		static const RenderCore::Assets::VertexElement* FindElement(
			IteratorRange<const RenderCore::Assets::VertexElement*> ele,
			StringSection<> semantic, unsigned semanticIndex = 0)
		{
			auto i = std::find_if(
				ele.begin(), ele.end(),
				[semantic, semanticIndex](const RenderCore::Assets::VertexElement& ele) {
					return XlEqString(semantic, ele._semanticName) && ele._semanticIndex == semanticIndex;
				});
			if (i==ele.end())
				return nullptr;
			return i;
		}

		static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
			IteratorRange<void*> vbData,
			const RenderCore::Assets::VertexElement& ele,
			unsigned vertexStride)
		{
			unsigned vCount = vbData.size() / vertexStride;
			auto beginPtr = PtrAdd(vbData.begin(), ele._alignedByteOffset);		
			auto endPtr = PtrAdd(vbData.begin(), ele._alignedByteOffset + vertexStride*vCount);
			VertexElementIterator begin {
				MakeIteratorRange(beginPtr, endPtr),
				vertexStride, ele._nativeFormat };
			VertexElementIterator end {
				MakeIteratorRange(endPtr, endPtr),
				vertexStride, ele._nativeFormat };
			return { begin, end };
		}

		static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
			IteratorRange<void*> vbData,
			Format format,
			unsigned byteOffset,
			unsigned vertexStride)
		{
			unsigned vCount = vbData.size() / vertexStride;
			auto beginPtr = PtrAdd(vbData.begin(), byteOffset);
			auto endPtr = PtrAdd(vbData.begin(), byteOffset + vertexStride*vCount);
			VertexElementIterator begin { MakeIteratorRange(beginPtr, endPtr), vertexStride, format };
			VertexElementIterator end { MakeIteratorRange(endPtr, endPtr), vertexStride, format };
			return { begin, end };
		}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		struct GPUDeformerIAParams
		{
			unsigned _inputStride, _outputStride, _deformTemporariesStride;
			unsigned _inPositionsOffset, _inNormalsOffset, _inTangentsOffset;
			unsigned _outPositionsOffset, _outNormalsOffset, _outTangentsOffset;
			unsigned _mappingBufferByteOffset;
			unsigned _dummy[2];
		};

		class GPUDeformEntryHelper
		{
		public:
			ParameterBox _selectors;
			GPUDeformerIAParams _iaParams;

			GPUDeformEntryHelper(const DeformerInputBinding& bindings, unsigned geoIdx);
		};

		class DeformerPipelineCollection
		{
		public:
			using PipelineMarkerPtr = std::shared_ptr<::Assets::Marker<ComputePipelineAndLayout>>;
			using PipelineMarkerIdx = unsigned;

			PipelineMarkerIdx GetPipeline(ParameterBox&& selectors);
			void StallForPipeline();
			void OnFrameBarrier();
			
			struct PreparedSharedResources
			{
				std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
				Metal::BoundUniforms _boundUniforms;
				std::shared_ptr<Techniques::CompiledShaderPatchCollection> _patchCollection;
				::Assets::DependencyValidation _depVal;
				const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; };
			};
			::Assets::Marker<PreparedSharedResources> _preparedSharedResources;
			std::vector<PipelineMarkerPtr> _pipelines;
			std::shared_ptr<PipelineCollection> _pipelineCollection;

			DeformerPipelineCollection(
				std::shared_ptr<PipelineCollection> pipelineCollection,
				StringSection<> predefinedPipeline,
				UniformsStreamInterface&& usi0,
				UniformsStreamInterface&& usi1,
				ShaderSourceParser::InstantiationRequest&& instRequest,
				IteratorRange<const uint64_t*> patchExpansions);
			~DeformerPipelineCollection();
		private:
			std::vector<uint64_t> _pipelineHashes;
			std::vector<ParameterBox> _pipelineSelectors;
			UniformsStreamInterface _usi0;
			UniformsStreamInterface _usi1;
			ShaderSourceParser::InstantiationRequest _instRequest;
			std::vector<uint64_t> _patchExpansions;
			std::string _predefinedPipelineInitializer;
			bool _pendingCreateSharedResources = true;
			Threading::Mutex _mutex;

			void RebuildSharedResources();
		};
	}
}}


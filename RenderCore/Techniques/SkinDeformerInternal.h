// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SkinDeformer.h"
#include "DeformerInternal.h"
#include "SimpleModelDeform.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/SkeletonScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Metal/InputLayout.h"
#include "../Format.h"
#include "../../Assets/Marker.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { class IDevice; class IResource; class UniformsStreamInterface; class ICompiledPipelineLayout; }
namespace RenderCore { namespace Assets { class PredefinedPipelineLayout; }}
namespace Utility { class ParameterBox; }

namespace RenderCore { namespace Techniques
{
	class CPUSkinDeformer : public IDeformer, public ISkinDeformer
	{
	public:
		virtual void ExecuteCPU(
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			IteratorRange<const void*> srcVB,
			IteratorRange<const void*> deformTemporariesVB,
			IteratorRange<const void*> dstVB) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const override;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding) override;
		
		CPUSkinDeformer(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			const std::string& modelScaffoldName);
		~CPUSkinDeformer();

		Internal::DeformerInputBindingHelper _bindingHelper;

		static std::vector<RenderCore::Techniques::DeformOperationInstantiation> InstantiationFunction(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);
	private:
		struct Section
		{
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
			Float4x4 _bindShapeMatrix;
			Float4x4 _postSkinningBindMatrix;
		};
			
		struct Geo
		{
			unsigned _geoId = ~0u;
			
			std::vector<Section> 	_sections;
			std::vector<float>		_jointWeights;
			std::vector<unsigned>	_jointIndices;
			size_t					_influencesPerVertex;
		};
		std::vector<Geo> _geos;

		struct Instance
		{
			IteratorRange<const Float4x4*> _skeletonMachineOutput;
			const RenderCore::Assets::SkeletonBinding* _binding;
		};
		std::vector<Instance> _instanceData;
		std::vector<Float4x4> _skeletonMachineOutput;
		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;
		RenderCore::Assets::SkeletonBinding _skeletonBinding;

		void WriteJointTransforms(
			const Section& section,
			IteratorRange<Float3x4*>		destination,
			IteratorRange<const Float4x4*>	skeletonMachineResult) const;
	};

    class PipelineCollection;
    struct ComputePipelineAndLayout;

	struct SkinDeformerPipelineCollection
	{
		using PipelineMarkerPtr = std::shared_ptr<::Assets::Marker<ComputePipelineAndLayout>>;
		using PipelineMarkerIdx = unsigned;

		PipelineMarkerIdx GetPipeline(ParameterBox&& selectors);
		void StallForPipeline();
		void OnFrameBarrier();
		
		struct PreparedPipelineLayout
		{
			std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
			Metal::BoundUniforms _boundUniforms;
		};
		::Assets::Marker<PreparedPipelineLayout> _preparedPipelineLayout;
		std::shared_ptr<IDevice> _device;
		std::vector<PipelineMarkerPtr> _pipelines;

		SkinDeformerPipelineCollection(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<PipelineCollection> pipelineCollection);
		~SkinDeformerPipelineCollection();
	private:
		std::vector<uint64_t> _pipelineHashes;
		std::vector<ParameterBox> _pipelineSelectors;
		std::shared_ptr<PipelineCollection> _pipelineCollection;
		::Assets::PtrToMarkerPtr<RenderCore::Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;
		uint64_t _predefinedPipelineLayoutNameHash;
		Threading::Mutex _mutex;

		void RebuildPipelineLayout();
	};

	class GPUSkinDeformer : public IDeformer, public ISkinDeformer
	{
	public:
		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			IteratorRange<const unsigned*> instanceIndices,
			unsigned outputInstanceStride,
			const IResourceView& srcVB,
			const IResourceView& deformTemporariesVB,
			const IResourceView& dstVB,
			Metrics& metrics) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const override;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding) override;

		void Bind(const DeformerInputBinding& binding);
	
		GPUSkinDeformer(
			std::shared_ptr<SkinDeformerPipelineCollection> pipelineCollection,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName);
		~GPUSkinDeformer();
	private:
		std::shared_ptr<IResource>	_staticVertexAttachments;
		std::shared_ptr<IResourceView> _staticVertexAttachmentsView;
		unsigned _staticVertexAttachmentsSize = 0;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		struct IAParams
		{
			unsigned _inputStride, _outputStride;
			unsigned _inPositionsOffset, _inNormalsOffset, _inTangentsOffset;
			unsigned _outPositionsOffset, _outNormalsOffset, _outTangentsOffset;
			unsigned _weightsOffset, _jointIndicesOffset, _staticVertexAttachmentsStride;
			unsigned _jointMatricesInstanceStride;
		};
		std::vector<IAParams> _iaParams;

		struct Section
		{
			SkinDeformerPipelineCollection::PipelineMarkerIdx _pipelineMarker;
			unsigned _geoId = ~0u;
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			std::pair<unsigned, unsigned> _rangeInJointMatrices;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
			Float4x4 _bindShapeMatrix, _postSkinningBindMatrix;

			unsigned _influencesPerVertex = 0;
			unsigned _iaParamsIdx = 0;
			Format _indicesFormat = Format(0), _weightsFormat = Format(0);
		};
		std::vector<Section> _sections;

		std::vector<Float3x4> _jointMatrices;
		unsigned _jointMatricesInstanceStride = 0;

		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
		std::shared_ptr<SkinDeformerPipelineCollection> _pipelineCollection;
	};
}}

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
#include "../Format.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { class IDevice; class IResource; class UniformsStreamInterface; }
namespace Utility { class ParameterBox; }

namespace RenderCore { namespace Techniques
{
	class CPUSkinDeformer : public IDeformer, public ISkinDeformer
	{
	public:
		virtual void ExecuteCPU(
			unsigned instanceIdx,
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
    class IComputeShaderOperator;

	struct SkinDeformerPipelineCollection
	{
		using PipelineMarkerPtr = ::Assets::PtrToMarkerPtr<IComputeShaderOperator>;
		uint64_t GetPipeline(const ParameterBox& selectors, const UniformsStreamInterface& usi);
		PipelineMarkerPtr GetPipelineMarker(const ParameterBox& selectors, const UniformsStreamInterface& usi);
		
		std::vector<std::pair<uint64_t, PipelineMarkerPtr>> _pipelines;
		std::shared_ptr<PipelineCollection> _pipelineCollection;

		SkinDeformerPipelineCollection();
		~SkinDeformerPipelineCollection();
	};

	class GPUSkinDeformer : public IDeformer, public ISkinDeformer
	{
	public:
		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			unsigned instanceIdx,
			const IResourceView& srcVB,
			const IResourceView& deformTemporariesVB,
			const IResourceView& dstVB) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const override;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding) override;

		void Bind(
			SkinDeformerPipelineCollection& pipelineCollection,
			const DeformerInputBinding& binding);

		void StallForPipeline();
		
		GPUSkinDeformer(
			IDevice& device,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName);
		~GPUSkinDeformer();
	private:
		std::shared_ptr<IResource>	_staticVertexAttachments;
		std::shared_ptr<IResourceView> _staticVertexAttachmentsView;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		struct IAParams
		{
			unsigned _inputStride, _outputStride;
			unsigned _inPositionsOffset, _inNormalsOffset, _inTangentsOffset;
			unsigned _outPositionsOffset, _outNormalsOffset, _outTangentsOffset;
			unsigned _weightsOffset, _jointIndicesOffset, _staticVertexAttachmentsStride;
		};

		struct Section
		{
			SkinDeformerPipelineCollection::PipelineMarkerPtr _pipelineMarker;
			unsigned _geoId = ~0u;
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			std::pair<unsigned, unsigned> _rangeInJointMatrices;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;

			unsigned _influencesPerVertex = 0;
			Format _indicesFormat = Format(0), _weightsFormat = Format(0);
			IAParams _iaParams;
		};
		std::vector<Section> _sections;

		struct Instance
		{
			IteratorRange<const Float4x4*> _skeletonMachineOutput;
			const RenderCore::Assets::SkeletonBinding* _binding;
		};
		std::vector<Instance> _instanceData;
		std::vector<Float3x4> _jointMatrices;

		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
	};
}}

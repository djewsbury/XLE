// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SkinDeformer.h"
#include "SimpleModelDeform.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/SkeletonScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Format.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore { class IDevice; class IResource; }

namespace RenderCore { namespace Techniques
{
	class SkinDeformer : public ICPUDeformOperator, public ISkinDeformer
	{
	public:
		virtual void Execute(
			unsigned instanceId,
			IteratorRange<const VertexElementRange*> sourceElements,
			IteratorRange<const VertexElementRange*> destinationElements) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const override;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding) override;
		
		SkinDeformer(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			unsigned geoId);
		~SkinDeformer();

		static std::vector<RenderCore::Techniques::DeformOperationInstantiation> InstantiationFunction(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);
	private:
		std::vector<float>		_jointWeights;
		std::vector<unsigned>	_jointIndices;
		size_t					_influencesPerVertex;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		struct Section
		{
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
		};
		std::vector<Section> _sections;

		struct Instance
		{
			IteratorRange<const Float4x4*> _skeletonMachineOutput;
			const RenderCore::Assets::SkeletonBinding* _binding;
		};
		std::vector<Instance> _instanceData;
		std::vector<Float4x4> _skeletonMachineOutput;
		RenderCore::Assets::SkeletonBinding _skeletonBinding;

		void WriteJointTransforms(
			const Section& section,
			IteratorRange<Float3x4*>		destination,
			IteratorRange<const Float4x4*>	skeletonMachineResult) const;
	};

    class PipelineCollection;
    class IComputeShaderOperator;

	class GPUSkinDeformer : public IGPUDeformOperator, public ISkinDeformer
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
		
		~GPUSkinDeformer();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<IGPUDeformOperator>>&& promise,
			std::shared_ptr<IDevice> device,
			std::shared_ptr<RenderCore::Techniques::PipelineCollection> pipelinePool,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			unsigned geoId,
			InputLayout srcVBLayout, 
			InputLayout deformTemporariesVBLayout,
			InputLayout dstVBLayout);

        struct IAParams
		{
			unsigned _inputStride, _outputStride;
			unsigned _positionsOffset, _normalsOffset, _tangentsOffset;
			unsigned _weightsOffset, _jointIndicesOffset, _staticVertexAttachmentsStride;
		};
        GPUSkinDeformer(
			IDevice& device,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			std::shared_ptr<IComputeShaderOperator> op,
			unsigned geoId,
			const IAParams& iaParams,
			unsigned influencesPerVertex);

	private:
		std::shared_ptr<IResource>	_staticVertexAttachments;
		std::shared_ptr<IResourceView> _staticVertexAttachmentsView;
		size_t						_influencesPerVertex;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		struct Section
		{
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			std::pair<unsigned, unsigned> _rangeInJointMatrices;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
		};
		std::vector<Section> _sections;

		IAParams _iaParams;

		struct Instance
		{
			IteratorRange<const Float4x4*> _skeletonMachineOutput;
			const RenderCore::Assets::SkeletonBinding* _binding;
		};
		std::vector<Instance> _instanceData;
		std::vector<Float3x4> _jointMatrices;

		std::shared_ptr<IComputeShaderOperator> _operator;
		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
	};
}}

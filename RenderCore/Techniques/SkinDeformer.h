// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelDeform.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/SkeletonScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Format.h"
#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>

namespace RenderCore { class VertexElementIterator; class IDevice; class IResource; }

namespace RenderCore { namespace Techniques
{
	class SkinDeformer : public IDeformOperation
	{
	public:
		virtual void Execute(
			unsigned instanceId,
			IteratorRange<const VertexElementRange*> sourceElements,
			IteratorRange<const VertexElementRange*> destinationElements) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding);
		
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

	class GPUSkinDeformer : public IDeformOperation
	{
	public:
		virtual void ExecuteGPU(
			IThreadContext& threadContext,
			unsigned instanceIdx,
			const IResourceView& sourceElements,
			const IResourceView& destinationElements) const override;
		virtual void* QueryInterface(size_t) override;

		RenderCore::Assets::SkeletonBinding CreateBinding(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& skeletonMachineOutputInterface) const;

		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput,
			const RenderCore::Assets::SkeletonBinding& binding);
		
		GPUSkinDeformer(
			IDevice& device,
			std::shared_ptr<RenderCore::Techniques::PipelineCollection>& pipelinePool,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			unsigned geoId);
		~GPUSkinDeformer();

		static std::vector<RenderCore::Techniques::DeformOperationInstantiation> InstantiationFunction(
			StringSection<> initializer,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold);
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

		struct IAParams
		{
			unsigned _inputStride, _outputStride;
			unsigned _positionsOffset, _normalsOffset, _tangentsOffset;
			unsigned _weightsOffset, _jointIndicesOffset, _staticVertexAttachmentsStride;
		};
		IAParams _iaParams;

		struct Instance
		{
			IteratorRange<const Float4x4*> _skeletonMachineOutput;
			const RenderCore::Assets::SkeletonBinding* _binding;
		};
		std::vector<Instance> _instanceData;
		std::vector<Float3x4> _jointMatrices;

		::Assets::PtrToMarkerPtr<IComputeShaderOperator> _operator;
		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
	};
}}


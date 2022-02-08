// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "SkinDeformer.h"
#include "DeformInternal.h"
#include "DeformOperationFactory.h"
#include "../Assets/ModelScaffoldInternal.h"
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
	class CPUSkinDeformer : public IGeoDeformer, public ISkinDeformer
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
	class CompiledShaderPatchCollection;
	namespace Internal { struct GPUDeformerIAParams; class DeformerPipelineCollection; }

	class GPUSkinDeformer : public IGeoDeformer, public ISkinDeformer
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
			std::shared_ptr<Internal::DeformerPipelineCollection> pipelineCollection,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName);
		~GPUSkinDeformer();
	private:
		std::shared_ptr<IResource>	_staticVertexAttachments;
		std::shared_ptr<IResourceView> _staticVertexAttachmentsView;
		std::shared_ptr<IResourceView> _iaParamsView;
		std::shared_ptr<IResourceView> _skinIAParamsView;
		unsigned _staticVertexAttachmentsSize = 0;

		RenderCore::Assets::ModelCommandStream::InputInterface _jointInputInterface;

		std::vector<Internal::GPUDeformerIAParams> _iaParams;

		struct SkinIAParams
		{
			unsigned _weightsOffset, _jointIndicesOffset, _staticVertexAttachmentsStride;
			unsigned _dummy;
		};
		std::vector<SkinIAParams> _skinIAParams;

		struct Section
		{
			unsigned _geoId = ~0u;
			IteratorRange<const RenderCore::Assets::DrawCallDesc*> _preskinningDrawCalls;
			std::pair<unsigned, unsigned> _rangeInJointMatrices;
			IteratorRange<const Float4x4*> _bindShapeByInverseBindMatrices;
			IteratorRange<const uint16_t*> _jointMatrices;
			Float4x4 _bindShapeMatrix, _postSkinningBindMatrix;

			unsigned _sectionInfluencesPerVertex = 0;
			Format _indicesFormat = Format(0), _weightsFormat = Format(0);
			unsigned _skinIAParamsIdx = ~0u;
		};
		std::vector<Section> _sections;

		struct Dispatch
		{
			unsigned _iaParamsIdx = ~0u;
			unsigned _skinIAParamsIdx = ~0u;
			unsigned _vertexCount = 0;
			unsigned _firstVertex = 0;
			unsigned _softInfluenceCount = 0;
			unsigned _firstJointTransform = 0;
			unsigned _pipelineMarker;
		};
		std::vector<Dispatch> _dispatches;

		std::vector<Float3x4> _jointMatrices;
		unsigned _jointMatricesInstanceStride = 0;

		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
		std::shared_ptr<Internal::DeformerPipelineCollection> _pipelineCollection;
	};
}}

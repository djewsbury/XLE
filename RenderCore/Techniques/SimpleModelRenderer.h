// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DrawableDelegates.h"					// for IUniformBufferDelegate
#include "../Assets/AnimationBindings.h"		// for SkeletonBinding
#include "../../Math/Matrix.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/StringUtils.h"
#include <vector>
#include <memory>

namespace RenderCore { namespace Assets { class ModelScaffold; class MaterialScaffold; class SkeletonScaffold; }} // todo -- remove

namespace RenderCore { namespace Assets { class DrawCallDesc; class ModelRendererConstruction; }}
namespace RenderCore { class UniformsStreamInterface; }
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}
namespace std { template<typename T> class future; }

namespace RenderCore { namespace Techniques 
{
	struct Drawable;
	class DrawablesPacket; 
	class ParsingContext; 
	class IPipelineAcceleratorPool; 
	class IDrawablesPool;
	class DescriptorSetAccelerator;
	class IDeformAcceleratorPool;
	class DeformAccelerator;
	class IDeformGeoAttachment;
	class IGeoDeformer;
	class ISkinDeformer;
	class IUniformBufferDelegate;
	class DrawableConstructor;
	class ExecuteDrawableContext;
	class ResourceConstructionContext;
	class DeformerConstruction;

	class ICustomDrawDelegate
	{
	public:
		virtual void OnDraw(ParsingContext&, const ExecuteDrawableContext&, const Drawable&) = 0;

		static uint64_t GetMaterialGuid(const Drawable&);
		static unsigned GetDrawCallIndex(const Drawable&);
		static Float3x4 GetLocalToWorld(const Drawable&);
		static uint32_t GetViewMask(const Drawable&);
		static RenderCore::Assets::DrawCallDesc GetDrawCallDesc(const Drawable&);
		static void ExecuteStandardDraw(ParsingContext&, const ExecuteDrawableContext&, const Drawable&);
		virtual ~ICustomDrawDelegate();
	};

	struct ModelConstructionSkeletonBinding
	{
	public:
		unsigned ModelJointToMachineOutput(unsigned elementIdx, unsigned modelJointIdx) const;
		const Float4x4& ModelJointToUnanimatedTransform(unsigned elementIdx, unsigned modelJointIdx) const;

		ModelConstructionSkeletonBinding(const Assets::ModelRendererConstruction& construction);
		ModelConstructionSkeletonBinding();

		std::vector<unsigned>	_modelJointIndexToMachineOutput;
		std::vector<Float4x4>	_unanimatedTransforms;
		std::vector<unsigned>	_elementStarts;
		std::vector<Float3x4>	_elementToObject;
	};
	
	class SimpleModelRenderer
	{
	public:
		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>(),
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask = 1,
			uint64_t cmdStream = 0) const;		/* s_CmdStreamGuid_Default */

		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld,
			IteratorRange<const Float4x4*> animatedSkeletonOutput,
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask = 1,
			uint64_t cmdStream = 0) const;		/* s_CmdStreamGuid_Default */

		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld,
			IteratorRange<const Float4x4*> animatedSkeletonOutput,
			unsigned deformInstanceIdx,
			const std::shared_ptr<ICustomDrawDelegate>& delegate,
			uint32_t viewMask = 1,
			uint64_t cmdStream = 0) const;		/* s_CmdStreamGuid_Default */

		void BuildGeometryProcables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>(),
			uint64_t cmdStream = 0) const;		/* s_CmdStreamGuid_Default */

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const std::shared_ptr<DeformAccelerator>& GetDeformAccelerator() const { return _deformAccelerator; }
		const std::shared_ptr<DrawableConstructor>& GetDrawableConstructor() const { return _drawableConstructor; }
		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCmdList; }

		using UniformBufferBinding = std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>;

		SimpleModelRenderer(
			IDrawablesPool& drawablesPool,
			const Assets::ModelRendererConstruction& construction,
			std::shared_ptr<DrawableConstructor> drawableConstructor,
			std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool = nullptr,
			std::shared_ptr<DeformAccelerator> deformAccelerator = nullptr,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});
		~SimpleModelRenderer();

		SimpleModelRenderer& operator=(const SimpleModelRenderer&) = delete;
		SimpleModelRenderer(const SimpleModelRenderer&) = delete;
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			std::shared_ptr<IDrawablesPool> drawablesPool,
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool,
			std::shared_ptr<ResourceConstructionContext> constructionContext,
			std::shared_ptr<Assets::ModelRendererConstruction> construction,
			std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool = nullptr,
			std::shared_ptr<DeformerConstruction> deformerConstruction = nullptr,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			std::shared_ptr<IDrawablesPool> drawablesPool,
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool,
			StringSection<> modelScaffoldName,
			StringSection<> materialScaffoldName,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			std::shared_ptr<IDrawablesPool> drawablesPool,
			std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool,
			std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool,
			StringSection<> modelScaffoldName);

	private:
		std::shared_ptr<DrawableConstructor> _drawableConstructor;
		ModelConstructionSkeletonBinding _skeletonBinding;
		std::shared_ptr<UniformsStreamInterface> _usi;

		std::shared_ptr<DeformAccelerator> _deformAccelerator;

		::Assets::DependencyValidation _depVal;
		BufferUploads::CommandListID _completionCmdList;

		class GeoCallBuilder;
		class DrawableGeoBuilder;
	};

	std::future<std::shared_ptr<DeformAccelerator>> CreateDefaultDeformAccelerator(
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const Assets::ModelRendererConstruction& rendererConstruction);

	class RendererSkeletonInterface
	{
	public:
		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput);

		RendererSkeletonInterface(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
			IteratorRange<const std::shared_ptr<IGeoDeformer>*> skinDeformers);
		RendererSkeletonInterface(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
			IDeformGeoAttachment& geoDeformerInfrastructure,
			::Assets::DependencyValidation depVal = {});
		~RendererSkeletonInterface();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<RendererSkeletonInterface>>&& skeletonInterfaceFuture,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator,
			const std::shared_ptr<Assets::ModelRendererConstruction>& construction);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

	private:
		struct Deformer
		{
			ISkinDeformer* _skinDeformer;
			RenderCore::Assets::SkeletonBinding _deformerBindings;
			std::shared_ptr<IGeoDeformer> _skinDeformerRef;
		};
		std::vector<Deformer> _deformers;
		::Assets::DependencyValidation _depVal;
	};

	class SkinningUniformBufferDelegate : public IUniformBufferDelegate
	{
	public:
		void FeedInSkeletonMachineResults(
			IteratorRange<const Float4x4*> skeletonMachineOutput);

		void WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst) override;
        size_t GetSize() override;

		SkinningUniformBufferDelegate(
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& scaffoldActual, 
			const std::shared_ptr<RenderCore::Assets::SkeletonScaffold>& skeletonActual);
		~SkinningUniformBufferDelegate();
	private:
		struct Section
		{
			std::vector<unsigned> _sectionMatrixToMachineOutput;
			std::vector<Float4x4> _bindShapeByInverseBind;
			std::vector<Float3x4> _cbData;
			unsigned _geoIdx = ~0u;
		};
		std::vector<Section> _sections;
	};

	inline unsigned ModelConstructionSkeletonBinding::ModelJointToMachineOutput(unsigned elementIdx, unsigned modelJointIdx) const
	{
		return _modelJointIndexToMachineOutput[_elementStarts[elementIdx] + modelJointIdx];
	}

	inline const Float4x4& ModelConstructionSkeletonBinding::ModelJointToUnanimatedTransform(unsigned elementIdx, unsigned modelJointIdx) const
	{
		return _unanimatedTransforms[_elementStarts[elementIdx] + modelJointIdx];
	}
}}
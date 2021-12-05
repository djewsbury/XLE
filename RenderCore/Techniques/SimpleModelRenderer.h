// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DrawableDelegates.h"					// for IUniformBufferDelegate
#include "Drawables.h"							// for ExecuteDrawableContext
#include "../Assets/ModelImmutableData.h"		// for SkeletonBinding
#include "../../Math/Matrix.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/StringUtils.h"
#include <vector>
#include <memory>

namespace RenderCore { namespace Assets 
{ 
	class ModelScaffold;
	class MaterialScaffold;
	class SkeletonScaffold;
}}
namespace RenderCore { class IThreadContext; class IResource; class UniformsStreamInterface; }
namespace Utility { class VariantArray; }

namespace RenderCore { namespace Techniques 
{
	class Drawable; class DrawableGeo;
	class DrawablesPacket; 
	class ParsingContext; 
	class IPipelineAcceleratorPool; 
	class PipelineAccelerator; 
	class DescriptorSetAccelerator;
	class IDeformer;
	class ISkinDeformer;
	class IUniformBufferDelegate;

	class ICustomDrawDelegate
	{
	public:
		virtual void OnDraw(ParsingContext&, const ExecuteDrawableContext&, const Drawable&) = 0;

		static uint64_t GetMaterialGuid(const Drawable&);
		static unsigned GetDrawCallIndex(const Drawable&);
		static Float3x4 GetLocalToWorld(const Drawable&);
		static RenderCore::Assets::DrawCallDesc GetDrawCallDesc(const Drawable&);
		static void ExecuteStandardDraw(ParsingContext&, const ExecuteDrawableContext&, const Drawable&);
		virtual ~ICustomDrawDelegate();
	};
	
	class SimpleModelRenderer
	{
	public:
		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>(),
			unsigned deformInstanceIdx = 0,
			uint32_t viewMask = 1) const;

		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld,
			unsigned deformInstanceIdx,
			const std::shared_ptr<ICustomDrawDelegate>& delegate) const;

		void BuildGeometryProcables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>()) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const;

		const std::shared_ptr<DeformAccelerator>& GetDeformAccelerator() const { return _deformAccelerator; }
		const std::shared_ptr<IDeformAcceleratorPool>& GetDeformAcceleratorPool() const { return _deformAcceleratorPool; }

		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& GetModelScaffold() const { return _modelScaffold; }
		const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& GetMaterialScaffold() const { return _materialScaffold; }
		const std::string& GetModelScaffoldName() const { return _modelScaffoldName; }
		const std::string& GetMaterialScaffoldName() const { return _materialScaffoldName; }

		using UniformBufferBinding = std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>;

		SimpleModelRenderer(
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& materialScaffold,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool = nullptr,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator = nullptr,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {},
			const std::string& modelScaffoldName = {},
			const std::string& materialScaffoldName = {});
		~SimpleModelRenderer();

		SimpleModelRenderer& operator=(const SimpleModelRenderer&) = delete;
		SimpleModelRenderer(const SimpleModelRenderer&) = delete;
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
			StringSection<> deformOperations = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {},
			const std::string& modelScaffoldNameString = {},
			const std::string& materialScaffoldNameString = {});
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			StringSection<> modelScaffoldName,
			StringSection<> materialScaffoldName,
			StringSection<> deformOperations = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			StringSection<> modelScaffoldName);

	private:
		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
		std::shared_ptr<RenderCore::Assets::MaterialScaffold> _materialScaffold;

		std::unique_ptr<Float4x4[]> _baseTransforms;
		unsigned _baseTransformCount;

		std::vector<std::shared_ptr<DrawableGeo>> _geos;
		std::vector<std::shared_ptr<DrawableGeo>> _boundSkinnedControllers;

		struct GeoCall
		{
			std::shared_ptr<PipelineAccelerator> _pipelineAccelerator;
			std::shared_ptr<DescriptorSetAccelerator> _descriptorSetAccelerator;
			unsigned _batchFilter;
			unsigned _iaIdx;
		};

		std::vector<GeoCall> _geoCalls;
		std::vector<GeoCall> _boundSkinnedControllerGeoCalls;
		unsigned _drawablesCount[4];

		RenderCore::Assets::SkeletonBinding _skeletonBinding;

		std::shared_ptr<UniformsStreamInterface> _usi;

		std::vector<std::shared_ptr<DrawableInputAssembly>> _drawableIAs;
		std::shared_ptr<IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<DeformAccelerator> _deformAccelerator;

		std::string _modelScaffoldName;
		std::string _materialScaffoldName;

		::Assets::DependencyValidation _depVal;

		class GeoCallBuilder;
		class DrawableGeoBuilder;
	};

	class RendererSkeletonInterface
	{
	public:
		void FeedInSkeletonMachineResults(
			unsigned instanceIdx,
			IteratorRange<const Float4x4*> skeletonMachineOutput);

		RendererSkeletonInterface(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
			IteratorRange<const std::shared_ptr<IDeformer>*> skinDeformers);
		~RendererSkeletonInterface();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<RendererSkeletonInterface>>&& skeletonInterfaceFuture,
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& rendererFuture,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,
			StringSection<> deformOperations = {},
			IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates = {});

	private:
		struct Deformer
		{
			ISkinDeformer* _skinDeformer;
			RenderCore::Assets::SkeletonBinding _deformerBindings;
			std::shared_ptr<IDeformer> _skinDeformerRef;
		};
		std::vector<Deformer> _deformers;
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
		};
		std::vector<Section> _sections;
	};
}}
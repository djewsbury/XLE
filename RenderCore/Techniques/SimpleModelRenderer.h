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

namespace RenderCore { namespace Assets { class RendererConstruction; }}
namespace RenderCore { class UniformsStreamInterface; }
namespace BufferUploads { using CommandListID = uint32_t; }

namespace RenderCore { namespace Techniques 
{
	class Drawable;
	class DrawablesPacket; 
	class ParsingContext; 
	class IPipelineAcceleratorPool; 
	class DescriptorSetAccelerator;
	class IDeformAcceleratorPool;
	class DeformAccelerator;
	class IGeoDeformerInfrastructure;
	class IGeoDeformer;
	class ISkinDeformer;
	class IUniformBufferDelegate;
	class DrawableConstructor;
	class ExecuteDrawableContext;

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

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		const std::shared_ptr<IGeoDeformerInfrastructure>& GetGeoDeformerInfrastructure() const { return _geoDeformerInfrastructure; }

		/*const std::shared_ptr<RenderCore::Assets::ModelScaffold>& GetModelScaffold() const { return _modelScaffold; }
		const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& GetMaterialScaffold() const { return _materialScaffold; }
		const std::string& GetModelScaffoldName() const { return _modelScaffoldName; }
		const std::string& GetMaterialScaffoldName() const { return _materialScaffoldName; }*/
		BufferUploads::CommandListID GetCompletionCommandList() const;

		using UniformBufferBinding = std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>;

		SimpleModelRenderer(
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<Assets::RendererConstruction>& construction,
			const std::shared_ptr<DrawableConstructor>& drawableConstructor,
			const std::shared_ptr<Assets::SkeletonScaffold>& skeletonScaffold,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool = nullptr,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator = nullptr,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});
		~SimpleModelRenderer();

		SimpleModelRenderer& operator=(const SimpleModelRenderer&) = delete;
		SimpleModelRenderer(const SimpleModelRenderer&) = delete;
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<Assets::RendererConstruction>& construction,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});
		
		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			StringSection<> modelScaffoldName,
			StringSection<> materialScaffoldName,
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});

		static void ConstructToPromise(
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			StringSection<> modelScaffoldName);

		static void ConstructToPromise(	// todo -- remove
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
			StringSection<> deformOperations = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {},
			const std::string& modelScaffoldNameString = {},
			const std::string& materialScaffoldNameString = {});

	private:
		std::shared_ptr<DrawableConstructor> _drawableConstructor;

		struct Element
		{
			RenderCore::Assets::SkeletonBinding _skeletonBinding;
			std::unique_ptr<Float4x4[]> _baseTransforms;
			unsigned _baseTransformCount;
		};
		std::vector<Element> _elements;

		std::shared_ptr<UniformsStreamInterface> _usi;

		std::shared_ptr<IDeformAcceleratorPool> _deformAcceleratorPool;
		std::shared_ptr<DeformAccelerator> _deformAccelerator;
		std::shared_ptr<IGeoDeformerInfrastructure> _geoDeformerInfrastructure;

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
			IteratorRange<const std::shared_ptr<IGeoDeformer>*> skinDeformers);
		RendererSkeletonInterface(
			const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
			IGeoDeformerInfrastructure& geoDeformerInfrastructure);
		~RendererSkeletonInterface();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<RendererSkeletonInterface>>&& skeletonInterfaceFuture,
			std::promise<std::shared_ptr<SimpleModelRenderer>>&& rendererFuture,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<Assets::RendererConstruction>& rendererConstruction,
			const ::Assets::PtrToMarkerPtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,	// todo -- this could be managed by RendererConstruction
			StringSection<> deformOperations = {},
			IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates = {});

	private:
		struct Deformer
		{
			ISkinDeformer* _skinDeformer;
			RenderCore::Assets::SkeletonBinding _deformerBindings;
			std::shared_ptr<IGeoDeformer> _skinDeformerRef;
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
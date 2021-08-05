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
	class DeformOperationInstantiation;
	class IDeformOperation;
	class IUniformBufferDelegate;

	class IPreDrawDelegate
	{
	public:
		virtual bool OnDraw(
			const ExecuteDrawableContext&, ParsingContext&,
			const Drawable&,
			uint64_t materialGuid, unsigned drawCallIdx) = 0;
		virtual ~IPreDrawDelegate();
	};
	
	class SimpleModelRenderer
	{
	public:
		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>()) const;

		void BuildDrawables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld,
			const std::shared_ptr<IPreDrawDelegate>& delegate) const;

		void BuildGeometryProcables(
			IteratorRange<DrawablesPacket** const> pkts,
			const Float4x4& localToWorld = Identity<Float4x4>()) const;

		void GenerateDeformBuffer(IThreadContext& context);
		unsigned DeformOperationCount() const;
		IDeformOperation& DeformOperation(unsigned idx);
		const ::Assets::DependencyValidation& GetDependencyValidation() const;

		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& GetModelScaffold() const { return _modelScaffold; }
		const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& GetMaterialScaffold() const { return _materialScaffold; }
		const std::string& GetModelScaffoldName() const { return _modelScaffoldName; }
		const std::string& GetMaterialScaffoldName() const { return _materialScaffoldName; }

		using UniformBufferBinding = std::pair<uint64_t, std::shared_ptr<IUniformBufferDelegate>>;

		SimpleModelRenderer(
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
			const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& materialScaffold,
			IteratorRange<const DeformOperationInstantiation*> deformAttachments = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {},
			const std::string& modelScaffoldName = {},
			const std::string& materialScaffoldName = {});
		~SimpleModelRenderer();

		SimpleModelRenderer& operator=(const SimpleModelRenderer&) = delete;
		SimpleModelRenderer(const SimpleModelRenderer&) = delete;
		
		static void ConstructToFuture(
			::Assets::FuturePtr<SimpleModelRenderer>& future,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const ::Assets::PtrToFuturePtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
			const ::Assets::PtrToFuturePtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
			StringSection<> deformOperations = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {},
			const std::string& modelScaffoldNameString = {},
			const std::string& materialScaffoldNameString = {});
		
		static void ConstructToFuture(
			::Assets::FuturePtr<SimpleModelRenderer>& future,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			StringSection<> modelScaffoldName,
			StringSection<> materialScaffoldName,
			StringSection<> deformOperations = {},
			IteratorRange<const UniformBufferBinding*> uniformBufferDelegates = {});

		static void ConstructToFuture(
			::Assets::FuturePtr<SimpleModelRenderer>& future,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			StringSection<> modelScaffoldName);

		struct DeformOp;
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

		std::vector<DeformOp> _deformOps;
		std::vector<uint8_t> _deformStaticDataInput;
		std::vector<uint8_t> _deformTemporaryBuffer;

		std::shared_ptr<IResource> _dynVB;

		std::vector<std::shared_ptr<DrawableInputAssembly>> _drawableIAs;

		std::string _modelScaffoldName;
		std::string _materialScaffoldName;

		::Assets::DependencyValidation _depVal;

		class GeoCallBuilder;
	};

	class RendererSkeletonInterface : public IUniformBufferDelegate
	{
	public:
		void FeedInSkeletonMachineResults(
			IteratorRange<const Float4x4*> skeletonMachineOutput);

		void WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst) override;
        size_t GetSize() override;

		RendererSkeletonInterface(
			const std::shared_ptr<RenderCore::Assets::ModelScaffold>& scaffoldActual, 
			const std::shared_ptr<RenderCore::Assets::SkeletonScaffold>& skeletonActual);
		~RendererSkeletonInterface();

		static void ConstructToFuture(
			::Assets::PtrToFuturePtr<RendererSkeletonInterface>& skeletonInterfaceFuture,
			::Assets::PtrToFuturePtr<SimpleModelRenderer>& rendererFuture,
			const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
			const ::Assets::PtrToFuturePtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
			const ::Assets::PtrToFuturePtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
			const ::Assets::PtrToFuturePtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,
			StringSection<> deformOperations = {},
			IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates = {});
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
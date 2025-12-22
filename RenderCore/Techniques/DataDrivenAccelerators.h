
#pragma once

#include "ManualDrawables.h"
#include "DescriptorSetAccelerator.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Utility/IteratorUtils.h"

namespace RenderCore::Assets { class CompiledMaterialSet; class ShaderPatchCollection; class PredefinedDescriptorSetLayout; }

namespace RenderCore { namespace Techniques
{
	class IDeformAcceleratorPool; class DeformAccelerator; struct DeformerToDescriptorSetBinding;

	class DescriptorSetConstructorHelper
	{
	public:
		IteratorRange<RenderCore::Assets::ScaffoldCmdIterator> _matMachine;
		std::shared_ptr<RenderCore::Assets::CompiledMaterialSet> _matScaffold;
		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _matDescSet;
		RenderCore::Techniques::MatMachineDecompositionHelper _matMachineDecomposed;

		DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat);
		DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat, std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches, std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> matDescSet);
	};

	class DataDrivenAccelerators
	{
	public:
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipeline;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSet;
		size_t _vertexStride = 0;

		::Assets::DependencyValidation _depVal;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		DataDrivenAccelerators(
			std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> pipeline,
			std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> descriptorSet,
			size_t vertexStride,
			::Assets::DependencyValidation depVal);
		DataDrivenAccelerators();
		~DataDrivenAccelerators();

		static void ConstructToPromise(
			std::promise<DataDrivenAccelerators>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology);

		static void ConstructToPromise(
			std::promise<DataDrivenAccelerators>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology);

		static void ConstructToPromise(
			std::promise<DataDrivenAccelerators>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			const RenderCore::Assets::RawMaterial&& materialOverrides,
			IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology);

		static void ConstructToPromise(
			std::promise<DataDrivenAccelerators>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			const RenderCore::Assets::RawMaterial&& materialOverrides,
			IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology);
	};

	class DataDrivenAcceleratorsWithDeform : public DataDrivenAccelerators
	{
	public:
		RenderCore::Techniques::UniformDeformHelper _uniformDeformHelper;

		DataDrivenAcceleratorsWithDeform(
			std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> pipeline,
			std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> descriptorSet,
			RenderCore::Techniques::UniformDeformHelper&& uniformDeformHelper,
			size_t vertexStride,
			::Assets::DependencyValidation depVal);
		DataDrivenAcceleratorsWithDeform();
		~DataDrivenAcceleratorsWithDeform();

		static void ConstructToPromise(
			std::promise<DataDrivenAcceleratorsWithDeform>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology);

		static void ConstructToPromise(
			std::promise<DataDrivenAcceleratorsWithDeform>&& promise,
			std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators,
			std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
			IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology);
	};

}}


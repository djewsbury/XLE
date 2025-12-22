#include "DataDrivenAccelerators.h"
#include "PipelineAccelerator.h"
#include "DeformAccelerator.h"
#include "TechniqueUtils.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/MaterialCompiler.h"
#include "../Assets/CompiledMaterialSet.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../../Assets/Continuation.h"

namespace RenderCore { namespace Techniques
{

	DescriptorSetConstructorHelper::DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat)
	{
		auto matScaffoldConstr = std::make_shared<RenderCore::Assets::MaterialSetConstruction>();
		std::string baseMaterials[] { "main" };
		matScaffoldConstr->SetBaseMaterials(baseMaterials);
		matScaffoldConstr->AddOverride("main", std::move(rawMat));

		std::promise<std::shared_ptr<RenderCore::Assets::CompiledMaterialSet>> promisedMatScaffold;
		auto futureMatScaffold = promisedMatScaffold.get_future();
		RenderCore::Assets::ConstructMaterialSet(std::move(promisedMatScaffold), std::move(matScaffoldConstr));

		YieldToPool(futureMatScaffold);
		_matScaffold = futureMatScaffold.get();

		using namespace Utility::Literals;
		_matMachine = _matScaffold->GetMaterialMachine("main"_h);

		_matMachineDecomposed = RenderCore::Techniques::DecomposeMaterialMachine(_matMachine);
		if (_matMachineDecomposed._shaderPatchCollection != ~0u)
			_patchCollection = _matScaffold->GetShaderPatchCollection(_matMachineDecomposed._shaderPatchCollection);
	}

	DescriptorSetConstructorHelper::DescriptorSetConstructorHelper(RenderCore::Assets::RawMaterial&& rawMat, std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> shaderPatches, std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> matDescSet)
	: _patchCollection(std::move(shaderPatches)), _matDescSet(std::move(matDescSet))
	{
		auto matScaffoldConstr = std::make_shared<RenderCore::Assets::MaterialSetConstruction>();
		std::string baseMaterials[] { "main" };
		matScaffoldConstr->SetBaseMaterials(baseMaterials);
		matScaffoldConstr->AddOverride("main", std::move(rawMat));

		std::promise<std::shared_ptr<RenderCore::Assets::CompiledMaterialSet>> promisedMatScaffold;
		auto futureMatScaffold = promisedMatScaffold.get_future();
		RenderCore::Assets::ConstructMaterialSet(std::move(promisedMatScaffold), std::move(matScaffoldConstr));

		YieldToPool(futureMatScaffold);
		_matScaffold = futureMatScaffold.get();

		using namespace Utility::Literals;
		_matMachine = _matScaffold->GetMaterialMachine("main"_h);

		_matMachineDecomposed = RenderCore::Techniques::DecomposeMaterialMachine(_matMachine);
	}

	DataDrivenAccelerators::DataDrivenAccelerators(
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> pipeline,
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> descriptorSet,
		size_t vertexStride,
		::Assets::DependencyValidation depVal)
	: _pipeline(std::move(pipeline)), _descriptorSet(std::move(descriptorSet)), _vertexStride(vertexStride), _depVal(std::move(depVal))
	{}

	DataDrivenAccelerators::DataDrivenAccelerators() = default;
	DataDrivenAccelerators::~DataDrivenAccelerators() = default;

	void DataDrivenAccelerators::ConstructToPromise(
		std::promise<DataDrivenAccelerators>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::InputElementDesc>(inputAssembly.begin(), inputAssembly.end()), name](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStrideForSlot(ia, 0);
				return DataDrivenAccelerators { std::move(pipeline), std::move(descriptorSet), vertexStride, std::move(depVal) };
			});
	}

	void DataDrivenAccelerators::ConstructToPromise(
		std::promise<DataDrivenAccelerators>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		assert(pipelineAccelerators);
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::MiniInputElementDesc>(inputAssembly.begin(), inputAssembly.end()), name](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStride(ia);
				return DataDrivenAccelerators { std::move(pipeline), std::move(descriptorSet), vertexStride, std::move(depVal) };
			});
	}

	void DataDrivenAccelerators::ConstructToPromise(
		std::promise<DataDrivenAccelerators>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		const RenderCore::Assets::RawMaterial&& materialOverrides,
		IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::InputElementDesc>(inputAssembly.begin(), inputAssembly.end()), name, rawMatOverrides=std::move(materialOverrides)](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				rawMat.MergeInWithFilenameResolve(std::move(rawMatOverrides), {});
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStrideForSlot(ia, 0);
				return DataDrivenAccelerators { std::move(pipeline), std::move(descriptorSet), vertexStride, std::move(depVal) };
			});
	}

	void DataDrivenAccelerators::ConstructToPromise(
		std::promise<DataDrivenAccelerators>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		const RenderCore::Assets::RawMaterial&& materialOverrides,
		IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		assert(pipelineAccelerators);
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::MiniInputElementDesc>(inputAssembly.begin(), inputAssembly.end()), name, rawMatOverrides=std::move(materialOverrides)](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				rawMat.MergeInWithFilenameResolve(std::move(rawMatOverrides), {});
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStride(ia);
				return DataDrivenAccelerators { std::move(pipeline), std::move(descriptorSet), vertexStride, std::move(depVal) };
			});
	}

	DataDrivenAcceleratorsWithDeform::DataDrivenAcceleratorsWithDeform(
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> pipeline,
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> descriptorSet,
		RenderCore::Techniques::UniformDeformHelper&& uniformDeformHelper,
		size_t vertexStride,
		::Assets::DependencyValidation depVal)
	: DataDrivenAccelerators(std::move(pipeline), std::move(descriptorSet), vertexStride, std::move(depVal)), _uniformDeformHelper(std::move(uniformDeformHelper))
	{}

	DataDrivenAcceleratorsWithDeform::DataDrivenAcceleratorsWithDeform() = default;
	DataDrivenAcceleratorsWithDeform::~DataDrivenAcceleratorsWithDeform() = default;

	void DataDrivenAcceleratorsWithDeform::ConstructToPromise(
		std::promise<DataDrivenAcceleratorsWithDeform>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		IteratorRange<const RenderCore::InputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::InputElementDesc>(inputAssembly.begin(), inputAssembly.end()), deformAccelerators=std::move(deformAccelerators), name](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				RenderCore::Techniques::UniformDeformHelper uniformDeformHelper { *descSetHelper._matDescSet, descSetHelper._matMachine };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name), uniformDeformHelper.MakeDeformerToDescriptorSetBinding(*deformAccelerators));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStrideForSlot(ia, 0);
				return DataDrivenAcceleratorsWithDeform { std::move(pipeline), std::move(descriptorSet), std::move(uniformDeformHelper), vertexStride, std::move(depVal) };
			});
	}

	void DataDrivenAcceleratorsWithDeform::ConstructToPromise(
		std::promise<DataDrivenAcceleratorsWithDeform>&& promise,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAccelerators,
		std::shared_ptr<AssetsNew::CompoundAssetUtil> util, const AssetsNew::ScaffoldIndexer& indexer,
		IteratorRange<const RenderCore::MiniInputElementDesc*> inputAssembly, RenderCore::Topology topology)
	{
		using namespace Utility::Literals;
		assert(pipelineAccelerators);
		auto futureMaterial = util->GetFuture<RenderCore::Assets::RawMaterial>("RawMaterial"_h, indexer);
		auto futureShaderPatches = util->GetFuture<std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>>("ShaderPatchCollection"_h, indexer);
		auto futureDescSet = util->GetFuture<std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>>("DescriptorSet"_h, indexer);
		std::string name = "particle-material";
		if (auto* contextAndIdentifier =  std::get_if<AssetsNew::ContextAndIdentifier>(&indexer)) name = contextAndIdentifier->_identifier;
		#if defined(_DEBUG)
			else if (auto* scaffoldAndEntityName =  std::get_if<AssetsNew::ScaffoldAndEntityName>(&indexer)) name = scaffoldAndEntityName->_entityName;
		#endif
		::Assets::WhenAll(std::move(futureMaterial), std::move(futureShaderPatches), std::move(futureDescSet)).ThenConstructToPromise(
			std::move(promise),
			[pa=pipelineAccelerators, topology, ia=std::vector<RenderCore::MiniInputElementDesc>(inputAssembly.begin(), inputAssembly.end()), deformAccelerators=std::move(deformAccelerators), name](const auto& material, const auto& shaderPatches, const auto& descSet) mutable {
				RenderCore::Assets::RawMaterial rawMat = std::get<0>(std::move(material));
				DescriptorSetConstructorHelper descSetHelper { std::move(rawMat), shaderPatches, descSet };
				RenderCore::Techniques::UniformDeformHelper uniformDeformHelper { *descSetHelper._matDescSet, descSetHelper._matMachine };
				auto descriptorSet = pa->CreateDescriptorSetAccelerator(nullptr, descSetHelper._patchCollection, descSetHelper._matDescSet, descSetHelper._matMachine, descSetHelper._matScaffold, std::move(name), uniformDeformHelper.MakeDeformerToDescriptorSetBinding(*deformAccelerators));
				auto pipeline = pa->CreatePipelineAccelerator(descSetHelper._patchCollection, descSetHelper._matDescSet, std::move(descSetHelper._matMachineDecomposed._matSelectors), ia, topology, descSetHelper._matMachineDecomposed._stateSet);

				::Assets::DependencyValidationMarker depVals[] { descSetHelper._matScaffold->GetDependencyValidation(), std::get<::Assets::DependencyValidation>(material), std::get<::Assets::DependencyValidation>(shaderPatches) };
				auto depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				auto vertexStride = RenderCore::CalculateVertexStride(ia);
				return DataDrivenAcceleratorsWithDeform { std::move(pipeline), std::move(descriptorSet), std::move(uniformDeformHelper), vertexStride, std::move(depVal) };
			});
	}

}}


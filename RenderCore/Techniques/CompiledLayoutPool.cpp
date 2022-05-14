// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompiledLayoutPool.h"
#include "CompiledShaderPatchCollection.h"
#include "PipelineOperators.h"
#include "Services.h"
#include "CommonResources.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../../Assets/AssetHeap.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Threading/Mutex.h"

namespace RenderCore { namespace Techniques
{
	
	class CompiledLayoutPool : public ICompiledLayoutPool
	{
	public:
		::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> GetPatchCollectionFuture(
			const Assets::ShaderPatchCollection&) override;

		::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> GetDefaultPatchCollectionFuture() override;

		::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> GetPatchedPipelineLayout(
			StringSection<> techniquePipelineLayoutSrc) override;

		const RenderCore::Assets::PredefinedDescriptorSetLayout& GetBaseMaterialDescriptorSetLayout() const override;

		CompiledLayoutPool(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<DescriptorSetLayoutAndBinding> matDescSetLayout);
		virtual ~CompiledLayoutPool();
	
	private:
		std::shared_ptr<DescriptorSetLayoutAndBinding> _matDescSetLayout;
		::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> _emptyPatchCollection;
		std::shared_ptr<IDevice> _device;

		std::vector<std::pair<uint64_t, ::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection>>> _compiledPatchCollections;
		::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> _defaultCompiledPatchCollection;

		Threading::Mutex _lock;
	};

	::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> CompiledLayoutPool::GetPatchCollectionFuture(
		const Assets::ShaderPatchCollection& shaderPatchCollection)
	{
		::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> result;
		{
			ScopedLock(_lock);
			auto hash = shaderPatchCollection.GetHash();
			auto i = LowerBound(_compiledPatchCollections, hash);
			if (i!= _compiledPatchCollections.end() && i->first == hash) {
				if (!::Assets::IsInvalidated(*i->second))
					return i->second;
			} else {
				i = _compiledPatchCollections.insert(i, std::make_pair(hash, nullptr));
			}

			result = i->second = std::make_shared<::Assets::MarkerPtr<CompiledShaderPatchCollection>>();
		}

		// Call AutoConstructToPromise outside of the lock. Note that this opens the door to other threads
		// using the marker before we even initialize the promise like this
		::Assets::AutoConstructToPromise(result->AdoptPromise(), shaderPatchCollection, std::ref(*_matDescSetLayout));
		return result;
	}

	::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> CompiledLayoutPool::GetDefaultPatchCollectionFuture()
	{
		return _emptyPatchCollection;
	}

	::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> CompiledLayoutPool::GetPatchedPipelineLayout(
		StringSection<> techniquePipelineLayoutSrc)
	{
		return ::Assets::MakeAssetPtr<CompiledPipelineLayoutAsset>(_device, techniquePipelineLayoutSrc, _matDescSetLayout);
	}

	const RenderCore::Assets::PredefinedDescriptorSetLayout& CompiledLayoutPool::GetBaseMaterialDescriptorSetLayout() const
	{
		return *_matDescSetLayout->GetLayout();
	}

	CompiledLayoutPool::CompiledLayoutPool(
		std::shared_ptr<IDevice> device,
		std::shared_ptr<DescriptorSetLayoutAndBinding> matDescSetLayout)
	: _matDescSetLayout(std::move(matDescSetLayout))
	, _device(std::move(device))
	{
		_emptyPatchCollection = std::make_shared<::Assets::MarkerPtr<CompiledShaderPatchCollection>>("empty-patch-collection");
		_emptyPatchCollection->SetAsset(std::make_shared<CompiledShaderPatchCollection>());
	}

	CompiledLayoutPool::~CompiledLayoutPool() {}

	std::shared_ptr<ICompiledLayoutPool> CreateCompiledLayoutPool(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<DescriptorSetLayoutAndBinding>& matDescSetLayout)
	{
		return std::make_shared<CompiledLayoutPool>(device, matDescSetLayout);
	}

	ICompiledLayoutPool::~ICompiledLayoutPool() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	const ::Assets::DependencyValidation CompiledPipelineLayoutAsset::GetDependencyValidation() const { return _predefinedLayout->GetDependencyValidation(); };

	CompiledPipelineLayoutAsset::CompiledPipelineLayoutAsset(
		std::shared_ptr<RenderCore::IDevice> device,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> predefinedLayout,
		std::shared_ptr<DescriptorSetLayoutAndBinding> patchInDescSet,
		RenderCore::ShaderLanguage shaderLanguage)
	: _predefinedLayout(std::move(predefinedLayout))
	{
		assert(Services::HasInstance() && Services::GetCommonResources());
		auto& commonResources = *Services::GetCommonResources();
		auto initializer = _predefinedLayout->MakePipelineLayoutInitializer(shaderLanguage, &commonResources._samplerPool);
		if (patchInDescSet) {
			if (patchInDescSet->GetSlotIndex() >= initializer._descriptorSets.size())
				initializer._descriptorSets.resize(patchInDescSet->GetSlotIndex()+1);
			auto& dst = initializer._descriptorSets[patchInDescSet->GetSlotIndex()];
			dst._signature = patchInDescSet->GetLayout()->MakeDescriptorSetSignature(&commonResources._samplerPool);
			dst._name = patchInDescSet->GetName();
			dst._pipelineType = patchInDescSet->GetPipelineType();
		}
		_pipelineLayout = device->CreatePipelineLayout(initializer);
	}

	void CompiledPipelineLayoutAsset::ConstructToPromise(
		std::promise<std::shared_ptr<CompiledPipelineLayoutAsset>>&& promise,
		const std::shared_ptr<RenderCore::IDevice>& device,
		StringSection<> srcFile,
		const std::shared_ptr<DescriptorSetLayoutAndBinding>& patchInDescSet,
		RenderCore::ShaderLanguage shaderLanguage)
	{
		using namespace RenderCore;
		auto src = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(srcFile);
		::Assets::WhenAll(src).ThenConstructToPromise(
			std::move(promise),
			[device, patchInDescSet, shaderLanguage](auto predefinedLayout) {
				return std::make_shared<CompiledPipelineLayoutAsset>(device, predefinedLayout, patchInDescSet, shaderLanguage);
			});
	}

	DescriptorSetLayoutAndBinding::DescriptorSetLayoutAndBinding(
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& layout,
		unsigned slotIdx,
		const std::string& name,
		PipelineType pipelineType,
		::Assets::DependencyValidation depVal)
	: _layout(layout), _slotIdx(slotIdx)
	, _name(name), _pipelineType(pipelineType)
	, _depVal(std::move(depVal))
	{
		if (layout) {
			_hash = HashCombine(layout->CalculateHash(), HashCombine(slotIdx, (uint64_t)_pipelineType));
		} else {
			_hash = 0;
		}
	}

	DescriptorSetLayoutAndBinding::DescriptorSetLayoutAndBinding()
	{
		_hash = 0;
		_slotIdx = ~0u;
		_pipelineType = PipelineType::Graphics;
	}

	DescriptorSetLayoutAndBinding::~DescriptorSetLayoutAndBinding()
	{}

	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayoutFile& file, const std::string& pipelineLayoutName, const std::string& descriptorSetName, PipelineType pipelineType)
	{
		auto pipeline = file._pipelineLayouts.find(pipelineLayoutName);
		if (pipeline == file._pipelineLayouts.end())
			return nullptr;

		auto i = std::find_if(pipeline->second->_descriptorSets.begin(), pipeline->second->_descriptorSets.end(),
			[descriptorSetName](const auto& c) {
				return c._name == descriptorSetName;
			});
		if (i == pipeline->second->_descriptorSets.end())
			return {};
		
		return std::make_shared<DescriptorSetLayoutAndBinding>(i->_descSet, (unsigned)std::distance(pipeline->second->_descriptorSets.begin(), i), descriptorSetName, pipelineType, file.GetDependencyValidation());
	}

	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayout& pipeline, const std::string& descriptorSetName, PipelineType pipelineType)
	{
		auto i = std::find_if(pipeline._descriptorSets.begin(), pipeline._descriptorSets.end(),
			[descriptorSetName](const auto& c) {
				return c._name == descriptorSetName;
			});
		if (i == pipeline._descriptorSets.end())
			return {};
		
		return std::make_shared<DescriptorSetLayoutAndBinding>(i->_descSet, (unsigned)std::distance(pipeline._descriptorSets.begin(), i), descriptorSetName, pipelineType, pipeline.GetDependencyValidation());
	}

}}

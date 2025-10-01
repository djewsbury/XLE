// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineLayoutDelegate.h"
#include "ShaderPatchInstantiationUtil.h"
#include "PipelineOperators.h"
#include "Services.h"
#include "CommonResources.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../../Assets/AssetHeap.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/Streams/PathUtils.h"

namespace RenderCore { namespace Techniques
{
	
	class PipelineLayoutDelegate : public IPipelineLayoutDelegate, public std::enable_shared_from_this<PipelineLayoutDelegate>
	{
	public:
		::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil> CompileShaderPatchCollection(
			const std::shared_ptr<Assets::ShaderPatchCollection>&,
			const std::shared_ptr<Assets::PredefinedDescriptorSetLayout>&) override;

		std::shared_ptr<Assets::PredefinedPipelineLayout> BuildPatchedLayout(
			const Assets::PredefinedPipelineLayout& skeletonPipelineLayout,
			IteratorRange<const PatchInDescriptorSet*> patchInDescSets) override;

		::Assets::DependencyValidation GetDependencyValidation() const override;

		PipelineLayoutDelegate(
			std::shared_ptr<DescriptorSetLayoutAndBinding> matDescSetLayout);
		virtual ~PipelineLayoutDelegate();
	
	private:
		std::shared_ptr<DescriptorSetLayoutAndBinding> _matDescSetLayout;
		::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil> _fallbackPatchCollection;
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<PipelineCollection> _pipelineCollection;

		std::vector<std::pair<uint64_t, ::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil>>> _compiledPatchCollections;
		::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil> _defaultCompiledPatchCollection;

		Threading::Mutex _lock;
	};

	::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil> PipelineLayoutDelegate::CompileShaderPatchCollection(
		const std::shared_ptr<Assets::ShaderPatchCollection>& shaderPatchCollection,
		const std::shared_ptr<Assets::PredefinedDescriptorSetLayout>& matDescSet)
	{
		if (!shaderPatchCollection) return _fallbackPatchCollection;

		::Assets::PtrToMarkerPtr<ShaderPatchInstantiationUtil> result;
		{
			ScopedLock(_lock);
			auto hash = shaderPatchCollection->GetHash();
			if (matDescSet)
				hash = matDescSet->CalculateHash(hash);
			auto i = LowerBound(_compiledPatchCollections, hash);
			if (i!= _compiledPatchCollections.end() && i->first == hash) {
				if (!::Assets::IsInvalidated(*i->second))
					return i->second;
			} else {
				i = _compiledPatchCollections.insert(i, std::make_pair(hash, nullptr));
			}

			result = i->second = std::make_shared<::Assets::MarkerPtr<ShaderPatchInstantiationUtil>>();
		}

		// Call AutoConstructToPromise outside of the lock. Note that this opens the door to other threads
		// using the marker before we even initialize the promise like this
		assert(_matDescSetLayout);
		::ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise = result->AdoptPromise(), shaderPatchCollection, matDescSet, weakThis=weak_from_this()]() mutable {
				TRY {
					auto l = weakThis.lock();
					if (!l) Throw(std::runtime_error("expired"));
					promise.set_value(std::make_shared<ShaderPatchInstantiationUtil>(*shaderPatchCollection, matDescSet.get(), *l->_matDescSetLayout));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
	}

	std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> PipelineLayoutDelegate::BuildPatchedLayout(
		const Assets::PredefinedPipelineLayout& skeletonPipelineLayout,
		IteratorRange<const PatchInDescriptorSet*> patchInDescSets)
	{
		assert(!patchInDescSets.empty());

		auto result = std::make_shared<RenderCore::Assets::PredefinedPipelineLayout>(skeletonPipelineLayout);

		// Take each descriptor set either from skeletonPipelineLayout, or the patch in list
		for (auto& ds:result->_descriptorSets) {
			auto patchIn = std::find_if(
				patchInDescSets.begin(), patchInDescSets.end(),
				[n=&ds._name](const auto& q) { return XlEqString(q._bindingName, *n); });
			if (patchIn != patchInDescSets.end()) {
				ds._isAuto = false;
				ds._descSet = patchIn->_descSet;
			}
		}
		
		return result;
	}

	::Assets::DependencyValidation PipelineLayoutDelegate::GetDependencyValidation() const { return _matDescSetLayout->GetDependencyValidation(); }

	PipelineLayoutDelegate::PipelineLayoutDelegate(
		std::shared_ptr<DescriptorSetLayoutAndBinding> matDescSetLayout)
	: _matDescSetLayout(std::move(matDescSetLayout))
	{
		_fallbackPatchCollection = std::make_shared<::Assets::MarkerPtr<ShaderPatchInstantiationUtil>>("empty-patch-collection");
		_fallbackPatchCollection->SetAsset(std::make_shared<ShaderPatchInstantiationUtil>(*_matDescSetLayout));
	}

	PipelineLayoutDelegate::~PipelineLayoutDelegate() {}

	std::shared_ptr<IPipelineLayoutDelegate> CreatePipelineLayoutDelegate(
		StringSection<> skeletonPipelineLayoutFile,
		StringSection<> fallbackMaterialDescriptorSetFile)
	{
		// The pipeline layout probably has an empty descriptor set named "Material", into which a fully formed
		// material descriptor set will be patched
		auto pipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayout>(skeletonPipelineLayoutFile);
		auto matDescSetLayout = FindLayout(*pipelineLayout, "Material", PipelineType::Graphics);
		if (!matDescSetLayout)
			Throw(std::runtime_error("Missing \"Material\" descriptor set in skeleton pipeline layout (" + skeletonPipelineLayoutFile.AsString() + "). Expecting empty descriptor set."));

		assert(!fallbackMaterialDescriptorSetFile.IsEmpty());
		auto splitFn = MakeFileNameSplitter(fallbackMaterialDescriptorSetFile);
		if (splitFn.Parameters().IsEmpty()) {
			// expecting .ds file -- ie, raw PredefinedDescriptorSet
			auto descSet = ::Assets::ActualizeAssetPtr<Assets::PredefinedDescriptorSetLayout>(fallbackMaterialDescriptorSetFile);
			matDescSetLayout = std::make_shared<DescriptorSetLayoutAndBinding>(
				descSet,
				matDescSetLayout->GetSlotIndex(),
				matDescSetLayout->GetName(),
				matDescSetLayout->GetPipelineType(),
				descSet->GetDependencyValidation());
		} else {
			auto matDescSetLayoutContainer = ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(splitFn.AllExceptParameters());
			auto i2 = matDescSetLayoutContainer->_descriptorSets.find(splitFn.Parameters().AsString());
			if (i2 == matDescSetLayoutContainer->_descriptorSets.end())
				Throw(std::runtime_error("Missing (" + splitFn.Parameters().AsString() + ") descriptor set entry in fallback material file (" + splitFn.AllExceptParameters().AsString() + ")"));

			matDescSetLayout = std::make_shared<DescriptorSetLayoutAndBinding>(
				i2->second,
				matDescSetLayout->GetSlotIndex(),
				matDescSetLayout->GetName(),
				matDescSetLayout->GetPipelineType(),
				matDescSetLayoutContainer->GetDependencyValidation());
		}

		return std::make_shared<PipelineLayoutDelegate>(std::move(matDescSetLayout));
	}
	
	std::shared_ptr<IPipelineLayoutDelegate> CreatePipelineLayoutDelegate(
		StringSection<> pipelineLayoutFile)
	{
		// the default material layout is embedded within the pipeline layout itself
		auto pipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayout>(pipelineLayoutFile);
		auto matDescSetLayout = FindLayout(*pipelineLayout, "Material", PipelineType::Graphics);
		if (!matDescSetLayout)
			Throw(std::runtime_error("Missing \"Material\" descriptor set in pipeline layout (" + pipelineLayoutFile.AsString() + ")"));
		return std::make_shared<PipelineLayoutDelegate>(std::move(matDescSetLayout));
	}

	std::shared_ptr<IPipelineLayoutDelegate> CreatePipelineLayoutDelegate(
		std::shared_ptr<DescriptorSetLayoutAndBinding> fallbackMaterialDescriptorSet)
	{
		return std::make_shared<PipelineLayoutDelegate>(std::move(fallbackMaterialDescriptorSet));
	}

	IPipelineLayoutDelegate::~IPipelineLayoutDelegate() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	CompiledPipelineLayoutAsset::CompiledPipelineLayoutAsset(
		std::shared_ptr<RenderCore::IDevice> device,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> predefinedLayout,
		StringSection<> name,
		std::shared_ptr<DescriptorSetLayoutAndBinding> patchInDescSet,
		RenderCore::ShaderLanguage shaderLanguage)
	: _predefinedLayout(std::move(predefinedLayout))
	, _initializer(name.AsString())
	{
		_depVal = _predefinedLayout->GetDependencyValidation();

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

			::Assets::DependencyValidationMarker depVals[] { _depVal, patchInDescSet->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}
		_pipelineLayout = device->CreatePipelineLayout(initializer, name);
	}

	CompiledPipelineLayoutAsset::CompiledPipelineLayoutAsset(
		std::shared_ptr<PipelineCollection> pipelineCollection,
		std::shared_ptr<Assets::PredefinedPipelineLayout> predefinedLayout,
		StringSection<> name,
		ShaderLanguage shaderLanguage)
	: _predefinedLayout(std::move(predefinedLayout))
	, _initializer(name.AsString())
	{
		_depVal = _predefinedLayout->GetDependencyValidation();

		assert(Services::HasInstance() && Services::GetCommonResources());
		auto& commonResources = *Services::GetCommonResources();
		auto initializer = _predefinedLayout->MakePipelineLayoutInitializer(shaderLanguage, &commonResources._samplerPool);
		_pipelineLayout = pipelineCollection->CreatePipelineLayout(initializer, name);
	}

	void CompiledPipelineLayoutAsset::ConstructToPromise(
		std::promise<std::shared_ptr<CompiledPipelineLayoutAsset>>&& promise,
		const std::shared_ptr<RenderCore::IDevice>& device,
		StringSection<> srcFile,
		const std::shared_ptr<DescriptorSetLayoutAndBinding>& patchInDescSet,
		RenderCore::ShaderLanguage shaderLanguage)
	{
		using namespace RenderCore;
		auto src = ::Assets::GetAssetFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>(srcFile);
		::Assets::WhenAll(src).ThenConstructToPromise(
			std::move(promise),
			[device, patchInDescSet, shaderLanguage, name=srcFile.AsString()](auto predefinedLayout) {
				return std::make_shared<CompiledPipelineLayoutAsset>(device, predefinedLayout, name, patchInDescSet, shaderLanguage);
			});
	}

	void CompiledPipelineLayoutAsset::ConstructToPromise(
		std::promise<std::shared_ptr<CompiledPipelineLayoutAsset>>&& promise,
		const std::shared_ptr<PipelineCollection>& pipelineCollection,
		StringSection<> srcFile,
		ShaderLanguage shaderLanguage)
	{
		using namespace RenderCore;
		auto src = ::Assets::GetAssetFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>(srcFile);
		::Assets::WhenAll(src).ThenConstructToPromise(
			std::move(promise),
			[pipelineCollection, shaderLanguage, name=srcFile.AsString()](auto predefinedLayout) {
				return std::make_shared<CompiledPipelineLayoutAsset>(pipelineCollection, predefinedLayout, name, shaderLanguage);
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

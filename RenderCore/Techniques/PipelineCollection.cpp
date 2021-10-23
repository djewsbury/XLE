// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineCollection.h"
#include "PipelineBuilderUtil.h"
#include "ShaderVariationSet.h"
#include "RenderPass.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/InputLayout.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
	uint64_t FrameBufferTarget::GetHash() const 
	{
		assert(_fbDesc);
		assert(_subpassIdx < _fbDesc->GetSubpasses().size());
		return RenderCore::Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(*_fbDesc, _subpassIdx); 
	}

	FrameBufferTarget::FrameBufferTarget(const FrameBufferDesc* fbDesc, unsigned subpassIdx) : _fbDesc(fbDesc), _subpassIdx(subpassIdx) {}
	FrameBufferTarget::FrameBufferTarget(const RenderPassInstance& rpi) : _fbDesc(&rpi.GetFrameBufferDesc()), _subpassIdx(rpi.GetCurrentSubpassIndex()) {}

	uint64_t VertexInputStates::GetHash() const
	{
		auto seed = DefaultSeed64;
		lrot(seed, (int)_topology);
		if (!_miniInputAssembly.empty())
			return HashInputAssembly(_miniInputAssembly, seed);
		return HashInputAssembly(_inputAssembly, seed);
	}

#if 0
	std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> PipelinePool::CreateGraphicsPipeline(
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		const GraphicsPipelineDesc& pipelineDesc,
		const ParameterBox& selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget)
	{
		assert(0);
		return nullptr;
	}
#endif

	std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		return CreateComputePipelineInternal(pipelineLayout, shader, selectors);
	}

	std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		::Assets::PtrToFuturePtr<RenderCore::Assets::PredefinedPipelineLayout> futurePipelineLayout,
		uint64_t futurePipelineLayoutGuid,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		return CreateComputePipelineInternal({std::move(futurePipelineLayout), futurePipelineLayoutGuid}, shader, selectors);
	}

	std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		return CreateComputePipelineInternal({}, shader, selectors);
	}

	std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> PipelinePool::CreateComputePipelineInternal(
		const PipelineLayoutOptions& pipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		// accelerated return when the filtering rules are already available
		auto filteringFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		if (filteringFuture->GetAssetState() == ::Assets::AssetState::Ready) {
			ScopedLock(_sharedPools->_lock);

			const ParameterBox* selectorsList[] { &selectors };
			auto filteredSelectors = _sharedPools->FilterSelectorsAlreadyLocked(
				ShaderStage::Compute, MakeIteratorRange(selectorsList), *filteringFuture->Actualize(), 
				{}, nullptr, nullptr, {});
			return _sharedPools->CreateComputePipelineAlreadyLocked(shader, pipelineLayout, filteredSelectors);
		} else {
			auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>(shader.AsString());
			::Assets::WhenAll(filteringFuture).ThenConstructToFuture(
				*result,
				[selectorsCopy = selectors, shaderCopy=shader.AsString(), sharedPools=_sharedPools, pipelineLayout]( 
					::Assets::Future<ComputePipelineAndLayout>& resultFuture,
					std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) {

					const ParameterBox* selectorsList[] { &selectorsCopy };
					auto filteredSelectors = sharedPools->FilterSelectorsAlreadyLocked(
						ShaderStage::Compute, MakeIteratorRange(selectorsList), *automaticFiltering, 
						{}, nullptr, nullptr, {});
					auto chainedFuture = sharedPools->CreateComputePipelineAlreadyLocked(shaderCopy, pipelineLayout, filteredSelectors);
					::Assets::WhenAll(chainedFuture).ThenConstructToFuture(resultFuture);
				});
			return result;
		}	
	}

#if 0
	class PipelinePool::OldSharedPools
	{
	public:
		Threading::Mutex _lock;
		UniqueShaderVariationSet _selectorVariationsSet;

		::Assets::PtrToFuturePtr<CompiledShaderByteCode> MakeByteCodeFuture(
			const ShaderSourceParser::SelectorFilteringRules& automaticFiltering,
			ShaderStage shaderStage,
			StringSection<> shader,
			const ParameterBox& selectors,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const uint64_t*> patchExpansions);
	};
#endif

	std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> PipelinePool::CreateGraphicsPipeline(
		const PipelineLayoutOptions& pipelineLayout,
		const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
		const ParameterBox& selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget)
	{
#if 0
		auto hash = HashCombine(inputStates.GetHash(), fbTarget.GetHash());
		hash = HashCombine(pipelineDesc.GetHash(), hash);
		hash = HashCombine(selectors.GetParameterNamesHash(), hash);
		hash = HashCombine(selectors.GetHash(), hash);
		hash = HashCombine(pipelineLayout->GetGUID(), hash);

		std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
		bool replaceExisting = false;
		auto i = LowerBound(_graphicsPipelines, hash);
		if (i != _graphicsPipelines.end() && i->first == hash) {
			if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
				return i->second;
			replaceExisting = true;
		}

		auto result = std::make_shared<::Assets::Future<GraphicsPipelineAndLayout>>();
		if (replaceExisting) {
			i->second = result;
		} else
			_graphicsPipelines.insert(i, std::make_pair(hash, result));
		lk = {};
		ConstructToFuture(*result, pipelineLayout, pipelineDesc, selectors, inputStates, fbTarget);
		return result;
#endif
		auto pipelineDescWithFilteringFuture = Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(*pipelineDesc);
		if (pipelineDescWithFilteringFuture->GetAssetState() == ::Assets::AssetState::Ready) {
			auto pipelineDescWithFiltering = pipelineDescWithFilteringFuture->Actualize();

			auto configurationDepVal = ::Assets::GetDepValSys().Make();
			if (pipelineDesc->GetDependencyValidation())
				configurationDepVal.RegisterDependency(pipelineDesc->GetDependencyValidation());
			for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
				if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering->_automaticFiltering[c])
					configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_automaticFiltering[c]->GetDependencyValidation());
			if (pipelineDescWithFiltering->_preconfiguration)
				configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_preconfiguration->GetDependencyValidation());

			ScopedLock(_sharedPools->_lock);
			UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];
			const ParameterBox* paramBoxes[] = { &selectors };

			{
				for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
					if (!pipelineDesc->_shaders[c].empty()) {
						const ShaderSourceParser::SelectorFilteringRules* autoFiltering[] = {
							pipelineDescWithFiltering->_automaticFiltering[c].get()
						};
						filteredSelectors[c] = _sharedPools->_selectorVariationsSet.FilterSelectors(
							MakeIteratorRange(paramBoxes),
							pipelineDesc->_manualSelectorFiltering,
							MakeIteratorRange(autoFiltering),
							pipelineDescWithFiltering->_preconfiguration.get());
					}
			}
			return _sharedPools->CreateGraphicsPipelineAlreadyLocked(
				inputStates, pipelineDesc, pipelineDescWithFiltering,
				pipelineLayout, nullptr,
				filteredSelectors, fbTarget);
		} else {
			auto result = std::make_shared<::Assets::Future<GraphicsPipelineAndLayout>>("graphics-pipeline");
			::Assets::WhenAll(pipelineDescWithFilteringFuture).ThenConstructToFuture(
				*result,
				[sharedPools=_sharedPools, pipelineDesc, selectorsCopy=selectors, pipelineLayout,
					inputAssembly=Internal::AsVector(inputStates._inputAssembly), miniInputAssembly=Internal::AsVector(inputStates._miniInputAssembly), topology=inputStates._topology,
			 		fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx](

					::Assets::Future<GraphicsPipelineAndLayout>& resultFuture,
					auto pipelineDescWithFiltering) {
						
					auto configurationDepVal = ::Assets::GetDepValSys().Make();
					if (pipelineDesc->GetDependencyValidation())
						configurationDepVal.RegisterDependency(pipelineDesc->GetDependencyValidation());
					for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
						if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering->_automaticFiltering[c])
							configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_automaticFiltering[c]->GetDependencyValidation());
					if (pipelineDescWithFiltering->_preconfiguration)
						configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_preconfiguration->GetDependencyValidation());

					ScopedLock(sharedPools->_lock);
					UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];
					const ParameterBox* paramBoxes[] = { &selectorsCopy };

					{
						for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
							if (!pipelineDesc->_shaders[c].empty()) {
								const ShaderSourceParser::SelectorFilteringRules* autoFiltering[] = {
									pipelineDescWithFiltering->_automaticFiltering[c].get()
								};
								filteredSelectors[c] = sharedPools->_selectorVariationsSet.FilterSelectors(
									MakeIteratorRange(paramBoxes),
									pipelineDesc->_manualSelectorFiltering,
									MakeIteratorRange(autoFiltering),
									pipelineDescWithFiltering->_preconfiguration.get());
							}
					}

					auto chain = sharedPools->CreateGraphicsPipelineAlreadyLocked(
						VertexInputStates{inputAssembly, miniInputAssembly, topology}, pipelineDesc, pipelineDescWithFiltering,
						pipelineLayout, nullptr,
						filteredSelectors, {&fbDesc, spIdx});
					::Assets::WhenAll(chain).ThenConstructToFuture(resultFuture);
				});
			return result;
		}

	}

#if 0
	static ::Assets::PtrToFuturePtr<CompiledShaderByteCode> MakeByteCodeFuture(
		ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
	{
		char temp[MaxPath];
		auto meld = StringMeldInPlace(temp);
		meld << initializer;

		// shader profile
		{
			char profileStr[] = "?s_";
			switch (stage) {
			case ShaderStage::Vertex: profileStr[0] = 'v'; break;
			case ShaderStage::Geometry: profileStr[0] = 'g'; break;
			case ShaderStage::Pixel: profileStr[0] = 'p'; break;
			case ShaderStage::Domain: profileStr[0] = 'd'; break;
			case ShaderStage::Hull: profileStr[0] = 'h'; break;
			case ShaderStage::Compute: profileStr[0] = 'c'; break;
			default: assert(0); break;
			}
			if (!XlFindStringI(initializer, profileStr))
				meld << ":" << profileStr << "*";
		}

		return ::Assets::MakeAsset<CompiledShaderByteCode>(MakeStringSection(temp), definesTable);
	}

	void PipelinePool::ConstructToFuture(
		::Assets::Future<GraphicsPipelineAndLayout>& future,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		const GraphicsPipelineDesc& pipelineDescInit,
		const ParameterBox& selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget)
	{
		auto resolvedTechnique = Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDescInit);
		auto pipelineDesc = std::make_shared<GraphicsPipelineDesc>(pipelineDescInit);
		::Assets::WhenAll(resolvedTechnique).ThenConstructToFuture(
			future,
			[selectorsCopy = selectors, pipelineDesc=pipelineDesc, sharedPools=_sharedPools, 
			 pipelineLayout=pipelineLayout,
			 inputAssembly=AsVector(inputStates._inputLayout), iaHash=inputStates.GetHash(), topology=inputStates._topology,
			 fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx,
			 weakDevice=std::weak_ptr<IDevice>{_device}]( 
				::Assets::Future<GraphicsPipelineAndLayout>& resultFuture,
				std::shared_ptr<Internal::GraphicsPipelineDescWithFilteringRules> pipelineDescWithFiltering) {

				UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];

				const ParameterBox* paramBoxes[] = {
					&selectorsCopy
				};

				{
					ScopedLock(sharedPools->_lock);
					for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
						if (!pipelineDesc->_shaders[c].empty()) {
							const ShaderSourceParser::SelectorFilteringRules* autoFiltering[] = {
								pipelineDescWithFiltering->_automaticFiltering[c].get()
							};
							filteredSelectors[c] = sharedPools->_selectorVariationsSet.FilterSelectors(
								MakeIteratorRange(paramBoxes),
								pipelineDesc->_manualSelectorFiltering,
								MakeIteratorRange(autoFiltering),
								pipelineDescWithFiltering->_preconfiguration.get());
						}
				}

				auto configurationDepVal = ::Assets::GetDepValSys().Make();
				if (pipelineDesc->GetDependencyValidation())
					configurationDepVal.RegisterDependency(pipelineDesc->GetDependencyValidation());
				for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
					if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering->_automaticFiltering[c])
						configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_automaticFiltering[c]->GetDependencyValidation());
				if (pipelineDescWithFiltering->_preconfiguration)
					configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_preconfiguration->GetDependencyValidation());

				// todo -- now that we have the filtered selectors, we could attempt to reuse an existing pipeline (if one exists with
				// the same filtered selectors)

				::Assets::PtrToFuturePtr<CompiledShaderByteCode> byteCodeFutures[3];
				for (unsigned c=0; c<3; ++c) {
					if (pipelineDesc->_shaders[c].empty())
						continue;

					byteCodeFutures[c] = Internal::MakeByteCodeFuture((ShaderStage)c, pipelineDesc->_shaders[c], filteredSelectors[c]._selectors, nullptr, {}, {});
				}

				Internal::GraphicsPipelineConstructionParams constructionParams;
				constructionParams._pipelineDesc = pipelineDesc;
				constructionParams._ia._miniInputAssembly = inputAssembly;
				constructionParams._ia._hashCode = iaHash;
				constructionParams._topology = topology;
				constructionParams._fbDesc = fbDesc;
				constructionParams._subpassIdx = spIdx;
				MakeGraphicsPipelineFuture0(resultFuture, weakDevice.lock(), byteCodeFutures, pipelineLayout, std::move(constructionParams));

				auto chain = sharedPools->CreateGraphicsPipelineAlreadyLocked(
					containingPipelineAccelerator->_ia, containingPipelineAccelerator->_topology, 
					pipelineDesc, pipelineDescWithFiltering,
					pipelineLayoutAsset->GetPipelineLayout(),
					compiledPatchCollection,
					MakeIteratorRange(filteredSelectors),
					FrameBufferTarget{&cfg->_fbDesc, cfg->_subpassIdx});

#if 0
				auto shaderProgram = Internal::MakeShaderProgram(byteCodeFutures, pipelineLayout);
				std::string vsd, psd, gsd;
				#if defined(_DEBUG)
					vsd = Internal::MakeShaderDescription(ShaderStage::Vertex, pipelineDesc, pipelineLayout, nullptr, filteredSelectors[(unsigned)ShaderStage::Vertex]);
					psd = Internal::MakeShaderDescription(ShaderStage::Pixel, pipelineDesc, pipelineLayout, nullptr, filteredSelectors[(unsigned)ShaderStage::Pixel]);
					gsd = Internal::MakeShaderDescription(ShaderStage::Geometry, pipelineDesc, pipelineLayout, nullptr, filteredSelectors[(unsigned)ShaderStage::Geometry]);
				#endif

				::Assets::WhenAll(shaderProgram).ThenConstructToFuture(
					resultFuture,
					[pipelineLayout,
					inputAssembly, topology, fbDesc, spIdx,
					pipelineDesc=std::move(pipelineDesc),
					configurationDepVal
					](std::shared_ptr<Metal::ShaderProgram> shaderActual) {
						Metal::GraphicsPipelineBuilder builder;
						builder.Bind(*shaderActual);
						builder.Bind(pipelineDesc._blend);
						builder.Bind(pipelineDesc._depthStencil);
						builder.Bind(pipelineDesc._rasterization);

						Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(inputAssembly), 0 };
						Metal::BoundInputLayout ia(MakeIteratorRange(&slotBinding, &slotBinding+1), *shaderActual);
						builder.Bind(ia, topology);

						builder.SetRenderPassConfiguration(fbDesc, spIdx);

						// todo -- we have to use configurationDepVal for something!
						auto pipeline = builder.CreatePipeline(Metal::GetObjectFactory());
						return GraphicsPipelineAndLayout {
							pipeline,
							pipelineLayout,
							pipeline->GetDependencyValidation() };
					});
#endif
			});
	}
#endif

#if 0
	std::shared_ptr<::Assets::Future<PipelinePool::ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		auto hash = Hash64(shader, HashCombine(selectors.GetParameterNamesHash(), selectors.GetHash()));
		hash = HashCombine(pipelineLayout->GetGUID(), hash);

		std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
		bool replaceExisting = false;
		auto i = LowerBound(_computePipelines, hash);
		if (i != _computePipelines.end() && i->first == hash) {
			if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
				return i->second;
			replaceExisting = true;
		}

		auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>();
		if (replaceExisting) {
			i->second = result;
		} else
			_computePipelines.insert(i, std::make_pair(hash, result));
		lk = {};
		ConstructToFuture(*result, pipelineLayout, shader, selectors);
		return result;
	}

	std::shared_ptr<::Assets::Future<PipelinePool::ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		std::shared_ptr<::Assets::Future<RenderCore::Assets::PredefinedPipelineLayout>> futurePipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		auto hash = Hash64(shader, HashCombine(selectors.GetParameterNamesHash(), selectors.GetHash()));
		hash = HashCombine(pipelineLayout->GetGUID(), hash);

		std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
		bool replaceExisting = false;
		auto i = LowerBound(_computePipelines, hash);
		if (i != _computePipelines.end() && i->first == hash) {
			if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
				return i->second;
			replaceExisting = true;
		}

		auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>();
		if (replaceExisting) {
			i->second = result;
		} else
			_computePipelines.insert(i, std::make_pair(hash, result));
		lk = {};
		ConstructToFuture(*result, pipelineLayout, shader, selectors);
		return result;
	}

	std::shared_ptr<::Assets::Future<PipelinePool::ComputePipelineAndLayout>> PipelinePool::CreateComputePipeline(
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		auto hash = Hash64(shader, HashCombine(selectors.GetParameterNamesHash(), selectors.GetHash()));

		std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
		bool replaceExisting = false;
		auto i = LowerBound(_computePipelines, hash);
		if (i != _computePipelines.end() && i->first == hash) {
			if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
				return i->second;
			replaceExisting = true;
		}

		auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>();
		if (replaceExisting) {
			i->second = result;
		} else
			_computePipelines.insert(i, std::make_pair(hash, result));
		lk = {};
		ConstructToFuture(*result, shader, selectors);
		return result;
	}

	

	void PipelinePool::ConstructToFuture(
		::Assets::Future<ComputePipelineAndLayout>& future,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		auto filteringFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		::Assets::WhenAll(filteringFuture).ThenConstructToFuture(
			future,
			[selectorsCopy = selectors, shaderCopy=shader.AsString(), sharedPools=_sharedPools, pipelineLayout=pipelineLayout]( 
				::Assets::Future<ComputePipelineAndLayout>& resultFuture,
				std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) {

				auto configurationDepVal = ::Assets::GetDepValSys().Make();
				configurationDepVal.RegisterDependency(automaticFiltering->GetDependencyValidation());

				auto byteCodeFuture = sharedPools->MakeByteCodeFuture(
					*automaticFiltering, ShaderStage::Compute,
					shaderCopy, selectorsCopy,
					nullptr, {});

				::Assets::WhenAll(byteCodeFuture).ThenConstructToFuture(
					resultFuture,
					[pipelineLayout](auto csCode) {
						Metal::ComputeShader shaderActual{Metal::GetObjectFactory(), pipelineLayout, *csCode};
						Metal::ComputePipelineBuilder builder;
						builder.Bind(shaderActual);
						return ComputePipelineAndLayout {
							builder.CreatePipeline(Metal::GetObjectFactory()),
							pipelineLayout };
					});
			});
	}

	static std::shared_ptr<ICompiledPipelineLayout> MakeAutoPipelineLayout(
		IDevice& device,
		const CompiledShaderByteCode& byteCode)
	{
		auto initializer = Metal::BuildPipelineLayoutInitializer(byteCode);
		return device.CreatePipelineLayout(initializer);
	}

	static std::shared_ptr<ICompiledPipelineLayout> MakeAutoPipelineLayout(
		IDevice& device,
		const CompiledShaderByteCode& byteCode,
		const RenderCore::Assets::PredefinedPipelineLayout& layout)
	{
		auto autoInitializer = Metal::BuildPipelineLayoutInitializer(byteCode);
		PipelineLayoutInitializer initializer { {}, autoInitializer.GetPushConstants() };
	}

	void PipelinePool::ConstructToFuture(
		::Assets::Future<ComputePipelineAndLayout>& future,
		StringSection<> shader,
		const ParameterBox& selectors)
	{
		auto filteringFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		std::weak_ptr<IDevice> weakDevice = _device;
		::Assets::WhenAll(filteringFuture).ThenConstructToFuture(
			future,
			[selectorsCopy = selectors, shaderCopy=shader.AsString(), sharedPools=_sharedPools, weakDevice]( 
				::Assets::Future<ComputePipelineAndLayout>& resultFuture,
				std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) {

				auto configurationDepVal = ::Assets::GetDepValSys().Make();
				configurationDepVal.RegisterDependency(automaticFiltering->GetDependencyValidation());

				auto byteCodeFuture = sharedPools->MakeByteCodeFuture(
					*automaticFiltering, ShaderStage::Compute,
					shaderCopy, selectorsCopy,
					nullptr, {});

				::Assets::WhenAll(byteCodeFuture).ThenConstructToFuture(
					resultFuture,
					[weakDevice](auto csCode) {
						auto d = weakDevice.lock();
						if (!d) Throw(std::runtime_error("Device shutdown before completion"));
						auto pipelineLayout = MakeAutoPipelineLayout(*d, *csCode);
						Metal::ComputeShader shaderActual{Metal::GetObjectFactory(), pipelineLayout, *csCode};
						Metal::ComputePipelineBuilder builder;
						builder.Bind(shaderActual);
						return ComputePipelineAndLayout {
							builder.CreatePipeline(Metal::GetObjectFactory()),
							pipelineLayout };
					});
			});
	}

#endif
	static uint64_t s_nextGraphicsPipelinePoolGUID = 1;
	PipelinePool::PipelinePool(std::shared_ptr<IDevice> device)
	: _device(std::move(device))
	, _guid(s_nextGraphicsPipelinePoolGUID++)
	{
		_sharedPools = std::make_shared<Internal::SharedPools>(_device);
		// _oldSharedPools = std::make_shared<OldSharedPools>();
	}

	PipelinePool::~PipelinePool()
	{}

	PipelineLayoutOptions::PipelineLayoutOptions(std::shared_ptr<ICompiledPipelineLayout> prebuilt)
	: _prebuiltPipelineLayout(std::move(prebuilt))
	{
		_hashCode = _prebuiltPipelineLayout->GetGUID();
	}

	PipelineLayoutOptions::PipelineLayoutOptions(::Assets::PtrToFuturePtr<RenderCore::Assets::PredefinedPipelineLayout> future, uint64_t guid)
	: _predefinedPipelineLayout(std::move(future))
	, _hashCode(guid)
	{}

}}


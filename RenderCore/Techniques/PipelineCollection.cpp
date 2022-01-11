// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineCollection.h"
#include "PipelineBuilderUtil.h"
#include "RenderPass.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
	struct RetainedSelectors
	{
		std::vector<ParameterBox> _selectors;
		RetainedSelectors(IteratorRange<const ParameterBox*const*> selectors)
		{
			_selectors.reserve(selectors.size());
			for (const auto&b:selectors) _selectors.push_back(*b);
		}
	};

	void PipelineCollection::CreateComputePipeline(
		std::promise<ComputePipelineAndLayout>&& promise,
		PipelineLayoutOptions&& pipelineLayout,
		StringSection<> shader,
		IteratorRange<const ParameterBox*const*> selectors,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
		IteratorRange<const uint64_t*> patchExpansionsInit)
	{
		std::vector<std::pair<uint64_t, ShaderStage>> patchExpansions;
		if (compiledPatchCollection && !patchExpansionsInit.empty()) {
			patchExpansions.reserve(patchExpansionsInit.size());
			for (auto p:patchExpansionsInit) patchExpansions.emplace_back(p, ShaderStage::Compute);
		}
		auto filteringFuture = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		::Assets::WhenAll(filteringFuture).ThenConstructToPromise(
			std::move(promise),
			[selectorsCopy = RetainedSelectors{selectors}, shaderCopy=shader.AsString(), sharedPools=_sharedPools, pipelineLayout=std::move(pipelineLayout), patchExpansions=std::move(patchExpansions), compiledPatchCollection]( 
				std::promise<ComputePipelineAndLayout>&& promise,
				std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) mutable {

				TRY {
					const ParameterBox* selectorsList[selectorsCopy._selectors.size()];
					for (unsigned c=0; c<selectorsCopy._selectors.size(); ++c)
						selectorsList[c] = &selectorsCopy._selectors[c];
					ScopedLock(sharedPools->_lock);
					auto filteredSelectors = sharedPools->FilterSelectorsAlreadyLocked(
						ShaderStage::Compute, MakeIteratorRange(selectorsList, &selectorsList[selectorsCopy._selectors.size()]), *automaticFiltering, 
						{}, nullptr, compiledPatchCollection, patchExpansions);
					auto chainedFuture = sharedPools->CreateComputePipelineAlreadyLocked(shaderCopy, std::move(pipelineLayout), compiledPatchCollection, patchExpansions, filteredSelectors);
					::Assets::WhenAll(chainedFuture).ThenConstructToPromise(std::move(promise));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	static ::Assets::DependencyValidation MakeConfigurationDepVal(const Internal::GraphicsPipelineDescWithFilteringRules& pipelineDescWithFiltering)
	{
		auto* pipelineDesc = pipelineDescWithFiltering._pipelineDesc.get();
		std::vector<::Assets::DependencyValidationMarker> dependencies;
		if (pipelineDesc->GetDependencyValidation())
			dependencies.push_back(pipelineDesc->GetDependencyValidation());
		/*if (compiledPatchCollection && compiledPatchCollection->GetDependencyValidation())		should be included naturally
			configurationDepVal.RegisterDependency(compiledPatchCollection->GetDependencyValidation());*/
		for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
			if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering._automaticFiltering[c])
				dependencies.push_back(pipelineDescWithFiltering._automaticFiltering[c]->GetDependencyValidation());
		if (pipelineDescWithFiltering._preconfiguration)
			dependencies.push_back(pipelineDescWithFiltering._preconfiguration->GetDependencyValidation());
		return ::Assets::GetDepValSys().Make(dependencies);
	}

	static GraphicsPipelineAndLayout MergeDepVal(const GraphicsPipelineAndLayout& src, const ::Assets::DependencyValidation& cfgDepVal)
	{
		GraphicsPipelineAndLayout result = src;
		result._depVal = ::Assets::GetDepValSys().Make();
		result._depVal.RegisterDependency(src._depVal);
		result._depVal.RegisterDependency(cfgDepVal);
		return result;
	}

	void PipelineCollection::CreateGraphicsPipelineInternal(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		PipelineLayoutOptions&& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFilteringFuture,
		IteratorRange<const ParameterBox*const*> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		auto result = std::make_shared<::Assets::Marker<GraphicsPipelineAndLayout>>("graphics-pipeline");
		::Assets::WhenAll(pipelineDescWithFilteringFuture).ThenConstructToPromise(
			std::move(promise),
			[sharedPools=_sharedPools, selectorsCopy=RetainedSelectors{selectors}, pipelineLayout=std::move(pipelineLayout), compiledPatchCollection,
				inputAssembly=Internal::AsVector(inputStates._inputAssembly), miniInputAssembly=Internal::AsVector(inputStates._miniInputAssembly), topology=inputStates._topology,
				fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx](

				std::promise<GraphicsPipelineAndLayout>&& promise,
				auto pipelineDescWithFiltering) mutable {
					
				TRY {
					auto cfgDepVal = MakeConfigurationDepVal(*pipelineDescWithFiltering);

					ScopedLock(sharedPools->_lock);
					UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];
					const ParameterBox* selectorsList[selectorsCopy._selectors.size()];
					for (unsigned c=0; c<selectorsCopy._selectors.size(); ++c)
						selectorsList[c] = &selectorsCopy._selectors[c];

					auto* pipelineDesc = pipelineDescWithFiltering->_pipelineDesc.get();
					for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
						if (!pipelineDesc->_shaders[c].empty())
							filteredSelectors[c] = sharedPools->FilterSelectorsAlreadyLocked(
								(ShaderStage)c,
								MakeIteratorRange(selectorsList, &selectorsList[selectorsCopy._selectors.size()]),
								*pipelineDescWithFiltering->_automaticFiltering[c],
								pipelineDesc->_manualSelectorFiltering,
								pipelineDescWithFiltering->_preconfiguration.get(),
								compiledPatchCollection,
								pipelineDesc->_patchExpansions);

					auto chainFuture = sharedPools->CreateGraphicsPipelineAlreadyLocked(
						VertexInputStates{inputAssembly, miniInputAssembly, topology}, pipelineDescWithFiltering,
						std::move(pipelineLayout), compiledPatchCollection,
						filteredSelectors, {&fbDesc, spIdx});
					::Assets::WhenAll(chainFuture).ThenConstructToPromise(
						std::move(promise),
						[cfgDepVal](auto chainActual) { return MergeDepVal(chainActual, cfgDepVal); });
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	void PipelineCollection::CreateGraphicsPipeline(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		PipelineLayoutOptions&& pipelineLayout,
		const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
		IteratorRange<const ParameterBox*const*> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		CreateGraphicsPipelineInternal(
			std::move(promise),
			std::move(pipelineLayout), 
			Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDesc),
			selectors, inputStates, fbTarget, compiledPatchCollection);
	}

	void PipelineCollection::CreateGraphicsPipeline(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		PipelineLayoutOptions&& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc>& pipelineDescFuture,
		IteratorRange<const ParameterBox*const*> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		CreateGraphicsPipelineInternal(
			std::move(promise),
			std::move(pipelineLayout), 
			Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDescFuture),
			selectors, inputStates, fbTarget, compiledPatchCollection);
	}

	PipelineCollection::Metrics PipelineCollection::GetMetrics() const
	{
		PipelineCollection::Metrics result;
		ScopedLock(_sharedPools->_lock);
		result._graphicsPipelineCount = _sharedPools->_pendingGraphicsPipelines.size();
		for (auto& p:_sharedPools->_completedGraphicsPipelines)
			if (!p.second._pipeline.expired())
				++result._graphicsPipelineCount;
		result._computePipelineCount = _sharedPools->_pendingComputePipelines.size();
		for (auto& p:_sharedPools->_completedComputePipelines)
			if (!p.second._pipeline.expired())
				++result._computePipelineCount;
		return result;
	}

	static uint64_t s_nextGraphicsPipelinePoolGUID = 1;
	PipelineCollection::PipelineCollection(std::shared_ptr<IDevice> device)
	: _device(std::move(device))
	, _guid(s_nextGraphicsPipelinePoolGUID++)
	{
		_sharedPools = std::make_shared<Internal::SharedPools>(_device);
	}

	PipelineCollection::~PipelineCollection()
	{}

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

	PipelineLayoutOptions::PipelineLayoutOptions(std::shared_ptr<ICompiledPipelineLayout> prebuilt)
	: _prebuiltPipelineLayout(std::move(prebuilt))
	{
		_hashCode = _prebuiltPipelineLayout->GetGUID();
	}

	PipelineLayoutOptions::PipelineLayoutOptions(std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> future, uint64_t guid)
	: _predefinedPipelineLayout(std::move(future))
	, _hashCode(guid)
	{}

}}


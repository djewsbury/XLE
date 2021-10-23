// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineCollection.h"
#include "PipelineBuilderUtil.h"
#include "RenderPass.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
	struct RetainedSelectors
	{
		std::vector<ParameterBox> _selectors;
		RetainedSelectors(IteratorRange<const ParameterBox**> selectors)
		{
			_selectors.reserve(selectors.size());
			for (const auto&b:selectors) _selectors.push_back(*b);
		}
	};

	std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> PipelineCollection::CreateComputePipeline(
		const PipelineLayoutOptions& pipelineLayout,
		StringSection<> shader,
		IteratorRange<const ParameterBox**> selectors)
	{
		// accelerated return when the filtering rules are already available
		auto filteringFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		if (filteringFuture->GetAssetState() == ::Assets::AssetState::Ready) {
			ScopedLock(_sharedPools->_lock);

			auto filteredSelectors = _sharedPools->FilterSelectorsAlreadyLocked(
				ShaderStage::Compute, selectors, *filteringFuture->Actualize(), 
				{}, nullptr, nullptr, {});
			return _sharedPools->CreateComputePipelineAlreadyLocked(shader, pipelineLayout, filteredSelectors);
		} else {
			auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>(shader.AsString());
			::Assets::WhenAll(filteringFuture).ThenConstructToFuture(
				*result,
				[selectorsCopy = RetainedSelectors{selectors}, shaderCopy=shader.AsString(), sharedPools=_sharedPools, pipelineLayout]( 
					::Assets::Future<ComputePipelineAndLayout>& resultFuture,
					std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) {

					const ParameterBox* selectorsList[selectorsCopy._selectors.size()];
					for (unsigned c=0; c<selectorsCopy._selectors.size(); ++c)
						selectorsList[c] = &selectorsCopy._selectors[c];
					auto filteredSelectors = sharedPools->FilterSelectorsAlreadyLocked(
						ShaderStage::Compute, MakeIteratorRange(selectorsList, &selectorsList[selectorsCopy._selectors.size()]), *automaticFiltering, 
						{}, nullptr, nullptr, {});
					auto chainedFuture = sharedPools->CreateComputePipelineAlreadyLocked(shaderCopy, pipelineLayout, filteredSelectors);
					::Assets::WhenAll(chainedFuture).ThenConstructToFuture(resultFuture);
				});
			return result;
		}	
	}

	static ::Assets::DependencyValidation MakeConfigurationDepVal(const Internal::GraphicsPipelineDescWithFilteringRules& pipelineDescWithFiltering)
	{
		auto* pipelineDesc = pipelineDescWithFiltering._pipelineDesc.get();
		auto configurationDepVal = ::Assets::GetDepValSys().Make();
		if (pipelineDesc->GetDependencyValidation())
			configurationDepVal.RegisterDependency(pipelineDesc->GetDependencyValidation());
		/*if (compiledPatchCollection && compiledPatchCollection->GetDependencyValidation())		should be included naturally
			configurationDepVal.RegisterDependency(compiledPatchCollection->GetDependencyValidation());*/
		for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
			if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering._automaticFiltering[c])
				configurationDepVal.RegisterDependency(pipelineDescWithFiltering._automaticFiltering[c]->GetDependencyValidation());
		if (pipelineDescWithFiltering._preconfiguration)
			configurationDepVal.RegisterDependency(pipelineDescWithFiltering._preconfiguration->GetDependencyValidation());
		return configurationDepVal;
	}

	static GraphicsPipelineAndLayout MergeDepVal(const GraphicsPipelineAndLayout& src, const ::Assets::DependencyValidation& cfgDepVal)
	{
		GraphicsPipelineAndLayout result = src;
		result._depVal = ::Assets::GetDepValSys().Make();
		result._depVal.RegisterDependency(src._depVal);
		result._depVal.RegisterDependency(cfgDepVal);
		return result;
	}

	std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipelineInternal(
		const PipelineLayoutOptions& pipelineLayout,
		const ::Assets::PtrToFuturePtr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFilteringFuture,
		IteratorRange<const ParameterBox**> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		auto result = std::make_shared<::Assets::Future<GraphicsPipelineAndLayout>>("graphics-pipeline");
		if (pipelineDescWithFilteringFuture->GetAssetState() == ::Assets::AssetState::Ready) {
			auto pipelineDescWithFiltering = pipelineDescWithFilteringFuture->Actualize();
			auto* pipelineDesc = pipelineDescWithFiltering->_pipelineDesc.get();

			auto cfgDepVal = MakeConfigurationDepVal(*pipelineDescWithFiltering);

			ScopedLock(_sharedPools->_lock);
			UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];

			for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
				if (!pipelineDesc->_shaders[c].empty())
					filteredSelectors[c] = _sharedPools->FilterSelectorsAlreadyLocked(
						(ShaderStage)c,
						selectors,
						*pipelineDescWithFiltering->_automaticFiltering[c],
						pipelineDesc->_manualSelectorFiltering,
						pipelineDescWithFiltering->_preconfiguration.get(),
						compiledPatchCollection,
						pipelineDesc->_patchExpansions);
			auto chainFuture = _sharedPools->CreateGraphicsPipelineAlreadyLocked(
				inputStates, pipelineDescWithFiltering,
				pipelineLayout, compiledPatchCollection,
				filteredSelectors, fbTarget);
			::Assets::WhenAll(chainFuture).ThenConstructToFuture(
				*result,
				[cfgDepVal](auto chainActual) { return MergeDepVal(chainActual, cfgDepVal); });
		} else {
			::Assets::WhenAll(pipelineDescWithFilteringFuture).ThenConstructToFuture(
				*result,
				[sharedPools=_sharedPools, selectorsCopy=RetainedSelectors{selectors}, pipelineLayout, compiledPatchCollection,
					inputAssembly=Internal::AsVector(inputStates._inputAssembly), miniInputAssembly=Internal::AsVector(inputStates._miniInputAssembly), topology=inputStates._topology,
			 		fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx](

					::Assets::Future<GraphicsPipelineAndLayout>& resultFuture,
					auto pipelineDescWithFiltering) {
						
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
						pipelineLayout, compiledPatchCollection,
						filteredSelectors, {&fbDesc, spIdx});
					::Assets::WhenAll(chainFuture).ThenConstructToFuture(
						resultFuture,
						[cfgDepVal](auto chainActual) { return MergeDepVal(chainActual, cfgDepVal); });
				});
		}
		return result;
	}

	std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipeline(
		const PipelineLayoutOptions& pipelineLayout,
		const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
		IteratorRange<const ParameterBox**> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		return CreateGraphicsPipelineInternal(
			pipelineLayout, 
			Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDesc),
			selectors, inputStates, fbTarget, compiledPatchCollection);
	}

	std::shared_ptr<::Assets::Future<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipeline(
		const PipelineLayoutOptions& pipelineLayout,
		const ::Assets::PtrToFuturePtr<GraphicsPipelineDesc>& pipelineDescFuture,
		IteratorRange<const ParameterBox**> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		return CreateGraphicsPipelineInternal(
			pipelineLayout, 
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

	PipelineLayoutOptions::PipelineLayoutOptions(::Assets::PtrToFuturePtr<RenderCore::Assets::PredefinedPipelineLayout> future, uint64_t guid)
	: _predefinedPipelineLayout(std::move(future))
	, _hashCode(guid)
	{}

}}


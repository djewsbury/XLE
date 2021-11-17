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
		RetainedSelectors(IteratorRange<const ParameterBox**> selectors)
		{
			_selectors.reserve(selectors.size());
			for (const auto&b:selectors) _selectors.push_back(*b);
		}
	};

	std::shared_ptr<::Assets::Marker<ComputePipelineAndLayout>> PipelineCollection::CreateComputePipeline(
		const PipelineLayoutOptions& pipelineLayout,
		StringSection<> shader,
		IteratorRange<const ParameterBox**> selectors)
	{
		auto result = std::make_shared<::Assets::Marker<ComputePipelineAndLayout>>(shader.AsString());
		auto filteringFuture = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorFilteringRules>(MakeFileNameSplitter(shader).AllExceptParameters());
		::Assets::WhenAll(filteringFuture).ThenConstructToPromise(
			result->AdoptPromise(),
			[selectorsCopy = RetainedSelectors{selectors}, shaderCopy=shader.AsString(), sharedPools=_sharedPools, pipelineLayout]( 
				std::promise<ComputePipelineAndLayout>&& promise,
				std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> automaticFiltering) {

				TRY {
					const ParameterBox* selectorsList[selectorsCopy._selectors.size()];
					for (unsigned c=0; c<selectorsCopy._selectors.size(); ++c)
						selectorsList[c] = &selectorsCopy._selectors[c];
					ScopedLock(sharedPools->_lock);
					auto filteredSelectors = sharedPools->FilterSelectorsAlreadyLocked(
						ShaderStage::Compute, MakeIteratorRange(selectorsList, &selectorsList[selectorsCopy._selectors.size()]), *automaticFiltering, 
						{}, nullptr, nullptr, {});
					auto chainedFuture = sharedPools->CreateComputePipelineAlreadyLocked(shaderCopy, pipelineLayout, filteredSelectors);
					::Assets::WhenAll(chainedFuture).ThenConstructToPromise(std::move(promise));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
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

	std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipelineInternal(
		const PipelineLayoutOptions& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFilteringFuture,
		IteratorRange<const ParameterBox**> selectors,
		const VertexInputStates& inputStates,
		const FrameBufferTarget& fbTarget,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection)
	{
		auto result = std::make_shared<::Assets::Marker<GraphicsPipelineAndLayout>>("graphics-pipeline");
		::Assets::WhenAll(pipelineDescWithFilteringFuture).ThenConstructToPromise(
			result->AdoptPromise(),
			[sharedPools=_sharedPools, selectorsCopy=RetainedSelectors{selectors}, pipelineLayout, compiledPatchCollection,
				inputAssembly=Internal::AsVector(inputStates._inputAssembly), miniInputAssembly=Internal::AsVector(inputStates._miniInputAssembly), topology=inputStates._topology,
				fbDesc=*fbTarget._fbDesc, spIdx=fbTarget._subpassIdx](

				std::promise<GraphicsPipelineAndLayout>&& promise,
				auto pipelineDescWithFiltering) {
					
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
						pipelineLayout, compiledPatchCollection,
						filteredSelectors, {&fbDesc, spIdx});
					::Assets::WhenAll(chainFuture).ThenConstructToPromise(
						std::move(promise),
						[cfgDepVal](auto chainActual) { return MergeDepVal(chainActual, cfgDepVal); });
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
		return result;
	}

	std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipeline(
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

	std::shared_ptr<::Assets::Marker<GraphicsPipelineAndLayout>> PipelineCollection::CreateGraphicsPipeline(
		const PipelineLayoutOptions& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc>& pipelineDescFuture,
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

	PipelineLayoutOptions::PipelineLayoutOptions(::Assets::PtrToMarkerPtr<RenderCore::Assets::PredefinedPipelineLayout> future, uint64_t guid)
	: _predefinedPipelineLayout(std::move(future))
	, _hashCode(guid)
	{}

}}


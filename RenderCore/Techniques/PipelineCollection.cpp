// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineCollection.h"
#include "PipelineAcceleratorInternal.h"
#include "ShaderVariationSet.h"
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

	uint64_t PixelOutputStates::GetHash() const
	{
		/*assert(_attachmentBlend.size() == _fbTarget._fbDesc->GetSubpasses()[_fbTarget._subpassIdx].GetOutputs().size());
		uint64_t renderPassRelevance = _fbTarget.GetHash();
		auto result = HashCombine(_depthStencil.HashDepthAspect() ^ _depthStencil.HashStencilAspect(), renderPassRelevance);
		result = HashCombine(_rasterization.Hash(), result);
		for (const auto& a:_attachmentBlend)
			result = HashCombine(a.Hash(), result);
		return result;*/
		return _fbTarget.GetHash();
	}

	uint64_t VertexInputStates::GetHash() const
	{
		auto seed = DefaultSeed64;
		lrot(seed, (int)_topology);
		return HashInputAssembly(_inputLayout, seed);
	}

	class GraphicsPipelinePool::SharedPools
	{
	public:
		Threading::Mutex _lock;
		UniqueShaderVariationSet _selectorVariationsSet;
	};

	template<typename Type>
		std::vector<Type> AsVector(IteratorRange<const Type*> range) { return std::vector<Type>{range.begin(), range.end()}; }

	::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> GraphicsPipelinePool::CreatePipeline(
		const GraphicsPipelineDesc& pipelineDesc,
		const ParameterBox& selectors,
		const VertexInputStates& inputStates,
		const PixelOutputStates& outputStates)
	{
		auto hash = HashCombine(inputStates.GetHash(), outputStates.GetHash());
		hash = HashCombine(pipelineDesc.GetHash(), hash);
		hash = HashCombine(selectors.GetParameterNamesHash(), hash);
		hash = HashCombine(selectors.GetHash(), hash);

		std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
		bool replaceExisting = false;
		auto i = LowerBound(_pipelines, hash);
		if (i != _pipelines.end() && i->first == hash) {
			if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
				return i->second;
			replaceExisting = true;
		}

		auto result = std::make_shared<::Assets::FuturePtr<Metal::GraphicsPipeline>>();
		if (replaceExisting) {
			i->second = result;
		} else
			_pipelines.insert(i, std::make_pair(hash, result));
		lk = {};
		ConstructToFuture(result, pipelineDesc, selectors, inputStates, outputStates);
		return result;
	}

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

	void GraphicsPipelinePool::ConstructToFuture(
		std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
		const GraphicsPipelineDesc& pipelineDescInit,
		const ParameterBox& selectors,
		const VertexInputStates& inputStates,
		const PixelOutputStates& outputStates)
	{
		auto resolvedTechnique = Internal::GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDescInit);
		::Assets::WhenAll(resolvedTechnique).ThenConstructToFuture(
			*future,
			[selectorsCopy = selectors, pipelineDesc=pipelineDescInit, sharedPools=_sharedPools, 
			 pipelineLayout=_pipelineLayout, pipelineLayoutDepVal=_pipelineLayoutDepVal,
			 inputAssembly=AsVector(inputStates._inputLayout), topology=inputStates._topology,
			 fbDesc=*outputStates._fbTarget._fbDesc, spIdx=outputStates._fbTarget._subpassIdx]( 
				::Assets::FuturePtr<Metal::GraphicsPipeline>& resultFuture,
				std::shared_ptr<Internal::GraphicsPipelineDescWithFilteringRules> pipelineDescWithFiltering) {

				UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(GraphicsPipelineDesc::_shaders)];

				const ParameterBox* paramBoxes[] = {
					&selectorsCopy
				};

				ScopedLock(sharedPools->_lock);
				for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
					if (!pipelineDesc._shaders[c].empty()) {
						const ShaderSourceParser::SelectorFilteringRules* autoFiltering[] = {
							pipelineDescWithFiltering->_automaticFiltering[c].get()
						};
						filteredSelectors[c] = sharedPools->_selectorVariationsSet.FilterSelectors(
							MakeIteratorRange(paramBoxes),
							pipelineDesc._manualSelectorFiltering,
							MakeIteratorRange(autoFiltering),
							pipelineDescWithFiltering->_preconfiguration.get());
					}

				auto configurationDepVal = ::Assets::GetDepValSys().Make();
				if (pipelineDesc.GetDependencyValidation())
					configurationDepVal.RegisterDependency(pipelineDesc.GetDependencyValidation());
				for (unsigned c=0; c<dimof(GraphicsPipelineDesc::_shaders); ++c)
					if (!pipelineDesc._shaders[c].empty() && pipelineDescWithFiltering->_automaticFiltering[c])
						configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_automaticFiltering[c]->GetDependencyValidation());
				if (pipelineDescWithFiltering->_preconfiguration)
					configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_preconfiguration->GetDependencyValidation());
				if (pipelineLayoutDepVal)
					configurationDepVal.RegisterDependency(pipelineLayoutDepVal);

				// todo -- now that we have the filtered selectors, we could attempt to reuse an existing pipeline (if one exists with
				// the same filtered selectors)

				auto shaderProgram = Internal::MakeShaderProgram(pipelineDesc, pipelineLayout, nullptr, MakeIteratorRange(filteredSelectors));
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
						return builder.CreatePipeline(Metal::GetObjectFactory());
					});
			});		
	}
	
	uint64_t GraphicsPipelinePool::GetGUID() const
	{
		return _pipelineLayout->GetGUID();
	}

	GraphicsPipelinePool::GraphicsPipelinePool(
		std::shared_ptr<IDevice> device,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		const ::Assets::DependencyValidation& pipelineLayoutDepVal)
	: _device(std::move(device)), _pipelineLayout(std::move(pipelineLayout)), _pipelineLayoutDepVal(pipelineLayoutDepVal) 
	{
		_sharedPools = std::make_shared<SharedPools>();
	}

	GraphicsPipelinePool::~GraphicsPipelinePool()
	{}

}}


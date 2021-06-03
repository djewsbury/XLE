// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineAccelerator.h"
#include "DescriptorSetAccelerator.h"
#include "CompiledShaderPatchCollection.h"
#include "CommonResources.h"
#include "ShaderVariationSet.h"
#include "../FrameBufferDesc.h"
#include "../Format.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/ObjectFactory.h"
#include "../Assets/MaterialScaffold.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"
#include <cctype>

#include "Techniques.h"
#include "TechniqueDelegates.h"

namespace RenderCore { namespace Techniques
{
	using SequencerConfigId = uint64_t;

	class SequencerConfig
	{
	public:
		SequencerConfigId _cfgId = ~0ull;

		std::shared_ptr<ITechniqueDelegate> _delegate;
		ParameterBox _sequencerSelectors;

		FrameBufferDesc _fbDesc;
		unsigned _subpassIdx = 0;
		uint64_t _fbRelevanceValue = 0;
	};

	class SharedPools
	{
	public:
		Threading::Mutex _lock;
		UniqueShaderVariationSet _selectorVariationsSet;
		::Assets::PtrToFuturePtr<CompiledShaderPatchCollection> _emptyPatchCollection;
	};

	class PipelineAccelerator : public std::enable_shared_from_this<PipelineAccelerator>
	{
	public:
		PipelineAccelerator(
			unsigned ownerPoolId,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const InputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet);
		PipelineAccelerator(
			unsigned ownerPoolId,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet);
		~PipelineAccelerator();
	
		struct Pipeline
		{
			::Assets::PtrToFuturePtr<IPipelineAcceleratorPool::Pipeline> _future;
		};
		std::vector<Pipeline> _finalPipelines;

		Pipeline CreatePipelineForSequencerState(
			const SequencerConfig& cfg,
			const ParameterBox& globalSelectors,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const DescriptorSetLayoutAndBinding& matDescSetLayout,
			const std::shared_ptr<SharedPools>& sharedPools);

		Pipeline& PipelineForCfgId(SequencerConfigId cfgId);

		std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _shaderPatches;
		ParameterBox _materialSelectors;
		ParameterBox _geoSelectors;

		std::vector<InputElementDesc> _inputAssembly;
		std::vector<MiniInputElementDesc> _miniInputAssembly;
		Topology _topology;
		RenderCore::Assets::RenderStateSet _stateSet;

		unsigned _ownerPoolId;

		std::shared_ptr<Metal::GraphicsPipeline> InternalCreatePipeline(
			const Metal::ShaderProgram& shader,
			const DepthStencilDesc& depthStencil,
			IteratorRange<const AttachmentBlendDesc*> attachmentBlends,
			const RasterizationDesc& rasterization,
			const SequencerConfig& sequencerCfg)
		{
			Metal::GraphicsPipelineBuilder builder;
			builder.Bind(shader);
			builder.Bind(attachmentBlends);
			builder.Bind(depthStencil);
			builder.Bind(rasterization);

			if (!_inputAssembly.empty()) {
				Metal::BoundInputLayout ia(MakeIteratorRange(_inputAssembly), shader);
				builder.Bind(ia, _topology);
			} else {
				Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(_miniInputAssembly), 0 };
				Metal::BoundInputLayout ia(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
				builder.Bind(ia, _topology);
			}

			builder.SetRenderPassConfiguration(sequencerCfg._fbDesc, sequencerCfg._subpassIdx);

			return builder.CreatePipeline(Metal::GetObjectFactory());
		}
	};

	static std::string BuildSODefinesString(IteratorRange<const RenderCore::InputElementDesc*> desc)
	{
		std::stringstream str;
		str << "SO_OFFSETS=";
		bool first = true;
		for (const auto&e:desc) {
			if (!first) str << ",";
			first = false;
			assert(e._alignedByteOffset != ~0x0u);		// we should have called NormalizeInputAssembly before hand
			str << Hash64(e._semanticName) + e._semanticIndex << "," << e._alignedByteOffset;
		}
		return str.str();
	}

	static ::Assets::PtrToFuturePtr<CompiledShaderByteCode_InstantiateShaderGraph> MakeByteCodeFuture(
		ShaderStage stage, StringSection<> initializer, const std::string& definesTable,
		const std::shared_ptr<CompiledShaderPatchCollection>& patchCollection,
		IteratorRange<const uint64_t*> patchExpansions,
		StreamOutputInitializers so)
	{
		assert(!initializer.IsEmpty());

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
			if (!XlFindStringI(initializer, profileStr)) {
				meld << ":" << profileStr << "*";
			}
		}

		std::vector<uint64_t> patchExpansionsCopy(patchExpansions.begin(), patchExpansions.end());

		auto adjustedDefinesTable = definesTable;
		if (stage == ShaderStage::Geometry && !so._outputElements.empty()) {
			if (!definesTable.empty()) adjustedDefinesTable += ";";
			adjustedDefinesTable += BuildSODefinesString(so._outputElements);
		}

		return ::Assets::MakeAsset<CompiledShaderByteCode_InstantiateShaderGraph>(
			MakeStringSection(temp), adjustedDefinesTable, patchCollection, patchExpansionsCopy);
	}

	static ::Assets::PtrToFuturePtr<Metal::ShaderProgram> MakeShaderProgram(
		const ITechniqueDelegate::GraphicsPipelineDesc& pipelineDesc,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
		IteratorRange<const UniqueShaderVariationSet::FilteredSelectorSet*> filteredSelectors,
		StreamOutputInitializers so)
	{
		::Assets::PtrToFuturePtr<CompiledShaderByteCode_InstantiateShaderGraph> byteCodeFuture[3];

		for (unsigned c=0; c<3; ++c) {
			if (pipelineDesc._shaders[c].empty())
				continue;

			byteCodeFuture[c] = MakeByteCodeFuture(
				(ShaderStage)c,
				pipelineDesc._shaders[c],
				filteredSelectors[c]._selectors,
				compiledPatchCollection,
				pipelineDesc._patchExpansions,
				so);
		}

		if (!byteCodeFuture[(unsigned)ShaderStage::Vertex])
			Throw(std::runtime_error("Missing vertex shader stage while building shader program"));

		auto result = std::make_shared<::Assets::FuturePtr<Metal::ShaderProgram>>();
		if (byteCodeFuture[(unsigned)ShaderStage::Pixel] && !byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Pixel]).ThenConstructToFuture(
				*result,
				[pipelineLayout](
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> vsCode, 
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> psCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *psCode);
				});
		} else if (byteCodeFuture[(unsigned)ShaderStage::Pixel] && byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			auto soElements = pipelineDesc._soElements;
			auto soBufferStrides = pipelineDesc._soBufferStrides;
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Pixel], byteCodeFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
				*result,
				[pipelineLayout, soElements, soBufferStrides](
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> vsCode, 
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> psCode,
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> gsCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *gsCode, *psCode,
						StreamOutputInitializers{soElements, soBufferStrides});
				});
		} else if (!byteCodeFuture[(unsigned)ShaderStage::Pixel] && byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			auto soElements = pipelineDesc._soElements;
			auto soBufferStrides = pipelineDesc._soBufferStrides;
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
				*result,
				[pipelineLayout, soElements, soBufferStrides](
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> vsCode, 
					std::shared_ptr<CompiledShaderByteCode_InstantiateShaderGraph> gsCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *gsCode, CompiledShaderByteCode{},
						StreamOutputInitializers{soElements, soBufferStrides});
				});
		} else
			Throw(std::runtime_error("Missing shader stages while building shader program"));
		return result;
	}

#if defined(_DEBUG)
	static std::ostream& CompressFilename(std::ostream& str, StringSection<> path)
	{
		auto split = MakeFileNameSplitter(path);
		if (!split.DriveAndPath().IsEmpty()) {
			return str << ".../" << split.FileAndExtension();
		} else
			return str << path;
	}

	static std::string MakeShaderDescription(
		ShaderStage stage,
		const ITechniqueDelegate::GraphicsPipelineDesc& pipelineDesc,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
 		const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors)
	{
		if (pipelineDesc._shaders[(unsigned)stage].empty())
			return {};

		std::stringstream str;
		const char* stageName[] = { "vs", "ps", "gs" };
		bool first = true;
		if (!first) str << ", "; first = false;
		str << stageName[(unsigned)stage] << ": ";
		CompressFilename(str, pipelineDesc._shaders[(unsigned)stage]);
		for (const auto& patch:compiledPatchCollection->GetInterface().GetPatches()) {
			if (!first) str << ", "; first = false;
			str << "patch: " << patch._entryPointName;
		}
		str << "[" << filteredSelectors._selectors << "]";
		return str.str();
	}
#endif

	class GraphicsPipelineDescWithFilteringRules
	{
	public:
		std::shared_ptr<ITechniqueDelegate::GraphicsPipelineDesc> _pipelineDesc;
		std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> _automaticFiltering[3];
		std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> _preconfiguration;

		static ::Assets::PtrToFuturePtr<GraphicsPipelineDescWithFilteringRules> CreateFuture(
			const ::Assets::PtrToFuturePtr<ITechniqueDelegate::GraphicsPipelineDesc>& pipelineDescFuture)
		{
			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>>(pipelineDescFuture->Initializer());
			::Assets::WhenAll(pipelineDescFuture).ThenConstructToFuture(
				*result,
				[](	::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>& resultFuture,
					std::shared_ptr<ITechniqueDelegate::GraphicsPipelineDesc> pipelineDesc) {

					static_assert(std::is_same_v<decltype(resultFuture), ::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>&>);

					::Assets::PtrToFuturePtr<ShaderSourceParser::SelectorFilteringRules> filteringFuture[3];
					for (unsigned c=0; c<3; ++c) {
						auto fn = MakeFileNameSplitter(pipelineDesc->_shaders[c]).AllExceptParameters();
						if (!fn.IsEmpty())
							filteringFuture[c] = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(fn);
					}

					if (!filteringFuture[(unsigned)ShaderStage::Vertex])
						Throw(std::runtime_error("Missing vertex shader stage while building filtering rules"));

					if (filteringFuture[(unsigned)ShaderStage::Pixel] && !filteringFuture[(unsigned)ShaderStage::Geometry]) {

						if (pipelineDesc->_selectorPreconfigurationFile.empty()) {
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel]).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
									return finalObject;
								});
						} else {
							auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_selectorPreconfigurationFile);
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], preconfigurationFuture).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
									finalObject->_preconfiguration = preconfiguration;
									return finalObject;
								});
						}

					} else if (filteringFuture[(unsigned)ShaderStage::Pixel] && filteringFuture[(unsigned)ShaderStage::Geometry]) {

						if (pipelineDesc->_selectorPreconfigurationFile.empty()) {
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
									return finalObject;
								});
						} else {
							auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_selectorPreconfigurationFile);
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
									finalObject->_preconfiguration = preconfiguration;
									return finalObject;
								});
						}

					} else if (!filteringFuture[(unsigned)ShaderStage::Pixel] && filteringFuture[(unsigned)ShaderStage::Geometry]) {

						if (pipelineDesc->_selectorPreconfigurationFile.empty()) {
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
									return finalObject;
								});
						} else {
							auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_selectorPreconfigurationFile);
							::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).ThenConstructToFuture(
								resultFuture,
								[pipelineDesc](
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
									std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
									
									auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
									finalObject->_pipelineDesc = pipelineDesc;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
									finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
									finalObject->_preconfiguration = preconfiguration;
									return finalObject;
								});
						}

					} else
						Throw(std::runtime_error("Missing shader stages while building filtering rules"));

				});
			return result;
		}
	};

	auto PipelineAccelerator::CreatePipelineForSequencerState(
		const SequencerConfig& cfg,
		const ParameterBox& globalSelectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const std::shared_ptr<SharedPools>& sharedPools) -> Pipeline
	{
		Pipeline result;
		result._future = std::make_shared<::Assets::FuturePtr<IPipelineAcceleratorPool::Pipeline>>("PipelineAccelerator Pipeline");
		ParameterBox copyGlobalSelectors = globalSelectors;
		std::weak_ptr<PipelineAccelerator> weakThis = shared_from_this();

		::Assets::PtrToFuturePtr<CompiledShaderPatchCollection> patchCollectionFuture;
		if (_shaderPatches) {
			patchCollectionFuture = ::Assets::MakeAsset<CompiledShaderPatchCollection>(*_shaderPatches, matDescSetLayout);
		} else {
			patchCollectionFuture = sharedPools->_emptyPatchCollection;
		}

		// Queue massive chain of future continuation functions (it's not as scary as it looks)
		//
		//    CompiledShaderPatchCollection -> GraphicsPipelineDesc -> Metal::GraphicsPipeline
		//
		// Note there may be an issue here in that if the shader compile fails, the dep val for the 
		// final pipeline will only contain the dependencies for the shader. So if the root problem
		// is actually something about the configuration, we won't get the proper recompile functionality 
		::Assets::WhenAll(patchCollectionFuture).ThenConstructToFuture(
			*result._future,
			[sharedPools, pipelineLayout, copyGlobalSelectors, cfg, weakThis](
				::Assets::FuturePtr<IPipelineAcceleratorPool::Pipeline>& resultFuture,
				std::shared_ptr<CompiledShaderPatchCollection> compiledPatchCollection) {

				auto containingPipelineAccelerator = weakThis.lock();
				if (!containingPipelineAccelerator)
					Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

				// we will actually begin a "GraphicsPipelineDescWithFilteringRules" future here, which
				// will complete to the pipeline desc + automatic shader filtering rules
				auto pipelineDescFuture = cfg._delegate->Resolve(compiledPatchCollection->GetInterface(), containingPipelineAccelerator->_stateSet);
				auto resolvedTechnique = GraphicsPipelineDescWithFilteringRules::CreateFuture(pipelineDescFuture);
				
				::Assets::WhenAll(resolvedTechnique).ThenConstructToFuture(
					resultFuture,
					[sharedPools, pipelineLayout, copyGlobalSelectors, cfg, weakThis, compiledPatchCollection](
						::Assets::FuturePtr<IPipelineAcceleratorPool::Pipeline>& resultFuture,
						std::shared_ptr<GraphicsPipelineDescWithFilteringRules> pipelineDescWithFiltering) {

						const auto& pipelineDesc = pipelineDescWithFiltering->_pipelineDesc;
						UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors[dimof(ITechniqueDelegate::GraphicsPipelineDesc::_shaders)];

						auto containingPipelineAccelerator = weakThis.lock();
						if (!containingPipelineAccelerator)
							Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

						{
							// The list here defines the override order. Note that the global settings are last
							// because they can actually override everything
							const ParameterBox* paramBoxes[] = {
								&cfg._sequencerSelectors,
								&containingPipelineAccelerator->_geoSelectors,
								&containingPipelineAccelerator->_materialSelectors,
								&copyGlobalSelectors
							};
							
							ScopedLock(sharedPools->_lock);
							for (unsigned c=0; c<dimof(ITechniqueDelegate::GraphicsPipelineDesc::_shaders); ++c)
								if (!pipelineDesc->_shaders[c].empty()) {
									const ShaderSourceParser::SelectorFilteringRules* autoFiltering[] = {
										pipelineDescWithFiltering->_automaticFiltering[c].get(), 
										&compiledPatchCollection->GetInterface().GetSelectorFilteringRules() 
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
						if (compiledPatchCollection->GetDependencyValidation())
							configurationDepVal.RegisterDependency(compiledPatchCollection->GetDependencyValidation());
						for (unsigned c=0; c<dimof(ITechniqueDelegate::GraphicsPipelineDesc::_shaders); ++c)
							if (!pipelineDesc->_shaders[c].empty() && pipelineDescWithFiltering->_automaticFiltering[c])
								configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_automaticFiltering[c]->GetDependencyValidation());
						if (pipelineDescWithFiltering->_preconfiguration)
							configurationDepVal.RegisterDependency(pipelineDescWithFiltering->_preconfiguration->GetDependencyValidation());

						StreamOutputInitializers so;
						so._outputElements = MakeIteratorRange(pipelineDesc->_soElements);
						so._outputBufferStrides = MakeIteratorRange(pipelineDesc->_soBufferStrides);

						auto shaderProgram = MakeShaderProgram(*pipelineDesc, pipelineLayout, compiledPatchCollection, MakeIteratorRange(filteredSelectors), so);
						std::string vsd, psd, gsd;
						#if defined(_DEBUG)
							vsd = MakeShaderDescription(ShaderStage::Vertex, *pipelineDesc, pipelineLayout, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Vertex]);
							psd = MakeShaderDescription(ShaderStage::Pixel, *pipelineDesc, pipelineLayout, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Pixel]);
							gsd = MakeShaderDescription(ShaderStage::Geometry, *pipelineDesc, pipelineLayout, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Geometry]);
						#endif
						::Assets::WhenAll(shaderProgram).ThenConstructToFuture(
							resultFuture,
							[cfg, pipelineDesc, configurationDepVal, vsd, psd, gsd, weakThis](std::shared_ptr<Metal::ShaderProgram> shaderProgram) {
								auto containingPipelineAccelerator = weakThis.lock();
								if (!containingPipelineAccelerator)
									Throw(std::runtime_error("Containing GraphicsPipeline builder has been destroyed"));

								auto result = std::make_shared<IPipelineAcceleratorPool::Pipeline>();
								result->_metalPipeline = containingPipelineAccelerator->InternalCreatePipeline(
									*shaderProgram, 
									pipelineDesc->_depthStencil, 
									pipelineDesc->_blend, 
									pipelineDesc->_rasterization, 
									cfg);
								result->_depVal = configurationDepVal;
								#if defined(_DEBUG)
									result->_vsDescription = vsd;
									result->_psDescription = psd;
									result->_gsDescription = gsd;
								#endif
								result->_depVal.RegisterDependency(result->_metalPipeline->GetDependencyValidation());
								return result;
							});
					});
			});

		return result;
	}

	auto PipelineAccelerator::PipelineForCfgId(SequencerConfigId cfgId) -> Pipeline&
	{
		unsigned poolId = unsigned(cfgId >> 32ull);
		unsigned sequencerIdx = unsigned(cfgId);
		if (poolId != _ownerPoolId)
			Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));

		if (_finalPipelines.size() < sequencerIdx+1)
			_finalPipelines.resize(sequencerIdx+1);
		return _finalPipelines[sequencerIdx];
	}

	PipelineAccelerator::PipelineAccelerator(
		unsigned ownerPoolId,
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	: _shaderPatches(shaderPatches)
	, _materialSelectors(materialSelectors)
	, _inputAssembly(inputAssembly.begin(), inputAssembly.end())
	, _topology(topology)
	, _stateSet(stateSet)
	, _ownerPoolId(ownerPoolId)
	{
		std::vector<InputElementDesc> sortedIA = _inputAssembly;
		std::sort(
			sortedIA.begin(), sortedIA.end(),
			[](const InputElementDesc& lhs, const InputElementDesc& rhs) {
				if (lhs._semanticName < rhs._semanticName) return true;
				if (lhs._semanticName > rhs._semanticName) return false;
				return lhs._semanticIndex > rhs._semanticIndex;	// note -- reversing order
			});

		// Build up the geometry selectors. 
		for (auto i = sortedIA.begin(); i!=sortedIA.end(); ++i) {
			// If we have the same name as the last one, we should just skip (because the
			// previous one would have had a larger semantic index, and effectively took
			// care of this selector)
			if (i!=sortedIA.begin() && (i-1)->_semanticName == i->_semanticName)
				continue;

			char buffer[256] = "GEO_HAS_";
			unsigned c=0;
			for (; c<i->_semanticName.size() && c < 255-8; ++c)
				buffer[8+c] = (char)std::toupper(i->_semanticName[c]);	// ensure that we're using upper case for the full semantic
			buffer[8+c] = '\0';
			_geoSelectors.SetParameter((const utf8*)buffer, i->_semanticIndex+1);
		}

		// If we have no IA elements at all, force on GEO_HAS_VERTEX_ID. Shaders will almost always
		// require it in this case, because there's no other way to distinquish one vertex from
		// the next.
		if (sortedIA.empty())
			_geoSelectors.SetParameter("GEO_HAS_VERTEX_ID", 1);
	}

	PipelineAccelerator::PipelineAccelerator(
		unsigned ownerPoolId,
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const MiniInputElementDesc*> miniInputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	: _shaderPatches(shaderPatches)
	, _materialSelectors(materialSelectors)
	, _miniInputAssembly(miniInputAssembly.begin(), miniInputAssembly.end())
	, _topology(topology)
	, _stateSet(stateSet)
	, _ownerPoolId(ownerPoolId)
	{
		std::vector<MiniInputElementDesc> sortedIA = _miniInputAssembly;
		std::sort(
			sortedIA.begin(), sortedIA.end(),
			[](const MiniInputElementDesc& lhs, const MiniInputElementDesc& rhs) {
				return lhs._semanticHash < rhs._semanticHash;
			});

		// Build up the geometry selectors. 
		for (auto i = sortedIA.begin(); i!=sortedIA.end(); ++i) {
			StringMeld<256> meld;
			auto basicSemantic = CommonSemantics::TryDehash(i->_semanticHash);
			if (basicSemantic.first) {
				auto base = i->_semanticHash - basicSemantic.second;
				auto endEquivalents = i+1;
				while (endEquivalents != sortedIA.end() && (endEquivalents->_semanticHash - base) < 16)
					++endEquivalents;
				auto lastSemanticIndex = (endEquivalents-1)->_semanticHash - base;

				meld << "GEO_HAS_" << basicSemantic.first;
				_geoSelectors.SetParameter(meld.AsStringSection(), lastSemanticIndex+1);
			} else {
				// The MiniInputElementDesc is not all-knowing, unfortunately. We can only dehash the
				// "common" semantics
				meld << "GEO_HAS_" << std::hex << i->_semanticHash;
				_geoSelectors.SetParameter(meld.AsStringSection(), 1);
			}
		}

		// If we have no IA elements at all, force on GEO_HAS_VERTEX_ID. Shaders will almost always
		// require it in this case, because there's no other way to distinquish one vertex from
		// the next.
		if (sortedIA.empty())
			_geoSelectors.SetParameter("GEO_HAS_VERTEX_ID", 1);
	}

	PipelineAccelerator::~PipelineAccelerator()
	{}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		D E S C R I P T O R S E T
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class DescriptorSetAccelerator
	{
	public:
		std::shared_ptr<::Assets::Future<ActualizedDescriptorSet>> _descriptorSet;
		DescriptorSetBindingInfo _bindingInfo;
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		P   O   O   L
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class PipelineAcceleratorPool : public IPipelineAcceleratorPool
	{
	public:
		std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const InputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) override;

		std::shared_ptr<PipelineAccelerator> CreatePipelineAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			Topology topology,
			const RenderCore::Assets::RenderStateSet& stateSet) override;

		virtual std::shared_ptr<DescriptorSetAccelerator> CreateDescriptorSetAccelerator(
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
			const ParameterBox& materialSelectors,
			const ParameterBox& constantBindings,
			const ParameterBox& resourceBindings,
			IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings) override;

		std::shared_ptr<SequencerConfig> CreateSequencerConfig(
			const std::shared_ptr<ITechniqueDelegate>& delegate,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex = 0) override;

		const ::Assets::PtrToFuturePtr<Pipeline>& GetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const override;
		const Metal::GraphicsPipeline* TryGetPipeline(PipelineAccelerator& pipelineAccelerator, const SequencerConfig& sequencerConfig) const override;

		const std::shared_ptr<::Assets::Future<ActualizedDescriptorSet>>& GetDescriptorSet(DescriptorSetAccelerator& accelerator) const override;
		const ActualizedDescriptorSet* TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const override;

		void			SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) override;
		T1(Type) void   SetGlobalSelector(StringSection<> name, Type value);
		void			RemoveGlobalSelector(StringSection<> name) override;

		void			RebuildAllOutOfDatePipelines() override;

		const std::shared_ptr<IDevice>& GetDevice() const override;
		const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() const override;
		
		const DescriptorSetLayoutAndBinding& GetMaterialDescriptorSetLayout() const override;
		const DescriptorSetLayoutAndBinding& GetSequencerDescriptorSetLayout() const override;

		PipelineAcceleratorPool(const std::shared_ptr<IDevice>& device, const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout, PipelineAcceleratorPoolFlags::BitField flags, const DescriptorSetLayoutAndBinding& matDescSetLayout, const DescriptorSetLayoutAndBinding& sequencerDescSetLayout);
		~PipelineAcceleratorPool();
		PipelineAcceleratorPool(const PipelineAcceleratorPool&) = delete;
		PipelineAcceleratorPool& operator=(const PipelineAcceleratorPool&) = delete;

	protected:
		ParameterBox _globalSelectors;
		std::vector<std::pair<uint64_t, std::weak_ptr<SequencerConfig>>> _sequencerConfigById;
		std::vector<std::pair<uint64_t, std::weak_ptr<PipelineAccelerator>>> _pipelineAccelerators;
		std::vector<std::pair<uint64_t, std::weak_ptr<DescriptorSetAccelerator>>> _descriptorSetAccelerators;
		std::vector<std::pair<uint64_t, std::shared_ptr<ISampler>>> _compiledSamplerStates;

		SequencerConfig MakeSequencerConfig(
			/*out*/ uint64_t& hash,
			const std::shared_ptr<ITechniqueDelegate>& delegate,
			const ParameterBox& sequencerSelectors,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex);

		void RebuildAllPipelines(unsigned poolGuid);
		void RebuildAllPipelines(unsigned poolGuid, PipelineAccelerator& pipeline);

		const std::shared_ptr<ISampler>& GetMetalSampler(const SamplerDesc& desc);

		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		DescriptorSetLayoutAndBinding _matDescSetLayout;
		DescriptorSetLayoutAndBinding _sequencerDescSetLayout;
		std::shared_ptr<SharedPools> _sharedPools;
		PipelineAcceleratorPoolFlags::BitField _flags;
	};

	auto PipelineAcceleratorPool::GetPipeline(
		PipelineAccelerator& pipelineAccelerator, 
		const SequencerConfig& sequencerConfig) const -> const ::Assets::PtrToFuturePtr<Pipeline>&
	{
		unsigned sequencerIdx = unsigned(sequencerConfig._cfgId);
		#if defined(_DEBUG)
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != pipelineAccelerator._ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
			if (sequencerIdx >= pipelineAccelerator._finalPipelines.size())
				Throw(std::runtime_error("Bad sequencer config id"));
		#endif
		
		return pipelineAccelerator._finalPipelines[sequencerIdx]._future;
	}

	const Metal::GraphicsPipeline* PipelineAcceleratorPool::TryGetPipeline(
		PipelineAccelerator& pipelineAccelerator, 
		const SequencerConfig& sequencerConfig) const
	{
		unsigned sequencerIdx = unsigned(sequencerConfig._cfgId);
		#if defined(_DEBUG)
			unsigned poolId = unsigned(sequencerConfig._cfgId >> 32ull);
			if (poolId != pipelineAccelerator._ownerPoolId)
				Throw(std::runtime_error("Mixing a pipeline accelerator from an incorrect pool"));
			if (sequencerIdx >= pipelineAccelerator._finalPipelines.size())
				Throw(std::runtime_error("Bad sequencer config id"));
		#endif
		
		auto p = pipelineAccelerator._finalPipelines[sequencerIdx]._future->TryActualize();
		if (p) return (*p)->_metalPipeline.get();
		return nullptr;
	}

	const std::shared_ptr<::Assets::Future<ActualizedDescriptorSet>>& PipelineAcceleratorPool::GetDescriptorSet(DescriptorSetAccelerator& accelerator) const
	{
		return accelerator._descriptorSet;
	}

	const ActualizedDescriptorSet* PipelineAcceleratorPool::TryGetDescriptorSet(DescriptorSetAccelerator& accelerator) const
	{
		return accelerator._descriptorSet->TryActualize();
	}

	SequencerConfig PipelineAcceleratorPool::MakeSequencerConfig(
		/*out*/ uint64_t& hash,
		const std::shared_ptr<ITechniqueDelegate>& delegate,
		const ParameterBox& sequencerSelectors,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIndex)
	{
		// Search for an identical sequencer config already registered, and return it
		// if it's here already. Other create it and return the result
		assert(!fbDesc.GetSubpasses().empty());

		SequencerConfig cfg;
		cfg._delegate = delegate;
		cfg._sequencerSelectors = sequencerSelectors;

		cfg._fbDesc = fbDesc;
		cfg._subpassIdx = subpassIndex;

		// In Vulkan, the "subpass index" value (and subpass count of the containing pipeline layout) is
		// important when building a pipeline. That if there are 2 render passes that contain identical 
		// subpasses, just at different subpass indices, then we can't use the same pipelines in both
		// those subpasses (ie, pipelines created for one aren't compatible with the other)
		//
		// However, we can get away with this on other APIs... When we can, we can use this trick to
		// take advantage of it
		const bool apiSupportsSeparatingSubpasses = false;
		if ((subpassIndex != 0 || fbDesc.GetSubpasses().size() > 1) && apiSupportsSeparatingSubpasses) {
			cfg._fbDesc = SeparateSingleSubpass(fbDesc, subpassIndex);
			cfg._subpassIdx = 0;
		}

		cfg._fbRelevanceValue = Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(cfg._fbDesc, cfg._subpassIdx);

		hash = HashCombine(sequencerSelectors.GetHash(), sequencerSelectors.GetParameterNamesHash());
		hash = HashCombine(cfg._fbRelevanceValue, hash);

		// todo -- we must take into account the delegate itself; it must impact the hash
		hash = HashCombine(uint64_t(delegate.get()), hash);

		return cfg;
	}

	std::shared_ptr<PipelineAccelerator> PipelineAcceleratorPool::CreatePipelineAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const InputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	{
		uint64_t hash = HashCombine(materialSelectors.GetHash(), materialSelectors.GetParameterNamesHash());
		hash = HashInputAssembly(inputAssembly, hash);
		hash = HashCombine((unsigned)topology, hash);
		hash = HashCombine(stateSet.GetHash(), hash);
		if (shaderPatches)
			hash = HashCombine(shaderPatches->GetHash(), hash);

		// If it already exists in the cache, just return it now
		auto i = LowerBound(_pipelineAccelerators, hash);
		if (i != _pipelineAccelerators.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l)
				return l;
		}

		auto newAccelerator = std::make_shared<PipelineAccelerator>(
			_guid,
			shaderPatches, materialSelectors,
			inputAssembly, topology,
			stateSet);

		if (i != _pipelineAccelerators.end() && i->first == hash) {
			i->second = newAccelerator;		// (we replaced one that expired)
		} else {
			_pipelineAccelerators.insert(i, std::make_pair(hash, newAccelerator));
		}

		RebuildAllPipelines(_guid, *newAccelerator);

		return newAccelerator;
	}

	std::shared_ptr<PipelineAccelerator> PipelineAcceleratorPool::CreatePipelineAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		IteratorRange<const MiniInputElementDesc*> inputAssembly,
		Topology topology,
		const RenderCore::Assets::RenderStateSet& stateSet)
	{
		uint64_t hash = HashCombine(materialSelectors.GetHash(), materialSelectors.GetParameterNamesHash());
		hash = HashInputAssembly(inputAssembly, hash);
		hash = HashCombine((unsigned)topology, hash);
		hash = HashCombine(stateSet.GetHash(), hash);
		if (shaderPatches)
			hash = HashCombine(shaderPatches->GetHash(), hash);

		// If it already exists in the cache, just return it now
		auto i = LowerBound(_pipelineAccelerators, hash);
		if (i != _pipelineAccelerators.end() && i->first == hash) {
			auto l = i->second.lock();
			if (l)
				return l;
		}

		auto newAccelerator = std::make_shared<PipelineAccelerator>(
			_guid,
			shaderPatches, materialSelectors,
			inputAssembly, topology,
			stateSet);

		if (i != _pipelineAccelerators.end() && i->first == hash) {
			i->second = newAccelerator;		// (we replaced one that expired)
		} else {
			_pipelineAccelerators.insert(i, std::make_pair(hash, newAccelerator));
		}

		RebuildAllPipelines(_guid, *newAccelerator);

		return newAccelerator;
	}

	std::shared_ptr<DescriptorSetAccelerator> PipelineAcceleratorPool::CreateDescriptorSetAccelerator(
		const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& shaderPatches,
		const ParameterBox& materialSelectors,
		const ParameterBox& constantBindings,
		const ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings)
	{
		uint64_t hash = HashCombine(materialSelectors.GetHash(), materialSelectors.GetParameterNamesHash());
		hash = HashCombine(constantBindings.GetHash(), hash);
		hash = HashCombine(constantBindings.GetParameterNamesHash(), hash);
		hash = HashCombine(resourceBindings.GetHash(), hash);
		hash = HashCombine(resourceBindings.GetParameterNamesHash(), hash);
		for (const auto&s:samplerBindings) {		// (note, different ordering will result in different hashes)
			hash = HashCombine(s.first, hash);
			hash = HashCombine(s.second.Hash(), hash);
		}
		if (shaderPatches)
			hash = HashCombine(shaderPatches->GetHash(), hash);

		// If it already exists in the cache, just return it now
		auto cachei = LowerBound(_descriptorSetAccelerators, hash);
		if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash) {
			auto l = cachei->second.lock();
			if (l)
				return l;
		}

		auto result = std::make_shared<DescriptorSetAccelerator>();
		result->_descriptorSet = std::make_shared<::Assets::Future<ActualizedDescriptorSet>>("descriptorset-accelerator");

		std::vector<std::pair<uint64_t, std::shared_ptr<ISampler>>> metalSamplers;
		metalSamplers.reserve(samplerBindings.size());
		for (const auto&c:samplerBindings)
			metalSamplers.push_back(std::make_pair(c.first, GetMetalSampler(c.second)));

		if (shaderPatches) {
			auto patchCollectionFuture = ::Assets::MakeAsset<CompiledShaderPatchCollection>(*shaderPatches, _matDescSetLayout);

			// Most of the time, it will be ready immediately, and we can avoid some of the overhead of the
			// future continuation functions
			if (auto* patchCollection = patchCollectionFuture->TryActualize()) {
				ConstructDescriptorSet(
					*result->_descriptorSet,
					_device,
					constantBindings,
					resourceBindings,
					MakeIteratorRange(metalSamplers),
					(*patchCollection)->GetInterface().GetMaterialDescriptorSet(),
					!!(_flags & PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo));
			} else {
				ParameterBox constantBindingsCopy = constantBindings;
				ParameterBox resourceBindingsCopy = resourceBindings;

				std::weak_ptr<IDevice> weakDevice = _device;
				bool generateBindingInfo = !!(_flags & PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo);
				::Assets::WhenAll(patchCollectionFuture).ThenConstructToFuture(
					*result->_descriptorSet,
					[constantBindingsCopy, resourceBindingsCopy, metalSamplers, weakDevice, generateBindingInfo](
						::Assets::Future<ActualizedDescriptorSet>& future,
						std::shared_ptr<CompiledShaderPatchCollection> patchCollection) {

						auto d = weakDevice.lock();
						if (!d)
							Throw(std::runtime_error("Device has been destroyed"));
						
						ConstructDescriptorSet(
							future,
							d,
							constantBindingsCopy,
							resourceBindingsCopy,
							MakeIteratorRange(metalSamplers),
							patchCollection->GetInterface().GetMaterialDescriptorSet(),
							generateBindingInfo);
					});
			}
		} else {
			ConstructDescriptorSet(
				*result->_descriptorSet,
				_device,
				constantBindings,
				resourceBindings,
				MakeIteratorRange(metalSamplers),
				*_matDescSetLayout.GetLayout(),
				!!(_flags & PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo));
		}

		if (cachei != _descriptorSetAccelerators.end() && cachei->first == hash) {
			cachei->second = result;		// (we replaced one that expired)
		} else {
			_descriptorSetAccelerators.insert(cachei, std::make_pair(hash, result));
		}

		return result;
	}

	auto PipelineAcceleratorPool::CreateSequencerConfig(
		const std::shared_ptr<ITechniqueDelegate>& delegate,
		const ParameterBox& sequencerSelectors,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIndex) -> std::shared_ptr<SequencerConfig>
	{
		uint64_t hash = 0;
		auto cfg = MakeSequencerConfig(hash, delegate, sequencerSelectors, fbDesc, subpassIndex);

		// Look for an existing configuration with the same settings
		//	-- todo, not checking the delegate here!
		for (auto i=_sequencerConfigById.begin(); i!=_sequencerConfigById.end(); ++i) {
			if (i->first == hash) {
				auto cfgId = SequencerConfigId(i - _sequencerConfigById.begin()) | (SequencerConfigId(_guid) << 32ull);
				
				auto result = i->second.lock();

				// The configuration may have expired. In this case, we should just create it again, and reset
				// our pointer. Note that we only even hold a weak pointer, so if the caller doesn't hold
				// onto the result, it's just going to expire once more
				if (!result) {
					result = std::make_shared<SequencerConfig>(std::move(cfg));
					result->_cfgId = cfgId;
					i->second = result;

					// If a pipeline accelerator was added while this sequencer config was expired, the pipeline
					// accelerator would not have been configured. We have to check for this case and construct
					// as necessary -- 
					for (auto& accelerator:_pipelineAccelerators) {
						auto a = accelerator.second.lock();
						if (a) {
							auto& pipeline = a->PipelineForCfgId(cfgId);
							if (!pipeline._future || (pipeline._future->GetAssetState() != ::Assets::AssetState::Pending && pipeline._future->GetDependencyValidation().GetValidationIndex() != 0)) {
								pipeline = a->CreatePipelineForSequencerState(*result, _globalSelectors, _pipelineLayout, _matDescSetLayout, _sharedPools);
							}
						}
					}
				}

				return result;
			}
		}

		auto cfgId = SequencerConfigId(_sequencerConfigById.size()) | (SequencerConfigId(_guid) << 32ull);
		auto result = std::make_shared<SequencerConfig>(std::move(cfg));
		result->_cfgId = cfgId;

		_sequencerConfigById.emplace_back(std::make_pair(hash, result));		// (note; only holding onto a weak pointer here)

		// trigger creation of pipeline states for all accelerators
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a)
				a->PipelineForCfgId(cfgId) = a->CreatePipelineForSequencerState(*result, _globalSelectors, _pipelineLayout, _matDescSetLayout, _sharedPools);
		}

		return result;
	}

	void PipelineAcceleratorPool::RebuildAllPipelines(unsigned poolGuid, PipelineAccelerator& pipeline)
	{
		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto cfgId = SequencerConfigId(c) | (SequencerConfigId(poolGuid) << 32ull);
			auto l = _sequencerConfigById[c].second.lock();
			if (l) 
				pipeline.PipelineForCfgId(cfgId) = pipeline.CreatePipelineForSequencerState(*l, _globalSelectors, _pipelineLayout, _matDescSetLayout, _sharedPools);
		}
	}

	void PipelineAcceleratorPool::RebuildAllPipelines(unsigned poolGuid)
	{
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a)
				RebuildAllPipelines(poolGuid, *a);
		}
	}

	void PipelineAcceleratorPool::RebuildAllOutOfDatePipelines()
	{
		// Look through every pipeline registered in this pool, and 
		// trigger a rebuild of any that appear to be out of date.
		// This allows us to support hotreloading when files change, etc
		std::vector<std::shared_ptr<SequencerConfig>> lockedSequencerConfigs;
		lockedSequencerConfigs.reserve(_sequencerConfigById.size());
		for (unsigned c=0; c<_sequencerConfigById.size(); ++c) {
			auto l = _sequencerConfigById[c].second.lock();
			lockedSequencerConfigs.emplace_back(std::move(l));
		}
					
		for (auto& accelerator:_pipelineAccelerators) {
			auto a = accelerator.second.lock();
			if (a) {
				for (unsigned c=0; c<std::min(_sequencerConfigById.size(), a->_finalPipelines.size()); ++c) {
					if (!lockedSequencerConfigs[c])
						continue;

					auto& p = a->_finalPipelines[c];
					if (p._future->GetAssetState() != ::Assets::AssetState::Pending && p._future->GetDependencyValidation().GetValidationIndex() != 0) {
						// It's out of date -- let's rebuild and reassign it
						p = a->CreatePipelineForSequencerState(*lockedSequencerConfigs[c], _globalSelectors, _pipelineLayout, _matDescSetLayout, _sharedPools);
					}
				}
			}
		}
	}

	void PipelineAcceleratorPool::SetGlobalSelector(StringSection<> name, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type)
	{
		_globalSelectors.SetParameter(name.Cast<utf8>(), data, type);
		RebuildAllPipelines(_guid);
	}

	void PipelineAcceleratorPool::RemoveGlobalSelector(StringSection<> name)
	{
		_globalSelectors.RemoveParameter(name.Cast<utf8>());
		RebuildAllPipelines(_guid);
	}

	const std::shared_ptr<ISampler>& PipelineAcceleratorPool::GetMetalSampler(const SamplerDesc& desc)
	{
		auto hash = desc.Hash();
		auto i = LowerBound(_compiledSamplerStates, hash);
		if (i != _compiledSamplerStates.end() && i->first == hash)
			return i->second;

		auto result = _device->CreateSampler(desc);
		i = _compiledSamplerStates.insert(i, std::make_pair(hash, result));
		return i->second;
	}

	const std::shared_ptr<IDevice>& PipelineAcceleratorPool::GetDevice() const { return _device; }
	const std::shared_ptr<ICompiledPipelineLayout>& PipelineAcceleratorPool::GetPipelineLayout() const { return _pipelineLayout; }

	const DescriptorSetLayoutAndBinding& PipelineAcceleratorPool::GetMaterialDescriptorSetLayout() const { return _matDescSetLayout; }
	const DescriptorSetLayoutAndBinding& PipelineAcceleratorPool::GetSequencerDescriptorSetLayout() const { return _sequencerDescSetLayout; }

	static unsigned s_nextPipelineAcceleratorPoolGUID = 1;

	static void CheckDescSetLayout(
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const PipelineLayoutInitializer& pipelineLayoutDesc,
		const char descSetName[])
	{
		if (matDescSetLayout.GetSlotIndex() >= pipelineLayoutDesc.GetDescriptorSets().size())
			Throw(std::runtime_error("Invalid slot index (" + std::to_string(matDescSetLayout.GetSlotIndex()) + " for " + descSetName + " during pipeline accelerator pool construction"));

		const auto& matchingDesc = pipelineLayoutDesc.GetDescriptorSets()[matDescSetLayout.GetSlotIndex()]._signature;
		const auto& layout = *matDescSetLayout.GetLayout();
		// It's ok if the pipeline layout has more slots than the _matDescSetLayout version; just not the other way around
		// we just have the verify that the types match up for the slots that are there
		if (matchingDesc._slots.size() < layout._slots.size())
			Throw(std::runtime_error(std::string{"Pipeline layout does not match the provided "} + descSetName + " layout. There are too few slots in the pipeline layout"));

		for (unsigned s=0; s<layout._slots.size(); ++s) {
			auto expectedCount = layout._slots[s]._arrayElementCount ?: 1;
			if (matchingDesc._slots[s]._type != layout._slots[s]._type || matchingDesc._slots[s]._count != expectedCount)
				Throw(std::runtime_error(std::string{"Pipeline layout does not match the provided "} + descSetName + " layout. Slot type does not match for slot (" + std::to_string(s) + ")"));
		}
	}

	static DescriptorSetLayoutAndBinding ExtractDescriptorSetLayoutAndBinding(
		const PipelineLayoutInitializer& pipelineLayoutDesc,
		const char bindingName[])
	{
		auto i = std::find_if(
			pipelineLayoutDesc.GetDescriptorSets().begin(), pipelineLayoutDesc.GetDescriptorSets().end(),
			[bindingName](const auto& c) { return c._name == bindingName; });
		if (i != pipelineLayoutDesc.GetDescriptorSets().end()) {
			auto extractedLayout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>();
			extractedLayout->_slots.reserve(i->_signature._slots.size());
			for (const auto& slot:i->_signature._slots)
				extractedLayout->_slots.push_back(
					RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot{
						std::string{},
						slot._type,
						(slot._count == 1) ? 0u : slot._count});
			return DescriptorSetLayoutAndBinding { extractedLayout, (unsigned)std::distance(pipelineLayoutDesc.GetDescriptorSets().begin(), i) };
		}
		return {};
	}

	PipelineAcceleratorPool::PipelineAcceleratorPool(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		PipelineAcceleratorPoolFlags::BitField flags, 
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const DescriptorSetLayoutAndBinding& sequencerDescSetLayout)
	: _matDescSetLayout(matDescSetLayout)
	, _sequencerDescSetLayout(sequencerDescSetLayout)
	{
		_guid = s_nextPipelineAcceleratorPoolGUID++;
		_device = device;
		_pipelineLayout = pipelineLayout;
		_flags = flags;
		_sharedPools = std::make_shared<SharedPools>();
		_sharedPools->_emptyPatchCollection = std::make_shared<::Assets::FuturePtr<CompiledShaderPatchCollection>>("empty-patch-collection");
		_sharedPools->_emptyPatchCollection->SetAsset(std::make_shared<CompiledShaderPatchCollection>(), nullptr);

		auto pipelineLayoutDesc = _pipelineLayout->GetInitializer();
		if (_matDescSetLayout.GetLayout()) {
			// The _matDescSetLayout must agree with what we find the pipeline layout
			CheckDescSetLayout(_matDescSetLayout, pipelineLayoutDesc, "material descriptor set");
		} else {
			// Even when there's no explicitly provided material desc layout, we can extract what we need from the pipeline layout
			_matDescSetLayout = ExtractDescriptorSetLayoutAndBinding(pipelineLayoutDesc, "Material");
		}

		if (_sequencerDescSetLayout.GetLayout()) {
			// The _matDescSetLayout must agree with what we find the pipeline layout
			CheckDescSetLayout(_sequencerDescSetLayout, pipelineLayoutDesc, "sequencer descriptor set");
		} else {
			// Even when there's no explicitly provided material desc layout, we can extract what we need from the pipeline layout
			_sequencerDescSetLayout = ExtractDescriptorSetLayoutAndBinding(pipelineLayoutDesc, "Sequencer");
		}
	}

	PipelineAcceleratorPool::~PipelineAcceleratorPool() {}
	IPipelineAcceleratorPool::~IPipelineAcceleratorPool() {}

	std::shared_ptr<IPipelineAcceleratorPool> CreatePipelineAcceleratorPool(
		const std::shared_ptr<IDevice>& device, 
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		PipelineAcceleratorPoolFlags::BitField flags,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const DescriptorSetLayoutAndBinding& sequencerDescSetLayout)
	{
		return std::make_shared<PipelineAcceleratorPool>(device, pipelineLayout, flags, matDescSetLayout, sequencerDescSetLayout);
	}

	namespace Internal
	{
		const DescriptorSetLayoutAndBinding& GetDefaultDescriptorSetLayoutAndBinding()
		{
			static DescriptorSetLayoutAndBinding s_result;
			return s_result;
		}
	}

}}

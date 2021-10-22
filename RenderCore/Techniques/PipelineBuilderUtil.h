// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineCollection.h"
#include "TechniqueDelegates.h"
#include "ShaderVariationSet.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Metal/Shader.h"
#include "../Metal/InputLayout.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/DeviceContext.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/PathUtils.h"

namespace RenderCore { namespace Techniques { namespace Internal
{
	class GraphicsPipelineDescWithFilteringRules
	{
	public:
		std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> _automaticFiltering[3];
		std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> _preconfiguration;

		static ::Assets::PtrToFuturePtr<GraphicsPipelineDescWithFilteringRules> CreateFuture(
			const ::Assets::PtrToFuturePtr<GraphicsPipelineDesc>& pipelineDescFuture)
		{
			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>>(pipelineDescFuture->Initializer());
			::Assets::WhenAll(pipelineDescFuture).ThenConstructToFuture(*result, 
				[](auto& resultFuture, auto pipelineDesc) { InitializeFuture(resultFuture, *pipelineDesc); });
			return result;
		}

		static ::Assets::PtrToFuturePtr<GraphicsPipelineDescWithFilteringRules> CreateFuture(
			const GraphicsPipelineDesc& pipelineDesc)
		{
			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>>();
			InitializeFuture(*result, pipelineDesc);
			return result;
		}

		static void InitializeFuture(
			::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>& resultFuture,
			const GraphicsPipelineDesc& pipelineDesc)
		{
			static_assert(std::is_same_v<decltype(resultFuture), ::Assets::FuturePtr<GraphicsPipelineDescWithFilteringRules>&>);

			::Assets::PtrToFuturePtr<ShaderSourceParser::SelectorFilteringRules> filteringFuture[3];
			for (unsigned c=0; c<3; ++c) {
				auto fn = MakeFileNameSplitter(pipelineDesc._shaders[c]).AllExceptParameters();
				if (!fn.IsEmpty())
					filteringFuture[c] = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(fn);
			}

			if (!filteringFuture[(unsigned)ShaderStage::Vertex])
				Throw(std::runtime_error("Missing vertex shader stage while building filtering rules"));

			if (filteringFuture[(unsigned)ShaderStage::Pixel] && !filteringFuture[(unsigned)ShaderStage::Geometry]) {

				if (pipelineDesc._selectorPreconfigurationFile.empty()) {
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel]).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
							return finalObject;
						});
				} else {
					auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc._selectorPreconfigurationFile);
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], preconfigurationFuture).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
							finalObject->_preconfiguration = preconfiguration;
							return finalObject;
						});
				}

			} else if (filteringFuture[(unsigned)ShaderStage::Pixel] && filteringFuture[(unsigned)ShaderStage::Geometry]) {

				if (pipelineDesc._selectorPreconfigurationFile.empty()) {
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
							return finalObject;
						});
				} else {
					auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc._selectorPreconfigurationFile);
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = psFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
							finalObject->_preconfiguration = preconfiguration;
							return finalObject;
						});
				}

			} else if (!filteringFuture[(unsigned)ShaderStage::Pixel] && filteringFuture[(unsigned)ShaderStage::Geometry]) {

				if (pipelineDesc._selectorPreconfigurationFile.empty()) {
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
							return finalObject;
						});
				} else {
					auto preconfigurationFuture = ::Assets::MakeAsset<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc._selectorPreconfigurationFile);
					::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).ThenConstructToFuture(
						resultFuture,
						[]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
							std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
							
							auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = vsFiltering;
							finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = gsFiltering;
							finalObject->_preconfiguration = preconfiguration;
							return finalObject;
						});
				}

			} else
				Throw(std::runtime_error("Missing shader stages while building filtering rules"));
		}
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

	static ::Assets::PtrToFuturePtr<CompiledShaderByteCode> MakeByteCodeFuture(
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
			// Following MinimalShaderSource::MakeResId, the shader model comes after the second colon
			const char* colon = XlFindChar(initializer, ':');
			if (colon) colon = XlFindChar(MakeStringSection(colon+1, initializer.end()), ':');
			if (!colon) {
				char profileStr[] = "?s_*";
				switch (stage) {
				case ShaderStage::Vertex: profileStr[0] = 'v'; break;
				case ShaderStage::Geometry: profileStr[0] = 'g'; break;
				case ShaderStage::Pixel: profileStr[0] = 'p'; break;
				case ShaderStage::Domain: profileStr[0] = 'd'; break;
				case ShaderStage::Hull: profileStr[0] = 'h'; break;
				case ShaderStage::Compute: profileStr[0] = 'c'; break;
				default: assert(0); break;
				}
				meld << ":" << profileStr;
			} else {
				auto profileSection = MakeStringSection(colon+1, initializer.end());
				assert(profileSection.size() > 3 && profileSection[1] == 's' && profileSection[2] == '_');
			}
		}

		auto adjustedDefinesTable = definesTable;
		if (stage == ShaderStage::Geometry && !so._outputElements.empty()) {
			if (!definesTable.empty()) adjustedDefinesTable += ";";
			adjustedDefinesTable += BuildSODefinesString(so._outputElements);
		}

		if (patchCollection && !patchExpansions.empty()) {
			std::vector<uint64_t> patchExpansionsCopy(patchExpansions.begin(), patchExpansions.end());
			auto res = ::Assets::MakeAsset<CompiledShaderByteCode_InstantiateShaderGraph>(
				MakeStringSection(temp), adjustedDefinesTable, patchCollection, patchExpansionsCopy);
			return *reinterpret_cast<::Assets::PtrToFuturePtr<CompiledShaderByteCode>*>(&res);
		} else {
			return ::Assets::MakeAsset<CompiledShaderByteCode>(MakeStringSection(temp), adjustedDefinesTable);
		}
	}

	static ::Assets::PtrToFuturePtr<Metal::ShaderProgram> MakeShaderProgram(
		::Assets::PtrToFuturePtr<CompiledShaderByteCode> byteCodeFuture[3],
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StreamOutputInitializers so = {})
	{
		if (!byteCodeFuture[(unsigned)ShaderStage::Vertex])
			Throw(std::runtime_error("Missing vertex shader stage while building shader program"));

		auto result = std::make_shared<::Assets::FuturePtr<Metal::ShaderProgram>>();
		if (byteCodeFuture[(unsigned)ShaderStage::Pixel] && !byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Pixel]).ThenConstructToFuture(
				*result,
				[pipelineLayout](
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> psCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *psCode);
				});
		} else if (byteCodeFuture[(unsigned)ShaderStage::Pixel] && byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			std::vector<RenderCore::InputElementDesc> soElementsCopy{so._outputElements.begin(), so._outputElements.end()};
			std::vector<unsigned> soBufferStridesCopy{so._outputBufferStrides.begin(), so._outputBufferStrides.end()};
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Pixel], byteCodeFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
				*result,
				[pipelineLayout, soElementsCopy, soBufferStridesCopy](
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> psCode,
					std::shared_ptr<CompiledShaderByteCode> gsCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *gsCode, *psCode,
						StreamOutputInitializers{soElementsCopy, soBufferStridesCopy});
				});
		} else if (!byteCodeFuture[(unsigned)ShaderStage::Pixel] && byteCodeFuture[(unsigned)ShaderStage::Geometry]) {
			std::vector<RenderCore::InputElementDesc> soElementsCopy{so._outputElements.begin(), so._outputElements.end()};
			std::vector<unsigned> soBufferStridesCopy{so._outputBufferStrides.begin(), so._outputBufferStrides.end()};
			::Assets::WhenAll(byteCodeFuture[(unsigned)ShaderStage::Vertex], byteCodeFuture[(unsigned)ShaderStage::Geometry]).ThenConstructToFuture(
				*result,
				[pipelineLayout, soElementsCopy, soBufferStridesCopy](
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> gsCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *gsCode, CompiledShaderByteCode{},
						StreamOutputInitializers{soElementsCopy, soBufferStridesCopy});
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
		const GraphicsPipelineDesc& pipelineDesc,
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
		if (compiledPatchCollection)
			for (const auto& patch:compiledPatchCollection->GetInterface().GetPatches()) {
				if (!first) str << ", "; first = false;
				str << "patch: " << patch._entryPointName;
			}
		str << "[" << filteredSelectors._selectors << "]";
		return str.str();
	}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct InputAssemblyStates
	{
		std::vector<InputElementDesc> _inputAssembly;
		std::vector<MiniInputElementDesc> _miniInputAssembly;
		uint64_t _hashCode = 0;
	};

	struct PipelineLayoutOptions
	{
		std::shared_ptr<ICompiledPipelineLayout> _prebuiltPipelineLayout;
		::Assets::PtrToFuturePtr<RenderCore::Assets::PredefinedPipelineLayout> _predefinedPipelineLayout;
		uint64_t _hashCode = 0;

		PipelineLayoutOptions() = default;
		PipelineLayoutOptions(std::shared_ptr<ICompiledPipelineLayout>);
		PipelineLayoutOptions(::Assets::PtrToFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>, uint64_t);
	};

	class SharedPools : public std::enable_shared_from_this<SharedPools>
	{
	public:
		Threading::Mutex _lock;
		UniqueShaderVariationSet _selectorVariationsSet;
		::Assets::PtrToFuturePtr<CompiledShaderPatchCollection> _emptyPatchCollection;
		std::shared_ptr<SamplerPool> _samplerPool;
		std::shared_ptr<IDevice> _device;

		std::vector<std::pair<uint64_t, std::weak_ptr<Metal::GraphicsPipeline>>> _completedGraphicsPipelines;
		std::vector<std::pair<uint64_t, ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>>> _pendingGraphicsPipelines;

		::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> CreateGraphicsPipelineAlreadyLocked(
			const InputAssemblyStates& ia,
			Topology topology,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc,
			const std::shared_ptr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFiltering,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const UniqueShaderVariationSet::FilteredSelectorSet*> filteredSelectors,
			const FrameBufferTarget& fbTarget)
		{
			uint64_t hash = HashCombine(compiledPatchCollection->GetGUID(), pipelineLayout->GetGUID());
			for (auto s:filteredSelectors)
				if (s._hashValue)
					hash = HashCombine(s._hashValue, hash);
			hash = HashCombine(fbTarget.GetHash(), hash);
			hash = HashCombine((uint64_t)topology, hash);
			hash = HashCombine(ia._hashCode, hash);

			// we need to hash specific parts of the graphics pipeline desc -- only those parts that we'll use below
			// some parts of the pipeline desc (eg, the selectors) have already been used to create other inputs here
			// we don't want to use use them, because they may be more aggressively filtered in the secondary products
			// (particularly for the filtered selectors)
			hash = pipelineDesc->CalculateHashNoSelectors(hash);

			auto completedi = LowerBound(_completedGraphicsPipelines, hash);
			if (completedi != _completedGraphicsPipelines.end() && completedi->first == hash) {
				auto l = completedi->second.lock();
				if (l && l->GetDependencyValidation().GetValidationIndex() == 0) {
					// we can return an already completed pipeline
					auto result = std::make_shared<::Assets::FuturePtr<Metal::GraphicsPipeline>>("pipeline-accelerator");
					result->SetAsset(std::move(l), {});
					return result;
				}
			}

			auto i = LowerBound(_pendingGraphicsPipelines, hash);
			if (i!=_pendingGraphicsPipelines.end() && i->first == hash)
				if (!::Assets::IsInvalidated(*i->second))
					return i->second;

			#if 0
				Log(Verbose) << "Building pipeline for pipeline accelerator: " << std::endl;
				Log(Verbose) << "\tPipeline layout: " << pipelineLayout->GetGUID() << " (" << (size_t)pipelineLayout.get() << ")" << std::endl;
				Log(Verbose) << "\tFB relevance: " << sequencerCfg._fbRelevanceValue << std::endl;
				Log(Verbose) << "\tIA: " << ia._hash << std::endl;
				Log(Verbose) << "\tPatch collection: " << compiledPatchCollection->GetGUID() << std::endl;
				for (unsigned c=0; c<filteredSelectors.size(); ++c) {
					if (!filteredSelectors[c]._hashValue) continue;
					Log(Verbose) << "\tFiltered selectors[" << c << "]: " << filteredSelectors[c]._selectors << std::endl;
				}
			#endif

			StreamOutputInitializers so;
			so._outputElements = MakeIteratorRange(pipelineDesc->_soElements);
			so._outputBufferStrides = MakeIteratorRange(pipelineDesc->_soBufferStrides);
			::Assets::PtrToFuturePtr<CompiledShaderByteCode> byteCodeFutures[3];
			for (unsigned c=0; c<3; ++c) {
				if (pipelineDesc->_shaders[c].empty())
					continue;
				byteCodeFutures[c] = MakeByteCodeFuture((ShaderStage)c, pipelineDesc->_shaders[c], filteredSelectors[c], compiledPatchCollection, pipelineDesc->_patchExpansions, so);
			}

			auto shaderProgram = Internal::MakeShaderProgram(byteCodeFutures, pipelineLayout, so);
			
			auto result = std::make_shared<::Assets::FuturePtr<Metal::GraphicsPipeline>>("pipeline-accelerator");
			::Assets::WhenAll(shaderProgram).ThenConstructToFuture(
				*result,
				[	ia=ia, topology, pipelineDesc, 
					fbDesc=*fbTarget._fbDesc, subpassIdx=fbTarget._subpassIdx
					](std::shared_ptr<Metal::ShaderProgram> shaderProgram) {

					return InternalCreatePipeline(
						*shaderProgram, 
						*pipelineDesc, ia, topology,
						fbDesc, subpassIdx);
				});

			AddGraphicsPipelineFuture(result, hash);
			return result;
		}

		void AddGraphicsPipelineFuture(const ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>& future, uint64_t hash)
		{
			auto i = LowerBound(_pendingGraphicsPipelines, hash);
			if (i!=_pendingGraphicsPipelines.end() && i->first == hash) {
				i->second = future;
			} else
				_pendingGraphicsPipelines.insert(i, std::make_pair(hash, future));

			::Assets::WhenAll(future).Then(
				[weakThis=weak_from_this(), hash](std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> completedFuture) {
					auto t = weakThis.lock();
					if (!t) return;
					// Invalid futures stay in the "_pendingGraphicsPipelines" list
					if (completedFuture->GetAssetState() == ::Assets::AssetState::Invalid) return;
					ScopedLock(t->_lock);

					auto i = LowerBound(t->_pendingGraphicsPipelines, hash);
					assert(i!=t->_pendingGraphicsPipelines.end() && i->first == hash);
					if (i!=t->_pendingGraphicsPipelines.end() && i->first == hash) {
						if (i->second.get() != completedFuture.get())
							return;		// possibly scheduled a replacement while the first was still pending
						t->_pendingGraphicsPipelines.erase(i);
					}

					auto completedi = LowerBound(t->_completedGraphicsPipelines, hash);
					if (completedi != t->_completedGraphicsPipelines.end() && completedi->first == hash) {
						completedi->second = *completedFuture->TryActualize();
					} else
						t->_completedGraphicsPipelines.insert(completedi, std::make_pair(hash, *completedFuture->TryActualize()));
				});
		}

		std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> CreateComputePipelineAlreadyLocked(
			StringSection<> shader,
			const PipelineLayoutOptions& pipelineLayout,
			const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors)
		{
			auto hash = Hash64(shader, filteredSelectors._hashValue);
			hash = HashCombine(pipelineLayout._hashCode, hash);

			auto completedi = LowerBound(_completedComputePipelines, hash);
			if (completedi != _completedComputePipelines.end() && completedi->first == hash) {
				auto pipeline = completedi->second._pipeline.lock();
				auto layout = completedi->second._layout.lock();
				if (pipeline && pipeline->GetDependencyValidation().GetValidationIndex() == 0 && layout) {
					// we can return an already completed pipeline
					auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>("compute-pipeline");
					result->SetAsset(ComputePipelineAndLayout{std::move(pipeline), std::move(layout)}, {});
					return result;
				}
			}

			auto i = LowerBound(_pendingComputePipelines, hash);
			if (i!=_pendingComputePipelines.end() && i->first == hash)
				if (!::Assets::IsInvalidated(*i->second))
					return i->second;

			auto byteCodeFuture = MakeByteCodeFuture(ShaderStage::Compute, shader, filteredSelectors, nullptr, {});

			auto result = std::make_shared<::Assets::Future<ComputePipelineAndLayout>>("compute-pipeline");
			if (pipelineLayout._prebuiltPipelineLayout) {
				::Assets::WhenAll(byteCodeFuture).ThenConstructToFuture(
					*result,
					[pipelineLayout = pipelineLayout._prebuiltPipelineLayout](auto csCode) {
						return MakeComputePipeline(*csCode, pipelineLayout);
					});
			} else if (!pipelineLayout._predefinedPipelineLayout) {
				::Assets::WhenAll(byteCodeFuture).ThenConstructToFuture(
					*result,
					[weakDevice=std::weak_ptr<IDevice>{_device}](auto csCode) {
						auto d = weakDevice.lock();
						if (!d) Throw(std::runtime_error("Device shutdown before completion"));

						auto initializer = Metal::BuildPipelineLayoutInitializer(*csCode);
						auto pipelineLayout = d->CreatePipelineLayout(initializer);
						return MakeComputePipeline(*csCode, pipelineLayout);
					});
			} else {
				::Assets::WhenAll(byteCodeFuture, pipelineLayout._predefinedPipelineLayout).ThenConstructToFuture(
					*result,
					[pipelineLayout, weakDevice=std::weak_ptr<IDevice>{_device}, weakSamplerPool=std::weak_ptr<SamplerPool>{_samplerPool}](auto csCode, auto predefinedPipelineLayout) {
						auto d = weakDevice.lock();
						auto samplers = weakSamplerPool.lock();
						if (!d || !samplers) Throw(std::runtime_error("Device shutdown before completion"));

						// This case is a little more complicated because we need to generate a pipeline layout 
						// (potentially using the shader byte code)
						std::shared_ptr<ICompiledPipelineLayout> finalPipelineLayout;
						if (predefinedPipelineLayout->HasAutoDescriptorSets()) {
							auto autoInitializer = Metal::BuildPipelineLayoutInitializer(*csCode);
							auto initializer = predefinedPipelineLayout->MakePipelineLayoutInitializerWithAutoMatching(
								autoInitializer, GetDefaultShaderLanguage(), samplers.get());
							finalPipelineLayout = d->CreatePipelineLayout(initializer);
						} else {
							auto initializer = predefinedPipelineLayout->MakePipelineLayoutInitializer(GetDefaultShaderLanguage(), samplers.get());
							finalPipelineLayout = d->CreatePipelineLayout(initializer);
						}

						return MakeComputePipeline(*csCode, finalPipelineLayout);
					});
			}

			AddComputePipelineFuture(result, hash);
			return result;
		};

		void AddComputePipelineFuture(const std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>>& future, uint64_t hash)
		{
			auto i = LowerBound(_pendingComputePipelines, hash);
			if (i!=_pendingComputePipelines.end() && i->first == hash) {
				i->second = future;
			} else
				_pendingComputePipelines.insert(i, std::make_pair(hash, future));

			::Assets::WhenAll(future).Then(
				[weakThis=weak_from_this(), hash](std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>> completedFuture) {
					auto t = weakThis.lock();
					if (!t) return;
					// Invalid futures stay in the "_pendingComputePipelines" list
					if (completedFuture->GetAssetState() == ::Assets::AssetState::Invalid) return;

					ScopedLock(t->_lock);

					auto i = LowerBound(t->_pendingComputePipelines, hash);
					assert(i!=t->_pendingComputePipelines.end() && i->first == hash);
					if (i!=t->_pendingComputePipelines.end() && i->first == hash) {
						if (i->second.get() != completedFuture.get())
							return;		// possibly scheduled a replacement while the first was still pending
						t->_pendingComputePipelines.erase(i);
					}

					WeakComputePipelineAndLayout weakPtrs;
					weakPtrs._pipeline = completedFuture->TryActualize()->_pipeline;
					weakPtrs._layout = completedFuture->TryActualize()->_layout;

					auto completedi = LowerBound(t->_completedComputePipelines, hash);
					if (completedi != t->_completedComputePipelines.end() && completedi->first == hash) {
						completedi->second = std::move(weakPtrs);
					} else
						t->_completedComputePipelines.insert(completedi, std::make_pair(hash, std::move(weakPtrs)));
				});
		}

		struct WeakComputePipelineAndLayout
		{
			std::weak_ptr<Metal::ComputePipeline> _pipeline;
			std::weak_ptr<ICompiledPipelineLayout> _layout;
		};
		std::vector<std::pair<uint64_t, WeakComputePipelineAndLayout>> _completedComputePipelines;
		std::vector<std::pair<uint64_t, std::shared_ptr<::Assets::Future<ComputePipelineAndLayout>>>> _pendingComputePipelines;

		UniqueShaderVariationSet::FilteredSelectorSet FilterSelectorsAlreadyLocked(
			ShaderStage shaderStage,
			IteratorRange<const ParameterBox**> selectors,
			const ShaderSourceParser::SelectorFilteringRules& automaticFiltering,
			const ShaderSourceParser::ManualSelectorFiltering& manualFiltering,
			const ShaderSourceParser::SelectorPreconfiguration* preconfiguration,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const std::pair<uint64_t, ShaderStage>*> patchExpansions)
		{
			UniqueShaderVariationSet::FilteredSelectorSet filteredSelectors;

			const ShaderSourceParser::SelectorFilteringRules* autoFiltering[1+patchExpansions.size()];
			unsigned filteringRulesPulledIn[1+patchExpansions.size()];
			unsigned autoFilteringCount = 0;
			autoFiltering[autoFilteringCount++] = &automaticFiltering;
			filteringRulesPulledIn[0] = ~0u;

			// Figure out which filtering rules we need from the compiled patch collection, and include them
			// This is important because the filtering rules for different shader stages might be vastly different
			for (auto exp:patchExpansions) {
				if (exp.second != shaderStage) continue;
				auto i = std::find_if(
					compiledPatchCollection->GetInterface().GetPatches().begin(), compiledPatchCollection->GetInterface().GetPatches().end(),
					[exp](const auto& c) { return c._implementsHash == exp.first; });
				assert(i != compiledPatchCollection->GetInterface().GetPatches().end());
				if (i == compiledPatchCollection->GetInterface().GetPatches().end()) continue;
				if (std::find(filteringRulesPulledIn, &filteringRulesPulledIn[autoFilteringCount], i->_filteringRulesId) != &filteringRulesPulledIn[autoFilteringCount]) continue;
				filteringRulesPulledIn[autoFilteringCount] = i->_filteringRulesId;
				autoFiltering[autoFilteringCount++] = &compiledPatchCollection->GetInterface().GetSelectorFilteringRules(i->_filteringRulesId);
			}

			return _selectorVariationsSet.FilterSelectors(
				selectors,
				manualFiltering, 
				MakeIteratorRange(autoFiltering, &autoFiltering[autoFilteringCount]), 
				preconfiguration);
		}

		SharedPools(std::shared_ptr<IDevice> device)
		: _device(std::move(device))
		{
			_samplerPool = std::make_shared<SamplerPool>(*_device);
		}

	private:
		static std::shared_ptr<Metal::GraphicsPipeline> InternalCreatePipeline(
			const Metal::ShaderProgram& shader,
			const GraphicsPipelineDesc& pipelineDesc,
			const InputAssemblyStates& ia,
			Topology topology,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIdx)
		{
			Metal::GraphicsPipelineBuilder builder;
			builder.Bind(shader);
			builder.Bind(pipelineDesc._blend);
			builder.Bind(pipelineDesc._depthStencil);
			builder.Bind(pipelineDesc._rasterization);

			if (!ia._inputAssembly.empty()) {
				Metal::BoundInputLayout boundIA(MakeIteratorRange(ia._inputAssembly), shader);
				assert(boundIA.AllAttributesBound());
				builder.Bind(boundIA, topology);
			} else {
				Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(ia._miniInputAssembly), 0 };
				Metal::BoundInputLayout boundIA(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
				assert(boundIA.AllAttributesBound());
				builder.Bind(boundIA, topology);
			}

			builder.SetRenderPassConfiguration(fbDesc, subpassIdx);

			return builder.CreatePipeline(Metal::GetObjectFactory());
		}

		::Assets::PtrToFuturePtr<CompiledShaderByteCode> MakeByteCodeFuture(
			ShaderStage shaderStage,
			StringSection<> shader,
			const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const std::pair<uint64_t, ShaderStage>*> patchExpansions,
			StreamOutputInitializers so = {})
		{
			uint64_t patchExpansionsBuffer[patchExpansions.size()];
			unsigned patchExpansionCount = 0;
			for (auto p:patchExpansions)
				if (p.second == shaderStage) 
					patchExpansionsBuffer[patchExpansionCount++] = p.first;

			return Internal::MakeByteCodeFuture(
				shaderStage,
				shader,
				filteredSelectors._selectors,
				compiledPatchCollection,
				MakeIteratorRange(patchExpansionsBuffer, &patchExpansionsBuffer[patchExpansionCount]), 
				so);
		};

		static ComputePipelineAndLayout MakeComputePipeline(
			const CompiledShaderByteCode& csCode,
			const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout)
		{
			Metal::ComputeShader shaderActual{Metal::GetObjectFactory(), pipelineLayout, csCode};
			Metal::ComputePipelineBuilder builder;
			builder.Bind(shaderActual);
			return ComputePipelineAndLayout {
				builder.CreatePipeline(Metal::GetObjectFactory()),
				pipelineLayout };
		}
	};

}}}

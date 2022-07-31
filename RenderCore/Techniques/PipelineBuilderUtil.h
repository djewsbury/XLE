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
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/Streams/PathUtils.h"

namespace RenderCore { namespace Techniques { namespace Internal
{
	class GraphicsPipelineDescWithFilteringRules
	{
	public:
		std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> _automaticFiltering[3];
		std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> _preconfiguration;
		std::shared_ptr<GraphicsPipelineDesc> _pipelineDesc;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<GraphicsPipelineDescWithFilteringRules>> promise,
			std::shared_future<std::shared_ptr<GraphicsPipelineDesc>> pipelineDescFuture)
		{
			::Assets::WhenAll(std::move(pipelineDescFuture)).CheckImmediately().ThenConstructToPromise(
				std::move(promise), 
				[](std::promise<std::shared_ptr<GraphicsPipelineDescWithFilteringRules>>&& resultPromise, auto pipelineDesc) { InitializePromise(std::move(resultPromise), pipelineDesc); });
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<GraphicsPipelineDescWithFilteringRules>> promise,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc)
		{
			InitializePromise(std::move(promise), pipelineDesc);
		}

		static void InitializePromise(
			std::promise<std::shared_ptr<GraphicsPipelineDescWithFilteringRules>>&& promise,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc)
		{
			TRY {
				std::shared_future<std::shared_ptr<ShaderSourceParser::SelectorFilteringRules>> filteringFuture[3];
				for (unsigned c=0; c<3; ++c) {
					auto fn = MakeFileNameSplitter(pipelineDesc->_shaders[c]).AllExceptParameters();
					if (!fn.IsEmpty())
						filteringFuture[c] = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorFilteringRules>(fn);
				}

				if (!filteringFuture[(unsigned)ShaderStage::Vertex].valid())
					Throw(std::runtime_error("Missing vertex shader stage while building filtering rules"));

				if (filteringFuture[(unsigned)ShaderStage::Pixel].valid() && !filteringFuture[(unsigned)ShaderStage::Geometry].valid()) {

					if (pipelineDesc->_techniquePreconfigurationFile.empty() && pipelineDesc->_materialPreconfigurationFile.empty()) {
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel]).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = std::move(psFiltering);
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					} else {
						auto preconfigurationFuture = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], preconfigurationFuture).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = std::move(psFiltering);
								finalObject->_preconfiguration = preconfiguration;
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					}

				} else if (filteringFuture[(unsigned)ShaderStage::Pixel].valid() && filteringFuture[(unsigned)ShaderStage::Geometry].valid()) {

					if (pipelineDesc->_techniquePreconfigurationFile.empty() && pipelineDesc->_materialPreconfigurationFile.empty()) {
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry]).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = std::move(psFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = std::move(gsFiltering);
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					} else {
						auto preconfigurationFuture = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Pixel], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> psFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Pixel] = std::move(psFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = std::move(gsFiltering);
								finalObject->_preconfiguration = preconfiguration;
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					}

				} else if (!filteringFuture[(unsigned)ShaderStage::Pixel].valid() && filteringFuture[(unsigned)ShaderStage::Geometry].valid()) {

					if (pipelineDesc->_techniquePreconfigurationFile.empty() && pipelineDesc->_materialPreconfigurationFile.empty()) {
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry]).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = std::move(gsFiltering);
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					} else {
						auto preconfigurationFuture = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
						::Assets::WhenAll(filteringFuture[(unsigned)ShaderStage::Vertex], filteringFuture[(unsigned)ShaderStage::Geometry], preconfigurationFuture).CheckImmediately().ThenConstructToPromise(
							std::move(promise),
							[pipelineDesc]( std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> vsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> gsFiltering,
								std::shared_ptr<ShaderSourceParser::SelectorPreconfiguration> preconfiguration) {
								
								auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Vertex] = std::move(vsFiltering);
								finalObject->_automaticFiltering[(unsigned)ShaderStage::Geometry] = std::move(gsFiltering);
								finalObject->_preconfiguration = preconfiguration;
								finalObject->_pipelineDesc = pipelineDesc;
								return finalObject;
							});
					}

				} else
					Throw(std::runtime_error("Missing shader stages while building filtering rules"));
			} CATCH(...) {
				promise.set_exception(std::current_exception());
			} CATCH_END
		}
	};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Type>
		std::vector<Type> AsVector(IteratorRange<const Type*> range) { return std::vector<Type>{range.begin(), range.end()}; }

	static void MergeInPipelineLayoutInitializer(
		PipelineLayoutInitializer& srcAndDst,
		const PipelineLayoutInitializer& one)
	{
		unsigned descSet=0;
		for (;descSet < srcAndDst.GetDescriptorSets().size() && descSet < one.GetDescriptorSets().size(); ++descSet) {
			auto& d = srcAndDst._descriptorSets[descSet];
			auto& s = one.GetDescriptorSets()[descSet];

			if (d._signature._slots.size() < s._signature._slots.size()) {
				d._signature._slots.resize(s._signature._slots.size());
				d._signature._slotNames.resize(s._signature._slotNames.size());
			}

			unsigned slot=0;
			for (;slot < d._signature._slots.size() && slot < s._signature._slots.size(); ++slot) {
				if (d._signature._slots[slot]._type != DescriptorType::Empty && s._signature._slots[slot]._type != DescriptorType::Empty) {
					if (d._signature._slots[slot]._type != s._signature._slots[slot]._type)
						Throw(std::runtime_error(StringMeld<256>() << "Descriptor set slot conflict when merging slot (" << slot << ") of desc set (" << descSet << ")"));
				} else if (s._signature._slots[slot]._type != DescriptorType::Empty) {
					d._signature._slots[slot] = s._signature._slots[slot];
					d._signature._slotNames[slot] = s._signature._slotNames[slot];
					if (s._signature._fixedSamplers.size() > slot && s._signature._fixedSamplers[slot]) {
						if (d._signature._fixedSamplers.size() < s._signature._fixedSamplers.size())
							d._signature._fixedSamplers.resize(s._signature._fixedSamplers.size());
						d._signature._fixedSamplers[slot] = s._signature._fixedSamplers[slot];
					}
				}
			}
		}

		while (srcAndDst._descriptorSets.size() < one.GetDescriptorSets().size()) {
			auto& s = one.GetDescriptorSets()[srcAndDst._descriptorSets.size()];
			srcAndDst._descriptorSets.push_back(s);
		}

		for (const auto& s:one.GetPushConstants()) {
			auto i = std::find_if(srcAndDst.GetPushConstants().begin(), srcAndDst.GetPushConstants().end(),
				[shaderStage = s._shaderStage](const auto& c) { return c._shaderStage == shaderStage; });
			if (i!=srcAndDst.GetPushConstants().end())
				Throw(std::runtime_error(StringMeld<256>() << "Conflict in push constants for shader stage (" << AsString(s._shaderStage) << ")"));
			srcAndDst._pushConstants.push_back(*i);
		}
	}

	static std::shared_ptr<ICompiledPipelineLayout> MakeCompiledPipelineLayout(
		IDevice& d,
		PipelineLayoutOptions&& pipelineLayout,
		const CompiledShaderByteCode& code0)
	{
		if (pipelineLayout._prebuiltPipelineLayout) {
			return std::move(pipelineLayout._prebuiltPipelineLayout);
		} else {
			auto initializer = Metal::BuildPipelineLayoutInitializer(code0);
			return d.CreatePipelineLayout(initializer);
		}
	}

	static std::shared_ptr<ICompiledPipelineLayout> MakeCompiledPipelineLayout(
		IDevice& d,
		PipelineLayoutOptions&& pipelineLayout,
		const CompiledShaderByteCode& code0,
		const CompiledShaderByteCode& code1)
	{
		if (pipelineLayout._prebuiltPipelineLayout) {
			return std::move(pipelineLayout._prebuiltPipelineLayout);
		} else {
			auto initializer = Metal::BuildPipelineLayoutInitializer(code0);
			MergeInPipelineLayoutInitializer(initializer, Metal::BuildPipelineLayoutInitializer(code1));
			return d.CreatePipelineLayout(initializer);
		}
	}

	static std::shared_ptr<ICompiledPipelineLayout> MakeCompiledPipelineLayout(
		IDevice& d,
		PipelineLayoutOptions&& pipelineLayout,
		const CompiledShaderByteCode& code0,
		const CompiledShaderByteCode& code1,
		const CompiledShaderByteCode& code2)
	{
		if (pipelineLayout._prebuiltPipelineLayout) {
			return std::move(pipelineLayout._prebuiltPipelineLayout);
		} else {
			auto initializer = Metal::BuildPipelineLayoutInitializer(code0);
			MergeInPipelineLayoutInitializer(initializer, Metal::BuildPipelineLayoutInitializer(code1));
			MergeInPipelineLayoutInitializer(initializer, Metal::BuildPipelineLayoutInitializer(code2));
			return d.CreatePipelineLayout(initializer);
		}
	}

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

	static std::shared_future<CompiledShaderByteCode> MakeByteCodeFuture(
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
			return *reinterpret_cast<std::shared_future<CompiledShaderByteCode>*>(&res);
		} else {
			return ::Assets::MakeAsset<CompiledShaderByteCode>(MakeStringSection(temp), adjustedDefinesTable);
		}
	}

	struct GraphicsPipelineRetainedConstructionParams
	{
		std::shared_ptr<GraphicsPipelineDesc> _pipelineDesc;
		struct InputAssemblyStates
		{
			std::vector<InputElementDesc> _inputAssembly;
			std::vector<MiniInputElementDesc> _miniInputAssembly;
		};
		InputAssemblyStates _ia;
		Topology _topology;
		FrameBufferDesc _fbDesc;
		unsigned _subpassIdx = 0;

		#if defined(_DEBUG)
			GraphicsPipelineAndLayout::DebugInfo _debugInfo;
		#endif
	};	

	static GraphicsPipelineAndLayout MakeGraphicsPipelineAndLayout(
		const Metal::ShaderProgram& shader,
		const GraphicsPipelineRetainedConstructionParams& params)
	{
		Metal::GraphicsPipelineBuilder builder;
		builder.Bind(shader);
		builder.Bind(params._pipelineDesc->_blend);
		builder.Bind(params._pipelineDesc->_depthStencil);
		builder.Bind(params._pipelineDesc->_rasterization);

		if (!params._ia._inputAssembly.empty()) {
			Metal::BoundInputLayout boundIA(MakeIteratorRange(params._ia._inputAssembly), shader);
			assert(boundIA.AllAttributesBound());
			builder.Bind(boundIA, params._topology);
		} else {
			Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(params._ia._miniInputAssembly), 0 };
			Metal::BoundInputLayout boundIA(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
			assert(boundIA.AllAttributesBound());
			builder.Bind(boundIA, params._topology);
		}

		builder.SetRenderPassConfiguration(params._fbDesc, params._subpassIdx);

		auto pipeline = builder.CreatePipeline(Metal::GetObjectFactory());
		auto depVal = pipeline->GetDependencyValidation();
		return GraphicsPipelineAndLayout { 
			std::move(pipeline), shader.GetPipelineLayout(), std::move(depVal)
			#if defined(_DEBUG)
				, params._debugInfo
			#endif
			};
	}

	static void MakeGraphicsPipelineFuture0(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		const std::shared_ptr<IDevice>& device,
		std::shared_future<CompiledShaderByteCode> byteCodeFuture[3],
		PipelineLayoutOptions&& pipelineLayout,
		GraphicsPipelineRetainedConstructionParams&& params)
	{
		if (!byteCodeFuture[(unsigned)ShaderStage::Vertex].valid())
			Throw(std::runtime_error("Missing vertex shader stage while building shader program"));

		if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && !byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompiledPipelineLayout(*d, std::move(pipelineLayout), vsCode, psCode);
					Metal::ShaderProgram shaderProgram{
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, psCode};
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode,
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompiledPipelineLayout(*d, std::move(pipelineLayout), vsCode, psCode, gsCode);
					Metal::ShaderProgram shaderProgram(
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, gsCode, psCode,
						StreamOutputInitializers{params._pipelineDesc->_soElements, params._pipelineDesc->_soBufferStrides});
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (!byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompiledPipelineLayout(*d, std::move(pipelineLayout), vsCode, gsCode);
					Metal::ShaderProgram shaderProgram(
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, gsCode, CompiledShaderByteCode{},
						StreamOutputInitializers{params._pipelineDesc->_soElements, params._pipelineDesc->_soBufferStrides});
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else
			Throw(std::runtime_error("Missing shader stages while building shader program"));
	}

	// Make a final pipeline laaout (for a graphics pipeline) including filling in "auto" descriptor sets as necessary
	static std::shared_ptr<ICompiledPipelineLayout> MakeCompatibleCompiledPipelineLayout(
		IDevice& device,
		SamplerPool* samplerPool,
		RenderCore::Assets::PredefinedPipelineLayout& predefinedPipelineLayout,
		const CompiledShaderByteCode* vsByteCode,
		const CompiledShaderByteCode* psByteCode=nullptr,
		const CompiledShaderByteCode* gsByteCode=nullptr)
	{
		std::shared_ptr<ICompiledPipelineLayout> finalPipelineLayout;
		if (predefinedPipelineLayout.HasAutoDescriptorSets()) {
			PipelineLayoutInitializer layoutInits[3];
			const PipelineLayoutInitializer* layoutPtrs[3];
			unsigned layoutInitCount = 0;
			if (vsByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*vsByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
			}
			if (psByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*psByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
			}
			if (gsByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*gsByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
			}
			auto initializer = predefinedPipelineLayout.MakePipelineLayoutInitializerWithAutoMatching(
				MakeIteratorRange(layoutPtrs, &layoutPtrs[layoutInitCount]), GetDefaultShaderLanguage(), samplerPool);
			finalPipelineLayout = device.CreatePipelineLayout(initializer);
		} else {
			auto initializer = predefinedPipelineLayout.MakePipelineLayoutInitializer(GetDefaultShaderLanguage(), samplerPool);
			finalPipelineLayout = device.CreatePipelineLayout(initializer);
		}
		return finalPipelineLayout;
	}

	static void MakeGraphicsPipelineFuture1(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<SamplerPool>& samplerPool,
		std::shared_future<CompiledShaderByteCode> byteCodeFuture[3],
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>&& pipelineLayout,
		const GraphicsPipelineRetainedConstructionParams& params)
	{
		if (!byteCodeFuture[(unsigned)ShaderStage::Vertex].valid())
			Throw(std::runtime_error("Missing vertex shader stage while building shader program"));

		if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && !byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, samplerPool, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, samplerPool.get(), *pipelineLayout, &vsCode, &psCode);
					Metal::ShaderProgram shaderProgram{
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, psCode};
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, samplerPool, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode,
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, samplerPool.get(), *pipelineLayout, &vsCode, &psCode);
					Metal::ShaderProgram shaderProgram(
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, gsCode, psCode,
						StreamOutputInitializers{params._pipelineDesc->_soElements, params._pipelineDesc->_soBufferStrides});
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (!byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, samplerPool, params=std::move(params)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, samplerPool.get(), *pipelineLayout, &vsCode, &gsCode);
					Metal::ShaderProgram shaderProgram(
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, gsCode, CompiledShaderByteCode{},
						StreamOutputInitializers{params._pipelineDesc->_soElements, params._pipelineDesc->_soBufferStrides});
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else
			Throw(std::runtime_error("Missing shader stages while building shader program"));
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static ComputePipelineAndLayout MakeComputePipelineAndLayout(
		const CompiledShaderByteCode& csCode,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::DependencyValidation& pipelineLayoutDepVal)
	{
		Metal::ComputeShader shader{Metal::GetObjectFactory(), pipelineLayout, csCode};
		Metal::ComputePipelineBuilder builder;
		builder.Bind(shader);
		auto pipeline = builder.CreatePipeline(Metal::GetObjectFactory());
		::Assets::DependencyValidation depVal; 
		if (pipelineLayoutDepVal) {
			depVal = ::Assets::GetDepValSys().Make();
			depVal.RegisterDependency(pipeline->GetDependencyValidation());
			depVal.RegisterDependency(pipelineLayoutDepVal);
		} else
			depVal = pipeline->GetDependencyValidation();
		return ComputePipelineAndLayout { std::move(pipeline), pipelineLayout, std::move(depVal) };
	}

	static void MakeComputePipelineFuture0(
		std::promise<ComputePipelineAndLayout>&& promise,
		const std::shared_ptr<IDevice>& device,
		std::shared_future<CompiledShaderByteCode> csCode,
		PipelineLayoutOptions&& pipelineLayout)
	{
		// Variation without a PredefinedPipelineLayout
		::Assets::WhenAll(std::move(csCode)).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}](const auto& csCodeActual) mutable {
				auto d = weakDevice.lock();
				if (!d) Throw(std::runtime_error("Device shutdown before completion"));
				auto pipelineLayoutActual = MakeCompiledPipelineLayout(*d, std::move(pipelineLayout), csCodeActual);
				return MakeComputePipelineAndLayout(csCodeActual, pipelineLayoutActual, {});
			});
	}

	static void MakeComputePipelineFuture1(
		std::promise<ComputePipelineAndLayout>&& promise,
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<SamplerPool>& samplerPool,
		std::shared_future<CompiledShaderByteCode> csCode,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>&& pipelineLayout)
	{
		// Variation for MakePipelineLayoutInitializerWithAutoMatching
		::Assets::WhenAll(std::move(csCode)).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[weakDevice=std::weak_ptr<IDevice>{device}, samplerPool=samplerPool, predefinedPipelineLayout=std::move(pipelineLayout)](const auto& csCodeActual) {
				auto d = weakDevice.lock();
				if (!d) Throw(std::runtime_error("Device shutdown before completion"));

				// This case is a little more complicated because we need to generate a pipeline layout 
				// (potentially using the shader byte code)
				auto finalPipelineLayout = MakeCompatibleCompiledPipelineLayout(*d, samplerPool.get(), *predefinedPipelineLayout, &csCodeActual);
				return MakeComputePipelineAndLayout(csCodeActual, finalPipelineLayout, predefinedPipelineLayout->GetDependencyValidation());
			});
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

	class SharedPools : public std::enable_shared_from_this<SharedPools>
	{
	public:
		Threading::Mutex _lock;
		UniqueShaderVariationSet _selectorVariationsSet;
		std::shared_ptr<SamplerPool> _samplerPool;
		std::shared_ptr<IDevice> _device;
		using PendingUpdateId = unsigned;

		struct WeakGraphicsPipelineAndLayout
		{
			std::weak_ptr<Metal::GraphicsPipeline> _pipeline;
			std::weak_ptr<ICompiledPipelineLayout> _layout;
			::Assets::DependencyValidation _depVal;
			#if defined(_DEBUG)
				GraphicsPipelineAndLayout::DebugInfo _debugInfo;
			#endif
		};
		std::vector<std::pair<uint64_t, WeakGraphicsPipelineAndLayout>> _completedGraphicsPipelines;
		std::vector<std::pair<uint64_t, std::pair<PendingUpdateId, std::shared_future<GraphicsPipelineAndLayout>>>> _pendingGraphicsPipelines;

		std::shared_future<GraphicsPipelineAndLayout> CreateGraphicsPipelineAlreadyLocked(
			const VertexInputStates& ia,
			const std::shared_ptr<Internal::GraphicsPipelineDescWithFilteringRules>& pipelineDescWithFiltering,
			PipelineLayoutOptions&& pipelineLayout,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const UniqueShaderVariationSet::FilteredSelectorSet*> filteredSelectors,
			const FrameBufferTarget& fbTarget)
		{
			uint64_t hash = pipelineLayout._hashCode;
			if (compiledPatchCollection)
			 	hash = HashCombine(compiledPatchCollection->GetGUID(), hash);
			for (auto s:filteredSelectors)
				if (s._hashValue)
					hash = HashCombine(s._hashValue, hash);
			hash = HashCombine(fbTarget.GetHash(), hash);
			hash = HashCombine(ia.GetHash(), hash);

			// we need to hash specific parts of the graphics pipeline desc -- only those parts that we'll use below
			// some parts of the pipeline desc (eg, the selectors) have already been used to create other inputs here
			// we don't want to use use them, because they may be more aggressively filtered in the secondary products
			// (particularly for the filtered selectors)
			auto* pipelineDesc = pipelineDescWithFiltering->_pipelineDesc.get();
			hash = pipelineDesc->CalculateHashNoSelectors(hash);

			auto completedi = LowerBound(_completedGraphicsPipelines, hash);
			if (completedi != _completedGraphicsPipelines.end() && completedi->first == hash) {
				auto pipeline = completedi->second._pipeline.lock();
				auto layout = completedi->second._layout.lock();
				if (pipeline && pipeline->GetDependencyValidation().GetValidationIndex() == 0 && layout) {
					// we can return an already completed pipeline
					std::promise<GraphicsPipelineAndLayout> promise;
					GraphicsPipelineAndLayout pipelineAndLayout{std::move(pipeline), std::move(layout), completedi->second._depVal};
					#if defined(_DEBUG)
						pipelineAndLayout._debugInfo = completedi->second._debugInfo;
					#endif
					promise.set_value(std::move(pipelineAndLayout));
					return promise.get_future();
				}
			}

			auto i = LowerBound(_pendingGraphicsPipelines, hash);
			if (i!=_pendingGraphicsPipelines.end() && i->first == hash)
				return i->second.second;

			#if 0
				Log(Verbose) << "Building pipeline for pipeline accelerator: " << std::endl;
				Log(Verbose) << "\tPipeline layout: " << pipelineLayout->GetGUID() << " (" << (size_t)pipelineLayout.get() << ")" << std::endl;
				Log(Verbose) << "\tFB relevance: " << sequencerCfg._fbRelevanceValue << std::endl;
				Log(Verbose) << "\tIA: " << ia._hash << std::endl;
				if (compiledPatchCollection)
					Log(Verbose) << "\tPatch collection: " << compiledPatchCollection->GetGUID() << std::endl;
				else
					Log(Verbose) << "\tNo patch collection" << std::endl;
				for (unsigned c=0; c<filteredSelectors.size(); ++c) {
					if (!filteredSelectors[c]._hashValue) continue;
					Log(Verbose) << "\tFiltered selectors[" << c << "]: " << filteredSelectors[c]._selectors << std::endl;
				}
			#endif

			StreamOutputInitializers so;
			so._outputElements = MakeIteratorRange(pipelineDesc->_soElements);
			so._outputBufferStrides = MakeIteratorRange(pipelineDesc->_soBufferStrides);
			std::shared_future<CompiledShaderByteCode> byteCodeFutures[3];
			for (unsigned c=0; c<3; ++c) {
				if (pipelineDesc->_shaders[c].empty())
					continue;
				byteCodeFutures[c] = MakeByteCodeFuture((ShaderStage)c, pipelineDesc->_shaders[c], filteredSelectors[c], compiledPatchCollection, pipelineDesc->_patchExpansions, so);
			}

			GraphicsPipelineRetainedConstructionParams constructionParams;
			constructionParams._pipelineDesc = pipelineDescWithFiltering->_pipelineDesc;
			constructionParams._ia._inputAssembly = AsVector(ia._inputAssembly);
			constructionParams._ia._miniInputAssembly = AsVector(ia._miniInputAssembly);
			constructionParams._topology = ia._topology;
			constructionParams._fbDesc = *fbTarget._fbDesc;
			constructionParams._subpassIdx = fbTarget._subpassIdx;

			#if defined(_DEBUG)
				constructionParams._debugInfo._vsDescription = Internal::MakeShaderDescription(ShaderStage::Vertex, *pipelineDesc, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Vertex]);
				constructionParams._debugInfo._psDescription = Internal::MakeShaderDescription(ShaderStage::Pixel, *pipelineDesc, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Pixel]);
				constructionParams._debugInfo._gsDescription = Internal::MakeShaderDescription(ShaderStage::Geometry, *pipelineDesc, compiledPatchCollection, filteredSelectors[(unsigned)ShaderStage::Geometry]);
			#endif

			std::promise<GraphicsPipelineAndLayout> promise;
			std::shared_future<GraphicsPipelineAndLayout> result = promise.get_future();
			if (pipelineLayout._predefinedPipelineLayout) {
				MakeGraphicsPipelineFuture1(std::move(promise), _device, _samplerPool, byteCodeFutures, std::move(pipelineLayout._predefinedPipelineLayout), std::move(constructionParams));
			} else {
				MakeGraphicsPipelineFuture0(std::move(promise), _device, byteCodeFutures, std::move(pipelineLayout), std::move(constructionParams));
			}

			AddGraphicsPipelineFuture(result, hash);
			return result;
		}

		void AddGraphicsPipelineFuture(const std::shared_future<GraphicsPipelineAndLayout>& future, uint64_t hash)
		{
			auto updateId = _nextPendingUpdateId++;
			auto i = LowerBound(_pendingGraphicsPipelines, hash);
			if (i!=_pendingGraphicsPipelines.end() && i->first == hash) {
				i->second.first = updateId;
				i->second.second = future;
			} else
				_pendingGraphicsPipelines.insert(i, std::make_pair(hash, std::make_pair(updateId, future)));

			::Assets::WhenAll(future).Then(
				[weakThis=weak_from_this(), hash, updateId](std::shared_future<GraphicsPipelineAndLayout> completedFuture) {
					auto t = weakThis.lock();
					if (!t) return;
					
					TRY {
						ScopedLock(t->_lock);

						auto actualized = completedFuture.get();

						{
							auto i = LowerBound(t->_pendingGraphicsPipelines, hash);
							assert(i!=t->_pendingGraphicsPipelines.end() && i->first == hash);
							if (i!=t->_pendingGraphicsPipelines.end() && i->first == hash) {
								if (i->second.first != updateId)
									return;		// possibly scheduled a replacement while the first was still pending
								t->_pendingGraphicsPipelines.erase(i);
							}
						}

						WeakGraphicsPipelineAndLayout weakPtrs;
						weakPtrs._pipeline = actualized._pipeline;
						weakPtrs._layout = actualized._layout;
						weakPtrs._depVal = actualized._depVal;
						#if defined(_DEBUG)
							weakPtrs._debugInfo = actualized._debugInfo;
						#endif

						auto completedi = LowerBound(t->_completedGraphicsPipelines, hash);
						if (completedi != t->_completedGraphicsPipelines.end() && completedi->first == hash) {
							completedi->second = std::move(weakPtrs);
						} else
							t->_completedGraphicsPipelines.insert(completedi, std::make_pair(hash, std::move(weakPtrs)));
					} CATCH(...) {
						// Invalid futures stay in the "_pendingGraphicsPipelines" list
					} CATCH_END
				});
		}

		std::shared_future<ComputePipelineAndLayout> CreateComputePipelineAlreadyLocked(
			StringSection<> shader,
			PipelineLayoutOptions&& pipelineLayout,
			const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
			IteratorRange<const std::pair<uint64_t, ShaderStage>*> patchExpansions,
			const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors)
		{
			auto hash = Hash64(shader, filteredSelectors._hashValue);
			hash = HashCombine(pipelineLayout._hashCode, hash);
			if (compiledPatchCollection && !patchExpansions.empty()) {
				hash = HashCombine(hash, compiledPatchCollection->GetGUID());
				for (auto& p:patchExpansions) {
					assert(p.second == ShaderStage::Compute);
					hash = HashCombine(p.first, hash);
				}
			}

			auto completedi = LowerBound(_completedComputePipelines, hash);
			if (completedi != _completedComputePipelines.end() && completedi->first == hash) {
				auto pipeline = completedi->second._pipeline.lock();
				auto layout = completedi->second._layout.lock();
				if (pipeline && completedi->second._depVal.GetValidationIndex() == 0 && layout) {
					// we can return an already completed pipeline
					std::promise<ComputePipelineAndLayout> result;
					result.set_value(ComputePipelineAndLayout{std::move(pipeline), std::move(layout), completedi->second._depVal});
					return result.get_future();
				}
			}

			auto i = LowerBound(_pendingComputePipelines, hash);
			if (i!=_pendingComputePipelines.end() && i->first == hash)
				return i->second.second;

			// Make the futures and setup caching
			auto byteCodeFuture = MakeByteCodeFuture(ShaderStage::Compute, shader, filteredSelectors, compiledPatchCollection, patchExpansions);
			std::promise<ComputePipelineAndLayout> promise;
			std::shared_future<ComputePipelineAndLayout> result = promise.get_future();
			if (pipelineLayout._predefinedPipelineLayout) {
				MakeComputePipelineFuture1(std::move(promise), _device, _samplerPool, byteCodeFuture, std::move(pipelineLayout._predefinedPipelineLayout));
			} else {
				MakeComputePipelineFuture0(std::move(promise), _device, byteCodeFuture, std::move(pipelineLayout));
			}
			AddComputePipelineFuture(result, hash);
			return result;
		};

		void AddComputePipelineFuture(const std::shared_future<ComputePipelineAndLayout>& future, uint64_t hash)
		{
			auto updateId = _nextPendingUpdateId++;
			auto i = LowerBound(_pendingComputePipelines, hash);
			if (i!=_pendingComputePipelines.end() && i->first == hash) {
				i->second.first = updateId;
				i->second.second = future;
			} else
				_pendingComputePipelines.insert(i, std::make_pair(hash, std::make_pair(updateId, future)));

			::Assets::WhenAll(future).Then(
				[weakThis=weak_from_this(), hash, updateId](std::shared_future<ComputePipelineAndLayout> completedFuture) {
					auto t = weakThis.lock();
					if (!t) return;

					TRY {
						ScopedLock(t->_lock);

						auto actualized = completedFuture.get();

						{
							auto i = LowerBound(t->_pendingComputePipelines, hash);
							assert(i!=t->_pendingComputePipelines.end() && i->first == hash);
							if (i!=t->_pendingComputePipelines.end() && i->first == hash) {
								if (i->second.first != updateId)
									return;		// possibly scheduled a replacement while the first was still pending
								t->_pendingComputePipelines.erase(i);
							}
						}

						WeakComputePipelineAndLayout weakPtrs;
						weakPtrs._pipeline = actualized._pipeline;
						weakPtrs._layout = actualized._layout;
						weakPtrs._depVal = actualized._depVal;

						auto completedi = LowerBound(t->_completedComputePipelines, hash);
						if (completedi != t->_completedComputePipelines.end() && completedi->first == hash) {
							completedi->second = std::move(weakPtrs);
						} else
							t->_completedComputePipelines.insert(completedi, std::make_pair(hash, std::move(weakPtrs)));

					} CATCH(...) {
						// Invalid futures stay in the "_pendingComputePipelines" list
					} CATCH_END
				});
		}

		struct WeakComputePipelineAndLayout
		{
			std::weak_ptr<Metal::ComputePipeline> _pipeline;
			std::weak_ptr<ICompiledPipelineLayout> _layout;
			::Assets::DependencyValidation _depVal;
		};
		std::vector<std::pair<uint64_t, WeakComputePipelineAndLayout>> _completedComputePipelines;
		std::vector<std::pair<uint64_t, std::pair<PendingUpdateId, std::shared_future<ComputePipelineAndLayout>>>> _pendingComputePipelines;

		UniqueShaderVariationSet::FilteredSelectorSet FilterSelectorsAlreadyLocked(
			ShaderStage shaderStage,
			IteratorRange<const ParameterBox*const*> selectors,
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
			if (compiledPatchCollection) {
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
			} else {
				assert(patchExpansions.empty());		// without a CompiledShaderPatchCollection we can't do anything with "patchExpansions"
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
		std::shared_future<CompiledShaderByteCode> MakeByteCodeFuture(
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

		PendingUpdateId _nextPendingUpdateId = 1;
	};

}}}

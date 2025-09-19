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
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IArtifact.h"
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

		static std::shared_future<std::shared_ptr<ShaderSourceParser::SelectorFilteringRules>> BuildFutureFiltering(
			const Internal::ShaderVariant& variantShader)
		{
			if (std::holds_alternative<ShaderCompileResourceName>(variantShader)) {
				auto& name = std::get<ShaderCompileResourceName>(variantShader);
				assert(!name._filename.empty());
				return ::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorFilteringRules>(name._filename);
			} else if (std::holds_alternative<ShaderCompilePatchResource>(variantShader)) {
				// We can return filtering rules for anything that's not included with the CompileShaderPatchCollection
				auto& name = std::get<ShaderCompilePatchResource>(variantShader);
				if (!name._entrypoint._filename.empty())
					return ::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorFilteringRules>(name._entrypoint._filename);
				return {};
			}
			return {};
		}

		static void InitializePromise(
			std::promise<std::shared_ptr<GraphicsPipelineDescWithFilteringRules>>&& promise,
			const std::shared_ptr<GraphicsPipelineDesc>& pipelineDesc)
		{
			TRY {
				std::shared_future<std::shared_ptr<ShaderSourceParser::SelectorFilteringRules>> filteringFuture[3];
				for (unsigned c=0; c<3; ++c)
					filteringFuture[c] = BuildFutureFiltering(pipelineDesc->_shaders[c]);

				if (!filteringFuture[(unsigned)ShaderStage::Vertex].valid()) {
					assert(!filteringFuture[(unsigned)ShaderStage::Pixel].valid());
					assert(!filteringFuture[(unsigned)ShaderStage::Geometry].valid());
					auto finalObject = std::make_shared<GraphicsPipelineDescWithFilteringRules>();
					finalObject->_pipelineDesc = pipelineDesc;
					promise.set_value(std::move(finalObject));
					return;
				}

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
						auto preconfigurationFuture = ::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
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
						auto preconfigurationFuture = ::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
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
						auto preconfigurationFuture = ::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorPreconfiguration>(pipelineDesc->_materialPreconfigurationFile, pipelineDesc->_techniquePreconfigurationFile);
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
			StringMeld<256> meld;
			meld << "AutoLayout[" << code0.GetIdentifier() << "]";
			return d.CreatePipelineLayout(initializer, meld.AsStringSection());
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
			StringMeld<256> meld;
			meld << "AutoLayout[" << code0.GetIdentifier() << ", " << code1.GetIdentifier() << "]";
			return d.CreatePipelineLayout(initializer, meld);
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
			StringMeld<256> meld;
			meld << "AutoLayout[" << code0.GetIdentifier() << ", " << code1.GetIdentifier() << ", " << code2.GetIdentifier() << "]";
			return d.CreatePipelineLayout(initializer, meld.AsStringSection());
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::string BuildSODefinesString(IteratorRange<const RenderCore::InputElementDesc*> desc)
	{
		using ::Hash64;
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
	
	static void AdjustForStage(ShaderCompileResourceName& result, ShaderStage stage)
	{
		if (!result._shaderModel.empty()) return;
		switch (stage) {
			case ShaderStage::Vertex: result._shaderModel = s_SMVS; break;
			case ShaderStage::Geometry: result._shaderModel = s_SMGS; break;
			case ShaderStage::Pixel: result._shaderModel = s_SMPS; break;
			case ShaderStage::Domain: result._shaderModel = s_SMDS; break;
			case ShaderStage::Hull: result._shaderModel = s_SMHS; break;
			case ShaderStage::Compute: result._shaderModel = s_SMCS; break;
			default: UNREACHABLE(); break;
		}
	}

	static std::shared_future<CompiledShaderByteCode> MakeByteCodeFuture(
		ShaderStage stage, const Internal::ShaderVariant& variant,
		const std::string& definesTable,
		StreamOutputInitializers so)
	{
		auto adjustedDefinesTable = definesTable;
		if (stage == ShaderStage::Geometry && !so._outputElements.empty()) {
			if (!definesTable.empty()) adjustedDefinesTable += ";";
			adjustedDefinesTable += BuildSODefinesString(so._outputElements);
		}

		if (std::holds_alternative<ShaderCompileResourceName>(variant)) {
			auto name = std::get<ShaderCompileResourceName>(variant);
			AdjustForStage(name, stage);
			return ::Assets::GetAssetFuture<CompiledShaderByteCode>(name, adjustedDefinesTable);
		} else if (std::holds_alternative<ShaderCompilePatchResource>(variant)) {
			auto res = std::get<ShaderCompilePatchResource>(variant);
			AdjustForStage(res._entrypoint, stage);
			auto result = ::Assets::GetAssetFuture<CompiledShaderByteCode_InstantiateShaderGraph>(res, adjustedDefinesTable);
			return *reinterpret_cast<std::shared_future<CompiledShaderByteCode>*>(&result);

		} else
			return {};
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

	[[noreturn]] static void ThrowUnboundAttributesExceptions(const Metal::ShaderProgram& shader, Metal::BoundInputLayout& boundIA)
	{
		auto unboundAttributes = boundIA.FindUnboundShaderAttributes(shader);
		std::stringstream str;
		str << "Vertex input attributes (";
		if (!unboundAttributes.empty()) {
			str << *unboundAttributes.begin();
			for (auto i=unboundAttributes.begin()+1; i!=unboundAttributes.end(); ++i) str << ", " << *i;
		}
		str << ") unbound for shader (" << shader.GetCompiledCode(ShaderStage::Vertex).GetIdentifier() << ")";
		Throw(std::runtime_error(str.str()));
	}

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
			if (expect_evaluation(!boundIA.AllAttributesBound(), false))
				ThrowUnboundAttributesExceptions(shader, boundIA);
			builder.Bind(boundIA, params._topology);
		} else {
			Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(params._ia._miniInputAssembly), 0 };
			Metal::BoundInputLayout boundIA(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
			if (expect_evaluation(!boundIA.AllAttributesBound(), false))
				ThrowUnboundAttributesExceptions(shader, boundIA);
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

	static std::shared_ptr<ICompiledPipelineLayout> MakeCompiledPipelineLayout(
		IDevice& device,
		PipelineLayoutPool* pipelineLayoutPool,
		const PipelineLayoutInitializer& initializer,
		StringSection<> name)
	{
		if (pipelineLayoutPool) {
			return pipelineLayoutPool->GetPipelineLayout(initializer, name);
		} else
			return device.CreatePipelineLayout(initializer, name);
	}

	// Make a final pipeline layout (for a graphics pipeline) including filling in "auto" descriptor sets as necessary
	static std::shared_ptr<ICompiledPipelineLayout> MakeCompatibleCompiledPipelineLayout(
		IDevice& device,
		PipelineLayoutPool* pipelineLayoutPool, SamplerPool* samplerPool,
		RenderCore::Assets::PredefinedPipelineLayout& predefinedPipelineLayout,
		StringSection<> pipelineLayoutInitializer,
		const CompiledShaderByteCode* vsByteCode,
		const CompiledShaderByteCode* psByteCode=nullptr,
		const CompiledShaderByteCode* gsByteCode=nullptr)
	{
		std::shared_ptr<ICompiledPipelineLayout> finalPipelineLayout;
		if (predefinedPipelineLayout.HasAutoDescriptorSets()) {
			PipelineLayoutInitializer layoutInits[3];
			const PipelineLayoutInitializer* layoutPtrs[3];
			unsigned layoutInitCount = 0;
			StringMeld<256> meld;
			meld << pipelineLayoutInitializer;
			if (vsByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*vsByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
				meld << "[" << vsByteCode->GetIdentifier() << "]";
			}
			if (psByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*psByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
				meld << "[" << psByteCode->GetIdentifier() << "]";
			}
			if (gsByteCode) {
				layoutInits[layoutInitCount] = Metal::BuildPipelineLayoutInitializer(*gsByteCode);
				layoutPtrs[layoutInitCount] = &layoutInits[layoutInitCount];
				++layoutInitCount;
				meld << "[" << gsByteCode->GetIdentifier() << "]";
			}
			auto initializer = predefinedPipelineLayout.MakePipelineLayoutInitializerWithAutoMatching(
				MakeIteratorRange(layoutPtrs, &layoutPtrs[layoutInitCount]), GetDefaultShaderLanguage(), samplerPool);
			return MakeCompiledPipelineLayout(device, pipelineLayoutPool, initializer, meld.AsStringSection());
		} else {
			auto initializer = predefinedPipelineLayout.MakePipelineLayoutInitializer(GetDefaultShaderLanguage(), samplerPool);
			return MakeCompiledPipelineLayout(device, pipelineLayoutPool, initializer, pipelineLayoutInitializer);
		}
	}

	static void MakeGraphicsPipelineFuture1(
		std::promise<GraphicsPipelineAndLayout>&& promise,
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<PipelineLayoutPool>& pipelineLayoutPool,
		const std::shared_ptr<SamplerPool>& samplerPool,
		std::shared_future<CompiledShaderByteCode> byteCodeFuture[3],
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>&& pipelineLayout,
		std::string&& pipelineLayoutInitializer,
		const GraphicsPipelineRetainedConstructionParams& params)
	{
		if (!byteCodeFuture[(unsigned)ShaderStage::Vertex].valid())
			Throw(std::runtime_error("Missing vertex shader stage while building shader program"));

		if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && !byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, pipelineLayoutPool, samplerPool, params=std::move(params), pipelineLayoutInitializer=std::move(pipelineLayoutInitializer)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, pipelineLayoutPool.get(), samplerPool.get(), *pipelineLayout, pipelineLayoutInitializer, &vsCode, &psCode);
					Metal::ShaderProgram shaderProgram{
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, psCode};
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Pixel]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, pipelineLayoutPool, samplerPool, params=std::move(params), pipelineLayoutInitializer=std::move(pipelineLayoutInitializer)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& psCode,
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, pipelineLayoutPool.get(), samplerPool.get(), *pipelineLayout, pipelineLayoutInitializer, &vsCode, &psCode, &gsCode);
					Metal::ShaderProgram shaderProgram(
						Metal::GetObjectFactory(),
						pipelineLayoutActual, vsCode, gsCode, psCode,
						StreamOutputInitializers{params._pipelineDesc->_soElements, params._pipelineDesc->_soBufferStrides});
					return MakeGraphicsPipelineAndLayout(shaderProgram, params);
				});
		} else if (!byteCodeFuture[(unsigned)ShaderStage::Pixel].valid() && byteCodeFuture[(unsigned)ShaderStage::Geometry].valid()) {
			::Assets::WhenAll(std::move(byteCodeFuture[(unsigned)ShaderStage::Vertex]), std::move(byteCodeFuture[(unsigned)ShaderStage::Geometry])).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[pipelineLayout=std::move(pipelineLayout), weakDevice=std::weak_ptr<IDevice>{device}, pipelineLayoutPool, samplerPool, params=std::move(params), pipelineLayoutInitializer=std::move(pipelineLayoutInitializer)](
					const CompiledShaderByteCode& vsCode, 
					const CompiledShaderByteCode& gsCode) mutable {
					auto d = weakDevice.lock();
					if (!d) Throw(std::runtime_error("Device shutdown before completion"));

					auto pipelineLayoutActual = MakeCompatibleCompiledPipelineLayout(*d, pipelineLayoutPool.get(), samplerPool.get(), *pipelineLayout, pipelineLayoutInitializer, &vsCode, &gsCode);
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
		::Assets::DependencyValidationMarker subDepVals[] { pipeline->GetDependencyValidation(), pipelineLayoutDepVal };
		auto depVal = ::Assets::GetDepValSys().MakeOrReuse(subDepVals);
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
		const std::shared_ptr<PipelineLayoutPool>& pipelineLayoutPool,
		const std::shared_ptr<SamplerPool>& samplerPool,
		std::shared_future<CompiledShaderByteCode> csCode,
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout>&& pipelineLayout,
		std::string&& pipelineLayoutInitializer)
	{
		// Variation for MakePipelineLayoutInitializerWithAutoMatching
		::Assets::WhenAll(std::move(csCode)).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[weakDevice=std::weak_ptr<IDevice>{device}, pipelineLayoutPool, samplerPool, predefinedPipelineLayout=std::move(pipelineLayout), pipelineLayoutInitializer=std::move(pipelineLayoutInitializer)](const auto& csCodeActual) {
				auto d = weakDevice.lock();
				if (!d) Throw(std::runtime_error("Device shutdown before completion"));

				// This case is a little more complicated because we need to generate a pipeline layout 
				// (potentially using the shader byte code)
				auto finalPipelineLayout = MakeCompatibleCompiledPipelineLayout(*d, pipelineLayoutPool.get(), samplerPool.get(), *predefinedPipelineLayout, pipelineLayoutInitializer, &csCodeActual);
				return MakeComputePipelineAndLayout(csCodeActual, finalPipelineLayout, predefinedPipelineLayout->GetDependencyValidation());
			});
	}

#if defined(_DEBUG)
	static std::ostream& CompressFilename(std::ostream& str, StringSection<> path)
	{
		auto split = MakeFileNameSplitter(path);
		if (!split.StemAndPath().IsEmpty()) {
			return str << ".../" << split.FileAndExtension();
		} else
			return str << path;
	}

	static std::string MakeShaderDescription(
		ShaderStage stage,
		const GraphicsPipelineDesc& pipelineDesc,
 		const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors)
	{
		std::stringstream str;
		bool first = true;
		auto& variantShader = pipelineDesc._shaders[(unsigned)stage];
		if (std::holds_alternative<ShaderCompileResourceName>(variantShader)) {
			auto& name = std::get<ShaderCompileResourceName>(variantShader);

			const char* stageName[] = { "vs", "ps", "gs" };
			if (!first) str << ", "; first = false;
			str << stageName[(unsigned)stage] << ": ";
			CompressFilename(str, name._filename);
			str << ":" << name._entryPoint;
		} else if (std::holds_alternative<ShaderCompilePatchResource>(variantShader)) {
			const ShaderCompilePatchResource& r = std::get<ShaderCompilePatchResource>(variantShader);

			if (r._patchCollection)
				for (const auto& patch:r._patchCollection->GetInterface().GetPatches()) {
					if (!first) str << ", "; first = false;
					str << "patch: " << patch._originalEntryPointName;
				}
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
		std::shared_ptr<PipelineLayoutPool> _pipelineLayoutPool;
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
			IteratorRange<const UniqueShaderVariationSet::FilteredSelectorSet*> filteredSelectors,
			const FrameBufferTarget& fbTarget)
		{
			uint64_t hash = pipelineLayout._hashCode;
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
			if (i!=_pendingGraphicsPipelines.end() && i->first == hash) {
				// somewhat awkwardly, we need to check if the pending pipeline has an exception, but is invalidated. In these cases, we'll
				// go ahead and rebuild
				bool isInvalidated = false;
				TRY {
					if (i->second.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
						i->second.second.get();
				} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
					isInvalidated = e.GetDependencyValidation().GetValidationIndex() != 0;
				} CATCH_END

				// only return if we know it's not invalidated
				if (!isInvalidated)
					return i->second.second;
			}

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
			for (unsigned c=0; c<3; ++c)
				byteCodeFutures[c] = Internal::MakeByteCodeFuture((ShaderStage)c, pipelineDesc->_shaders[c], filteredSelectors[c]._selectors, so);

			GraphicsPipelineRetainedConstructionParams constructionParams;
			constructionParams._pipelineDesc = pipelineDescWithFiltering->_pipelineDesc;
			constructionParams._ia._inputAssembly = AsVector(ia._inputAssembly);
			constructionParams._ia._miniInputAssembly = AsVector(ia._miniInputAssembly);
			constructionParams._topology = ia._topology;
			constructionParams._fbDesc = *fbTarget._fbDesc;
			constructionParams._subpassIdx = fbTarget._subpassIdx;

			#if defined(_DEBUG)
				constructionParams._debugInfo._vsDescription = Internal::MakeShaderDescription(ShaderStage::Vertex, *pipelineDesc, filteredSelectors[(unsigned)ShaderStage::Vertex]);
				constructionParams._debugInfo._psDescription = Internal::MakeShaderDescription(ShaderStage::Pixel, *pipelineDesc, filteredSelectors[(unsigned)ShaderStage::Pixel]);
				constructionParams._debugInfo._gsDescription = Internal::MakeShaderDescription(ShaderStage::Geometry, *pipelineDesc, filteredSelectors[(unsigned)ShaderStage::Geometry]);
			#endif

			std::promise<GraphicsPipelineAndLayout> promise;
			std::shared_future<GraphicsPipelineAndLayout> result = promise.get_future();
			if (pipelineLayout._predefinedPipelineLayout) {
				MakeGraphicsPipelineFuture1(std::move(promise), _device, _pipelineLayoutPool, _samplerPool, byteCodeFutures, std::move(pipelineLayout._predefinedPipelineLayout), std::move(pipelineLayout._name), std::move(constructionParams));
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
			const Internal::ShaderVariant& shaderVariant,
			PipelineLayoutOptions&& pipelineLayout,
			const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors)
		{
			auto hash = Hash64(shaderVariant, filteredSelectors._hashValue);
			hash = HashCombine(pipelineLayout._hashCode, hash);

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
			if (i!=_pendingComputePipelines.end() && i->first == hash) {
				// somewhat awkwardly, we need to check if the pending pipeline has an exception, but is invalidated. In these cases, we'll
				// go ahead and rebuild
				bool isInvalidated = false;
				TRY {
					if (i->second.second.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
						i->second.second.get();
				} CATCH(const ::Assets::Exceptions::ExceptionWithDepVal& e) {
					isInvalidated = e.GetDependencyValidation().GetValidationIndex() != 0;
				} CATCH_END

				// only return if we know it's not invalidated
				if (!isInvalidated)
					return i->second.second;
			}

			// Make the futures and setup caching
			auto byteCodeFuture = MakeByteCodeFuture(ShaderStage::Compute, shaderVariant, filteredSelectors._selectors, {});
			std::promise<ComputePipelineAndLayout> promise;
			std::shared_future<ComputePipelineAndLayout> result = promise.get_future();
			if (pipelineLayout._predefinedPipelineLayout) {
				MakeComputePipelineFuture1(std::move(promise), _device, _pipelineLayoutPool, _samplerPool, byteCodeFuture, std::move(pipelineLayout._predefinedPipelineLayout), std::move(pipelineLayout._name));
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
			const ShaderSourceParser::SelectorFilteringRules* automaticFiltering,
			const ShaderSourceParser::ManualSelectorFiltering& manualFiltering,
			const ShaderSourceParser::SelectorPreconfiguration* preconfiguration,
			const Internal::ShaderVariant& shaderVariant)		// (ShaderVariant required for compiledShaderPatchCollection)
		{
			if (std::holds_alternative<ShaderCompilePatchResource>(shaderVariant)) {
				auto& res = std::get<ShaderCompilePatchResource>(shaderVariant);
				auto patchExpansions = MakeIteratorRange(res._patchCollectionExpansions);

				VLA(const ShaderSourceParser::SelectorFilteringRules*, autoFiltering, 1+patchExpansions.size());
				VLA(unsigned, filteringRulesPulledIn, 1+patchExpansions.size());
				unsigned autoFilteringCount = 0;
				if (automaticFiltering) autoFiltering[autoFilteringCount++] = automaticFiltering;
				filteringRulesPulledIn[0] = ~0u;

				// Figure out which filtering rules we need from the compiled patch collection, and include them
				// This is important because the filtering rules for different shader stages might be vastly different
				if (res._patchCollection) {
					for (auto exp:patchExpansions) {
						auto i = std::find_if(
							res._patchCollection->GetInterface().GetPatches().begin(), res._patchCollection->GetInterface().GetPatches().end(),
							[exp](const auto& c) { return c._implementsHash == exp || c._originalEntryPointHash == exp; });
						if (i == res._patchCollection->GetInterface().GetPatches().end()) continue;
						if (std::find(filteringRulesPulledIn, &filteringRulesPulledIn[autoFilteringCount], i->_filteringRulesId) != &filteringRulesPulledIn[autoFilteringCount]) continue;
						filteringRulesPulledIn[autoFilteringCount] = i->_filteringRulesId;
						autoFiltering[autoFilteringCount++] = &res._patchCollection->GetInterface().GetSelectorFilteringRules(i->_filteringRulesId);
					}
				} else {
					assert(patchExpansions.empty());		// without a ShaderPatchInstantiationUtil we can't do anything with "patchExpansions"
				}

				return _selectorVariationsSet.FilterSelectors(
					selectors,
					manualFiltering, 
					MakeIteratorRange(autoFiltering, &autoFiltering[autoFilteringCount]), 
					preconfiguration);
			} else {
				assert(automaticFiltering);
				const ShaderSourceParser::SelectorFilteringRules* autoFilteringArray[] { automaticFiltering };
				return _selectorVariationsSet.FilterSelectors(
					selectors,
					manualFiltering,
					MakeIteratorRange(autoFilteringArray),
					preconfiguration);
			}
		}

		SharedPools(std::shared_ptr<IDevice> device)
		: _device(std::move(device))
		{
			_pipelineLayoutPool = std::make_shared<PipelineLayoutPool>(*_device);
			_samplerPool = std::make_shared<SamplerPool>(*_device);
		}

	private:
		PendingUpdateId _nextPendingUpdateId = 1;
	};

}}}

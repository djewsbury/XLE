// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TechniqueDelegates.h"
#include "ShaderVariationSet.h"
#include "../Metal/Shader.h"
#include "../Metal/ObjectFactory.h"
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

		if (patchCollection && !patchExpansions.empty()) {
			auto res = ::Assets::MakeAsset<CompiledShaderByteCode_InstantiateShaderGraph>(
				MakeStringSection(temp), adjustedDefinesTable, patchCollection, patchExpansionsCopy);
			return *reinterpret_cast<::Assets::PtrToFuturePtr<CompiledShaderByteCode>*>(&res);
		} else {
			return ::Assets::MakeAsset<CompiledShaderByteCode>(MakeStringSection(temp), adjustedDefinesTable);
		}
	}

	static ::Assets::PtrToFuturePtr<Metal::ShaderProgram> MakeShaderProgram(
		const GraphicsPipelineDesc& pipelineDesc,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
		IteratorRange<const UniqueShaderVariationSet::FilteredSelectorSet*> filteredSelectors,
		StreamOutputInitializers so = {})
	{
		::Assets::PtrToFuturePtr<CompiledShaderByteCode> byteCodeFuture[3];

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
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> psCode) {
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
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> psCode,
					std::shared_ptr<CompiledShaderByteCode> gsCode) {
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
					std::shared_ptr<CompiledShaderByteCode> vsCode, 
					std::shared_ptr<CompiledShaderByteCode> gsCode) {
					return std::make_shared<Metal::ShaderProgram>(
						Metal::GetObjectFactory(),
						pipelineLayout, *vsCode, *gsCode, CompiledShaderByteCode{},
						StreamOutputInitializers{soElements, soBufferStrides});
				});
		} else
			Throw(std::runtime_error("Missing shader stages while building shader program"));
		return result;
	}

	static ::Assets::PtrToFuturePtr<Metal::ComputeShader> MakeComputeShader(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> shader,
		const UniqueShaderVariationSet::FilteredSelectorSet& filteredSelectors,
		const std::shared_ptr<CompiledShaderPatchCollection>& compiledPatchCollection,
		IteratorRange<const uint64_t*> patchExpansions)
	{
		auto byteCodeFuture = MakeByteCodeFuture(
			ShaderStage::Compute,
			shader,
			filteredSelectors._selectors,
			compiledPatchCollection,
			patchExpansions, {});

		auto result = std::make_shared<::Assets::FuturePtr<Metal::ComputeShader>>();
		::Assets::WhenAll(byteCodeFuture).ThenConstructToFuture(
			*result,
			[pipelineLayout](
				std::shared_ptr<CompiledShaderByteCode> csCode) {
				return std::make_shared<Metal::ComputeShader>(
					Metal::GetObjectFactory(),
					pipelineLayout, *csCode);
			});
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

}}}

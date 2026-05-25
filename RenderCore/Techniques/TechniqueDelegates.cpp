// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueDelegates.h"
#include "TechniqueDelegateUtil.h"
#include "CommonResources.h"
#include "ShaderPatchInstantiationUtil.h"
#include "Techniques.h"
#include "SpriteTechnique.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Format.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StreamUtils.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
{

	class TechniqueDelegate_Legacy : public ITechniqueDelegate
	{
	public:
		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& input) override;

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override;
		::Assets::DependencyValidation GetDependencyValidation() override;

		TechniqueDelegate_Legacy(
			std::shared_ptr<Technique> technique,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			unsigned techniqueIndex,
			const AttachmentBlendDesc& blend,
			const RasterizationDesc& rasterization,
			const DepthStencilDesc& depthStencil);
		~TechniqueDelegate_Legacy();
	private:
		unsigned _techniqueIndex;
		AttachmentBlendDesc _blend;
		RasterizationDesc _rasterization;
		DepthStencilDesc _depthStencil;
		std::shared_ptr<Technique> _technique;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry)
	{
		if (!entry._vertexShaderName.empty())
			nascentDesc._shaders[(unsigned)ShaderStage::Vertex] = MakeShaderCompileResourceName(entry._vertexShaderName);
		if (!entry._pixelShaderName.empty())
			nascentDesc._shaders[(unsigned)ShaderStage::Pixel] = MakeShaderCompileResourceName(entry._pixelShaderName);
		if (!entry._geometryShaderName.empty())
			nascentDesc._shaders[(unsigned)ShaderStage::Geometry] = MakeShaderCompileResourceName(entry._geometryShaderName);
		nascentDesc._manualSelectorFiltering = entry._selectorFiltering;
		nascentDesc._techniquePreconfigurationFile = entry._preconfigurationFileName;
	}

	static Internal::ShaderVariant MakeShaderCompilePatchResource(StringSection<> shaderName, const std::shared_ptr<ShaderPatchInstantiationUtil>& shaderPatches, std::vector<uint64_t>&& patchExpansions)
	{
		if (!patchExpansions.empty()) {
			assert(shaderPatches);
			ShaderCompileResourceName entryPoint;
			if (!shaderName.IsEmpty()) entryPoint = MakeShaderCompileResourceName(shaderName);
			return ShaderCompilePatchResource {shaderPatches, std::move(patchExpansions), {}, {}, std::move(entryPoint) };
		} else if (!shaderName.IsEmpty()) {
			return MakeShaderCompileResourceName(shaderName);
		} else
			return std::monostate{};
	}

	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry,
		const std::shared_ptr<ShaderPatchInstantiationUtil>& shaderPatches,
		std::vector<uint64_t>&& vsPatchExpansions,
		std::vector<uint64_t>&& psPatchExpansions,
		std::vector<uint64_t>&& gsPatchExpansions)
	{
		nascentDesc._shaders[(unsigned)ShaderStage::Vertex] = MakeShaderCompilePatchResource(entry._vertexShaderName, shaderPatches, std::move(vsPatchExpansions));
		nascentDesc._shaders[(unsigned)ShaderStage::Pixel] = MakeShaderCompilePatchResource(entry._pixelShaderName, shaderPatches, std::move(psPatchExpansions));
		nascentDesc._shaders[(unsigned)ShaderStage::Geometry] = MakeShaderCompilePatchResource(entry._geometryShaderName, shaderPatches, std::move(gsPatchExpansions));
		nascentDesc._manualSelectorFiltering = entry._selectorFiltering;
		nascentDesc._techniquePreconfigurationFile = entry._preconfigurationFileName;
	}

	auto TechniqueDelegate_Legacy::GetPipelineDesc(
		std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
		IteratorRange<const uint64_t*> iaAttributes,
		const RenderCore::Assets::RenderStateSet& input) -> std::shared_ptr<GraphicsPipelineDesc>
	{
		auto result = std::make_shared<GraphicsPipelineDesc>();

		if (_techniqueIndex != Techniques::TechniqueIndex::DepthOnly)
			result->_blend.push_back(_blend);
		result->_rasterization = _rasterization;
		result->_depthStencil = _depthStencil;
		if (shaderPatches)
			result->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

		result->_depVal = _technique->GetDependencyValidation();
		auto& entry = _technique->GetEntry(_techniqueIndex);
		PrepareShadersFromTechniqueEntry(*result, entry);

		return result;
	}

	std::shared_ptr<Assets::PredefinedPipelineLayout> TechniqueDelegate_Legacy::GetPipelineLayout() { return _pipelineLayout; }
	::Assets::DependencyValidation TechniqueDelegate_Legacy::GetDependencyValidation() { return _depVal; }

	TechniqueDelegate_Legacy::TechniqueDelegate_Legacy(
		std::shared_ptr<Technique> technique,
		std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	: _technique(std::move(technique))
	, _pipelineLayout(std::move(pipelineLayout))
	, _techniqueIndex(techniqueIndex)
	, _blend(blend)
	, _rasterization(rasterization)
	, _depthStencil(depthStencil)
	{
		::Assets::DependencyValidationMarker depVals[] { _technique->GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
		_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
	}

	TechniqueDelegate_Legacy::~TechniqueDelegate_Legacy()
	{}

	void CreateTechniqueDelegateLegacy(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	{
		auto techniqueFuture = ::Assets::GetAssetFuturePtr<Technique>(ILLUM_LEGACY_TECH);
		::Assets::WhenAll(techniqueFuture).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[techniqueIndex, blend, rasterization, depthStencil](auto&& promise, auto technique) {
				TRY {
					auto pipelineLayoutName = technique->GetEntry(techniqueIndex)._pipelineLayoutName;
					if (pipelineLayoutName.empty()) Throw(std::runtime_error("Missing pipeline layout name in legacy technique delegate"));
					auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(pipelineLayoutName);
					::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
						std::move(promise),
						[technique, techniqueIndex, blend, rasterization, depthStencil](auto pipelineLayout) {
							return std::make_shared<TechniqueDelegate_Legacy>(std::move(technique), std::move(pipelineLayout), techniqueIndex, blend, rasterization, depthStencil);
						});
				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		T E C H N I Q U E   D E L E G A T E
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::string SetupTechniqueFileHelper(TechniqueSetFile& techniqueSet, IteratorRange<const std::pair<const char*, TechniqueEntry*>*> entriesToConfigure)
	{
		std::string pipelineLayout;
		char buffer[256];
		for (auto e:entriesToConfigure) {
			auto* entry = techniqueSet.FindEntry(Hash64(e.first));
			if (!entry)
				Throw(std::runtime_error(StringMeldInPlace(buffer) << "Could not construct technique delegate because required configurations (" << e.first << ") was not found"));
			*e.second = *entry;
			
			if (!e.second->_pipelineLayoutName.empty()) {
				if (pipelineLayout.empty()) {
					pipelineLayout = e.second->_pipelineLayoutName;
				} else if (pipelineLayout != e.second->_pipelineLayoutName) {
					auto meld = StringMeldInPlace(buffer);
					meld << "Pipeline layout does not agree in technique delegate. The entries (";
					CommaSeparatedList list{meld.AsOStream()};
					for (auto e2:entriesToConfigure) list << e2.first;
					meld << ") must all agree in pipeline layout, so they can be used together in the same sequencer config.";
					Throw(std::runtime_error(meld.AsString()));
				}
			}
		}

		if (pipelineLayout.empty()) {
			auto meld = StringMeldInPlace(buffer);
			meld << "None of the technique entries in the following list have a pipeline layout (";
			CommaSeparatedList list{meld.AsOStream()};
			for (auto e2:entriesToConfigure) list << e2.first;
			meld << "). At least one must have a pipeline layout, and every one that does must agree with the others.";
			Throw(std::runtime_error(meld.AsString()));
		}

		return pipelineLayout;
	}

	constexpr auto s_perPixel = "PerPixel"_h;
	constexpr auto s_perPixelCustomLighting = "PerPixelCustomLighting"_h;
	constexpr auto s_earlyRejectionTest = "EarlyRejectionTest"_h;
	constexpr auto s_vertexPatch = "VertexPatch"_h;
	static uint64_t s_patchExp_perPixelAndEarlyRejection[] = { s_perPixel, s_earlyRejectionTest };
	static uint64_t s_patchExp_perPixel[] = { s_perPixel };
	static uint64_t s_patchExp_perPixelCustomLighting[] = { s_perPixelCustomLighting };
	static uint64_t s_patchExp_earlyRejection[] = { s_earlyRejectionTest };
	static uint64_t s_patchExp_deformVertex[] = { s_vertexPatch };

	IllumType CalculateIllumType(const ShaderPatchInstantiationUtil::Interface& shaderPatches)
	{
		if (shaderPatches.HasPatchType("SV_AutoPS"_h) || shaderPatches.HasPatchType("SV_SpritePS"_h)) {
			return IllumType::SpriteTechnique;
		} else if (shaderPatches.HasPatchType(s_perPixel)) {
			if (shaderPatches.HasPatchType(s_earlyRejectionTest)) {
				return IllumType::PerPixelAndEarlyRejection;
			} else {
				return IllumType::PerPixel;
			}
		} else if (shaderPatches.HasPatchType(s_perPixelCustomLighting)) {
			return IllumType::PerPixelCustomLighting;
		}
		return IllumType::NoPerPixel;
	}

	class TechniqueDelegate_Deferred : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _noPatches;
			TechniqueEntry _perPixel;
			TechniqueEntry _perPixelAndEarlyRejection;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				std::pair<const char*, TechniqueEntry*> entriesToCheck[] {
					{"Deferred_NoPatches", &_noPatches},
					{"Deferred_PerPixel", &_perPixel},
					{"Deferred_PerPixelAndEarlyRejection", &_perPixelAndEarlyRejection},
					{"VS_NoPatches", &_vsNoPatchesSrc},
					{"VS_DeformVertex", &_vsDeformVertexSrc},
				};
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}

			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRasterizationDesc(stateSet);
			bool deferredDecal = 
					(stateSet._flag & Assets::RenderStateSet::Flag::BlendType)
				&&	(stateSet._blendType == Assets::RenderStateSet::BlendType::DeferredDecal);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWrite;
			// We must write the a flag to the stencil buffer to mark pixels as "not sky"
			nascentDesc->_depthStencil._stencilEnable = true;
			nascentDesc->_depthStencil._stencilWriteMask = 1<<7;
			nascentDesc->_depthStencil._frontFaceStencil._passOp = StencilOp::Replace;
			if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::DoubleSided && stateSet._doubleSided)
				nascentDesc->_depthStencil._backFaceStencil._passOp = StencilOp::Replace;
			nascentDesc->_manualSelectorFiltering.SetSelector("GBUFFER_TYPE", _gbufferTypeCode);

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
			if (shaderPatches) {
				nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

				auto illumType = CalculateIllumType(shaderPatches->GetInterface());
				bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

				switch (illumType) {
				case IllumType::PerPixel:
					psTechEntry = &_techniqueFileHelper._perPixel;
					psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
					break;
				case IllumType::PerPixelAndEarlyRejection:
					psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
					psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
					break;
				default:
					break;
				}

				if (hasDeformVertex) {
					vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
					vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
				}
			}

			// note -- we could premerge all of the combinations in the constructor, to cut down on cost here
			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();
			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));

			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Deferred(TechniqueFileHelper&& helper, std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout, unsigned gbufferTypeCode)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout)), _gbufferTypeCode(gbufferTypeCode)
		{
			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			unsigned gbufferTypeCode)
		{
			::Assets::WhenAll(std::move(techniqueSet)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[gbufferTypeCode](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), gbufferTypeCode](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_Deferred>(std::move(helper), std::move(pipelineLayout), gbufferTypeCode);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
		unsigned _gbufferTypeCode = 0;
	};

	void CreateTechniqueDelegate_Deferred(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		unsigned gbufferTypeCode)
	{
		TechniqueDelegate_Deferred::ConstructToPromise(std::move(promise), std::move(techniqueSet), gbufferTypeCode);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_Forward : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _noPatches;
			TechniqueEntry _perPixel;
			TechniqueEntry _perPixelAndEarlyRejection;
			TechniqueEntry _perPixelCustomLighting;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				std::pair<const char*, TechniqueEntry*> entriesToCheck[] {
					{"Forward_NoPatches", &_noPatches},
					{"Forward_PerPixel", &_perPixel},
					{"Forward_PerPixelAndEarlyRejection", &_perPixelAndEarlyRejection},
					{"Forward_PerPixelCustomLighting", &_perPixelCustomLighting},
					{"VS_NoPatches", &_vsNoPatchesSrc},
					{"VS_DeformVertex", &_vsDeformVertexSrc},
				};
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRasterizationDesc(stateSet);

			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}
			nascentDesc->_depthStencil = _depthStencil;

			auto illumType = shaderPatches ? CalculateIllumType(shaderPatches->GetInterface()) : IllumType::NoPerPixel;
			if (!shaderPatches || illumType != IllumType::SpriteTechnique) {

				const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
				const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
				std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;

				// old style more limited technique
				if (shaderPatches) {
					nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();
					bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &_techniqueFileHelper._perPixel;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					case IllumType::PerPixelCustomLighting:
						psTechEntry = &_techniqueFileHelper._perPixelCustomLighting;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelCustomLighting, &s_patchExp_perPixelCustomLighting[dimof(s_patchExp_perPixelCustomLighting)]);
					default:
						break;
					}

					if (hasDeformVertex) {
						vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
						vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
					}
				}

				TechniqueEntry mergedTechEntry = *vsTechEntry;
				mergedTechEntry.MergeIn(*psTechEntry);
				PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));

			} else {

				// new style, more flexible approach
				if (auto i = LowerBound(_flexibleHelper._entries, "main"_h); i!=_flexibleHelper._entries.end() && i->first == "main"_h)
					i->second.Configure(*nascentDesc, shaderPatches, iaAttributes);

			}

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Vertex).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Vertex));

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Geometry).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Geometry));

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Pixel).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Pixel));

			nascentDesc->_depVal = _depVal;
			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Forward(
			TechniqueFileHelper&& helper,
			const FlexibleTechniqueHelper& flexibleHelper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			TechniqueDelegateForwardFlags::BitField flags)
		: _techniqueFileHelper(std::move(helper)), _flexibleHelper(flexibleHelper), _pipelineLayout(std::move(pipelineLayout))
		{
			if (flags & TechniqueDelegateForwardFlags::DisableDepthWrite) {
				_depthStencil = CommonResourceBox::s_dsReadOnly;
			} else {
				_depthStencil = CommonResourceBox::s_dsReadWrite;
			}

			// Use "equal" when writing over geometry that has had a pre-depth pass. This avoid having to redo any alpha testing
			if (flags & TechniqueDelegateForwardFlags::DepthTestEqual)
				_depthStencil._depthTest = CompareOp::Equal;

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation(), _flexibleHelper.GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			TechniqueDelegateForwardFlags::BitField flags)
		{
			auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();
			::Assets::WhenAll(std::move(techniqueSet), ::Assets::GetAssetFuture<Techniques::FlexibleTechniqueHelper>(util, TECH_ENTRY_FORWARD)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[flags, util](auto techniqueSetFile, const auto& flexibleHelper) {
					TechniqueFileHelper helper{techniqueSetFile};
					auto pipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
					return std::make_shared<TechniqueDelegate_Forward>(std::move(helper), flexibleHelper, std::move(pipelineLayout), flags);
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		FlexibleTechniqueHelper _flexibleHelper;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
		DepthStencilDesc _depthStencil;
	};

	void CreateTechniqueDelegate_Forward(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags)
	{
		TechniqueDelegate_Forward::ConstructToPromise(std::move(promise), std::move(techniqueSet), flags);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_DepthOnly : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _noPatches;
			TechniqueEntry _earlyRejectionSrc;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;
			std::optional<ShadowGenType> _shadowGen;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet, std::optional<ShadowGenType> shadowGen)
			: _techniqueSet(std::move(techniqueSet)), _shadowGen(shadowGen)
			{
				std::vector<std::pair<const char*, TechniqueEntry*>> entriesToCheck;
				entriesToCheck.reserve(4);
				entriesToCheck.emplace_back("DepthOnly_NoPatches", &_noPatches);
				entriesToCheck.emplace_back("DepthOnly_EarlyRejection", &_earlyRejectionSrc);
				if (shadowGen) {
					if (*shadowGen == ShadowGenType::GSAmplify) {
						entriesToCheck.emplace_back("VSShadowGen_GSAmplify_NoPatches", &_vsNoPatchesSrc);
						entriesToCheck.emplace_back("VSShadowGen_GSAmplify_DeformVertex", &_vsDeformVertexSrc);
					} else {
						assert(*shadowGen == ShadowGenType::VertexIdViewInstancing);
						entriesToCheck.emplace_back("VSShadowProbe_NoPatches", &_vsNoPatchesSrc);
						entriesToCheck.emplace_back("VSShadowProbe_DeformVertex", &_vsDeformVertexSrc);
					}
				} else {
					entriesToCheck.emplace_back("VSDepthOnly_NoPatches", &_vsNoPatchesSrc);
					entriesToCheck.emplace_back("VSDepthOnly_DeformVertex", &_vsDeformVertexSrc);
				}
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			// always use less than (not less than or equal) here, because writing equally deep pixels is redundant
			// (and we can potentially skip a texture lookup for alpha test geo sometimes)
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteCloserThan;

			auto illumType = shaderPatches ? CalculateIllumType(shaderPatches->GetInterface()) : IllumType::NoPerPixel;
			// note -- HLSL vertex shaders have not be configured for shadow gen yet, so we must fall back to the old method for the moment
			// the SpriteTechnique method is still a little awkward for overriding default behaviour (in this case, we need shadow gen shaders
			// to calculate SV_Position from WORLDPOSITION differently). We can do it, just not in a very expressive and reliable way
			if (_techniqueFileHelper._shadowGen || (!shaderPatches || illumType != IllumType::SpriteTechnique)) {

				const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
				const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
				std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
				if (shaderPatches) {
					nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

					bool hasEarlyRejectionTest = shaderPatches->GetInterface().HasPatchType(s_earlyRejectionTest);
					bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

					if (hasEarlyRejectionTest) {
						psTechEntry = &_techniqueFileHelper._earlyRejectionSrc;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
					}

					if (hasDeformVertex) {
						vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
						vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
					}
				}

				nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();
				TechniqueEntry mergedTechEntry = *vsTechEntry;
				mergedTechEntry.MergeIn(*psTechEntry);
				PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));

			} else {

				// new style, more flexible approach
				if (auto i = LowerBound(_flexibleHelper._entries, "main"_h); i!=_flexibleHelper._entries.end() && i->first == "main"_h)
					i->second.Configure(*nascentDesc, shaderPatches, iaAttributes);

				// important selector -- but since it's read by the SpriteTechnique generated functions, it's not included in the default relevance
				nascentDesc->_manualSelectorFiltering.EnforceSelectorRelevance("NOEARLYDEPTHSTENCIL", "1");

			}

			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_DepthOnly(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			const FlexibleTechniqueHelper& flexibleHelper,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode, FaceWinding faceWinding,
			std::optional<ShadowGenType> shadowGen)
		: _techniqueFileHelper(std::move(helper)), _flexibleHelper(flexibleHelper), _pipelineLayout(std::move(pipelineLayout))
		{
			_rs[0x0] = RasterizationDesc{cullMode,        faceWinding, (float)singleSidedBias._depthBias, singleSidedBias._depthBiasClamp, singleSidedBias._slopeScaledBias};
			_rs[0x1] = RasterizationDesc{CullMode::None,  faceWinding, (float)doubleSidedBias._depthBias, doubleSidedBias._depthBiasClamp, doubleSidedBias._slopeScaledBias};

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation(), _flexibleHelper.GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode, FaceWinding faceWinding,
			std::optional<ShadowGenType> shadowGen)
		{
			auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();
			::Assets::WhenAll(std::move(techniqueSet), ::Assets::GetAssetFuture<Techniques::FlexibleTechniqueHelper>(util, TECH_ENTRY_DEPTHONLY)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen](auto&& promise, auto techniqueSetFile, const auto& flexibleHelper) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile, shadowGen};
						auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen, flexibleHelper](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_DepthOnly>(std::move(helper), std::move(pipelineLayout), std::move(flexibleHelper), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		FlexibleTechniqueHelper _flexibleHelper;
		RasterizationDesc _rs[2];
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_DepthOnly(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		const RSDepthBias& singleSidedBias,
		const RSDepthBias& doubleSidedBias,
		CullMode cullMode, FaceWinding faceWinding)
	{
		TechniqueDelegate_DepthOnly::ConstructToPromise(std::move(promise), std::move(techniqueSet), singleSidedBias, doubleSidedBias, cullMode, faceWinding, std::optional<ShadowGenType>{});
	}

	void CreateTechniqueDelegate_ShadowGen(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		ShadowGenType shadowGenType,
		const RSDepthBias& singleSidedBias,
		const RSDepthBias& doubleSidedBias,
		CullMode cullMode, FaceWinding faceWinding)
	{
		TechniqueDelegate_DepthOnly::ConstructToPromise(std::move(promise), std::move(techniqueSet), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGenType);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_PreDepth : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _psNoPatchesSrc;
			TechniqueEntry _psPerPixelSrc;
			TechniqueEntry _psPerPixelAndEarlyRejection;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet, PreDepthType preDepthType)
			: _techniqueSet(techniqueSet)
			{
				std::vector<std::pair<const char*, TechniqueEntry*>> entriesToCheck;
				entriesToCheck.reserve(5);
				entriesToCheck.emplace_back("VSDepthOnly_NoPatches", &_vsNoPatchesSrc);
				entriesToCheck.emplace_back("VSDepthOnly_DeformVertex", &_vsDeformVertexSrc);
				if (preDepthType != PreDepthType::DepthOnly) {
					entriesToCheck.emplace_back("DepthPlus_NoPatches", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("DepthPlus_PerPixel", &_psPerPixelSrc);
					entriesToCheck.emplace_back("DepthPlus_PerPixelAndEarlyRejection", &_psPerPixelAndEarlyRejection);
				} else {
					entriesToCheck.emplace_back("DepthOnly_NoPatches", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("DepthOnly_NoPatches", &_psPerPixelSrc);
					entriesToCheck.emplace_back("DepthOnly_EarlyRejection", &_psPerPixelAndEarlyRejection);
				}
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			if (stateSet._flag & Assets::RenderStateSet::Flag::DepthBias) 		// we must let the state set override depth bias for decal-style geometry
				nascentDesc->_rasterization._depthBiasConstantFactor = (float)stateSet._depthBias;
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteCloserThan;
			if (_preDepthType != PreDepthType::DepthOnly) {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
				if (_preDepthType == PreDepthType::DepthMotionNormal || _preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
				if (_preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}

			auto illumType = shaderPatches ? CalculateIllumType(shaderPatches->GetInterface()) : IllumType::NoPerPixel;
			if (!shaderPatches || illumType != IllumType::SpriteTechnique) {

				const TechniqueEntry* psTechEntry = &_techniqueFileHelper._psNoPatchesSrc;
				const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
				std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
				if (shaderPatches) {
					nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

					auto illumType = CalculateIllumType(shaderPatches->GetInterface());
					bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &_techniqueFileHelper._psPerPixelSrc;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &_techniqueFileHelper._psPerPixelAndEarlyRejection;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					if (hasDeformVertex) {
						vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
						vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
					}
				}

				nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

				TechniqueEntry mergedTechEntry = *vsTechEntry;
				mergedTechEntry.MergeIn(*psTechEntry);

				if (_preDepthType == PreDepthType::DepthMotion || _preDepthType == PreDepthType::DepthMotionNormal || _preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation) {
					mergedTechEntry._selectorFiltering.SetSelector("VSOUT_HAS_PREV_POSITION", 1);
					mergedTechEntry._selectorFiltering.SetSelector("DEPTH_PLUS_MOTION", 1);
				}
				if (_preDepthType == PreDepthType::DepthMotionNormal || _preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					mergedTechEntry._selectorFiltering.SetSelector("DEPTH_PLUS_NORMAL", 1);
				if (_preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					mergedTechEntry._selectorFiltering.SetSelector("DEPTH_PLUS_ROUGHNESS", 1);
				if (_preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					mergedTechEntry._selectorFiltering.SetSelector("DEPTH_PLUS_HISTORY_ACCUMULATION", 1);

				// important selector -- but since it's read by the SpriteTechnique generated functions, it's not included in the default relevance
				nascentDesc->_manualSelectorFiltering.EnforceSelectorRelevance("NOEARLYDEPTHSTENCIL", "1");

				PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));

			} else {

				// new style, more flexible approach
				if (auto i = LowerBound(_flexibleHelper._entries, "depthPlus"_h); i!=_flexibleHelper._entries.end() && i->first == "depthPlus"_h)
					i->second.Configure(*nascentDesc, shaderPatches, iaAttributes);

				if (_preDepthType == PreDepthType::DepthMotion || _preDepthType == PreDepthType::DepthMotionNormal || _preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation) {
					nascentDesc->_manualSelectorFiltering.SetSelector("VSOUT_HAS_PREV_POSITION", 1);
					nascentDesc->_manualSelectorFiltering.SetSelector("DEPTH_PLUS_MOTION", 1);
				}
				if (_preDepthType == PreDepthType::DepthMotionNormal || _preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					nascentDesc->_manualSelectorFiltering.SetSelector("DEPTH_PLUS_NORMAL", 1);
				if (_preDepthType == PreDepthType::DepthMotionNormalRoughness || _preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					nascentDesc->_manualSelectorFiltering.SetSelector("DEPTH_PLUS_ROUGHNESS", 1);
				if (_preDepthType == PreDepthType::DepthMotionNormalRoughnessAccumulation)
					nascentDesc->_manualSelectorFiltering.SetSelector("DEPTH_PLUS_HISTORY_ACCUMULATION", 1);

				// important selector -- but since it's read by the SpriteTechnique generated functions, it's not included in the default relevance
				nascentDesc->_manualSelectorFiltering.EnforceSelectorRelevance("NOEARLYDEPTHSTENCIL", "1");
			}

			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_PreDepth(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			const FlexibleTechniqueHelper& flexibleHelper,
			PreDepthType preDepthType)
		: _techniqueFileHelper(std::move(helper)), _flexibleHelper(flexibleHelper), _pipelineLayout(std::move(pipelineLayout)), _preDepthType(preDepthType)
		{
			_rs[0x0] = CommonResourceBox::s_rsDefault;
			_rs[0x1] = CommonResourceBox::s_rsCullDisable;

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation(), _flexibleHelper.GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			PreDepthType preDepthType)
		{
			auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();
			::Assets::WhenAll(std::move(techniqueSet), ::Assets::GetAssetFuture<Techniques::FlexibleTechniqueHelper>(util, TECH_ENTRY_DEPTHONLY)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[preDepthType](auto&& promise, auto techniqueSetFile, const auto& flexibleHelper) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile, preDepthType};
						auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), preDepthType, flexibleHelper](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_PreDepth>(std::move(helper), std::move(pipelineLayout), flexibleHelper, preDepthType);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		FlexibleTechniqueHelper _flexibleHelper;
		RasterizationDesc _rs[2];
		PreDepthType _preDepthType;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_PreDepth(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		PreDepthType preDepthType)
	{
		TechniqueDelegate_PreDepth::ConstructToPromise(std::move(promise), std::move(techniqueSet), preDepthType);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_Utility : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _psNoPatchesSrc;
			TechniqueEntry _psPerPixelSrc;
			TechniqueEntry _psPerPixelAndEarlyRejection;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet, UtilityDelegateType utilityType)
			: _techniqueSet(std::move(techniqueSet))
			{
				std::vector<std::pair<const char*, TechniqueEntry*>> entriesToCheck;
				entriesToCheck.reserve(5);
				entriesToCheck.emplace_back("VS_NoPatches", &_vsNoPatchesSrc);
				entriesToCheck.emplace_back("VS_DeformVertex", &_vsDeformVertexSrc);
				if (utilityType == UtilityDelegateType::SolidWireframe) {
					entriesToCheck.emplace_back("SolidWireframe", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("SolidWireframe", &_psPerPixelSrc);
					entriesToCheck.emplace_back("SolidWireframe", &_psPerPixelAndEarlyRejection);
				} else {
					entriesToCheck.emplace_back("Utility_NoPatches", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("Utility_PerPixel", &_psPerPixelSrc);
					entriesToCheck.emplace_back("Utility_PerPixelAndEarlyRejection", &_psPerPixelAndEarlyRejection);
				}
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			nascentDesc->_rasterization = BuildDefaultRasterizationDesc(stateSet);
			nascentDesc->_depthStencil = _dss[(stateSet._flag & Assets::RenderStateSet::Flag::WriteMask) ? (stateSet._writeMask & 3) : 3];
			if (_allowBlending && stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}

			auto illumType = shaderPatches ? CalculateIllumType(shaderPatches->GetInterface()) : IllumType::NoPerPixel;
			if (!shaderPatches || illumType != IllumType::SpriteTechnique) {

				const TechniqueEntry* psTechEntry = &_techniqueFileHelper._psNoPatchesSrc;
				const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
				std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
				if (shaderPatches) {
					nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

					bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &_techniqueFileHelper._psPerPixelSrc;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &_techniqueFileHelper._psPerPixelAndEarlyRejection;
						psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					if (hasDeformVertex) {
						vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
						vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
					}
				}

				nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

				TechniqueEntry mergedTechEntry = *vsTechEntry;
				mergedTechEntry.MergeIn(*psTechEntry);
				mergedTechEntry._selectorFiltering.SetSelector("UTILITY_SHADER", (unsigned)_utilityType);

				PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));

			} else {

				// new style, more flexible approach
				if (auto i = LowerBound(_flexibleHelper._entries, "main"_h); i!=_flexibleHelper._entries.end() && i->first == "main"_h) {
					i->second.Configure(*nascentDesc, shaderPatches, iaAttributes);
				}

				nascentDesc->_manualSelectorFiltering.SetSelector("UTILITY_SHADER", (unsigned)_utilityType);
				nascentDesc->_manualSelectorFiltering.EnforceSelectorRelevance("UTILITY_SHADER", "1");

			}

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Vertex).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Vertex));

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Geometry).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Geometry));

			if (shaderPatches && !shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Pixel).IsEmpty())
				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = MakeShaderCompileResourceName(shaderPatches->GetInterface().GetOverrideShader(ShaderStage::Pixel));

			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Utility(
			TechniqueFileHelper&& helper,
			const FlexibleTechniqueHelper& flexibleHelper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			UtilityDelegateType utilityType, bool allowBlending)
		: _techniqueFileHelper{std::move(helper)}, _flexibleHelper(flexibleHelper), _pipelineLayout(std::move(pipelineLayout))
		, _utilityType(utilityType), _allowBlending(allowBlending)
		{
			_dss[0] = CommonResourceBox::s_dsDisable;
			_dss[1] = CommonResourceBox::s_dsWriteOnly;
			_dss[2] = CommonResourceBox::s_dsReadOnly;
			_dss[3] = CommonResourceBox::s_dsReadWrite;

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			UtilityDelegateType utilityType, bool allowBlending)
		{
			auto util = std::make_shared<::AssetsNew::CompoundAssetUtil>();
			::Assets::WhenAll(std::move(techniqueSet), ::Assets::GetAssetFuture<Techniques::FlexibleTechniqueHelper>(util, TECH_ENTRY_UTILITY)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[utilityType, allowBlending, util](auto techniqueSetFile, const auto& flexibleHelper) {
					TechniqueFileHelper helper{techniqueSetFile, utilityType};
					auto pipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
					return std::make_shared<TechniqueDelegate_Utility>(std::move(helper), flexibleHelper, std::move(pipelineLayout), utilityType, allowBlending);
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		FlexibleTechniqueHelper _flexibleHelper;
		UtilityDelegateType _utilityType;
		DepthStencilDesc _dss[4];
		bool _allowBlending;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_Utility(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		UtilityDelegateType type, bool allowBlending)
	{
		TechniqueDelegate_Utility::ConstructToPromise(std::move(promise), std::move(techniqueSet), type, allowBlending);
	}

	std::optional<UtilityDelegateType> AsUtilityDelegateType(StringSection<> input)
	{
		if (XlEqString(input, "FlatColor")) return UtilityDelegateType::FlatColor;
		if (XlEqString(input, "CopyDiffuseAlbedo")) return UtilityDelegateType::CopyDiffuseAlbedo;
		if (XlEqString(input, "CopyWorldSpacePosition")) return UtilityDelegateType::CopyWorldSpacePosition;
		if (XlEqString(input, "CopyWorldSpaceNormal")) return UtilityDelegateType::CopyWorldSpaceNormal;
		if (XlEqString(input, "CopyRoughness")) return UtilityDelegateType::CopyRoughness;
		if (XlEqString(input, "CopyMetal")) return UtilityDelegateType::CopyMetal;
		if (XlEqString(input, "CopySpecular")) return UtilityDelegateType::CopySpecular;
		if (XlEqString(input, "CopyCookedAO")) return UtilityDelegateType::CopyCookedAO;
		if (XlEqString(input, "SolidWireframe")) return UtilityDelegateType::SolidWireframe;
		return {};
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_ProbePrepare : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _noPatches;
			TechniqueEntry _perPixel;
			TechniqueEntry _perPixelAndEarlyRejection;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				std::pair<const char*, TechniqueEntry*> entriesToCheck[] {
					{"ProbePrepare_NoPatches", &_noPatches},
					{"ProbePrepare_PerPixel", &_perPixel},
					{"ProbePrepare_PerPixelAndEarlyRejection", &_perPixelAndEarlyRejection},
					{"VS_NoPatches", &_vsNoPatchesSrc},
					{"VS_DeformVertex", &_vsDeformVertexSrc},
				};
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRasterizationDesc(stateSet);

			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteCloserThan;		// note -- read and write from depth -- if we do a pre-depth pass for probes we could just set this to read

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
			if (shaderPatches) {
				nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

				auto illumType = CalculateIllumType(shaderPatches->GetInterface());
				bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

				switch (illumType) {
				case IllumType::PerPixel:
					psTechEntry = &_techniqueFileHelper._perPixel;
					psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
					break;
				case IllumType::PerPixelAndEarlyRejection:
					psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
					psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
					break;
				default:
					break;
				}

				if (hasDeformVertex) {
					vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
					vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
				}
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));
			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_ProbePrepare(TechniqueFileHelper&& helper, std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout))
		{
			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet)
		{
			::Assets::WhenAll(std::move(techniqueSet)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper)](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_ProbePrepare>(std::move(helper), std::move(pipelineLayout));
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_ProbePrepare(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet)
	{
		TechniqueDelegate_ProbePrepare::ConstructToPromise(std::move(promise), std::move(techniqueSet));
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_RayTest : public ITechniqueDelegate
	{
	public:
		struct TechniqueFileHelper
		{
		public:
			std::shared_ptr<TechniqueSetFile> _techniqueSet;
			TechniqueEntry _noPatches;
			TechniqueEntry _earlyRejectionSrc;
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;
			std::string _pipelineLayout;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				std::pair<const char*, TechniqueEntry*> entriesToCheck[] {
					{"RayTest_NoPatches", &_noPatches},
					{"RayTest_EarlyRejection", &_earlyRejectionSrc},
					{"VSDepthOnly_NoPatches", &_vsNoPatchesSrc},
					{"VSDepthOnly_DeformVertex", &_vsDeformVertexSrc},
				};
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_depthStencil = CommonResourceBox::s_dsDisable;

			nascentDesc->_soElements = _soElements;
			nascentDesc->_soBufferStrides = _soStrides;

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			std::vector<uint64_t> vsPatchExpansions, psPatchExpansions;
			if (shaderPatches) {
				nascentDesc->_materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();

				bool hasEarlyRejectionTest = shaderPatches->GetInterface().HasPatchType(s_earlyRejectionTest);
				bool hasDeformVertex = shaderPatches->GetInterface().HasPatchType(s_vertexPatch);

				if (hasEarlyRejectionTest) {
					psTechEntry = &_techniqueFileHelper._earlyRejectionSrc;
					psPatchExpansions.insert(psPatchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
				}

				if (hasDeformVertex) {
					vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
					vsPatchExpansions.insert(vsPatchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
				}
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry, shaderPatches, std::move(vsPatchExpansions), std::move(psPatchExpansions));
			nascentDesc->_manualSelectorFiltering.SetSelector("INTERSECTION_TEST", _testTypeParameter);
			return nascentDesc;
		}

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_RayTest(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			unsigned testTypeParameter,
			std::vector<InputElementDesc> soElements,
			std::vector<unsigned> soStrides)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout))
		, _testTypeParameter(testTypeParameter)
		, _soElements(std::move(soElements)), _soStrides(std::move(soStrides))
		{
			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			TechniqueSetFileFuture techniqueSet,
			unsigned testTypeParameter,
			const StreamOutputInitializers& soInit)
		{
			auto soElements = NormalizeInputAssembly(soInit._outputElements);
			auto soStrides = std::vector<unsigned>(soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end());
			::Assets::WhenAll(std::move(techniqueSet)).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[soElements=std::move(soElements), soStrides=std::move(soStrides), testTypeParameter](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::GetAssetFuturePtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), soElements=std::move(soElements), soStrides=std::move(soStrides), testTypeParameter](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_RayTest>(std::move(helper), std::move(pipelineLayout), testTypeParameter, std::move(soElements), std::move(soStrides));
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		std::vector<InputElementDesc> _soElements;
		std::vector<unsigned> _soStrides;
		unsigned _testTypeParameter;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_RayTest(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit)
	{
		TechniqueDelegate_RayTest::ConstructToPromise(std::move(promise), std::move(techniqueSet), testTypeParameter, soInit);
	}

	namespace Internal
	{
		uint64_t Hash64(const ShaderVariant& var, uint64_t seed)
		{
			if (std::holds_alternative<ShaderCompileResourceName>(var)) {
				return std::get<ShaderCompileResourceName>(var).CalculateHash(seed);
			} else if (std::holds_alternative<ShaderCompilePatchResource>(var)) {
				return std::get<ShaderCompilePatchResource>(var).CalculateHash(seed);
			}
			return seed;
		}
	}

	uint64_t GraphicsPipelineDesc::GetHash() const
	{
		auto result = CalculateHashNoSelectors(_manualSelectorFiltering.GetHash());
		if (!_techniquePreconfigurationFile.empty())
			result = Hash64(_techniquePreconfigurationFile, result);
		if (!_materialPreconfigurationFile.empty())
			result = Hash64(_materialPreconfigurationFile, result);
		return result;
	}

	uint64_t GraphicsPipelineDesc::CalculateHashNoSelectors(uint64_t seed) const
	{
		uint64_t result = HashCombine(_depthStencil.HashDepthAspect(), seed);
		result = HashCombine(_depthStencil.HashStencilAspect(), result);
		result = HashCombine(_rasterization.Hash(), result);
		for (const auto&b:_blend)
			result = HashCombine(b.Hash(), result);
		if (!_soElements.empty()) {
			result = HashInputAssembly(MakeIteratorRange(_soElements), result);
			result = ::Hash64(AsPointer(_soBufferStrides.begin()), AsPointer(_soBufferStrides.end()), result);
		}
		for (unsigned c=0; c<dimof(_shaders); ++c)
			result = Hash64(_shaders[c], result);
		return result;
	}

	RasterizationDesc BuildDefaultRasterizationDesc(const Assets::RenderStateSet& states)
	{
		auto cullMode = CullMode::Back;
		auto fillMode = FillMode::Solid;
		int depthBias = 0;
		if (states._flag & Assets::RenderStateSet::Flag::DoubleSided) {
			cullMode = states._doubleSided ? CullMode::None : CullMode::Back;
		}
		if (states._flag & Assets::RenderStateSet::Flag::DepthBias) {
			depthBias = states._depthBias;
		}

		RasterizationDesc result;
		result._cullMode = cullMode;
		result._depthBiasConstantFactor = (float)depthBias;
		result._depthBiasClamp = 0.f;
		result._depthBiasSlopeFactor = 0.f;
		return result;
	}

	::Assets::DependencyValidation ITechniqueDelegate::GetDependencyValidation() { return {}; }

	TechniqueSetFileFuture GetDefaultTechniqueSetFileFuture()
	{
		return ::Assets::GetAssetFuturePtr<TechniqueSetFile>(ILLUM_TECH);
	}

	void FlexibleTechniqueHelper::Entry::Configure(
		Techniques::GraphicsPipelineDesc& nascentDesc,
		std::shared_ptr<Techniques::ShaderPatchInstantiationUtil> shaderPatches,
		IteratorRange<const uint64_t*> iaAttributes)
	{
		bool hasSpritePatch = false;
		std::vector<Techniques::PatchDelegateInput> patchesInterface;
		for (const auto& p:shaderPatches->GetInterface().GetPatches()) {
			patchesInterface.emplace_back(Techniques::PatchDelegateInput{p._originalEntryPointName, p._originalEntryPointSignature.get(), p._implementsHash});
			hasSpritePatch |= p._implementsHash == "SV_SpritePS"_h || p._implementsHash == "SV_SpriteVS"_h || p._implementsHash == "SV_SpriteGS"_h;
		}
		for (const auto& p:_patchDelegateInput)
			patchesInterface.emplace_back(Techniques::PatchDelegateInput{std::get<0>(p), &std::get<1>(p), std::get<2>(p)});

		// note --
		//		There's probably an issue here in that we're not using the proper ShaderInstantiationUtil structure for the system patches
		// 		So some #includes (or template instantiations) may not be completed for those system patches
		auto pipeline = hasSpritePatch ? Techniques::BuildSpritePipeline(patchesInterface, iaAttributes) : Techniques::BuildAutoPipeline(patchesInterface, iaAttributes);
		for (auto& out:pipeline) {
			if (unsigned(out._stage) >= dimof(nascentDesc._shaders)) continue;
			if (!out._resource._patchCollectionExpansions.empty()) {
				out._resource._patchCollection = shaderPatches;

				// HACK -- the file for our system patches will not be included by default, because it's not part of out._resource._patchCollection
				// We'll get around this by just forcing an include of our lighting technique file
				if (!_additionalPrePatchesFragment.empty()) {
					out._resource._prePatchesFragments.emplace_back(_additionalPrePatchesFragment);
				}
			}
			nascentDesc._shaders[unsigned(out._stage)] = std::move(out._resource);
			nascentDesc._additionalSelectorFiltering[unsigned(out._stage)] = _additionalSelectorFiltering;
		}

		nascentDesc._manualSelectorFiltering = _selectorFiltering;
		nascentDesc._techniquePreconfigurationFile = _patches.GetPreconfigurationFileName().AsString();

		if (!shaderPatches->GetInterface().GetPreconfigurationFileName().empty())
			nascentDesc._materialPreconfigurationFile = shaderPatches->GetInterface().GetPreconfigurationFileName();
		if (nascentDesc._techniquePreconfigurationFile.empty())
			nascentDesc._techniquePreconfigurationFile = _delegateConfig.GetPreconfiguration().AsString();
	}

	static void FlexibleTechniqueHelper_ConstructEntryToPromise(
		std::promise<FlexibleTechniqueHelper::Entry>&& promise,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		::Assets::WhenAll(
			util->GetFuture<ShaderSourceParser::ManualSelectorFiltering>("ManualSelectorFiltering"_h, indexer),
			util->GetFuture<RenderCore::Assets::ShaderPatchCollection>("ShaderPatchCollection"_h, indexer),
			util->GetFuture<RenderCore::Assets::TechniqueDelegateConfig>("TechniqueDelegateConfig"_h, indexer)).ThenConstructToPromise(
			std::move(promise), [util](const auto& selectorFiltering, const auto& patches, const auto& delegateConfig) {
				FlexibleTechniqueHelper::Entry result;
				result._selectorFiltering = selectorFiltering.get();
				result._patches = patches.get();
				result._delegateConfig = delegateConfig.get();
				::Assets::DependencyValidationMarker depVals[] { selectorFiltering.GetDependencyValidation(), patches.GetDependencyValidation(), delegateConfig.GetDependencyValidation() };
				result._depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);

				std::set<std::string> rawShadersForSystemPatches;

				// interpret the patches interface
				for (const auto& p:result._patches.GetPatches()) {
					ShaderSourceParser::GenerateFunctionOptions generateOptions;
					generateOptions._shaderLanguage = Techniques::GetDefaultShaderLanguage();
					// generateOptions._pipelineLayoutMaterialDescriptorSet = materialDescSetLayout.GetLayout().get();
					// generateOptions._materialDescriptorSetIndex = materialDescSetLayout.GetSlotIndex();
					auto instShader = ShaderSourceParser::InstantiateShader(MakeIteratorRange(&p.second, &p.second+1), generateOptions);
					for (const auto& e:instShader._entryPoints)
						result._patchDelegateInput.emplace_back(e._name, e._signature, Hash64(e._implementsName));
					rawShadersForSystemPatches.insert(instShader._rawShaderFileIncludes.begin(), instShader._rawShaderFileIncludes.end());
				}

				if (!rawShadersForSystemPatches.empty()) {
					std::vector<std::shared_future<std::shared_ptr<ShaderSourceParser::SelectorFilteringRules>>> additionalSelectorFiltering;
					for (auto s:rawShadersForSystemPatches) {
						additionalSelectorFiltering.emplace_back(::Assets::GetAssetFuturePtr<ShaderSourceParser::SelectorFilteringRules>(s));
						result._additionalPrePatchesFragment = Concatenate(result._additionalPrePatchesFragment, "#include \"", s, "\"\n");
					}
					if (additionalSelectorFiltering.size() == 1) {
						YieldToPool(additionalSelectorFiltering.back());
						result._additionalSelectorFiltering = additionalSelectorFiltering.back().get();
					} else {
						result._additionalSelectorFiltering = std::make_shared<ShaderSourceParser::SelectorFilteringRules>();
						for (auto& r:additionalSelectorFiltering) {
							YieldToPool(r);
							result._additionalSelectorFiltering->MergeIn(*r.get());
						}
					}
				}

				return result;
			});
	}
	
	void FlexibleTechniqueHelper::ConstructToPromise(
		std::promise<FlexibleTechniqueHelper>&& promise,
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		StringSection<> src)
	{
		::Assets::WhenAll(util->GetCachedFutureScaffold(src)).ThenConstructToPromise(
			std::move(promise),
			[util, src=src.AsString()](auto&& promise, const auto& scaffold) {
				TRY {
					struct Helper
					{
						std::vector<std::pair<uint64_t, std::future<FlexibleTechniqueHelper::Entry>>> _futures;
						unsigned _iterator = 0;
					};

					auto scaffoldActual = scaffold.get();
					auto helper = std::make_shared<Helper>();
					helper->_futures.reserve(scaffoldActual->_entityLookup.size());
					for (const auto& e:scaffoldActual->_entityLookup) {
						auto h = Hash64(e.second._name);
						helper->_futures.emplace_back(h, ::Assets::ConstructToFutureFn<FlexibleTechniqueHelper_ConstructEntryToPromise>(util, ::AssetsNew::ScaffoldAndEntityName{scaffold, h}));
					}

					::Assets::PollToPromise(
						std::move(promise),
						[helper](auto timeout) {
							auto timeoutTime = std::chrono::steady_clock::now() + timeout;
							for (auto& f:helper->_futures)
								if (f.second.wait_until(timeoutTime) == std::future_status::timeout)
									return ::Assets::PollStatus::Continue;
							return ::Assets::PollStatus::Finish;
						},
						[helper, src, util]() {
							FlexibleTechniqueHelper result;
							std::vector<::Assets::DependencyValidationMarker> depVals;
							depVals.reserve(helper->_futures.size());
							result._entries.reserve(helper->_futures.size());

							StringSection<> pipelineLayoutName;
							auto additionalPrepatchesFragment = Concatenate("#include \"", src, "\"");

							for (auto& f:helper->_futures) {
								result._entries.emplace_back(f.first, f.second.get());
								depVals.emplace_back(result._entries.back().second._depVal);

								// Note -- each configuration in this file is loaded, but we're not sure yet which are going to be relevant to the
								//		actual TechniqueDelegate. However, each technique delegate can only have a single pipeline layout
								//		(note that they can still have varied material descriptor set, via the mechanisms for that)
								//		So, we'll require that all entries use the same pipeline layout
								auto entryPipelineLayout = result._entries.back().second._delegateConfig.GetPipelineLayout();
								if (!entryPipelineLayout.IsEmpty()) {
									if (!pipelineLayoutName.IsEmpty() && !XlEqString(entryPipelineLayout, pipelineLayoutName))
										Throw(std::runtime_error("Pipeline layout name disagreement in technique delegate file: " + pipelineLayoutName.AsString() + ", and " + entryPipelineLayout.AsString()));
									pipelineLayoutName = entryPipelineLayout;
								}
							}

							if (!pipelineLayoutName.IsEmpty()) {
								auto pipelineLayoutFuture = ::Assets::GetAssetFuturePtr<RenderCore::Assets::PredefinedPipelineLayout>(pipelineLayoutName);
								YieldToPool(pipelineLayoutFuture);
								result._pipelineLayout = pipelineLayoutFuture.get();
								depVals.emplace_back(result._pipelineLayout->GetDependencyValidation());
							}
							
							result._depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
							return result;
						});

				} CATCH(...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

	static std::atomic<uint64_t> s_nextTechniqueDelegateGuid = 1;
	ITechniqueDelegate::ITechniqueDelegate() : _guid(s_nextTechniqueDelegateGuid++) {}
	ITechniqueDelegate::~ITechniqueDelegate() {}

}}


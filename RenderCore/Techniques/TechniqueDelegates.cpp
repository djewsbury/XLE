// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueDelegates.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"
#include "Techniques.h"
#include "../Assets/MaterialScaffold.h"
#include "../IDevice.h"
#include "../Format.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"
#include <sstream>
#include <regex>
#include <cctype>
#include <charconv>

namespace RenderCore { namespace Techniques
{

	class TechniqueDelegate_Legacy : public ITechniqueDelegate
	{
	public:
		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& input) override;

		std::string GetPipelineLayout() override;

		TechniqueDelegate_Legacy(
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
		::Assets::PtrToFuturePtr<Technique> _techniqueFuture;
	};

	static void PrepareShadersFromTechniqueEntry(
		const std::shared_ptr<GraphicsPipelineDesc>& nascentDesc,
		const TechniqueEntry& entry)
	{
		nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = entry._vertexShaderName;
		nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = entry._pixelShaderName;
		nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = entry._geometryShaderName;
		nascentDesc->_manualSelectorFiltering = entry._selectorFiltering;
		nascentDesc->_selectorPreconfigurationFile = entry._preconfigurationFileName;
	}

	auto TechniqueDelegate_Legacy::GetPipelineDesc(
		const CompiledShaderPatchCollection::Interface& shaderPatches,
		const RenderCore::Assets::RenderStateSet& input) -> ::Assets::PtrToFuturePtr<GraphicsPipelineDesc>
	{
		auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("resolved-technique");

		auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
		nascentDesc->_blend.push_back(_blend);
		nascentDesc->_rasterization = _rasterization;
		nascentDesc->_depthStencil = _depthStencil;

		auto technique = _techniqueFuture->TryActualize();
		if (technique) {
			auto& actualTechnique = *technique->get();
			nascentDesc->_depVal = actualTechnique.GetDependencyValidation();
			auto& entry = actualTechnique.GetEntry(_techniqueIndex);
			PrepareShadersFromTechniqueEntry(nascentDesc, entry);
			result->SetAsset(std::move(nascentDesc), {});
		} else {
			// We need to poll until the technique file is ready, and then continue on to figuring out the shader
			// information as usual
			::Assets::WhenAll(_techniqueFuture).ThenConstructToFuture(
				*result,
				[techniqueIndex = _techniqueIndex, nascentDesc](std::shared_ptr<Technique> technique) {
					nascentDesc->_depVal = technique->GetDependencyValidation();
					auto& entry = technique->GetEntry(techniqueIndex);
					PrepareShadersFromTechniqueEntry(nascentDesc, entry);
					return nascentDesc;
				});
		}

		return result;
	}

	std::string TechniqueDelegate_Legacy::GetPipelineLayout()
	{
		return MAIN_PIPELINE ":GraphicsMain";
	}

	TechniqueDelegate_Legacy::TechniqueDelegate_Legacy(
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	: _techniqueIndex(techniqueIndex)
	, _blend(blend)
	, _rasterization(rasterization)
	, _depthStencil(depthStencil)
	{
		const char* techFile = ILLUM_LEGACY_TECH;
		_techniqueFuture = ::Assets::MakeAsset<Technique>(techFile);
	}

	TechniqueDelegate_Legacy::~TechniqueDelegate_Legacy()
	{
	}

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegateLegacy(
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	{
		return std::make_shared<TechniqueDelegate_Legacy>(techniqueIndex, blend, rasterization, depthStencil);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		T E C H N I Q U E   D E L E G A T E
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const auto s_perPixel = Hash64("PerPixel");
	static const auto s_earlyRejectionTest = Hash64("EarlyRejectionTest");
	static const auto s_deformVertex = Hash64("DeformVertex");
	static uint64_t s_patchExp_perPixelAndEarlyRejection[] = { s_perPixel, s_earlyRejectionTest };
	static uint64_t s_patchExp_perPixel[] = { s_perPixel };
	static uint64_t s_patchExp_earlyRejection[] = { s_earlyRejectionTest };

	IllumType CalculateIllumType(const CompiledShaderPatchCollection::Interface& shaderPatches)
	{
		if (shaderPatches.HasPatchType(s_perPixel)) {
			if (shaderPatches.HasPatchType(s_earlyRejectionTest)) {
				return IllumType::PerPixelAndEarlyRejection;
			} else {
				return IllumType::PerPixel;
			}
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("Deferred_NoPatches");
				const auto perPixelHash = Hash64("Deferred_PerPixel");
				const auto perPixelAndEarlyRejectionHash = Hash64("Deferred_PerPixelAndEarlyRejection");
				const auto vsNoPatchesHash = Hash64("VS_NoPatches");
				const auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* perPixelSrc = _techniqueSet->FindEntry(perPixelHash);
				auto* perPixelAndEarlyRejectionSrc = _techniqueSet->FindEntry(perPixelAndEarlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !perPixelSrc || !perPixelAndEarlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_noPatches = *noPatchesSrc;
				_perPixel = *perPixelSrc;
				_perPixelAndEarlyRejection = *perPixelAndEarlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-deferred-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRastizerDesc(stateSet);
			bool deferredDecal = 
					(stateSet._flag & Assets::RenderStateSet::Flag::BlendType)
				&&	(stateSet._blendType == Assets::RenderStateSet::BlendType::DeferredDecal);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, illumType, hasDeformVertex](
					std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_perPixel;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_perPixelAndEarlyRejection;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();
					
					// note -- we could premerge all of the combinations in the constructor, to cut down on cost here
					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		TechniqueDelegate_Deferred(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(*_techniqueFileHelper);
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Deferred(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet)
	{
		return std::make_shared<TechniqueDelegate_Deferred>(techniqueSet);
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
			TechniqueEntry _vsNoPatchesSrc;
			TechniqueEntry _vsDeformVertexSrc;

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("Forward_NoPatches");
				const auto perPixelHash = Hash64("Forward_PerPixel");
				const auto perPixelAndEarlyRejectionHash = Hash64("Forward_PerPixelAndEarlyRejection");
				const auto vsNoPatchesHash = Hash64("VS_NoPatches");
				const auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* perPixelSrc = _techniqueSet->FindEntry(perPixelHash);
				auto* perPixelAndEarlyRejectionSrc = _techniqueSet->FindEntry(perPixelAndEarlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !perPixelSrc || !perPixelAndEarlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_noPatches = *noPatchesSrc;
				_perPixel = *perPixelSrc;
				_perPixelAndEarlyRejection = *perPixelAndEarlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-forward-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRastizerDesc(stateSet);

			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}
			nascentDesc->_depthStencil = _depthStencil;

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, illumType, hasDeformVertex](
					std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_perPixel;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_perPixelAndEarlyRejection;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsForwardPlus";
		}

		TechniqueDelegate_Forward(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
			TechniqueDelegateForwardFlags::BitField flags)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(*_techniqueFileHelper);

			if (flags & TechniqueDelegateForwardFlags::DisableDepthWrite) {
				_depthStencil = CommonResourceBox::s_dsReadOnly;
			} else {
				_depthStencil = {};
			}
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		DepthStencilDesc _depthStencil;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Forward(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags)
	{
		return std::make_shared<TechniqueDelegate_Forward>(techniqueSet, flags);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet, std::optional<ShadowGenType> shadowGen)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("DepthOnly_NoPatches");
				const auto earlyRejectionHash = Hash64("DepthOnly_EarlyRejection");
				auto vsNoPatchesHash = Hash64("VS_NoPatches");
				auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				if (shadowGen) {
					if (*shadowGen == ShadowGenType::GSAmplify) {
						vsNoPatchesHash = Hash64("VSShadowGen_GSAmplify_NoPatches");
						vsDeformVertexHash = Hash64("VSShadowGen_GSAmplify_DeformVertex");
					} else {
						vsNoPatchesHash = Hash64("VSShadowProbe_NoPatches");
						vsDeformVertexHash = Hash64("VSShadowProbe_DeformVertex");
					}
				}
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* earlyRejectionSrc = _techniqueSet->FindEntry(earlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !earlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_noPatches = *noPatchesSrc;
				_earlyRejectionSrc = *earlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-forward-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			// always use less than (not less than or equal) here, because writing equally deep pixels is redundant
			// (and we can potentially skip a texture lookup for alpha test geo sometimes)
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteLessThan;

			bool hasEarlyRejectionTest = shaderPatches.HasPatchType(s_earlyRejectionTest);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, hasEarlyRejectionTest, hasDeformVertex](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					if (hasEarlyRejectionTest) {
						psTechEntry = &techniqueFileHelper->_earlyRejectionSrc;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		TechniqueDelegate_DepthOnly(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode,
			std::optional<ShadowGenType> shadowGen)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(
				*_techniqueFileHelper, 
				[shadowGen](std::shared_ptr<TechniqueSetFile> techniqueSet) { return std::make_shared<TechniqueFileHelper>(techniqueSet, shadowGen); });

			_rs[0x0] = RasterizationDesc{cullMode,        FaceWinding::CCW, (float)singleSidedBias._depthBias, singleSidedBias._depthBiasClamp, singleSidedBias._slopeScaledBias};
            _rs[0x1] = RasterizationDesc{CullMode::None,  FaceWinding::CCW, (float)doubleSidedBias._depthBias, doubleSidedBias._depthBiasClamp, doubleSidedBias._slopeScaledBias};			
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		RasterizationDesc _rs[2];
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_DepthOnly(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode)
	{
		return std::make_shared<TechniqueDelegate_DepthOnly>(techniqueSet, singleSidedBias, doubleSidedBias, cullMode, std::optional<ShadowGenType>{});
	}

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ShadowGen(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		ShadowGenType shadowGenType,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode)
	{
		return std::make_shared<TechniqueDelegate_DepthOnly>(techniqueSet, singleSidedBias, doubleSidedBias, cullMode, shadowGenType);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet, PreDepthType preDepthType)
			: _techniqueSet(techniqueSet)
			{
				uint64_t psNoPatchesHash, psPerPixelHash, perPixelAndEarlyRejectionHash;
				if (preDepthType == PreDepthType::DepthMotionNormal) {
					psNoPatchesHash = Hash64("DepthMotionNormal_NoPatches");
					psPerPixelHash = Hash64("DepthMotionNormal_PerPixel");
					perPixelAndEarlyRejectionHash = Hash64("DepthMotionNormal_PerPixelAndEarlyRejection");
				} else if (preDepthType == PreDepthType::DepthMotion) {
					psNoPatchesHash = Hash64("DepthMotion_NoPatches");
					psPerPixelHash = Hash64("DepthMotion_PerPixel");
					perPixelAndEarlyRejectionHash = Hash64("DepthMotion_PerPixelAndEarlyRejection");
				} else {
					assert(preDepthType == PreDepthType::DepthOnly);
					psNoPatchesHash = Hash64("DepthOnly_NoPatches");
					psPerPixelHash = Hash64("DepthOnly_NoPatches");
					perPixelAndEarlyRejectionHash = Hash64("DepthOnly_EarlyRejection");
				}
				auto vsNoPatchesHash = Hash64("VS_NoPatches");
				auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* psNoPatchesSrc = _techniqueSet->FindEntry(psNoPatchesHash);
				auto* psPerPixelSrc = _techniqueSet->FindEntry(psPerPixelHash);
				auto* perPixelAndEarlyRejectionSrc = _techniqueSet->FindEntry(perPixelAndEarlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!psNoPatchesSrc || !psPerPixelSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_psNoPatchesSrc = *psNoPatchesSrc;
				_psPerPixelSrc = *psPerPixelSrc;
				_psPerPixelAndEarlyRejection = *perPixelAndEarlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif
			
			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-predepth-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			if (stateSet._flag & Assets::RenderStateSet::Flag::DepthBias) 		// we must let the state set override depth bias for decal-style geometry
				nascentDesc->_rasterization._depthBiasConstantFactor = (float)stateSet._depthBias;
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteLessThan;
			if (_preDepthType != PreDepthType::DepthOnly) {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
				if (_preDepthType == PreDepthType::DepthMotionNormal)
					nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, illumType, hasDeformVertex](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_psNoPatchesSrc;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_psPerPixelSrc;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_psPerPixelAndEarlyRejection;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		TechniqueDelegate_PreDepth(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
			PreDepthType preDepthType)
		: _preDepthType(preDepthType)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(
				*_techniqueFileHelper,
				[preDepthType](auto techSet) { return std::make_shared<TechniqueFileHelper>(techSet, preDepthType); });

			_rs[0x0] = CommonResourceBox::s_rsDefault;
            _rs[0x1] = CommonResourceBox::s_rsCullDisable;			
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		RasterizationDesc _rs[2];
		PreDepthType _preDepthType;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_PreDepth(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		PreDepthType preDepthType)
	{
		return std::make_shared<TechniqueDelegate_PreDepth>(techniqueSet, preDepthType);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet, UtilityDelegateType utilityType)
			: _techniqueSet(techniqueSet)
			{
				uint64_t psNoPatchesHash, psPerPixelHash, perPixelAndEarlyRejectionHash;
				if (utilityType == UtilityDelegateType::FlatColor) {
					psNoPatchesHash = Hash64("FlatColor_NoPatches");
					psPerPixelHash = Hash64("FlatColor_NoPatches");
					perPixelAndEarlyRejectionHash = Hash64("FlatColor_PerPixelAndEarlyRejection");
				} else {
					assert(0);
				}
				auto vsNoPatchesHash = Hash64("VS_NoPatches");
				auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* psNoPatchesSrc = _techniqueSet->FindEntry(psNoPatchesHash);
				auto* psPerPixelSrc = _techniqueSet->FindEntry(psPerPixelHash);
				auto* perPixelAndEarlyRejectionSrc = _techniqueSet->FindEntry(perPixelAndEarlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!psNoPatchesSrc || !psPerPixelSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_psNoPatchesSrc = *psNoPatchesSrc;
				_psPerPixelSrc = *psPerPixelSrc;
				_psPerPixelAndEarlyRejection = *perPixelAndEarlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-utility-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWrite;
			nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, illumType, hasDeformVertex](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_psNoPatchesSrc;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_psPerPixelSrc;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_psPerPixelAndEarlyRejection;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		TechniqueDelegate_Utility(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
			UtilityDelegateType utilityType)
		: _utilityType(utilityType)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(
				*_techniqueFileHelper,
				[utilityType](auto techSet) { return std::make_shared<TechniqueFileHelper>(techSet, utilityType); });

			_rs[0x0] = CommonResourceBox::s_rsDefault;
            _rs[0x1] = CommonResourceBox::s_rsCullDisable;			
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		RasterizationDesc _rs[2];
		UtilityDelegateType _utilityType;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Utility(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		UtilityDelegateType type)
	{
		return std::make_shared<TechniqueDelegate_Utility>(techniqueSet, type);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("ProbePrepare_NoPatches");
				const auto perPixelHash = Hash64("ProbePrepare_PerPixel");
				const auto earlyRejectionHash = Hash64("ProbePrepare_PerPixelAndEarlyRejection");
				auto vsNoPatchesHash = Hash64("VS_NoPatches");
				auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* perPixelSrc = _techniqueSet->FindEntry(perPixelHash);
				auto* earlyRejectionSrc = _techniqueSet->FindEntry(earlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !perPixelSrc || !earlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_noPatches = *noPatchesSrc;
				_perPixel = *perPixelSrc;
				_perPixelAndEarlyRejection = *earlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-probe-prepare-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRastizerDesc(stateSet);

			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteLessThan;		// note -- read and write from depth -- if we do a pre-depth pass for probes we could just set this to read

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, illumType, hasDeformVertex](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_perPixel;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_perPixelAndEarlyRejection;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsProbePrepare";
		}

		TechniqueDelegate_ProbePrepare(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(*_techniqueFileHelper);
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ProbePrepare(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet)
	{
		return std::make_shared<TechniqueDelegate_ProbePrepare>(techniqueSet);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("RayTest_NoPatches");
				const auto earlyRejectionHash = Hash64("RayTest_EarlyRejection");
				const auto vsNoPatchesHash = Hash64("VS_NoPatches");
				const auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* earlyRejectionSrc = _techniqueSet->FindEntry(earlyRejectionHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !earlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}

				_noPatches = *noPatchesSrc;
				_earlyRejectionSrc = *earlyRejectionSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
		};

		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			#if defined(_DEBUG)
				if (_techniqueFileHelper->GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			auto result = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("from-forward-delegate");
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_depthStencil = CommonResourceBox::s_dsDisable;

			nascentDesc->_soElements = _soElements;
			nascentDesc->_soBufferStrides = _soStrides;

			bool hasEarlyRejectionTest = shaderPatches.HasPatchType(s_earlyRejectionTest);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture(
				*result,
				[nascentDesc, hasEarlyRejectionTest, hasDeformVertex, testType=_testTypeParameter](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					if (hasEarlyRejectionTest) {
						psTechEntry = &techniqueFileHelper->_earlyRejectionSrc;
						nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						nascentDesc->_patchExpansions.push_back(s_deformVertex);
					}

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					nascentDesc->_manualSelectorFiltering._setValues.SetParameter("INTERSECTION_TEST", testType);
					if (hasEarlyRejectionTest)
						nascentDesc->_manualSelectorFiltering._setValues.SetParameter("PATCH_EARLY_REJECTION", 1);
					return nascentDesc;
				});			
			return result;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		TechniqueDelegate_RayTest(
			const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
			unsigned testTypeParameter,
			const StreamOutputInitializers& soInit)
		: _testTypeParameter(testTypeParameter)
		{
			_techniqueFileHelper = std::make_shared<::Assets::FuturePtr<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture(*_techniqueFileHelper);

			_soElements = NormalizeInputAssembly(soInit._outputElements);
			_soStrides = std::vector<unsigned>(soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end());
		}
	private:
		::Assets::PtrToFuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		std::vector<InputElementDesc> _soElements;
		std::vector<unsigned> _soStrides;
		unsigned _testTypeParameter;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_RayTest(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit)
	{
		return std::make_shared<TechniqueDelegate_RayTest>(techniqueSet, testTypeParameter, soInit);
	}

	uint64_t GraphicsPipelineDesc::GetHash() const
	{
		uint64_t result = _manualSelectorFiltering.GetHash();
		for (unsigned c=0; c<dimof(_shaders); ++c)
			if (!_shaders[c].empty()) result = Hash64(_shaders[c], result);
		if (!_selectorPreconfigurationFile.empty())
			result = Hash64(_selectorPreconfigurationFile, result);
		if (!_patchExpansions.empty())
			result = Hash64(AsPointer(_patchExpansions.begin()), AsPointer(_patchExpansions.end()), result);
		for (const auto&b:_blend)
			result = HashCombine(b.Hash(), result);
		result = HashCombine(_depthStencil.HashDepthAspect(), result);
		result = HashCombine(_depthStencil.HashStencilAspect(), result);
		result = HashCombine(_rasterization.Hash(), result);
		if (!_soElements.empty()) {
			result = HashInputAssembly(MakeIteratorRange(_soElements), result);
			result = Hash64(AsPointer(_soBufferStrides.begin()), AsPointer(_soBufferStrides.end()), result);
		}
		return result;
	}

	ITechniqueDelegate::~ITechniqueDelegate() {}

}}


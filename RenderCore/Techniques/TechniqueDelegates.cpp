// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueDelegates.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"
#include "Techniques.h"
#include "../Assets/RawMaterial.h"
#include "../IDevice.h"
#include "../Format.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"
#include <sstream>
#include <cctype>
#include <charconv>

namespace RenderCore { namespace Techniques
{

	class TechniqueDelegate_Legacy : public ITechniqueDelegate
	{
	public:
		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& input) override;

		std::string GetPipelineLayout() override;
		::Assets::DependencyValidation GetDependencyValidation() override;

		TechniqueDelegate_Legacy(
			std::shared_ptr<Technique> technique,
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
	};

	static void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry)
	{
		nascentDesc._shaders[(unsigned)ShaderStage::Vertex] = entry._vertexShaderName;
		nascentDesc._shaders[(unsigned)ShaderStage::Pixel] = entry._pixelShaderName;
		nascentDesc._shaders[(unsigned)ShaderStage::Geometry] = entry._geometryShaderName;
		nascentDesc._manualSelectorFiltering = entry._selectorFiltering;
		nascentDesc._techniquePreconfigurationFile = entry._preconfigurationFileName;
	}

	auto TechniqueDelegate_Legacy::GetPipelineDesc(
		const CompiledShaderPatchCollection::Interface& shaderPatches,
		const RenderCore::Assets::RenderStateSet& input) -> std::shared_ptr<GraphicsPipelineDesc>
	{
		auto result = std::make_shared<GraphicsPipelineDesc>();

		if (_techniqueIndex != Techniques::TechniqueIndex::DepthOnly)
			result->_blend.push_back(_blend);
		result->_rasterization = _rasterization;
		result->_depthStencil = _depthStencil;
		result->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

		result->_depVal = _technique->GetDependencyValidation();
		auto& entry = _technique->GetEntry(_techniqueIndex);
		PrepareShadersFromTechniqueEntry(*result, entry);

		return result;
	}

	std::string TechniqueDelegate_Legacy::GetPipelineLayout() { return MAIN_PIPELINE ":GraphicsMain"; }
	::Assets::DependencyValidation TechniqueDelegate_Legacy::GetDependencyValidation() { return {}; }

	TechniqueDelegate_Legacy::TechniqueDelegate_Legacy(
		std::shared_ptr<Technique> technique,
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	: _technique(std::move(technique))
	, _techniqueIndex(techniqueIndex)
	, _blend(blend)
	, _rasterization(rasterization)
	, _depthStencil(depthStencil)
	{}

	TechniqueDelegate_Legacy::~TechniqueDelegate_Legacy()
	{}

	void CreateTechniqueDelegateLegacy(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil)
	{
		const char* techFile = ILLUM_LEGACY_TECH;
		auto techniqueFuture = ::Assets::MakeAssetPtr<Technique>(techFile);
		::Assets::WhenAll(techniqueFuture).ThenConstructToPromise(
			std::move(promise),
			[techniqueIndex, blend, rasterization, depthStencil](auto technique) {
				return std::make_shared<TechniqueDelegate_Legacy>(std::move(technique), techniqueIndex, blend, rasterization, depthStencil);
			});
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//		T E C H N I Q U E   D E L E G A T E
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const auto s_perPixel = Hash64("PerPixel");
	static const auto s_perPixelCustomLighting = Hash64("PerPixelCustomLighting");
	static const auto s_earlyRejectionTest = Hash64("EarlyRejectionTest");
	static const auto s_vertexPatch = Hash64("VertexPatch");
	static std::pair<uint64_t, ShaderStage> s_patchExp_perPixelAndEarlyRejection[] = { {s_perPixel, ShaderStage::Pixel}, {s_earlyRejectionTest, ShaderStage::Pixel} };
	static std::pair<uint64_t, ShaderStage> s_patchExp_perPixel[] = { {s_perPixel, ShaderStage::Pixel} };
	static std::pair<uint64_t, ShaderStage> s_patchExp_perPixelCustomLighting[] = { {s_perPixelCustomLighting, ShaderStage::Pixel} };
	static std::pair<uint64_t, ShaderStage> s_patchExp_earlyRejection[] = { {s_earlyRejectionTest, ShaderStage::Pixel} };
	static std::pair<uint64_t, ShaderStage> s_patchExp_deformVertex[] = { { s_vertexPatch, ShaderStage::Vertex } };

	IllumType CalculateIllumType(const CompiledShaderPatchCollection::Interface& shaderPatches)
	{
		if (shaderPatches.HasPatchType(s_perPixel)) {
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
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

			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRastizerDesc(stateSet);
			bool deferredDecal = 
					(stateSet._flag & Assets::RenderStateSet::Flag::BlendType)
				&&	(stateSet._blendType == Assets::RenderStateSet::BlendType::DeferredDecal);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_blend.push_back(deferredDecal ? CommonResourceBox::s_abStraightAlpha : CommonResourceBox::s_abOpaque);
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWrite;
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			switch (illumType) {
			case IllumType::PerPixel:
				psTechEntry = &_techniqueFileHelper._perPixel;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
				break;
			case IllumType::PerPixelAndEarlyRejection:
				psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
				break;
			default:
				break;
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();
			
			// note -- we could premerge all of the combinations in the constructor, to cut down on cost here
			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_Deferred(std::shared_ptr<TechniqueSetFile> techniqueSet)
		: _techniqueFileHelper(std::move(techniqueSet))
		{
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
	};

	void CreateTechniqueDelegate_Deferred(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[](auto techniqueSet) { return std::make_shared<TechniqueDelegate_Deferred>(std::move(techniqueSet)); });
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				const auto noPatchesHash = Hash64("Forward_NoPatches");
				const auto perPixelHash = Hash64("Forward_PerPixel");
				const auto perPixelAndEarlyRejectionHash = Hash64("Forward_PerPixelAndEarlyRejection");
				const auto perPixelCustomLightingHash = Hash64("Forward_PerPixelCustomLighting");
				const auto vsNoPatchesHash = Hash64("VS_NoPatches");
				const auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				auto* noPatchesSrc = _techniqueSet->FindEntry(noPatchesHash);
				auto* perPixelSrc = _techniqueSet->FindEntry(perPixelHash);
				auto* perPixelAndEarlyRejectionSrc = _techniqueSet->FindEntry(perPixelAndEarlyRejectionHash);
				auto* perPixelCustomLightingSrc = _techniqueSet->FindEntry(perPixelCustomLightingHash);
				auto* vsNoPatchesSrc = _techniqueSet->FindEntry(vsNoPatchesHash);
				auto* vsDeformVertexSrc = _techniqueSet->FindEntry(vsDeformVertexHash);
				if (!noPatchesSrc || !perPixelSrc || !perPixelAndEarlyRejectionSrc || !vsNoPatchesSrc || !vsDeformVertexSrc || !perPixelCustomLightingSrc) {
					Throw(std::runtime_error("Could not construct technique delegate because required configurations were not found"));
				}
				_noPatches = *noPatchesSrc;
				_perPixel = *perPixelSrc;
				_perPixelAndEarlyRejection = *perPixelAndEarlyRejectionSrc;
				_perPixelCustomLighting = *perPixelCustomLightingSrc;
				_vsNoPatchesSrc = *vsNoPatchesSrc;
				_vsDeformVertexSrc = *vsDeformVertexSrc;
			}
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
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
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			switch (illumType) {
			case IllumType::PerPixel:
				psTechEntry = &_techniqueFileHelper._perPixel;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
				break;
			case IllumType::PerPixelAndEarlyRejection:
				psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
				break;
			case IllumType::PerPixelCustomLighting:
				psTechEntry = &_techniqueFileHelper._perPixelCustomLighting;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelCustomLighting, &s_patchExp_perPixelCustomLighting[dimof(s_patchExp_perPixelCustomLighting)]);
			default:
				break;
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsForwardPlus";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_Forward(
			std::shared_ptr<TechniqueSetFile> techniqueSet,
			TechniqueDelegateForwardFlags::BitField flags)
		: _techniqueFileHelper(std::move(techniqueSet))
		{
			if (flags & TechniqueDelegateForwardFlags::DisableDepthWrite) {
				_depthStencil = CommonResourceBox::s_dsReadOnly;
			} else {
				_depthStencil = CommonResourceBox::s_dsReadWrite;
			}
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		DepthStencilDesc _depthStencil;
	};

	void CreateTechniqueDelegate_Forward(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[flags](auto techniqueSet) {
				return std::make_shared<TechniqueDelegate_Forward>(std::move(techniqueSet), flags);
			});
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

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet, std::optional<ShadowGenType> shadowGen)
			: _techniqueSet(std::move(techniqueSet))
			{
				const auto noPatchesHash = Hash64("DepthOnly_NoPatches");
				const auto earlyRejectionHash = Hash64("DepthOnly_EarlyRejection");
				auto vsNoPatchesHash = Hash64("VSDepthOnly_NoPatches");
				auto vsDeformVertexHash = Hash64("VSDepthOnly_DeformVertex");
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
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
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
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			bool hasEarlyRejectionTest = shaderPatches.HasPatchType(s_earlyRejectionTest);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			if (hasEarlyRejectionTest) {
				psTechEntry = &_techniqueFileHelper._earlyRejectionSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_DepthOnly(
			std::shared_ptr<TechniqueSetFile> techniqueSet,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode, FaceWinding faceWinding,
			std::optional<ShadowGenType> shadowGen)
		: _techniqueFileHelper(std::move(techniqueSet), shadowGen)
		{
			_rs[0x0] = RasterizationDesc{cullMode,        faceWinding, (float)singleSidedBias._depthBias, singleSidedBias._depthBiasClamp, singleSidedBias._slopeScaledBias};
            _rs[0x1] = RasterizationDesc{CullMode::None,  faceWinding, (float)doubleSidedBias._depthBias, doubleSidedBias._depthBiasClamp, doubleSidedBias._slopeScaledBias};			
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
	};

	void CreateTechniqueDelegate_DepthOnly(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode, FaceWinding faceWinding)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[singleSidedBias, doubleSidedBias, cullMode, faceWinding](auto techniqueSet) {
				return std::make_shared<TechniqueDelegate_DepthOnly>(std::move(techniqueSet), singleSidedBias, doubleSidedBias, cullMode, faceWinding, std::optional<ShadowGenType>{});
			});
	}

	void CreateTechniqueDelegate_ShadowGen(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		ShadowGenType shadowGenType,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode, FaceWinding faceWinding)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGenType](auto techniqueSet) {
				return std::make_shared<TechniqueDelegate_DepthOnly>(std::move(techniqueSet), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGenType);
			});
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
				if (preDepthType != PreDepthType::DepthOnly) {
					psNoPatchesHash = Hash64("DepthPlus_NoPatches");
					psPerPixelHash = Hash64("DepthPlus_PerPixel");
					perPixelAndEarlyRejectionHash = Hash64("DepthPlus_PerPixelAndEarlyRejection");
				} else {
					psNoPatchesHash = Hash64("DepthOnly_NoPatches");
					psPerPixelHash = Hash64("DepthOnly_NoPatches");
					perPixelAndEarlyRejectionHash = Hash64("DepthOnly_EarlyRejection");
				}
				auto vsNoPatchesHash = Hash64("VSDepthOnly_NoPatches");
				auto vsDeformVertexHash = Hash64("VSDepthOnly_DeformVertex");
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
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
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
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._psNoPatchesSrc;
			switch (illumType) {
			case IllumType::PerPixel:
				psTechEntry = &_techniqueFileHelper._psPerPixelSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
				break;
			case IllumType::PerPixelAndEarlyRejection:
				psTechEntry = &_techniqueFileHelper._psPerPixelAndEarlyRejection;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
				break;
			default:
				break;
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
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

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_PreDepth(
			std::shared_ptr<TechniqueSetFile> techniqueSet,
			PreDepthType preDepthType)
		: _techniqueFileHelper(std::move(techniqueSet), preDepthType), _preDepthType(preDepthType)
		{
			_rs[0x0] = CommonResourceBox::s_rsDefault;
			_rs[0x1] = CommonResourceBox::s_rsCullDisable;
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
		PreDepthType _preDepthType;
	};

	void CreateTechniqueDelegate_PreDepth(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		PreDepthType preDepthType)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[preDepthType](auto techniqueSet) {
				return std::make_shared<TechniqueDelegate_PreDepth>(std::move(techniqueSet), preDepthType);
			});
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

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet, UtilityDelegateType utilityType)
			: _techniqueSet(std::move(techniqueSet))
			{
				uint64_t psNoPatchesHash, psPerPixelHash, perPixelAndEarlyRejectionHash;
				if (utilityType == UtilityDelegateType::FlatColor) {
					psNoPatchesHash = Hash64("FlatColor_NoPatches");
					psPerPixelHash = Hash64("FlatColor_NoPatches");
					perPixelAndEarlyRejectionHash = Hash64("FlatColor_PerPixelAndEarlyRejection");
				} else if (utilityType == UtilityDelegateType::CopyDiffuseAlbedo) {
					psNoPatchesHash = Hash64("CopyDiffuseAlbedo_NoPatches");
					psPerPixelHash = Hash64("CopyDiffuseAlbedo_PerPixel");
					perPixelAndEarlyRejectionHash = Hash64("CopyDiffuseAlbedo_PerPixelAndEarlyRejection");
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
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();

			unsigned cullDisable = 0;
			if (stateSet._flag & Assets::RenderStateSet::Flag::DoubleSided)
				cullDisable = !!stateSet._doubleSided;
			nascentDesc->_rasterization = _rs[cullDisable];
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWrite;
			nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._psNoPatchesSrc;
			switch (illumType) {
			case IllumType::PerPixel:
				psTechEntry = &_techniqueFileHelper._psPerPixelSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
				break;
			case IllumType::PerPixelAndEarlyRejection:
				psTechEntry = &_techniqueFileHelper._psPerPixelAndEarlyRejection;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
				break;
			default:
				break;
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_Utility(
			std::shared_ptr<TechniqueSetFile> techniqueSet,
			UtilityDelegateType utilityType)
		: _techniqueFileHelper{std::move(techniqueSet), utilityType}
		, _utilityType(utilityType)
		{
			_rs[0x0] = CommonResourceBox::s_rsDefault;
            _rs[0x1] = CommonResourceBox::s_rsCullDisable;
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
		UtilityDelegateType _utilityType;
	};

	void CreateTechniqueDelegate_Utility(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		UtilityDelegateType type)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[type](auto techniqueSet) {
				return std::make_shared<TechniqueDelegate_Utility>(std::move(techniqueSet), type);
			});
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

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
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
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_rasterization = BuildDefaultRastizerDesc(stateSet);

			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend) {
				nascentDesc->_blend.push_back(AttachmentBlendDesc {
					stateSet._forwardBlendOp != BlendOp::NoBlending,
					stateSet._forwardBlendSrc, stateSet._forwardBlendDst, stateSet._forwardBlendOp });
			} else {
				nascentDesc->_blend.push_back(CommonResourceBox::s_abOpaque);
			}
			nascentDesc->_depthStencil = CommonResourceBox::s_dsReadWriteCloserThan;		// note -- read and write from depth -- if we do a pre-depth pass for probes we could just set this to read
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			switch (illumType) {
			case IllumType::PerPixel:
				psTechEntry = &_techniqueFileHelper._perPixel;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
				break;
			case IllumType::PerPixelAndEarlyRejection:
				psTechEntry = &_techniqueFileHelper._perPixelAndEarlyRejection;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
				break;
			default:
				break;
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsProbePrepare";
		}
				
		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_ProbePrepare(std::shared_ptr<TechniqueSetFile> techniqueSet)
		: _techniqueFileHelper(std::move(techniqueSet))
		{
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
	};

	void CreateTechniqueDelegate_ProbePrepare(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet)
	{
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[](auto techniqueSet) { return std::make_shared<TechniqueDelegate_ProbePrepare>(std::move(techniqueSet)); });
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

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet)
			: _techniqueSet(std::move(techniqueSet))
			{
				const auto noPatchesHash = Hash64("RayTest_NoPatches");
				const auto earlyRejectionHash = Hash64("RayTest_EarlyRejection");
				const auto vsNoPatchesHash = Hash64("VSDepthOnly_NoPatches");
				const auto vsDeformVertexHash = Hash64("VSDepthOnly_DeformVertex");
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
			TechniqueFileHelper() = default;
		};

		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_depthStencil = CommonResourceBox::s_dsDisable;
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			nascentDesc->_soElements = _soElements;
			nascentDesc->_soBufferStrides = _soStrides;

			bool hasEarlyRejectionTest = shaderPatches.HasPatchType(s_earlyRejectionTest);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

			#if defined(_DEBUG)
				if (_techniqueFileHelper.GetDependencyValidation().GetValidationIndex() != 0)
					Log(Warning) << "Technique delegate configuration invalidated, but cannot be hot-loaded" << std::endl;
			#endif

			const TechniqueEntry* psTechEntry = &_techniqueFileHelper._noPatches;
			if (hasEarlyRejectionTest) {
				psTechEntry = &_techniqueFileHelper._earlyRejectionSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_earlyRejection[dimof(s_patchExp_earlyRejection)]);
			}

			const TechniqueEntry* vsTechEntry = &_techniqueFileHelper._vsNoPatchesSrc;
			if (hasDeformVertex) {
				vsTechEntry = &_techniqueFileHelper._vsDeformVertexSrc;
				nascentDesc->_patchExpansions.insert(nascentDesc->_patchExpansions.end(), s_patchExp_deformVertex, &s_patchExp_deformVertex[dimof(s_patchExp_deformVertex)]);
			}

			nascentDesc->_depVal = _techniqueFileHelper.GetDependencyValidation();

			TechniqueEntry mergedTechEntry = *vsTechEntry;
			mergedTechEntry.MergeIn(*psTechEntry);

			PrepareShadersFromTechniqueEntry(*nascentDesc, mergedTechEntry);
			nascentDesc->_manualSelectorFiltering.SetSelector("INTERSECTION_TEST", _testTypeParameter);
			return nascentDesc;
		}

		std::string GetPipelineLayout() override
		{
			return MAIN_PIPELINE ":GraphicsMain";
		}

		::Assets::DependencyValidation GetDependencyValidation() override
		{
			return _techniqueFileHelper.GetDependencyValidation();
		}

		TechniqueDelegate_RayTest(
			std::shared_ptr<TechniqueSetFile> techniqueSet,
			unsigned testTypeParameter,
			std::vector<InputElementDesc> soElements,
			std::vector<unsigned> soStrides)
		: _techniqueFileHelper(std::move(techniqueSet)), _testTypeParameter(testTypeParameter)
		, _soElements(std::move(soElements)), _soStrides(std::move(soStrides))
		{
		}
	private:
		TechniqueFileHelper _techniqueFileHelper;
		std::vector<InputElementDesc> _soElements;
		std::vector<unsigned> _soStrides;
		unsigned _testTypeParameter;
	};

	void CreateTechniqueDelegate_RayTest(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit)
	{
		auto soElements = NormalizeInputAssembly(soInit._outputElements);
		auto soStrides = std::vector<unsigned>(soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end());
		::Assets::WhenAll(techniqueSet).ThenConstructToPromise(
			std::move(promise),
			[testTypeParameter, soElements=std::move(soElements), soStrides=std::move(soStrides)](auto techniqueSet) mutable {
				return std::make_shared<TechniqueDelegate_RayTest>(std::move(techniqueSet), testTypeParameter, std::move(soElements), std::move(soStrides));
			});
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
			result = Hash64(AsPointer(_soBufferStrides.begin()), AsPointer(_soBufferStrides.end()), result);
		}
		for (unsigned c=0; c<dimof(_shaders); ++c)
			if (!_shaders[c].empty()) result = Hash64(_shaders[c], result);
		if (!_patchExpansions.empty())
			result = Hash64(AsPointer(_patchExpansions.begin()), AsPointer(_patchExpansions.end()), result);
		return result;
	}

	RasterizationDesc BuildDefaultRastizerDesc(const Assets::RenderStateSet& states)
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
		if (states._flag & Assets::RenderStateSet::Flag::Wireframe) {
			fillMode = states._wireframe ? FillMode::Wireframe : FillMode::Solid;
		}

		RasterizationDesc result;
		result._cullMode = cullMode;
		result._depthBiasConstantFactor = (float)depthBias;
		result._depthBiasClamp = 0.f;
		result._depthBiasSlopeFactor = 0.f;
		return result;
	}

	ITechniqueDelegate::~ITechniqueDelegate() {}

}}


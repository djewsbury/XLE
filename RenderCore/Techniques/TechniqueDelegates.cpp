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
	class TechniqueSharedResources
	{
	public:
		CommonResourceBox _commonResources;
		TechniqueSharedResources(IDevice& device) : _commonResources(device) {}
		~TechniqueSharedResources() {}
	};

	std::shared_ptr<TechniqueSharedResources> CreateTechniqueSharedResources(IDevice& device)
	{
		return std::make_shared<TechniqueSharedResources>(device);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////

	class TechniqueDelegate_Legacy : public ITechniqueDelegate
	{
	public:
		::Assets::FuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& input) override;

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
		::Assets::FuturePtr<Technique> _techniqueFuture;
	};

	static void PrepareShadersFromTechniqueEntry(
		const std::shared_ptr<ITechniqueDelegate::GraphicsPipelineDesc>& nascentDesc,
		const TechniqueEntry& entry)
	{
		nascentDesc->_shaders[(unsigned)ShaderStage::Vertex] = entry._vertexShaderName;
		nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = entry._pixelShaderName;
		nascentDesc->_shaders[(unsigned)ShaderStage::Geometry] = entry._geometryShaderName;
		nascentDesc->_manualSelectorFiltering = entry._selectorFiltering;
		nascentDesc->_selectorPreconfigurationFile = entry._preconfigurationFileName;
	}

	auto TechniqueDelegate_Legacy::Resolve(
		const CompiledShaderPatchCollection::Interface& shaderPatches,
		const RenderCore::Assets::RenderStateSet& input) -> ::Assets::FuturePtr<GraphicsPipelineDesc>
	{
		auto result = std::make_shared<::Assets::AssetFuture<GraphicsPipelineDesc>>("resolved-technique");

		auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
		nascentDesc->_blend.push_back(_blend);
		nascentDesc->_rasterization = _rasterization;
		nascentDesc->_depthStencil = _depthStencil;

		auto technique = _techniqueFuture->TryActualize();
		if (technique) {
			nascentDesc->_depVal = technique->GetDependencyValidation();
			auto& entry = technique->GetEntry(_techniqueIndex);
			PrepareShadersFromTechniqueEntry(nascentDesc, entry);
			result->SetAsset(std::move(nascentDesc), {});
		} else {
			// We need to poll until the technique file is ready, and then continue on to figuring out the shader
			// information as usual
			::Assets::WhenAll(_techniqueFuture).ThenConstructToFuture<GraphicsPipelineDesc>(
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

		::Assets::FuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto result = std::make_shared<::Assets::AssetFuture<GraphicsPipelineDesc>>("from-deferred-delegate");

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

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture<GraphicsPipelineDesc>(
				*result,
				[nascentDesc, illumType, hasDeformVertex](
					std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					nascentDesc->_depVal = techniqueFileHelper->GetDependencyValidation();

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
					
					// note -- we could premerge all of the combinations in the constructor, to cut down on cost here
					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			
			return result;
		}

		TechniqueDelegate_Deferred(
			const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
			const std::shared_ptr<TechniqueSharedResources>& sharedResources)
		: _sharedResources(sharedResources)
		{
			_techniqueFileHelper = std::make_shared<::Assets::AssetFuture<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture<TechniqueFileHelper>(*_techniqueFileHelper);
		}
	private:
		::Assets::FuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		std::shared_ptr<TechniqueSharedResources> _sharedResources;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Deferred(
		const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources)
	{
		return std::make_shared<TechniqueDelegate_Deferred>(techniqueSet, sharedResources);
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

		::Assets::FuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto result = std::make_shared<::Assets::AssetFuture<GraphicsPipelineDesc>>("from-forward-delegate");

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

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture<GraphicsPipelineDesc>(
				*result,
				[nascentDesc, illumType, hasDeformVertex](
					std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {

					std::vector<uint64_t> patchExpansions;
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					switch (illumType) {
					case IllumType::PerPixel:
						psTechEntry = &techniqueFileHelper->_perPixel;
						patchExpansions.insert(patchExpansions.end(), s_patchExp_perPixel, &s_patchExp_perPixel[dimof(s_patchExp_perPixel)]);
						break;
					case IllumType::PerPixelAndEarlyRejection:
						psTechEntry = &techniqueFileHelper->_perPixelAndEarlyRejection;
						patchExpansions.insert(patchExpansions.end(), s_patchExp_perPixelAndEarlyRejection, &s_patchExp_perPixelAndEarlyRejection[dimof(s_patchExp_perPixelAndEarlyRejection)]);
						break;
					default:
						break;
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						patchExpansions.push_back(s_deformVertex);
					}

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		TechniqueDelegate_Forward(
			const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
			const std::shared_ptr<TechniqueSharedResources>& sharedResources,
			TechniqueDelegateForwardFlags::BitField flags)
		{
			_sharedResources = sharedResources;

			_techniqueFileHelper = std::make_shared<::Assets::AssetFuture<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture<TechniqueFileHelper>(*_techniqueFileHelper);

			if (flags & TechniqueDelegateForwardFlags::DisableDepthWrite) {
				_depthStencil = CommonResourceBox::s_dsReadOnly;
			} else {
				_depthStencil = {};
			}
		}
	private:
		::Assets::FuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		std::shared_ptr<TechniqueSharedResources> _sharedResources;
		DepthStencilDesc _depthStencil;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Forward(
		const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		TechniqueDelegateForwardFlags::BitField flags)
	{
		return std::make_shared<TechniqueDelegate_Forward>(techniqueSet, sharedResources, flags);
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

			TechniqueFileHelper(const std::shared_ptr<TechniqueSetFile>& techniqueSet, bool shadowGen)
			: _techniqueSet(techniqueSet)
			{
				const auto noPatchesHash = Hash64("DepthOnly_NoPatches");
				const auto earlyRejectionHash = Hash64("DepthOnly_EarlyRejection");
				auto vsNoPatchesHash = Hash64("VS_NoPatches");
				auto vsDeformVertexHash = Hash64("VS_DeformVertex");
				if (shadowGen) {
					vsNoPatchesHash = Hash64("VSShadowGen_NoPatches");
					vsDeformVertexHash = Hash64("VSShadowGen_DeformVertex");
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

		::Assets::FuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto result = std::make_shared<::Assets::AssetFuture<GraphicsPipelineDesc>>("from-forward-delegate");
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

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture<GraphicsPipelineDesc>(
				*result,
				[nascentDesc, hasEarlyRejectionTest, hasDeformVertex](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {
					std::vector<uint64_t> patchExpansions;
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					if (hasEarlyRejectionTest) {
						psTechEntry = &techniqueFileHelper->_earlyRejectionSrc;
						patchExpansions.insert(patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_perPixel[dimof(s_patchExp_earlyRejection)]);
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						patchExpansions.push_back(s_deformVertex);
					}

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					return nascentDesc;
				});
			return result;
		}

		TechniqueDelegate_DepthOnly(
			const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
			const std::shared_ptr<TechniqueSharedResources>& sharedResources,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode,
			bool shadowGen)
		{
			_sharedResources = sharedResources;

			_techniqueFileHelper = std::make_shared<::Assets::AssetFuture<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture<TechniqueFileHelper>(
				*_techniqueFileHelper, 
				[shadowGen](std::shared_ptr<TechniqueSetFile> techniqueSet) { return std::make_shared<TechniqueFileHelper>(techniqueSet, shadowGen); });

			_rs[0x0] = RasterizationDesc{cullMode,        FaceWinding::CCW, (float)singleSidedBias._depthBias, singleSidedBias._depthBiasClamp, singleSidedBias._slopeScaledBias};
            _rs[0x1] = RasterizationDesc{CullMode::None,  FaceWinding::CCW, (float)doubleSidedBias._depthBias, doubleSidedBias._depthBiasClamp, doubleSidedBias._slopeScaledBias};			
		}
	private:
		::Assets::FuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		std::shared_ptr<TechniqueSharedResources> _sharedResources;
		RasterizationDesc _rs[2];
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_DepthOnly(
		const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode)
	{
		return std::make_shared<TechniqueDelegate_DepthOnly>(techniqueSet, sharedResources, singleSidedBias, doubleSidedBias, cullMode, false);
	}

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ShadowGen(
		const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode)
	{
		return std::make_shared<TechniqueDelegate_DepthOnly>(techniqueSet, sharedResources, singleSidedBias, doubleSidedBias, cullMode, true);
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

		::Assets::FuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& stateSet) override
		{
			auto result = std::make_shared<::Assets::AssetFuture<GraphicsPipelineDesc>>("from-forward-delegate");

			auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
			nascentDesc->_depthStencil = CommonResourceBox::s_dsDisable;

			nascentDesc->_soElements = _soElements;
			nascentDesc->_soBufferStrides = _soStrides;

			bool hasEarlyRejectionTest = shaderPatches.HasPatchType(s_earlyRejectionTest);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_deformVertex);

			::Assets::WhenAll(_techniqueFileHelper).ThenConstructToFuture<GraphicsPipelineDesc>(
				*result,
				[nascentDesc, hasEarlyRejectionTest, hasDeformVertex, testType=_testTypeParameter](std::shared_ptr<TechniqueFileHelper> techniqueFileHelper) {
					std::vector<uint64_t> patchExpansions;
					const TechniqueEntry* psTechEntry = &techniqueFileHelper->_noPatches;
					if (hasEarlyRejectionTest) {
						psTechEntry = &techniqueFileHelper->_earlyRejectionSrc;
						patchExpansions.insert(patchExpansions.end(), s_patchExp_earlyRejection, &s_patchExp_perPixel[dimof(s_patchExp_earlyRejection)]);
					}

					const TechniqueEntry* vsTechEntry = &techniqueFileHelper->_vsNoPatchesSrc;
					if (hasDeformVertex) {
						vsTechEntry = &techniqueFileHelper->_vsDeformVertexSrc;
						patchExpansions.push_back(s_deformVertex);
					}

					TechniqueEntry mergedTechEntry = *vsTechEntry;
					mergedTechEntry.MergeIn(*psTechEntry);

					PrepareShadersFromTechniqueEntry(nascentDesc, mergedTechEntry);
					nascentDesc->_manualSelectorFiltering._setValues.SetParameter("INTERSECTION_TEST", testType);
					return nascentDesc;
				});			
			return result;
		}

		TechniqueDelegate_RayTest(
			const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
			const std::shared_ptr<TechniqueSharedResources>& sharedResources,
			unsigned testTypeParameter,
			const StreamOutputInitializers& soInit)
		: _testTypeParameter(testTypeParameter)
		{
			_sharedResources = sharedResources;
			
			_techniqueFileHelper = std::make_shared<::Assets::AssetFuture<TechniqueFileHelper>>();
			::Assets::WhenAll(techniqueSet).ThenConstructToFuture<TechniqueFileHelper>(*_techniqueFileHelper);

			_soElements = NormalizeInputAssembly(soInit._outputElements);
			_soStrides = std::vector<unsigned>(soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end());
		}
	private:
		::Assets::FuturePtr<TechniqueFileHelper> _techniqueFileHelper;
		std::shared_ptr<TechniqueSharedResources> _sharedResources;
		std::vector<InputElementDesc> _soElements;
		std::vector<unsigned> _soStrides;
		unsigned _testTypeParameter;
	};

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_RayTest(
		const ::Assets::FuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit)
	{
		return std::make_shared<TechniqueDelegate_RayTest>(techniqueSet, sharedResources, testTypeParameter, soInit);
	}

	ITechniqueDelegate::~ITechniqueDelegate() {}

}}


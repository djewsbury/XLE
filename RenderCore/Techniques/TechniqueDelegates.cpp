// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueDelegates.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"
#include "Techniques.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../IDevice.h"
#include "../Format.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"
#include <sstream>
#include <cctype>
#include <charconv>

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
{

	class TechniqueDelegate_Legacy : public ITechniqueDelegate
	{
	public:
		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
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
		auto techniqueFuture = ::Assets::MakeAssetPtr<Technique>(ILLUM_LEGACY_TECH);
		::Assets::WhenAll(techniqueFuture).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[techniqueIndex, blend, rasterization, depthStencil](auto&& promise, auto technique) {
				TRY {
					auto pipelineLayoutName = technique->GetEntry(techniqueIndex)._pipelineLayoutName;
					if (pipelineLayoutName.empty()) Throw(std::runtime_error("Missing pipeline layout name in legacy technique delegate"));
					auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(pipelineLayoutName);
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
			// We must write the a flag to the stencil buffer to mark pixels as "not sky"
			nascentDesc->_depthStencil._stencilEnable = true;
			nascentDesc->_depthStencil._stencilWriteMask = 1<<7;
			nascentDesc->_depthStencil._frontFaceStencil._passOp = StencilOp::Replace;
			if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::DoubleSided && stateSet._doubleSided)
				nascentDesc->_depthStencil._backFaceStencil._passOp = StencilOp::Replace;
			nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			auto illumType = CalculateIllumType(shaderPatches);
			bool hasDeformVertex = shaderPatches.HasPatchType(s_vertexPatch);

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

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Deferred(TechniqueFileHelper&& helper, std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout))
		{
			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			const TechniqueSetFileFuture& techniqueSet)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper)](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_Deferred>(std::move(helper), std::move(pipelineLayout));
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

	void CreateTechniqueDelegate_Deferred(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet)
	{
		TechniqueDelegate_Deferred::ConstructToPromise(std::move(promise), techniqueSet);
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

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Forward(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			TechniqueDelegateForwardFlags::BitField flags)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout))
		{
			if (flags & TechniqueDelegateForwardFlags::DisableDepthWrite) {
				_depthStencil = CommonResourceBox::s_dsReadOnly;
			} else {
				_depthStencil = CommonResourceBox::s_dsReadWrite;
			}

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			const TechniqueSetFileFuture& techniqueSet,
			TechniqueDelegateForwardFlags::BitField flags)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[flags](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), flags](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_Forward>(std::move(helper), std::move(pipelineLayout), flags);
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
		DepthStencilDesc _depthStencil;
	};

	void CreateTechniqueDelegate_Forward(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags)
	{
		TechniqueDelegate_Forward::ConstructToPromise(std::move(promise), techniqueSet, flags);
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

			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSet->GetDependencyValidation(); }

			TechniqueFileHelper(std::shared_ptr<TechniqueSetFile> techniqueSet, std::optional<ShadowGenType> shadowGen)
			: _techniqueSet(std::move(techniqueSet))
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

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_DepthOnly(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode, FaceWinding faceWinding,
			std::optional<ShadowGenType> shadowGen)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout))
		{
			_rs[0x0] = RasterizationDesc{cullMode,        faceWinding, (float)singleSidedBias._depthBias, singleSidedBias._depthBiasClamp, singleSidedBias._slopeScaledBias};
            _rs[0x1] = RasterizationDesc{CullMode::None,  faceWinding, (float)doubleSidedBias._depthBias, doubleSidedBias._depthBiasClamp, doubleSidedBias._slopeScaledBias};			

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			const TechniqueSetFileFuture& techniqueSet,
			const RSDepthBias& singleSidedBias,
			const RSDepthBias& doubleSidedBias,
			CullMode cullMode, FaceWinding faceWinding,
			std::optional<ShadowGenType> shadowGen)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile, shadowGen};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_DepthOnly>(std::move(helper), std::move(pipelineLayout), singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGen);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_DepthOnly(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode, FaceWinding faceWinding)
	{
		TechniqueDelegate_DepthOnly::ConstructToPromise(std::move(promise), techniqueSet, singleSidedBias, doubleSidedBias, cullMode, faceWinding, std::optional<ShadowGenType>{});
	}

	void CreateTechniqueDelegate_ShadowGen(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		ShadowGenType shadowGenType,
		const RSDepthBias& singleSidedBias,
        const RSDepthBias& doubleSidedBias,
        CullMode cullMode, FaceWinding faceWinding)
	{
		TechniqueDelegate_DepthOnly::ConstructToPromise(std::move(promise), techniqueSet, singleSidedBias, doubleSidedBias, cullMode, faceWinding, shadowGenType);
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

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_PreDepth(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			PreDepthType preDepthType)
		: _techniqueFileHelper(std::move(helper)), _pipelineLayout(std::move(pipelineLayout)), _preDepthType(preDepthType)
		{
			_rs[0x0] = CommonResourceBox::s_rsDefault;
			_rs[0x1] = CommonResourceBox::s_rsCullDisable;

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			const TechniqueSetFileFuture& techniqueSet,
			PreDepthType preDepthType)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[preDepthType](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile, preDepthType};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), preDepthType](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_PreDepth>(std::move(helper), std::move(pipelineLayout), preDepthType);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
		PreDepthType _preDepthType;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_PreDepth(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		PreDepthType preDepthType)
	{
		TechniqueDelegate_PreDepth::ConstructToPromise(std::move(promise), techniqueSet, preDepthType);
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
				if (utilityType == UtilityDelegateType::FlatColor) {
					entriesToCheck.emplace_back("FlatColor_NoPatches", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("FlatColor_NoPatches", &_psPerPixelSrc);
					entriesToCheck.emplace_back("FlatColor_PerPixelAndEarlyRejection", &_psPerPixelAndEarlyRejection);
				} else if (utilityType == UtilityDelegateType::CopyDiffuseAlbedo) {
					entriesToCheck.emplace_back("CopyDiffuseAlbedo_NoPatches", &_psNoPatchesSrc);
					entriesToCheck.emplace_back("CopyDiffuseAlbedo_PerPixel", &_psPerPixelSrc);
					entriesToCheck.emplace_back("CopyDiffuseAlbedo_PerPixelAndEarlyRejection", &_psPerPixelAndEarlyRejection);
				} else {
					assert(0);
				}
				_pipelineLayout = SetupTechniqueFileHelper(*_techniqueSet, entriesToCheck);
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

		std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() override { return _pipelineLayout; }
		::Assets::DependencyValidation GetDependencyValidation() override { return _depVal; }

		TechniqueDelegate_Utility(
			TechniqueFileHelper&& helper,
			std::shared_ptr<Assets::PredefinedPipelineLayout> pipelineLayout,
			UtilityDelegateType utilityType)
		: _techniqueFileHelper{std::move(helper)}, _pipelineLayout(std::move(pipelineLayout))
		, _utilityType(utilityType)
		{
			_rs[0x0] = CommonResourceBox::s_rsDefault;
            _rs[0x1] = CommonResourceBox::s_rsCullDisable;

			::Assets::DependencyValidationMarker depVals[] { _techniqueFileHelper.GetDependencyValidation(), _pipelineLayout->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
			const TechniqueSetFileFuture& techniqueSet,
			UtilityDelegateType utilityType)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[utilityType](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile, utilityType};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
						::Assets::WhenAll(pipelineLayout).ThenConstructToPromise(
							std::move(promise),
							[helper=std::move(helper), utilityType](auto pipelineLayout) mutable {
								return std::make_shared<TechniqueDelegate_Utility>(std::move(helper), std::move(pipelineLayout), utilityType);
							});
					} CATCH (...) {
						promise.set_exception(std::current_exception());
					} CATCH_END
				});
		}

	private:
		TechniqueFileHelper _techniqueFileHelper;
		RasterizationDesc _rs[2];
		UtilityDelegateType _utilityType;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _pipelineLayout;
		::Assets::DependencyValidation _depVal;
	};

	void CreateTechniqueDelegate_Utility(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		const TechniqueSetFileFuture& techniqueSet,
		UtilityDelegateType type)
	{
		TechniqueDelegate_Utility::ConstructToPromise(std::move(promise), techniqueSet, type);
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
			const TechniqueSetFileFuture& techniqueSet)
		{
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
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
		const TechniqueSetFileFuture& techniqueSet)
	{
		TechniqueDelegate_ProbePrepare::ConstructToPromise(std::move(promise), techniqueSet);
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
			const TechniqueSetFileFuture& techniqueSet,
			unsigned testTypeParameter,
			const StreamOutputInitializers& soInit)
		{
			auto soElements = NormalizeInputAssembly(soInit._outputElements);
			auto soStrides = std::vector<unsigned>(soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end());
			::Assets::WhenAll(techniqueSet).CheckImmediately().ThenConstructToPromise(
				std::move(promise),
				[soElements=std::move(soElements), soStrides=std::move(soStrides), testTypeParameter](auto&& promise, auto techniqueSetFile) {
					TRY {
						TechniqueFileHelper helper{techniqueSetFile};
						auto pipelineLayout = ::Assets::MakeAssetPtr<Assets::PredefinedPipelineLayout>(helper._pipelineLayout);
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
		const TechniqueSetFileFuture& techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit)
	{
		TechniqueDelegate_RayTest::ConstructToPromise(std::move(promise), techniqueSet, testTypeParameter, soInit);
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

	::Assets::DependencyValidation ITechniqueDelegate::GetDependencyValidation() { return {}; }

	static std::atomic<uint64_t> s_nextTechniqueDelegateGuid = 1;
	ITechniqueDelegate::ITechniqueDelegate() : _guid(s_nextTechniqueDelegateGuid++) {}
	ITechniqueDelegate::~ITechniqueDelegate() {}

}}


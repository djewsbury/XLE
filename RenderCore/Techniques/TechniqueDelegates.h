// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CompiledShaderPatchCollection.h"
#include "TechniqueUtils.h"						// (for RSDepthBias)
#include "../../ShaderParser/ShaderAnalysis.h"	// (for ManualSelectorFiltering)
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class StreamOutputInitializers; class IDevice; }
namespace RenderCore { namespace Assets { class PredefinedPipelineLayout; class RenderStateSet; }}
namespace ShaderSourceParser { class SelectorFilteringRules; }

namespace RenderCore { namespace Techniques
{
	struct GraphicsPipelineDesc
	{
		std::string				_shaders[3];		// indexed by RenderCore::ShaderStage
		ShaderSourceParser::ManualSelectorFiltering _manualSelectorFiltering;
		std::string				_techniquePreconfigurationFile;
		std::string				_materialPreconfigurationFile;

		std::vector<std::pair<uint64_t, ShaderStage>>	_patchExpansions;

		std::vector<AttachmentBlendDesc> 	_blend;
		DepthStencilDesc					_depthStencil;
		RasterizationDesc					_rasterization;

		std::vector<RenderCore::InputElementDesc> _soElements;
		std::vector<unsigned> _soBufferStrides;

		::Assets::DependencyValidation _depVal;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		uint64_t GetHash() const;
		uint64_t CalculateHashNoSelectors(uint64_t seed) const;
	};

	class ITechniqueDelegate
	{
	public:
		using GraphicsPipelineDesc = Techniques::GraphicsPipelineDesc;
		virtual ::Assets::PtrToMarkerPtr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) = 0;
		virtual std::string GetPipelineLayout() = 0;
		virtual ~ITechniqueDelegate();
	};

	class TechniqueSetFile;
	class CompiledShaderPatchCollection;

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Deferred(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet);

	namespace TechniqueDelegateForwardFlags { 
		enum { DisableDepthWrite = 1<<0 };
		using BitField = unsigned;
	}
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Forward(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags = 0);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_DepthOnly(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		const RSDepthBias& singleSidedBias = RSDepthBias{},
        const RSDepthBias& doubleSidedBias = RSDepthBias{},
        CullMode cullMode = CullMode::Back,
		FaceWinding faceWinding = FaceWinding::CCW);

	enum class ShadowGenType { GSAmplify, VertexIdViewInstancing };
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ShadowGen(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		ShadowGenType shadowGenType = ShadowGenType::GSAmplify,
		const RSDepthBias& singleSidedBias = RSDepthBias{},
        const RSDepthBias& doubleSidedBias = RSDepthBias{},
        CullMode cullMode = CullMode::Back,
		FaceWinding faceWinding = FaceWinding::CCW);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_RayTest(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit);

	enum class PreDepthType { DepthOnly, DepthMotion, DepthMotionNormal, DepthMotionNormalRoughness, DepthMotionNormalRoughnessAccumulation };
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_PreDepth(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		PreDepthType preDepthType);

	enum class UtilityDelegateType { FlatColor, CopyDiffuseAlbedo };
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Utility(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet,
		UtilityDelegateType utilityType);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ProbePrepare(
		const ::Assets::PtrToMarkerPtr<TechniqueSetFile>& techniqueSet);

	/** <summary>Backwards compatibility for legacy style techniques</summary>
	This delegate allows for loading techniques from a legacy fixed function technique file.
	A default technique file is selected and the type of shader is picked via the technique
	index value. In this case, the material does now impact the technique selected.
	*/
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegateLegacy(
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil);

	RasterizationDesc BuildDefaultRastizerDesc(const Assets::RenderStateSet& states);

	enum class IllumType { NoPerPixel, PerPixel, PerPixelAndEarlyRejection, PerPixelCustomLighting };
	IllumType CalculateIllumType(const CompiledShaderPatchCollection& patchCollection);

}}


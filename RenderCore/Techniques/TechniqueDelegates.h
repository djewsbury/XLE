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
#include <variant>

namespace RenderCore { class StreamOutputInitializers; class IDevice; struct ShaderCompileResourceName; }
namespace RenderCore { namespace Assets { class PredefinedPipelineLayout; class RenderStateSet; }}
namespace ShaderSourceParser { class SelectorFilteringRules; }
namespace std { template<typename T> class future; template<typename T> class shared_future; }

namespace RenderCore { namespace Techniques
{
	namespace Internal {
		// we have to jump through some hoops to make sure Hash64() is visible viable argument dependant lookup
		// (without hiding all other overrides of Hash64 to this namespace)
		// Alternatively, we could use a "using" and have the override in the std namespace
		struct ShaderVariant : public std::variant<std::monostate, ShaderCompileResourceName, ShaderCompilePatchResource>
		{
			using variant::variant;
		};
		uint64_t Hash64(const ShaderVariant& var, uint64_t seed = DefaultSeed64);
	}

	struct GraphicsPipelineDesc
	{
		Internal::ShaderVariant			_shaders[3] { std::monostate{}, std::monostate{}, std::monostate{} };		// indexed by RenderCore::ShaderStage

		ShaderSourceParser::ManualSelectorFiltering _manualSelectorFiltering;
		std::string				_techniquePreconfigurationFile;
		std::string				_materialPreconfigurationFile;

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
		virtual std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			std::shared_ptr<CompiledShaderPatchCollection> shaderPatches,
			IteratorRange<const uint64_t*> iaAttributes,
			const RenderCore::Assets::RenderStateSet& renderStates) = 0;
		virtual std::shared_ptr<Assets::PredefinedPipelineLayout> GetPipelineLayout() = 0;
		virtual ::Assets::DependencyValidation GetDependencyValidation();

		uint64_t GetGUID() const { return _guid; }
		ITechniqueDelegate();
		virtual ~ITechniqueDelegate();
	private:
		ITechniqueDelegate&operator=(ITechniqueDelegate&&) = delete;
		ITechniqueDelegate(ITechniqueDelegate&&) = delete;
		uint64_t _guid;
	};

	class TechniqueSetFile;
	class CompiledShaderPatchCollection;
	using TechniqueSetFileFuture = std::shared_future<std::shared_ptr<TechniqueSetFile>>;

	void CreateTechniqueDelegate_Deferred(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		unsigned gbufferTypeCode);

	namespace TechniqueDelegateForwardFlags { 
		enum { DisableDepthWrite = 1<<0 };
		using BitField = unsigned;
	}
	void CreateTechniqueDelegate_Forward(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		TechniqueDelegateForwardFlags::BitField flags = 0);

	void CreateTechniqueDelegate_DepthOnly(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		const RSDepthBias& singleSidedBias = RSDepthBias{},
        const RSDepthBias& doubleSidedBias = RSDepthBias{},
        CullMode cullMode = CullMode::Back,
		FaceWinding faceWinding = FaceWinding::CCW);

	enum class ShadowGenType { GSAmplify, VertexIdViewInstancing };
	void CreateTechniqueDelegate_ShadowGen(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		ShadowGenType shadowGenType = ShadowGenType::GSAmplify,
		const RSDepthBias& singleSidedBias = RSDepthBias{},
        const RSDepthBias& doubleSidedBias = RSDepthBias{},
        CullMode cullMode = CullMode::Back,
		FaceWinding faceWinding = FaceWinding::CCW);

	void CreateTechniqueDelegate_RayTest(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit);

	enum class PreDepthType { DepthOnly, DepthMotion, DepthMotionNormal, DepthMotionNormalRoughness, DepthMotionNormalRoughnessAccumulation };
	void CreateTechniqueDelegate_PreDepth(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		PreDepthType preDepthType);

	enum class UtilityDelegateType { FlatColor, CopyDiffuseAlbedo, CopyWorldSpacePosition, CopyWorldSpaceNormal, CopyRoughness, CopyMetal, CopySpecular, CopyCookedAO, SolidWireframe };
	void CreateTechniqueDelegate_Utility(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet,
		UtilityDelegateType utilityType,
		bool allowBlending = true);

	std::optional<UtilityDelegateType> AsUtilityDelegateType(StringSection<>);

	void CreateTechniqueDelegate_ProbePrepare(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		TechniqueSetFileFuture techniqueSet);

	/** <summary>Backwards compatibility for legacy style techniques</summary>
	This delegate allows for loading techniques from a legacy fixed function technique file.
	A default technique file is selected and the type of shader is picked via the technique
	index value. In this case, the material does now impact the technique selected.
	*/
	void CreateTechniqueDelegateLegacy(
		std::promise<std::shared_ptr<ITechniqueDelegate>>&& promise,
		unsigned techniqueIndex,
		const AttachmentBlendDesc& blend,
		const RasterizationDesc& rasterization,
		const DepthStencilDesc& depthStencil);

	RasterizationDesc BuildDefaultRasterizationDesc(const Assets::RenderStateSet& states);

	enum class IllumType { NoPerPixel, PerPixel, PerPixelAndEarlyRejection, PerPixelCustomLighting };
	IllumType CalculateIllumType(const CompiledShaderPatchCollection& patchCollection);

	TechniqueSetFileFuture GetDefaultTechniqueSetFileFuture();

}}


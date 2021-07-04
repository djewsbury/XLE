// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CompiledShaderPatchCollection.h"
#include "RenderStateResolver.h"				// (for RSDepthBias)
#include "../../ShaderParser/ShaderAnalysis.h"	// (for ManualSelectorFiltering)
#include "../../Assets/DepVal.h"
#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore { class StreamOutputInitializers; class IDevice; }
namespace ShaderSourceParser { class SelectorFilteringRules; }

namespace RenderCore { namespace Techniques
{
	class ITechniqueDelegate
	{
	public:
		struct GraphicsPipelineDesc
		{
			std::string				_shaders[3];		// indexed by RenderCore::ShaderStage
			ShaderSourceParser::ManualSelectorFiltering _manualSelectorFiltering;
			std::string				_selectorPreconfigurationFile;

			std::vector<uint64_t>	_patchExpansions;

			std::vector<AttachmentBlendDesc> 	_blend;
			DepthStencilDesc					_depthStencil;
			RasterizationDesc					_rasterization;

			std::vector<RenderCore::InputElementDesc> _soElements;
			std::vector<unsigned> _soBufferStrides;

			::Assets::DependencyValidation _depVal;
			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		};

		virtual ::Assets::PtrToFuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) = 0;

		virtual ~ITechniqueDelegate();
	};

	class TechniqueSharedResources;
	class TechniqueSetFile;
	class CompiledShaderPatchCollection;
	std::shared_ptr<TechniqueSharedResources> CreateTechniqueSharedResources(IDevice&);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Deferred(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources);

	namespace TechniqueDelegateForwardFlags { 
		enum { DisableDepthWrite = 1<<0 };
		using BitField = unsigned;
	}
	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_Forward(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		TechniqueDelegateForwardFlags::BitField flags = 0);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_DepthOnly(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		const RSDepthBias& singleSidedBias = RSDepthBias(),
        const RSDepthBias& doubleSidedBias = RSDepthBias(),
        CullMode cullMode = CullMode::Back);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_ShadowGen(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		const RSDepthBias& singleSidedBias = RSDepthBias(),
        const RSDepthBias& doubleSidedBias = RSDepthBias(),
        CullMode cullMode = CullMode::Back);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_RayTest(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources,
		unsigned testTypeParameter,
		const StreamOutputInitializers& soInit);

	std::shared_ptr<ITechniqueDelegate> CreateTechniqueDelegate_DepthNormalVelocity(
		const ::Assets::PtrToFuturePtr<TechniqueSetFile>& techniqueSet,
		const std::shared_ptr<TechniqueSharedResources>& sharedResources);

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

	enum class IllumType { NoPerPixel, PerPixel, PerPixelAndEarlyRejection };
	IllumType CalculateIllumType(const CompiledShaderPatchCollection& patchCollection);

	auto AssembleShader(
		const CompiledShaderPatchCollection& patchCollection,
		IteratorRange<const uint64_t*> redirectedPatchFunctions,
		StringSection<> definesTable) -> SourceCodeWithRemapping;

}}


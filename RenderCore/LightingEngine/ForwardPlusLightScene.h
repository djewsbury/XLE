// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "LightTiler.h"
#include "ShadowProbes.h"
#include "SHCoefficients.h"
#include "../Techniques/TechniqueUtils.h"
#include <optional>

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class DeferredShaderResource; class IShaderResourceDelegate; } }

namespace RenderCore { namespace LightingEngine
{
	class ScreenSpaceReflectionsOperator;
	class HierarchicalDepthsOperator;
	class DynamicShadowPreparers;
	class SHCoefficients;
	namespace Internal { class SemiStaticShadowProbeScheduler; class DynamicShadowProjectionScheduler; class DominantLightSet; }

	class ForwardPlusLightScene : public Internal::StandardLightScene, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		RasterizationLightTileOperator& GetLightTiler() { return *_lightTiler; }
		ShadowProbes& GetShadowProbes() { return *_shadowProbes; }
		bool ShadowProbesSupported() const;
		const IPreparedShadowResult* GetDominantPreparedShadow();

		void FinalizeConfiguration();
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext, bool enableSSR);
		void Prerender(IThreadContext&);

		void SetDiffuseSHCoefficients(const SHCoefficients&);

		std::shared_ptr<Techniques::IShaderResourceDelegate> CreateMainSceneResourceDelegate();

		// ILightScene
		virtual LightSourceId CreateAmbientLightSource() override;
		virtual void DestroyLightSource(LightSourceId sourceId) override;
		virtual void Clear() override;
		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override;
		virtual void* QueryInterface(uint64_t typeCode) override;

		bool IsCompatible(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
			const AmbientLightOperatorDesc& ambientLightOperator);

		struct ShadowPreparerIdMapping
		{
			std::vector<unsigned> _operatorToShadowPreparerId;
			unsigned _operatorForStaticProbes = ~0u;
			std::vector<ShadowOperatorDesc> _shadowPreparers;
			ShadowProbes::Configuration _shadowProbesCfg;
			unsigned _dominantLightOperator = ~0u;
			unsigned _dominantShadowOperator = ~0u;
		};

		struct LightOperatorInfo
		{
			Internal::StandardPositionLightFlags::BitField _standardLightFlags = 0;
			unsigned _uniformShapeCode = 0;
		};

		ForwardPlusLightScene();

		struct ConstructionServices
		{
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
			std::shared_ptr<Techniques::PipelineCollection> _pipelinePool;
			std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;
		};

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ForwardPlusLightScene>>&& promise,
			const ConstructionServices&,
			ShadowPreparerIdMapping&& shadowPreparerMapping,
			std::vector<LightOperatorInfo>&& lightOperatorInfo,
			const RasterizationLightTileOperatorDesc& tilerCfg);

		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		std::shared_ptr<Internal::DynamicShadowProjectionScheduler> _shadowScheduler;
		std::function<void*(uint64_t)> _queryInterfaceHelper;

	private:
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;

		ShadowPreparerIdMapping _shadowPreparerIdMapping;
		std::vector<LightOperatorInfo> _lightOperatorInfo;

		std::shared_ptr<ShadowProbes> _shadowProbes;
		std::shared_ptr<Internal::SemiStaticShadowProbeScheduler> _shadowProbesManager;
		std::shared_ptr<Internal::DominantLightSet> _dominantLightSet;

		class AmbientLightConfig;
		std::shared_ptr<AmbientLightConfig> _ambientLight;

		Float4 _diffuseSHCoefficients[25];

		BufferUploads::CommandListID _completionCommandListID = 0;

		class ShaderResourceDelegate;
		struct SceneLightUniforms
		{
			std::shared_ptr<IResource> _propertyCB;
			std::shared_ptr<IResourceView> _propertyCBView;
			std::shared_ptr<IResource> _lightList;
			std::shared_ptr<IResourceView> _lightListUAV;
			std::shared_ptr<IResource> _lightDepthTable;
			std::shared_ptr<IResourceView> _lightDepthTableUAV;
		};
		SceneLightUniforms _uniforms[3];
		unsigned _pingPongCounter = 0;

		static std::shared_ptr<ForwardPlusLightScene> CreateInternal(
			const ConstructionServices&,
			std::shared_ptr<DynamicShadowPreparers> shadowPreparers,
			std::shared_ptr<RasterizationLightTileOperator> lightTiler, 
			ForwardPlusLightScene::ShadowPreparerIdMapping&& shadowPreparerMapping,
			std::vector<LightOperatorInfo>&& lightOperatorInfo);
	};

}}

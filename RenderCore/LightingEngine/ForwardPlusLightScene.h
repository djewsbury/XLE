// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "LightTiler.h"
#include "ShadowProbes.h"
#include "../Techniques/TechniqueUtils.h"
#include "../../Utility/FunctionUtils.h"
#include <optional>

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class DeferredShaderResource; } }

namespace RenderCore { namespace LightingEngine
{
	class ScreenSpaceReflectionsOperator;
	class HierarchicalDepthsOperator;
	class SkyOperator;
	class DynamicShadowPreparationOperators;

	class ForwardPlusLightScene : public Internal::StandardLightScene, public IDistantIBLSource, public ISSAmbientOcclusion, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		ScreenSpaceReflectionsOperator& GetScreenSpaceReflectionsOperator() { return *_ssrOperator; }
		RasterizationLightTileOperator& GetLightTiler() { return *_lightTiler; }
		HierarchicalDepthsOperator& GetHierarchicalDepthsOperator() { return *_hierarchicalDepthsOperator; }
		ShadowProbes& GetShadowProbes() { return *_shadowProbes; }

		void FinalizeConfiguration();
		virtual void SetEquirectangularSource(StringSection<> input) override;
		void SetupProjection(Techniques::ParsingContext& parsingContext);
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);

		std::optional<LightSourceOperatorDesc> GetDominantLightOperator() const;
		std::optional<ShadowOperatorDesc> GetDominantShadowOperator() const;
		std::shared_ptr<Techniques::IShaderResourceDelegate> CreateMainSceneResourceDelegate(Techniques::DeferredShaderResource& balanceNoiseTexture);

		Signal<std::shared_ptr<Techniques::DeferredShaderResource>> _onChangeSkyTexture;		// signaled on arbitrary thread

		// ILightScene
		virtual LightSourceId CreateLightSource(ILightScene::LightOperatorId opId) override;
		virtual void DestroyLightSource(LightSourceId sourceId) override;
		virtual void Clear() override;
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight) override;
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, IteratorRange<const LightSourceId*> associatedLights) override;
		virtual void DestroyShadowProjection(ShadowProjectionId) override;
		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override;
		virtual void* TryGetShadowProjectionInterface(ShadowProjectionId projectionid, uint64_t interfaceTypeCode) override;

		ForwardPlusLightScene(const AmbientLightOperatorDesc& ambientLightOperator);

		static void ConstructToFuture(
			::Assets::FuturePtr<ForwardPlusLightScene>& future,
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
			const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
			IteratorRange<const LightSourceOperatorDesc*> positionalLightOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
			const AmbientLightOperatorDesc& ambientLightOperator,
			const RasterizationLightTileOperator::Configuration& tilerCfg);

		std::shared_ptr<DynamicShadowPreparationOperators> _shadowPreparationOperators;
		std::vector<std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>> _preparedShadows;
		std::shared_ptr<IPreparedShadowResult> _preparedDominantShadow;

	private:
		std::vector<LightSourceOperatorDesc> _positionalLightOperators;
		std::shared_ptr<ScreenSpaceReflectionsOperator> _ssrOperator;
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
		std::shared_ptr<HierarchicalDepthsOperator> _hierarchicalDepthsOperator;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;

		struct ShadowOperatorIdMapping
		{
			std::vector<unsigned> _operatorToDynamicShadowOperator;
			unsigned _operatorForStaticProbes = ~0u;
			ShadowProbes::Configuration _shadowProbesCfg;
		};
		ShadowOperatorIdMapping _shadowOperatorIdMapping;

		std::shared_ptr<ShadowProbes> _shadowProbes;
		class ShadowProbePrepareDelegate;
		std::shared_ptr<ShadowProbePrepareDelegate> _spPrepareDelegate;

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
		SceneLightUniforms _uniforms[2];
		unsigned _pingPongCounter = 0;
		bool _hasPrevProjection = false;
		Techniques::ProjectionDesc _prevProjDesc;
	};

}}

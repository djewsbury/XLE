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

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class DeferredShaderResource; class IShaderResourceDelegate; } }

namespace RenderCore { namespace LightingEngine
{
	class ScreenSpaceReflectionsOperator;
	class HierarchicalDepthsOperator;
	class SkyOperator;
	class DynamicShadowPreparationOperators;
	class SHCoefficientsAsset;

	class ForwardPlusLightScene : public Internal::StandardLightScene, public IDistantIBLSource, public ISSAmbientOcclusion, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		RasterizationLightTileOperator& GetLightTiler() { return *_lightTiler; }
		ShadowProbes& GetShadowProbes() { return *_shadowProbes; }

		void FinalizeConfiguration();
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);

		std::optional<LightSourceOperatorDesc> GetDominantLightOperator() const;
		std::optional<ShadowOperatorDesc> GetDominantShadowOperator() const;
		std::shared_ptr<Techniques::IShaderResourceDelegate> CreateMainSceneResourceDelegate();
		const AmbientLightOperatorDesc& GetAmbientLightOperatorDesc() const;

		// The following are for propagating configuration settings to operators managed by the delegate
		// signaled on arbitrary thread
		Signal<std::shared_ptr<Techniques::DeferredShaderResource>> _onChangeSkyTexture;
		Signal<std::shared_ptr<Techniques::DeferredShaderResource>> _onChangeSpecularIBL;
		Signal<std::optional<SHCoefficientsAsset>> _onChangeDiffuseIBL;

		// ILightScene
		virtual LightSourceId CreateLightSource(ILightScene::LightOperatorId opId) override;
		virtual void DestroyLightSource(LightSourceId sourceId) override;
		virtual void Clear() override;
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight) override;
		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, IteratorRange<const LightSourceId*> associatedLights) override;
		virtual void DestroyShadowProjection(ShadowProjectionId) override;
		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override;
		virtual void* TryGetShadowProjectionInterface(ShadowProjectionId projectionid, uint64_t interfaceTypeCode) override;

		// IDistantIBLSource
		virtual void SetEquirectangularSource(StringSection<> input) override;

		bool IsCompatible(
			IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
			const AmbientLightOperatorDesc& ambientLightOperator);

		ForwardPlusLightScene(const AmbientLightOperatorDesc& ambientLightOperator);

		static void ConstructToPromise(
			std::promise<std::shared_ptr<ForwardPlusLightScene>>&& promise,
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
			const std::shared_ptr<Techniques::PipelineCollection>& pipelinePool,
			const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
			IteratorRange<const LightSourceOperatorDesc*> positionalLightOperators,
			IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
			const AmbientLightOperatorDesc& ambientLightOperator,
			const RasterizationLightTileOperator::Configuration& tilerCfg);

		std::shared_ptr<DynamicShadowPreparationOperators> _shadowPreparationOperators;

	private:
		std::vector<LightSourceOperatorDesc> _positionalLightOperators;
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
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
		std::shared_ptr<IPreparable> _spPrepareDelegate;

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

		static std::shared_ptr<ForwardPlusLightScene> CreateInternal(
			std::shared_ptr<DynamicShadowPreparationOperators> shadowPreparationOperators,
			std::shared_ptr<RasterizationLightTileOperator> lightTiler, 
			const std::vector<LightSourceOperatorDesc>& positionalLightOperators,
			const AmbientLightOperatorDesc& ambientLightOperator, 
			const ForwardPlusLightScene::ShadowOperatorIdMapping& shadowOperatorMapping, 
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators, 
			const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox);
	};

}}

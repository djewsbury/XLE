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
	class DynamicShadowPreparers;
	class SHCoefficientsAsset;
	namespace Internal { class SemiStaticShadowProbeScheduler; class DynamicShadowProjectionScheduler; class DominantLightSet; }

	class ForwardPlusLightScene : public Internal::StandardLightScene, public IDistantIBLSource, public ISSAmbientOcclusion, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		RasterizationLightTileOperator& GetLightTiler() { return *_lightTiler; }
		ShadowProbes& GetShadowProbes() { return *_shadowProbes; }
		bool ShadowProbesSupported() const;
		const IPreparedShadowResult* GetDominantPreparedShadow();

		void FinalizeConfiguration();
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);
		void CompleteInitialization(IThreadContext&);

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
		virtual LightSourceId CreateAmbientLightSource() override;
		virtual void DestroyLightSource(LightSourceId sourceId) override;
		virtual void Clear() override;
		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override;
		virtual void* QueryInterface(uint64_t typeCode) override;

		// IDistantIBLSource
		virtual void SetEquirectangularSource(std::shared_ptr<::Assets::OperationContext>, StringSection<> input) override;

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

		std::shared_ptr<DynamicShadowPreparers> _shadowPreparers;
		std::shared_ptr<Internal::DynamicShadowProjectionScheduler> _shadowScheduler;

	private:
		std::vector<LightSourceOperatorDesc> _positionalLightOperators;
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;

		std::vector<ShadowOperatorDesc> _shadowOperators;
		struct ShadowPreparerIdMapping
		{
			std::vector<unsigned> _operatorToShadowPreparerId;
			unsigned _operatorForStaticProbes = ~0u;
			ShadowProbes::Configuration _shadowProbesCfg;
		};
		ShadowPreparerIdMapping _shadowPreparerIdMapping;

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
		SceneLightUniforms _uniforms[2];
		unsigned _pingPongCounter = 0;

		static std::shared_ptr<ForwardPlusLightScene> CreateInternal(
			std::shared_ptr<DynamicShadowPreparers> shadowPreparers,
			std::shared_ptr<RasterizationLightTileOperator> lightTiler, 
			const std::vector<LightSourceOperatorDesc>& positionalLightOperators,
			const std::vector<ShadowOperatorDesc>& shadowOperators,
			const AmbientLightOperatorDesc& ambientLightOperator, 
			const ForwardPlusLightScene::ShadowPreparerIdMapping& shadowPreparerMapping, 
			const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators, 
			const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox);
	};

}}

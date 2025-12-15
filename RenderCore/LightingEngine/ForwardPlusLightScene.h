// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "LightTiler.h"
#include "ShadowProbes.h"
#include "SHCoefficients.h"

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class DeferredShaderResource; class IShaderResourceDelegate; } }

namespace RenderCore { namespace LightingEngine
{
	class ScreenSpaceReflectionsOperator;
	class HierarchicalDepthsOperator;
	class SHCoefficients;
	namespace Internal { class SemiStaticShadowProbeScheduler; class DynamicShadowProbeScheduler; class PriorityShadowProjectionScheduler; class DominantLightSet; class PriorityShadowSchedulerUtil; }

	class ForwardPlusLightScene : public Internal::StandardLightScene, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		RasterizationLightTileOperator& GetLightTiler() { return *_lightTiler; }
		ShadowProbes& GetShadowProbes() { return *_shadowProbes; }
		const IPreparedShadowResult* GetDominantPreparedShadow();

		void FinalizeConfiguration();
		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext, bool enableSSR);
		void Prerender(IThreadContext&);

		void SetDiffuseSHCoefficients(const SHCoefficients&);
		void SetDistantSpecularIBL(std::shared_ptr<IResourceView>, BufferUploads::CommandListID);

		std::shared_ptr<Techniques::IShaderResourceDelegate> CreateMainSceneResourceDelegate();

		// ILightScene
		virtual LightSourceId CreateLightSource(LightOperatorId operatorId) override;
		virtual void DestroyLightSource(LightSourceId sourceId) override;
		virtual void Clear() override;
		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override;
		virtual void* QueryInterface(uint64_t typeCode) override;

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }

		struct LightOperatorInfo
		{
			Internal::StandardPositionLightFlags::BitField _standardLightFlags = 0;
			unsigned _uniformShapeCode = 0;
		};

		struct LightOperatorsMapping
		{
			std::vector<unsigned> _operatorToPositionalLightOperator;
			std::vector<LightOperatorInfo> _positionalLightOperators;

			std::vector<unsigned> _operatorToPriorityShadowPreparerId;
			std::vector<ShadowOperatorDesc> _priorityShadowPreparers;

			std::vector<bool> _staticShadowProbeMask;
			std::vector<bool> _dynamicShadowProbeMask;
			std::optional<ShadowProbes::Configuration> _staticShadowProbesCfg;
			std::optional<ShadowProbes::Configuration> _dynamicShadowProbesCfg;

			unsigned _dominantLightOperator = ~0u;
			unsigned _ambientLightOperator = ~0u;
		};

		struct IntegrationParams
		{
			bool _specularIBLEnabled = false;
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
			LightOperatorsMapping&& shadowPreparerMapping,
			const RasterizationLightTileOperatorDesc& tilerCfg,
			const IntegrationParams& integrationParams);

		std::shared_ptr<Internal::PriorityShadowSchedulerUtil> _shadowPreparers;
		std::shared_ptr<ShadowProbes> _shadowProbes;
		std::shared_ptr<DynamicShadowProbes> _dynamicShadowProbes;
		std::shared_ptr<Internal::SemiStaticShadowProbeScheduler> _shadowProbesManager;
		std::shared_ptr<Internal::DynamicShadowProbeScheduler> _dynamicShadowProbesManager;
		std::shared_ptr<Internal::PriorityShadowProjectionScheduler> _shadowScheduler;
		std::shared_ptr<Internal::DominantLightSet> _dominantLightSet;

		std::function<void*(uint64_t)> _queryInterfaceHelper;

	private:
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<SharedTechniqueDelegateBox> _techDelBox;

		LightOperatorsMapping _lightOperatorsMapping;

		class AmbientLightConfig;
		std::shared_ptr<AmbientLightConfig> _ambientLight;

		Float4 _diffuseSHCoefficients[25];
		std::shared_ptr<IResourceView> _distantSpecularIBL;
		std::shared_ptr<IResourceView> _glossLut;
		BufferUploads::CommandListID _distantSpecularIBLAndGlossLutCompletion = 0;

		BufferUploads::CommandListID _completionCommandListID = 0;
		::Assets::DependencyValidation _depVal;

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
			std::shared_ptr<Internal::PriorityShadowSchedulerUtil> shadowPreparers,
			std::shared_ptr<RasterizationLightTileOperator> lightTiler, 
			ForwardPlusLightScene::LightOperatorsMapping&& shadowPreparerMapping,
			std::shared_ptr<IResourceView> glossLut,
			BufferUploads::CommandListID glossLutCompletion,
			::Assets::DependencyValidation depVal);
	};

}}

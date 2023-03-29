// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InteractiveTestHelper.h"
#include "../../Tools/ToolsRig/DrawablesWriter.h"
#include "../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../PlatformRig/OverlaySystem.h"
#include "../../PlatformRig/InputContext.h"
#include "../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../RenderCore/LightingEngine/ForwardLightingDelegate.h"
#include "../../RenderCore/LightingEngine/ToneMapOperator.h"
#include "../../RenderCore/LightingEngine/SkyOperator.h"
#include "../../RenderCore/LightingEngine/ILightScene.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ImmediateDrawables.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"
#include "../../RenderCore/Techniques/ManualDrawables.h"
#include "../../RenderCore/Techniques/PipelineOperators.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Assets/ScaffoldCmdStream.h"
#include "../../RenderCore/UniformsStream.h"
#include "../../RenderOverlays/OverlayContext.h"
#include "../../RenderOverlays/DebuggingDisplay.h"
#include "../../RenderOverlays/OverlayApparatus.h"
#include "../../Math/Transformations.h"
#include "../../Math/MathSerialization.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	using namespace RenderCore;
	using namespace Utility::Literals;

	static const unsigned s_sphereSeriesCount = 10;
	static RenderCore::UniformsStreamInterface MakeLocalTransformUSI()
	{
		RenderCore::UniformsStreamInterface result;
		result.BindImmediateData(0, "LocalTransform"_h);
		return result;
	}
	RenderCore::UniformsStreamInterface s_localTransformUSI = MakeLocalTransformUSI();

	class MultiSphereSeries
	{
	public:
		void PrepareDrawables(
			RenderCore::Techniques::DrawablesPacket& pkt,
			Float3 offset)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; Float4x4 _localToWorld; };
			auto* drawables = pkt._drawables.Allocate<CustomDrawable>(s_sphereSeriesCount);
			for (unsigned c=0; c<s_sphereSeriesCount; ++c) {
				drawables[c]._pipeline = _pipeline.get();
				drawables[c]._descriptorSet = _descriptorSets[c].get();
				drawables[c]._geo = _drawableGeo.get();
				drawables[c]._vertexCount = _vertexCount;
				drawables[c]._looseUniformsInterface = &s_localTransformUSI;
				drawables[c]._localToWorld = MakeObjectToWorld({1,0,0}, {0,0,1}, offset + Float3{2.5f*c, 0, 0});
				drawables[c]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
					{
						auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localToWorld, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
						drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
						drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
					};
			}
		}

		void SetMaterial(
			unsigned idx,
			ParameterBox& params)
		{
			assert(idx < s_sphereSeriesCount);
			auto matMachine = std::make_shared<Techniques::ManualMaterialMachine>(params, ParameterBox{});
			_descriptorSets[idx] = _pipelineAccelerators->CreateDescriptorSetAccelerator(
				nullptr, nullptr,
				matMachine->GetMaterialMachine(),
				matMachine,
				"multi-sphere-series");
		}

		BufferUploads::CommandListID GetCompletionCommandList() const { return _drawableGeo->_completionCmdList; }

		MultiSphereSeries(
			Techniques::IPipelineAcceleratorPool& pipelineAccelerators,
			BufferUploads::IManager& bufferUploads,
			Techniques::IDrawablesPool& drawablesPool)
		: _pipelineAccelerators(&pipelineAccelerators)
		{
			std::tie(_drawableGeo, _vertexCount) = ToolsRig::CreateSphereGeo(bufferUploads, drawablesPool);

			_pipeline = pipelineAccelerators.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});
		}

		std::shared_ptr<Techniques::DrawableGeo> _drawableGeo;
		size_t _vertexCount = 0;
		std::shared_ptr<Techniques::PipelineAccelerator> _pipeline;
		std::shared_ptr<Techniques::DescriptorSetAccelerator> _descriptorSets[s_sphereSeriesCount];

		Techniques::IPipelineAcceleratorPool* _pipelineAccelerators = nullptr;
	};

	class MaterialParameterizationDisplay : public IInteractiveTestOverlay
	{
	public:
		void Render(
			RenderCore::Techniques::ParsingContext& parserContext,
			IInteractiveTestHelper& testHelper) override
		{
			if (!_futureLightingTechnique.valid())
				BuildFutureLightingTechnique();
			auto lightingTechnique = _futureLightingTechnique.get();	// stall
			if (LightingEngine::GetDependencyValidation(*lightingTechnique).GetValidationIndex() != 0) {
				// rebuild again
				BuildFutureLightingTechnique();
				lightingTechnique = _futureLightingTechnique.get();	// stall
			}

			if (auto* skyProcessor = LightingEngine::QueryInterface<LightingEngine::ISkyTextureProcessor>(*lightingTechnique))
				skyProcessor->SetEquirectangularSource(nullptr, "xleres/DefaultResources/sky/desertsky.jpg");

			parserContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(_camera, {parserContext.GetViewport()._width, parserContext.GetViewport()._height});

			auto techniqueInstance = LightingEngine::BeginLightingTechniquePlayback(
				parserContext,
				*lightingTechnique);

			for (;;) {
				auto step = techniqueInstance.GetNextStep();
				if (step._type == LightingEngine::StepType::None || step._type == LightingEngine::StepType::Abort) {
					break;
				} else if (step._type == LightingEngine::StepType::ParseScene) {
					Float3 offset { 0.f, 0.f, 0.f };
					for (auto& s:_series) {
						s.PrepareDrawables(*step._pkts[0], offset);
						offset[1] += 2.5f;
						parserContext.RequireCommandList(s.GetCompletionCommandList());
					}
				} else if (step._type == LightingEngine::StepType::DrawSky) {
					// Simple black background
					Techniques::PixelOutputStates outputStates;
					outputStates.Bind(*step._parsingContext->_rpi);
					outputStates._depthStencilState._depthTest = CompareOp::Always;
					outputStates._depthStencilState._depthWrite = false;
					auto futureOp = Techniques::CreateFullViewportOperator(
						_apparatus->_lightingOperatorCollection,
						Techniques::FullViewportOperatorSubType::DisableDepth,
						"xleres/TechniqueLibrary/basic/basic.pixel.hlsl:blackOpaque",
						ParameterBox{},
						"xleres/TechniqueLibrary/LightingEngine/general-operator.pipeline:GraphicsMain",
						outputStates,
						{});
					if (auto* op = futureOp->TryActualize())
						(*op)->Draw(*step._parsingContext, {});
				}
			}
		}

		bool OnInputEvent(
			const PlatformRig::InputContext& context,
			const OSServices::InputSnapshot& evnt,
			IInteractiveTestHelper& testHelper) override
		{
			if (evnt._wheelDelta) {
				// zoom in by adjusting the edges in the camera desc
				auto* view = context.GetService<PlatformRig::WindowingSystemView>();
				if (view) {
					float xRatio = 1.f - std::clamp((evnt._mousePosition[0] - view->_viewMins[0]) / float(view->_viewMaxs[0] - view->_viewMins[0]), 0.f, 1.f);
					float yRatio = 1.f - std::clamp((evnt._mousePosition[1] - view->_viewMins[1]) / float(view->_viewMaxs[1] - view->_viewMins[1]), 0.f, 1.f);
					float movement = 0.1f / 120.0f * evnt._wheelDelta;
					_camera._left = LinearInterpolate(_camera._left, _camera._right, movement * (1-xRatio));
					_camera._right = LinearInterpolate(_camera._right, _camera._left, movement * xRatio);
					_camera._top = LinearInterpolate(_camera._top, _camera._bottom, movement * (1-yRatio));
					_camera._bottom = LinearInterpolate(_camera._bottom, _camera._top, movement * yRatio);
				}
			}
			if ((evnt._mouseDelta[0] || evnt._mouseDelta[1]) && evnt.IsHeld_LButton() && !evnt.IsPress_LButton()) {
				_camera._left -= evnt._mouseDelta[0] / 20.f;
				_camera._right -= evnt._mouseDelta[0] / 20.f;
				_camera._top += evnt._mouseDelta[1] / 20.f;
				_camera._bottom += evnt._mouseDelta[1] / 20.f;
			}
			if (evnt._pressedChar == ' ') {
				_camera._left = 2.5f * -.5f;
				_camera._right = 2.5f * 9.5f;
				_camera._top = 2.5f * .5f;
				_camera._bottom = 2.5f * -9.5f;
			}
			return false;
		}

		std::vector<Techniques::PreregisteredAttachment> _preRegs;

		void OnRenderTargetUpdate(
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachments,
			const RenderCore::FrameBufferProperties& fbProps,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats) override
		{
			_preRegs = {preregAttachments.begin(), preregAttachments.end()};
			_futureLightingTechnique = {};
		}

		void BuildFutureLightingTechnique()
		{
			const bool specularLight = false;
			LightingEngine::ChainedOperatorTemplate<LightingEngine::ForwardLightingTechniqueDesc> globalChain0 {};
			LightingEngine::ChainedOperatorTemplate<LightingEngine::ToneMapAcesOperatorDesc> globalChain1 {};
			globalChain1._desc._enablePreciseBloom = true;
			globalChain0._next = &globalChain1;

			if (!specularLight) {
				LightingEngine::ChainedOperatorTemplate<LightingEngine::SkyTextureProcessorDesc> globalChain2 {};
				globalChain2._desc._specularCubemapFaceDimension = 512;
				globalChain2._desc._specularCubemapFormat = Format::R32G32B32A32_FLOAT;
				globalChain1._next = &globalChain2;
			}

			LightingEngine::LightSourceOperatorDesc lightOperators[] {
				LightingEngine::LightSourceOperatorDesc{}
			};
			_futureLightingTechnique = LightingEngine::CreateLightingTechnique(
				_apparatus, lightOperators, {}, &globalChain0,
				_preRegs);

			if (specularLight) {
				auto technique = _futureLightingTechnique.get();		// stall
				auto& lightScene = LightingEngine::GetLightScene(*technique);
				auto lightId = lightScene.CreateLightSource(0);
				if (auto* positional = lightScene.TryGetLightSourceInterface<LightingEngine::IPositionalLightSource>(lightId))
					positional->SetLocalToWorld(AsFloat4x4(Float3{1.0f, 1.0f, -1.0f}));
				if (auto* emittance = lightScene.TryGetLightSourceInterface<LightingEngine::IUniformEmittance>(lightId))
					emittance->SetBrightness({10, 10, 10});
			}
		}

		MaterialParameterizationDisplay(
			const std::shared_ptr<LightingEngine::LightingEngineApparatus>& apparatus,
			BufferUploads::IManager& bufferUploads,
			Techniques::IDrawablesPool& drawablesPool)
		: _apparatus(apparatus)
		{
			// metal series
			{
				MultiSphereSeries metal{*apparatus->_pipelineAccelerators, bufferUploads, drawablesPool};
				for (unsigned c=0; c<s_sphereSeriesCount; ++c) {
					ParameterBox parameters;
					parameters.SetParameter("MetalMin"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("MetalMax"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("MaterialDiffuse"_h, Float3{.8f, .75f, .4f});
					metal.SetMaterial(c, parameters);
				}
				_series.emplace_back(std::move(metal));
			}

			// specular series
			{
				MultiSphereSeries specular{*apparatus->_pipelineAccelerators, bufferUploads, drawablesPool};
				for (unsigned c=0; c<s_sphereSeriesCount; ++c) {
					ParameterBox parameters;
					parameters.SetParameter("SpecularMin"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("SpecularMax"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("MaterialDiffuse"_h, Float3{.8f, .75f, .4f});
					specular.SetMaterial(c, parameters);
				}
				_series.emplace_back(std::move(specular));
			}

			// roughness series
			{
				MultiSphereSeries roughness{*apparatus->_pipelineAccelerators, bufferUploads, drawablesPool};
				for (unsigned c=0; c<s_sphereSeriesCount; ++c) {
					ParameterBox parameters;
					parameters.SetParameter("RoughnessMin"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("RoughnessMax"_h, c/float(s_sphereSeriesCount-1));
					parameters.SetParameter("MaterialDiffuse"_h, Float3{.8f, .75f, .4f});
					roughness.SetMaterial(c, parameters);
				}
				_series.emplace_back(std::move(roughness));
			}

			_camera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, 0.0f, 1.0f}), Normalize(Float3{0.0f, -1.0f, 0.0f}), Float3{0.0f, 0.0f, -200.0f});
			_camera._projection = Techniques::CameraDesc::Projection::Orthogonal;
			_camera._nearClip = 0.f;
			_camera._farClip = 400.f;
			_camera._left = 2.5f * -.5f;
			_camera._right = 2.5f * 9.5f;
			_camera._top = 2.5f * .5f;
			_camera._bottom = 2.5f * -9.5f;
		}
		~MaterialParameterizationDisplay() {}

		Techniques::CameraDesc _camera;
	protected:
		std::vector<MultiSphereSeries> _series;
		std::shared_ptr<LightingEngine::LightingEngineApparatus> _apparatus;
		std::shared_future<std::shared_ptr<LightingEngine::CompiledLightingTechnique>> _futureLightingTechnique;
	};

	TEST_CASE( "MaterialParameterization", "[lighting_engine]" )
	{
		auto testHelper = CreateInteractiveTestHelper(IInteractiveTestHelper::EnabledComponents::RenderCoreTechniques|IInteractiveTestHelper::EnabledComponents::LightingEngine);

		auto tester = std::make_shared<MaterialParameterizationDisplay>(
			testHelper->GetLightingEngineApparatus(),
			*testHelper->GetPrimaryResourcesApparatus()->_bufferUploads,
			*testHelper->GetDrawingApparatus()->_drawablesPool);
		testHelper->ResizeWindow(1280, 1280);
		testHelper->Run(tester->_camera, tester);
	}

}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueTestsHelper.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../ReusableDataFiles.h"
#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/Assets/MaterialCompiler.h"
#include "../../../RenderCore/Assets/RawMaterial.h"
#include "../../../RenderCore/Assets/PredefinedPipelineLayout.h"
#include "../../../RenderCore/Format.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Math/MathSerialization.h"
#include "../../../Math/Transformations.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../Utility/StringFormat.h"
#include "../../../xleres/FileList.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <thread>
#include <chrono>

using namespace Catch::literals;
using namespace std::chrono_literals;
using namespace Utility::Literals;

namespace UnitTests
{
	static const char* s_localPixelShader = R"--(
		#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"

		cbuffer Settings
		{
			float4 Color;
		} 

		float4 main(VSOUT geo) : SV_Target0 { return Color; }

	)--";

	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("local.pixel.hlsl", ::Assets::AsBlob(s_localPixelShader))
	};

	class SimpleTechniqueDelegate : public RenderCore::Techniques::ITechniqueDelegate
	{
	public:
		std::shared_ptr<RenderCore::Techniques::GraphicsPipelineDesc> GetPipelineDesc(
			const RenderCore::Techniques::CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) override
		{
			using namespace RenderCore;
			auto templateDesc = std::make_shared<RenderCore::Techniques::GraphicsPipelineDesc>();
			templateDesc->_shaders[(unsigned)ShaderStage::Vertex] = NO_PATCHES_VERTEX_HLSL ":main:vs_*";
			templateDesc->_shaders[(unsigned)ShaderStage::Pixel] = "ut-data/local.pixel.hlsl:main:ps_*";
			templateDesc->_techniquePreconfigurationFile = "xleres/TechniqueLibrary/Config/Preconfiguration.hlsl";
			templateDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

			templateDesc->_rasterization = Techniques::CommonResourceBox::s_rsDefault;
			templateDesc->_blend.push_back(Techniques::CommonResourceBox::s_abStraightAlpha);
			templateDesc->_depthStencil = Techniques::CommonResourceBox::s_dsReadWrite;
			return templateDesc;
		}

		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> GetPipelineLayout() override
		{
			return ::Assets::ActualizeAssetPtr<RenderCore::Assets::PredefinedPipelineLayout>(MAIN_PIPELINE ":GraphicsMain");
		}

		SimpleTechniqueDelegate() 
		{}
	};

	template<typename Type>
		void RequireReady(::Assets::Marker<Type>& future)
	{
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
	}

	TEST_CASE( "FrustumCulling", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);

		auto pipelineAcceleratorPool = testApparatus._pipelineAccelerators;

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc,
			TextureDesc::Plain2D(2048, 2048, Format::R8G8B8A8_UNORM_SRGB));
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);

		Techniques::CameraDesc sceneCamera;
		auto fwd = Normalize(Float3 { 1.0f, 0.0f, 1.0f });
		sceneCamera._cameraToWorld = MakeCameraToWorld(fwd, Float3{0.f, 1.f, 0.f}, Float3{50.f, 0.f, 50.f} - 45.0f * fwd);
		sceneCamera._projection = Techniques::CameraDesc::Projection::Perspective;
		sceneCamera._verticalFieldOfView = Deg2Rad(35.0f);
		sceneCamera._nearClip = 5.0f;
		sceneCamera._farClip = 75.f;

		auto sceneProjDesc = Techniques::BuildProjectionDesc(sceneCamera, UInt2(1920, 1080));

		RenderCore::Techniques::CameraDesc visCamera;
		visCamera._cameraToWorld = MakeCameraToWorld(Normalize(Float3{0.f, -1.0f, 0.0f}), Normalize(Float3{0.0f, 0.0f, -1.0f}), Float3{0.0f, 200.0f, 0.0f});
		visCamera._projection = Techniques::CameraDesc::Projection::Orthogonal;
		visCamera._nearClip = 0.f;
		visCamera._farClip = 400.f;
		visCamera._left = 0.f;
		visCamera._right = 100.f;
		visCamera._top = 0.f;
		visCamera._bottom = -100.f;

		testHelper->BeginFrameCapture();
		
		/////////////////////////////////////////////////////////////////

		SECTION("Test many spheres")
		{
			struct CustomDrawable : public Techniques::Drawable
			{ 
				unsigned _vertexCount; 
				std::vector<Float4> _culledSpheres, _insideSpheres, _boundarySpheres;
			};
			Techniques::DrawablesPacket pkt;
			auto* drawable = pkt._drawables.Allocate<CustomDrawable>();

			AccurateFrustumTester frustumTester(sceneProjDesc._worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
			std::mt19937_64 rng(891238634);
			for (unsigned c=0; c<1000; ++c) {
				auto radius = std::uniform_real_distribution<float>(0.2f, 3.0f)(rng);
				Float3 center {
					std::uniform_real_distribution<float>(0.f, 100.0f)(rng),
					0.f,
					std::uniform_real_distribution<float>(0.f, 100.0f)(rng)
				};
				auto status = frustumTester.TestSphere(center, radius);
				switch (status) {
				case CullTestResult::Culled: drawable->_culledSpheres.push_back(Expand(center, radius)); break;
				case CullTestResult::Boundary: drawable->_boundarySpheres.push_back(Expand(center, radius)); break;
				case CullTestResult::Within: drawable->_insideSpheres.push_back(Expand(center, radius)); break;
				default: assert(0); break;
				}
			}

			auto sphereGeo = ToolsRig::BuildGeodesicSphere();
			auto sphereVb = testHelper->CreateVB(sphereGeo);
			auto drawableGeo = testApparatus._drawablesPool->CreateGeo();
			drawableGeo->_vertexStreams[0]._resource = sphereVb;
			drawableGeo->_vertexStreamCount = 1;

			auto cfgId = pipelineAcceleratorPool->CreateSequencerConfig(
				"test",
				std::make_shared<SimpleTechniqueDelegate>(),
				ParameterBox {},
				fbHelper.GetDesc());

			auto pipelineWithTexCoord = pipelineAcceleratorPool->CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			drawable->_pipeline = pipelineWithTexCoord.get();
			drawable->_descriptorSet = nullptr;
			drawable->_geo = drawableGeo.get();
			drawable->_vertexCount = sphereGeo.size();
			RenderCore::UniformsStreamInterface usi;
			usi.BindImmediateData(0, "LocalTransform"_h);
			usi.BindImmediateData(1, "Settings"_h);
			drawable->_looseUniformsInterface = testApparatus._drawablesPool->CreateProtectedLifetime(std::move(usi)).get();
			drawable->_drawFn = [](Techniques::ParsingContext&, const Techniques::ExecuteDrawableContext& drawFnContext, const Techniques::Drawable& drawable)
				{
					auto& customDrawable = ((CustomDrawable&)drawable);
					Float4 culledColor { 1.0f, 0.4f, 0.4f, 1.0f };
					Float4 boundaryColor { 0.4f, 0.4f, 1.0f, 1.0f };
					Float4 withinColor { 0.4f, 1.0f, 0.4f, 1.0f };
					for (const auto& sphere:customDrawable._culledSpheres) {
						auto localToWorld = AsFloat4x4(UniformScaleYRotTranslation{sphere[3], 0.f, Truncate(sphere)});
						ImmediateDataStream immData { localToWorld, culledColor };
						drawFnContext.ApplyLooseUniforms(immData);
						drawFnContext.Draw(customDrawable._vertexCount);
					}
					for (const auto& sphere:customDrawable._boundarySpheres) {
						auto localToWorld = AsFloat4x4(UniformScaleYRotTranslation{sphere[3], 0.f, Truncate(sphere)});
						ImmediateDataStream immData { localToWorld, boundaryColor };
						drawFnContext.ApplyLooseUniforms(immData);
						drawFnContext.Draw(customDrawable._vertexCount);
					}
					for (const auto& sphere:customDrawable._insideSpheres) {
						auto localToWorld = AsFloat4x4(UniformScaleYRotTranslation{sphere[3], 0.f, Truncate(sphere)});
						ImmediateDataStream immData { localToWorld, withinColor };
						drawFnContext.ApplyLooseUniforms(immData);
						drawFnContext.Draw(customDrawable._vertexCount);
					}
				};

			PrepareAndStall(testApparatus, *cfgId, pkt);

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				auto parsingContext = BeginParsingContext(testApparatus, *threadContext);
				parsingContext.GetProjectionDesc() = Techniques::BuildProjectionDesc(visCamera, UInt2{ targetDesc._textureDesc._width, targetDesc._textureDesc._height });
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				Techniques::CommonResourceBox commonResBox{*testHelper->_device};
				parsingContext.GetUniformDelegateManager()->BindShaderResourceDelegate(std::make_shared<Techniques::SystemUniformsDelegate>(*testHelper->_device));
				
				Techniques::Draw(
					parsingContext, 
					*pipelineAcceleratorPool,
					*cfgId,
					pkt);
			}
			fbHelper.SaveImage(*threadContext, "frustum-cull-check");
		}

		testHelper->EndFrameCapture();

		/////////////////////////////////////////////////////////////////

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}
}

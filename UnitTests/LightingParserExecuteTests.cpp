// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UnitTestHelper.h"
#include "ReusableDataFiles.h"
#include "MetalUnitTest.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/DrawableDelegates.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../SceneEngine/IScene.h"
#include "../SceneEngine/LightingParser.h"
#include "../SceneEngine/LightingParserContext.h"
#include "../SceneEngine/BasicLightingStateDelegate.h"
#include "../Tools/ToolsRig/VisualisationGeo.h"
#include "../Assets/AssetServices.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/OSFileSystem.h"
#include "../Assets/MountingTree.h"
#include "../Assets/MemoryFile.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/Assets.h"
#include "../Math/Transformations.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/AttachablePtr.h"

#if !defined(XC_TEST_ADAPTER)
    #include <CppUnitTest.h>
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;
	#define ThrowsException ExpectException<const std::exception&>
#endif

static const char* s_colorFromSelectorShaderFile = R"--(
	#include "xleres/TechniqueLibrary/Framework/VSOUT.hlsl"
	#include "xleres/TechniqueLibrary/Framework/gbuffer.hlsl"
	#include "xleres/Nodes/Templates.pixel.sh"

	GBufferValues PerPixel(VSOUT geo)
	{
		GBufferValues result = GBufferValues_Default();
		#if VSOUT_HAS_TEXCOORD
			#if defined(COLOR_RED)
				result.diffuseAlbedo = float3(1,0,0);
			#elif defined(COLOR_GREEN)
				result.diffuseAlbedo = float3(0,1,0);
			#else
				#error Intentional compile error
			#endif
		#endif
		result.material.roughness = 1.0;		// (since this is written to SV_Target0.a, ensure it's set to 1)
		return result;
	}
)--";

static const char s_techniqueForColorFromSelector[] = R"--(
	~main
		ut-data/colorFromSelector.pixel.hlsl::PerPixel
)--";

static const char s_defaultEnvironmentSettings[] = R"--(
	~~!Format=1; Tab=4
	~environment
		Name=environment
		~ToneMapSettings; BloomDesaturationFactor=0.5f; BloomRampingFactor=0f; SceneKey=0.16f
			BloomBlurStdDev=2.2f; Flags=3i; BloomBrightness=1f; LuminanceMax=20f; WhitePoint=3f
			BloomThreshold=5f; LuminanceMin=0f; BloomScale=-1i
		~AmbientSettings; SpecularIBL=Game/xleres/DefaultResources/sky/samplesky2_specular.dds
			RangeFogThicknessScale=1f; AtmosBlurStart=1000f; AmbientLight=-12336707i
			AmbientBrightness=0f; SkyReflectionBlurriness=1f; AtmosBlurStdDev=1.3f
			SkyTexture=Game/xleres/DefaultResources/sky/samplesky2.dds; Flags=0i
			RangeFogInscatterScale=1f
			DiffuseIBL=Game/xleres/DefaultResources/sky/samplesky2_diffuse.dds; SkyReflectionScale=1f
			SkyBrightness=1f; RangeFogInscatter=0i; AtmosBlurEnd=1500f; RangeFogThickness=0i
			SkyTextureType=1i
)--";

namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("basic.tech", ::Assets::AsBlob(s_basicTechniqueFile)),
		std::make_pair("colorFromSelector.pixel.hlsl", ::Assets::AsBlob(s_colorFromSelectorShaderFile)),
		std::make_pair("envsettings.dat", ::Assets::AsBlob(s_defaultEnvironmentSettings))
	};

	static RenderCore::FrameBufferDesc MakeSimpleFrameBufferDesc()
	{
		using namespace RenderCore;
		SubpassDesc mainSubpass;
		mainSubpass.AppendOutput(0);

		std::vector<FrameBufferDesc::Attachment> attachments;
		attachments.push_back({0, AttachmentDesc{}});
		std::vector<SubpassDesc> subpasses;
		subpasses.push_back(mainSubpass);

		return FrameBufferDesc { std::move(attachments), std::move(subpasses) };
	}

	static std::shared_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> GetCompiledPatchCollectionFromText(StringSection<> techniqueText)
	{
		using namespace RenderCore;

		InputStreamFormatter<utf8> formattr { techniqueText.Cast<utf8>() };
		RenderCore::Assets::ShaderPatchCollection patchCollection(formattr, {}, {});
		return ::Assets::ActualizePtr<RenderCore::Techniques::CompiledShaderPatchCollection>(patchCollection);
	}

	class BasicScene : public SceneEngine::IScene
	{
	public:
		virtual void ExecuteScene(
            RenderCore::IThreadContext& threadContext,
			SceneEngine::SceneExecuteContext& executeContext) const
		{
			for (unsigned v=0; v<executeContext.GetViews().size(); ++v) {
				RenderCore::Techniques::DrawablesPacket* pkts[unsigned(RenderCore::Techniques::BatchFilter::Max)];
				for (unsigned c=0; c<unsigned(RenderCore::Techniques::BatchFilter::Max); ++c)
					pkts[c] = executeContext.GetDrawablesPacket(v, RenderCore::Techniques::BatchFilter(c));
			}
		}

		class Drawable : public RenderCore::Techniques::Drawable
		{
		public:
			RenderCore::Topology	_topology;
			unsigned	_vertexCount;

			static void DrawFn(
				RenderCore::Metal::DeviceContext& metalContext,
				RenderCore::Techniques::ParsingContext& parserContext,
				const Drawable& drawable, const RenderCore::Metal::BoundUniforms& boundUniforms,
				const RenderCore::Metal::ShaderProgram&)
			{
				assert(!drawable._geo->_ib);
				metalContext.Bind(drawable._topology);
				metalContext.Draw(drawable._vertexCount);
			}
		};

		void Draw(  RenderCore::IThreadContext& threadContext, 
                    SceneEngine::SceneExecuteContext& executeContext,
                    IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts) const
        {
			using namespace RenderCore;
			auto& drawable = *pkts[unsigned(Techniques::BatchFilter::General)]->_drawables.Allocate<Drawable>();
			// RenderCore::Assets::MaterialScaffoldMaterial mat;
			// drawable._material = Techniques::MakeDrawableMaterial(mat, {});
			drawable._geo = std::make_shared<Techniques::DrawableGeo>();
			drawable._geo->_vertexStreams[0]._resource = _vertexBuffer;
			drawable._geo->_vertexStreamCount = 1;
			drawable._drawFn = (Techniques::Drawable::ExecuteDrawFn*)&Drawable::DrawFn;
			drawable._topology = Topology::TriangleList;
			drawable._vertexCount = _vertexCount;
		}

		BasicScene(RenderCore::IDevice& device, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAccelerators)
		{
			using namespace RenderCore;
			auto compiledPatches = GetCompiledPatchCollectionFromText(s_techniqueForColorFromSelector);
			RenderCore::Assets::RenderStateSet stateSet;
			stateSet._doubleSided = true;
			doubledSidedStateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
			_pipelineAccelerator = pipelineAccelerators.CreatePipelineAccelerator(
				compiledPatches,
				ParameterBox { std::make_pair("COLOR_GREEN", "1") },
				ToolsRig::Vertex3D_InputLayout,
				Topology::TriangleList,
				stateSet);

			auto vertices = ToolsRig::BuildGeodesicSphere();
			auto vertexBuffer = CreateVB(device, MakeIteratorRange(vertices));
			_vertexCount = (unsigned)vertices.size();
		}
	private:
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::IResource> _vertexBuffer;
		unsigned _vertexCount;
	};

	TEST_CLASS(LightingParserExecuteTests)
	{
	public:
		TEST_METHOD(SimpleSceneExecute)
		{
			//
			// note --
			//		- move render state, technique delegates into sequencer config construction set
			//		- we also need to know more information about the output targets in the compiled
			//		scene technique step
			//		- construct & merge framebuffer fragments in CreateCompiledSceneTechnique 
			//
			using namespace RenderCore;
			std::shared_ptr<Techniques::TechniqueSetFile> techniqueSetFile = ::Assets::AutoConstructAsset<Techniques::TechniqueSetFile>("ut-data/basic.tech");
			auto techniqueDelegate = Techniques::CreateTechniqueDelegate_Utility(techniqueSetFile, Techniques::UtilityDelegateType::CopyDiffuseAlbedo);

			auto mainPool = Techniques::CreatePipelineAcceleratorPool();
			auto scene = std::make_shared<BasicScene>(*_device, *mainPool);

			auto targetDesc = CreateDesc(
				BindFlag::RenderTarget, 0, GPUAccess::Write,
				TextureDesc::Plain2D(1024, 1024, Format::R8G8B8A8_TYPELESS),
				"temporary-out");

			SceneEngine::SceneTechniqueDesc sceneTechniqueDesc;
			auto compiledSceneTechnique = SceneEngine::CreateCompiledSceneTechnique(
				sceneTechniqueDesc, mainPool,
				AsAttachmentDesc(targetDesc));

			Techniques::CameraDesc camera;
			camera._cameraToWorld = MakeCameraToWorld(
				Normalize(Float3(1.f, -0.5f, 0.f)),
				Float3(0.f, 1.f, 0.f),
				2.0f * Float3(-1.f, 0.5f, 0.f));

			SceneEngine::BasicLightingStateDelegate lightingParserDelegate(
				::Assets::AutoConstructAsset<SceneEngine::EnvironmentSettings>("ut-data/envsettings.dat"));

			{
				auto threadContext = _device->GetImmediateContext();
				auto target = _device->CreateResource(targetDesc);

				{
					Techniques::AttachmentPool namedResources;
					Techniques::FrameBufferPool frameBufferPool;
					Techniques::TechniqueContext techniqueContext;
					Techniques::ParsingContext parsingContext(techniqueContext, &namedResources, &frameBufferPool);
					auto parseContext = 
						SceneEngine::LightingParser_ExecuteScene(
							*threadContext, target,
							parsingContext,
							*compiledSceneTechnique,
							lightingParserDelegate,
							*scene, camera);
					(void)parseContext;
				}

				{
					auto data = target->ReadBack(*threadContext);

					assert(data.size() == (size_t)RenderCore::ByteCount(target->GetDesc()));
					auto pixels = MakeIteratorRange((unsigned*)AsPointer(data.begin()), (unsigned*)AsPointer(data.end()));
					std::map<unsigned, unsigned> breakdown;
					for (auto p:pixels) ++breakdown[p];

					(void)breakdown;
				}
			}
		}

		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
		ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;

		std::shared_ptr<RenderCore::IDevice> _device;
		ConsoleRig::AttachablePtr<RenderCore::Assets::Services> _renderCoreAssetServices;
		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniquesServices;

		LightingParserExecuteTests()
		{
			UnitTest_SetWorkingDirectory();
			_globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
			::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS("Game/xleres"));
			::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
			_assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>(0);

			#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
				auto api = RenderCore::UnderlyingAPI::AppleMetal;
			#elif GFXAPI_TARGET == GFXAPI_DX11
				auto api = RenderCore::UnderlyingAPI::DX11;
			#else
				auto api = RenderCore::UnderlyingAPI::OpenGLES;
			#endif

			_device = RenderCore::CreateDevice(api);

			_renderCoreAssetServices = ConsoleRig::MakeAttachablePtr<RenderCore::Assets::Services>(_device);
			_techniquesServices = ConsoleRig::MakeAttachablePtr<RenderCore::Techniques::Services>(_device);
		}

		~LightingParserExecuteTests()
		{
			_techniquesServices.reset();
			_renderCoreAssetServices.reset();
			_device.reset();
			_assetServices.reset();
			_globalServices.reset();
		}
	};
}



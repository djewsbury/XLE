// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightingEngineTestHelper.h"
#include "../../UnitTestHelper.h"
#include "../../EmbeddedRes.h"
#include "../../../RenderCore/LightingEngine/LightingEngineApparatus.h"
#include "../../../RenderCore/LightingEngine/LightingEngine.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Techniques/SystemUniformsDelegate.h"
#include "../../../RenderCore/Techniques/RenderPass.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/CompilerLibrary.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../Math/Transformations.h"
#include <regex>
#include <chrono>

namespace UnitTests
{
	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeMaterialDescriptorSetLayout();
	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeSequencerDescriptorSetLayout();

	LightingEngineTestApparatus::LightingEngineTestApparatus()
	{
		using namespace RenderCore;
		_globalServices = std::make_shared<ConsoleRig::GlobalServices>(GetStartupConfig());
		_xleresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		_metalTestHelper = MakeTestHelper();

		// Verbose.SetConfiguration(OSServices::MessageTargetConfiguration{});

		auto techniqueServices = ConsoleRig::MakeAttachablePtr<Techniques::Services>(_metalTestHelper->_device);
		techniqueServices->RegisterTextureLoader(std::regex(R"(.*\.[dD][dD][sS])"), Techniques::CreateDDSTextureLoader());
		techniqueServices->RegisterTextureLoader(std::regex(R"(.*)"), Techniques::CreateWICTextureLoader());
		_bufferUploads = BufferUploads::CreateManager(*_metalTestHelper->_device);
		techniqueServices->SetBufferUploads(_bufferUploads);

		_futureExecutor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
		_futureExecSetter = std::make_unique<thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter>(_futureExecutor);

		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();
		auto filteringRegistration = ShaderSourceParser::RegisterShaderSelectorFilteringCompiler(compilers);
		auto shaderCompilerRegistration = RenderCore::RegisterShaderCompiler(_metalTestHelper->_shaderSource, compilers);
		auto shaderCompiler2Registration = RenderCore::Techniques::RegisterInstantiateShaderGraphCompiler(_metalTestHelper->_shaderSource, compilers);

		_pipelineAcceleratorPool = Techniques::CreatePipelineAcceleratorPool(
			_metalTestHelper->_device, _metalTestHelper->_pipelineLayout, Techniques::PipelineAcceleratorPoolFlags::RecordDescriptorSetBindingInfo,
			MakeMaterialDescriptorSetLayout(),
			MakeSequencerDescriptorSetLayout());

		_techniquesSharedResources = RenderCore::Techniques::CreateTechniqueSharedResources(*_metalTestHelper->_device);
		_techDelBox = std::make_shared<LightingEngine::SharedTechniqueDelegateBox>(_techniquesSharedResources);

		_techniqueContext = std::make_shared<Techniques::TechniqueContext>();
		_techniqueContext->_drawablesSharedResources = Techniques::CreateDrawablesSharedResources();
		auto commonResources = std::make_shared<Techniques::CommonResourceBox>(*_metalTestHelper->_device);
		_techniqueContext->_systemUniformsDelegate = std::make_shared<Techniques::SystemUniformsDelegate>(*_metalTestHelper->_device, *commonResources);
		_techniqueContext->_attachmentPool = std::make_shared<Techniques::AttachmentPool>(_metalTestHelper->_device);
		_techniqueContext->_frameBufferPool = Techniques::CreateFrameBufferPool();
	}

	LightingEngineTestApparatus::~LightingEngineTestApparatus()
	{
		::Assets::Services::GetAssetSets().Clear();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateSphereGeo(MetalTestHelper& testHelper)
	{
		auto sphereGeo = ToolsRig::BuildGeodesicSphere();
		auto sphereVb = testHelper.CreateVB(sphereGeo);
		auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
		geo->_vertexStreams[0]._resource = sphereVb;
		geo->_vertexStreamCount = 1;
		return {geo, sphereGeo.size()};
	}

	static std::pair<std::shared_ptr<RenderCore::Techniques::DrawableGeo>, size_t> CreateCubeGeo(MetalTestHelper& testHelper)
	{
		auto cubeGeo = ToolsRig::BuildCube();
		auto cubeVb = testHelper.CreateVB(cubeGeo);
		auto geo = std::make_shared<RenderCore::Techniques::DrawableGeo>();
		geo->_vertexStreams[0]._resource = cubeVb;
		geo->_vertexStreamCount = 1;
		return {geo, cubeGeo.size()};
	}

	class SphereDrawableWriter : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; };
			auto* drawable = pkt._drawables.Allocate<CustomDrawable>();
			drawable->_pipeline = _pipelineAccelerator;
			drawable->_descriptorSet = _descriptorSetAccelerator;
			drawable->_geo = _geo;
			drawable->_vertexCount = _vertexCount;
			drawable->_drawFn = [](RenderCore::Techniques::ParsingContext&, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};
		}

		SphereDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateSphereGeo(testHelper);
			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr,
				{}, {}, {});
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateSphereDrawablesWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<SphereDrawableWriter>(testHelper, pipelineAcceleratorPool);
	}

	class ShapeStackDrawableWriter : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _sphereGeo, _cubeGeo;
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
		std::shared_ptr<RenderCore::UniformsStreamInterface> _usi;
		size_t _sphereVertexCount, _cubeVertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; };
			auto* drawables = pkt._drawables.Allocate<CustomDrawable>(3);
			drawables[0]._pipeline = _pipelineAccelerator;
			drawables[0]._descriptorSet = _descriptorSetAccelerator;
			drawables[0]._geo = _sphereGeo;
			drawables[0]._vertexCount = _sphereVertexCount;
			drawables[0]._looseUniformsInterface = _usi;
			drawables[0]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(AsFloat4x4(Float3{0.f, 1.0f + std::sqrt(8.0f)/2.0f, 0.f}), ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};

			drawables[1]._pipeline = _pipelineAccelerator;
			drawables[1]._descriptorSet = _descriptorSetAccelerator;
			drawables[1]._geo = _cubeGeo;
			drawables[1]._vertexCount = _cubeVertexCount;
			drawables[1]._looseUniformsInterface = _usi;
			drawables[1]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					Float4x4 transform = Identity<Float4x4>();
					Combine_IntoLHS(transform, RotationY{gPI / 4.0f});
					Combine_IntoLHS(transform, RotationZ{gPI / 4.0f});
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(transform, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};

			drawables[2]._pipeline = _pipelineAccelerator;
			drawables[2]._descriptorSet = _descriptorSetAccelerator;
			drawables[2]._geo = _cubeGeo;
			drawables[2]._vertexCount = _cubeVertexCount;
			drawables[2]._looseUniformsInterface = _usi;
			drawables[2]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(AsFloat4x4(Float3{0.f, -1.0f - std::sqrt(8.0f)/2.0f, 0.f}), ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};
		}

		ShapeStackDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			std::tie(_sphereGeo, _sphereVertexCount) = CreateSphereGeo(testHelper);
			std::tie(_cubeGeo, _cubeVertexCount) = CreateCubeGeo(testHelper);

			_usi = std::make_shared<RenderCore::UniformsStreamInterface>();
			_usi->BindImmediateData(0, Hash64("LocalTransform"));

			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr,
				{}, {}, {});
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateShapeStackDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<ShapeStackDrawableWriter>(testHelper, pipelineAcceleratorPool);
	}

	class StonehengeDrawableWriter : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
		std::shared_ptr<RenderCore::UniformsStreamInterface> _usi;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			const unsigned stoneCount = 32;
			
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; Float4x4 _localTransform; };
			auto* drawables = pkt._drawables.Allocate<CustomDrawable>(stoneCount+1);

			const float radius = 1.0f;
			const float circumference = 2.0f * gPI * radius;
			const float stoneWidth = circumference / 2.0f / stoneCount;
			for (unsigned c=0; c<stoneCount; ++c) {
				float theta = 2.f * gPI * c / stoneCount;

				Float4x4 transform = Identity<Float4x4>();
				Combine_IntoLHS(transform, ArbitraryScale{Float3{stoneWidth / 2.0f * 0.2f, 1.0f, stoneWidth / 2.0f}});
				Combine_IntoLHS(transform, RotationY{gPI / 2.0f + theta});
				Combine_IntoLHS(transform, Float3{std::sin(theta), 0.f, std::cos(theta)});

				drawables[c]._pipeline = _pipelineAccelerator;
				drawables[c]._descriptorSet = _descriptorSetAccelerator;
				drawables[c]._geo = _geo;
				drawables[c]._looseUniformsInterface = _usi;
				drawables[c]._vertexCount = _vertexCount;
				drawables[c]._localTransform = transform;
				drawables[c]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
					{
						auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localTransform, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
						drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
						drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
					};
			}

			Float4x4 baseTransform = Identity<Float4x4>();
			Combine_IntoLHS(baseTransform, ArbitraryScale{Float3{2.0f, 0.125f, 2.0f}});
			Combine_IntoLHS(baseTransform, Float3{0.f, -0.125f, 0.f});

			static unsigned count = 0;
			if (count < 2) {
				Combine_IntoLHS(baseTransform, ArbitraryScale{Float3{1e-3f, 1e-3f, 1e-3f}});
			}
			++count;

			drawables[stoneCount]._pipeline = _pipelineAccelerator;
			drawables[stoneCount]._descriptorSet = _descriptorSetAccelerator;
			drawables[stoneCount]._geo = _geo;
			drawables[stoneCount]._looseUniformsInterface = _usi;
			drawables[stoneCount]._vertexCount = _vertexCount;
			drawables[stoneCount]._localTransform = baseTransform;
			drawables[stoneCount]._drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(((CustomDrawable&)drawable)._localTransform, ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};
		}

		StonehengeDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(testHelper);
			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr,
				{}, {}, {});

			_usi = std::make_shared<RenderCore::UniformsStreamInterface>();
			_usi->BindImmediateData(0, Hash64("LocalTransform"));
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateStonehengeDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<StonehengeDrawableWriter>(testHelper, pipelineAcceleratorPool);
	}


	class FlatPlaneDrawableWriter : public IDrawablesWriter
	{
	public:
		std::shared_ptr<RenderCore::Techniques::DrawableGeo> _geo;
		std::shared_ptr<RenderCore::Techniques::PipelineAccelerator> _pipelineAccelerator;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
		std::shared_ptr<RenderCore::UniformsStreamInterface> _usi;
		size_t _vertexCount;

		void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt)
		{
			struct CustomDrawable : public RenderCore::Techniques::Drawable { unsigned _vertexCount; };
			auto* drawable = pkt._drawables.Allocate<CustomDrawable>();
			drawable->_pipeline = _pipelineAccelerator;
			drawable->_descriptorSet = _descriptorSetAccelerator;
			drawable->_geo = _geo;
			drawable->_vertexCount = _vertexCount;
			drawable->_looseUniformsInterface = _usi;
			drawable->_drawFn = [](RenderCore::Techniques::ParsingContext& parsingContext, const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext, const RenderCore::Techniques::Drawable& drawable)
				{
					ScaleRotationTranslationM srt {
						Float3 { 1000.f, 1.0f, 1000.f },
						Identity<Float3x3>(),
						Float3 { 0.f, -1.0f, 0.f }
					};
					auto localTransform = RenderCore::Techniques::MakeLocalTransform(AsFloat4x4(srt), ExtractTranslation(parsingContext.GetProjectionDesc()._cameraToWorld));
					drawFnContext.ApplyLooseUniforms(RenderCore::ImmediateDataStream(localTransform));
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};
		}

		FlatPlaneDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
		{
			std::tie(_geo, _vertexCount) = CreateCubeGeo(testHelper);
			_pipelineAccelerator = pipelineAcceleratorPool.CreatePipelineAccelerator(
				nullptr,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				RenderCore::Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			_descriptorSetAccelerator = pipelineAcceleratorPool.CreateDescriptorSetAccelerator(
				nullptr,
				{}, {}, {});

			_usi = std::make_shared<RenderCore::UniformsStreamInterface>();
			_usi->BindImmediateData(0, Hash64("LocalTransform"));
		}
	};

	std::shared_ptr<IDrawablesWriter> CreateFlatPlaneDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool)
	{
		return std::make_shared<FlatPlaneDrawableWriter>(testHelper, pipelineAcceleratorPool);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void ParseScene(RenderCore::LightingEngine::LightingTechniqueInstance& lightingIterator, IDrawablesWriter& drawableWriter)
	{
		using namespace RenderCore;
		for (;;) {
			auto next = lightingIterator.GetNextStep();
			if (next._type == LightingEngine::StepType::None || next._type == LightingEngine::StepType::Abort) break;
			if (next._type == LightingEngine::StepType::DrawSky) continue;
			assert(next._type == LightingEngine::StepType::ParseScene);
			assert(next._pkt);
			drawableWriter.WriteDrawables(*next._pkt);
		}
	}

	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeMaterialDescriptorSetLayout()
	{
		const char* unitTestsMaterialDescSet = R"(
			UniformBuffer BasicMaterialConstants
			{
				float3  MaterialDiffuse = {1,1,1};
				float   Opacity = 1;
				float3  MaterialSpecular = {1,1,1};
				float   AlphaThreshold = .5f;

				float   RoughnessMin = 0.1f;
				float   RoughnessMax = 0.6f;
				float   SpecularMin = 0.0f;
				float   SpecularMax = 0.5f;
				float   MetalMin = 0.f;
				float   MetalMax = 1.f;
			};
			UniformBuffer cb1;						// 1
			UniformBuffer cb2;						// 2

			SampledTexture tex0;					// 3
			SampledTexture tex1;					// 4
			SampledTexture tex2;					// 5
			SampledTexture tex3;					// 6
			SampledTexture tex4;					// 7
			SampledTexture tex5;					// 8
			SampledTexture tex6;					// 9
			SampledTexture tex7;					// 10

			UnorderedAccessBuffer uab0;				// 11
			Sampler sampler0;						// 12
		)";

		auto layout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>(
			unitTestsMaterialDescSet, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{}
		);
		return RenderCore::Techniques::DescriptorSetLayoutAndBinding { layout, 1 };
	}

	static RenderCore::Techniques::DescriptorSetLayoutAndBinding MakeSequencerDescriptorSetLayout()
	{
		auto layout = std::make_shared<RenderCore::Assets::PredefinedDescriptorSetLayout>();
		layout->_slots = {
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"GlobalTransform"}, RenderCore::DescriptorType::UniformBuffer },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"LocalTransform"}, RenderCore::DescriptorType::UniformBuffer },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::UniformBuffer },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"BasicLightingEnvironment"}, RenderCore::DescriptorType::UniformBuffer },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"ArbitraryShadowProjection"}, RenderCore::DescriptorType::UniformBuffer },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"OrthogonalShadowProjection"}, RenderCore::DescriptorType::UniformBuffer },
			
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{"NormalsFittingTexture"}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::SampledTexture },

			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::Sampler },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::Sampler },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::Sampler },
			RenderCore::Assets::PredefinedDescriptorSetLayout::ConditionalDescriptorSlot { std::string{}, RenderCore::DescriptorType::Sampler }
		};

		return RenderCore::Techniques::DescriptorSetLayoutAndBinding { layout, 0 };
	}

	RenderCore::Techniques::ParsingContext InitializeParsingContext(
		RenderCore::Techniques::TechniqueContext& techniqueContext,
		const RenderCore::ResourceDesc& targetDesc,
		const RenderCore::Techniques::CameraDesc& camera)
	{
		using namespace RenderCore;

		Techniques::PreregisteredAttachment preregisteredAttachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorLDR,
				targetDesc,
				Techniques::PreregisteredAttachment::State::Uninitialized
			}
		};
		FrameBufferProperties fbProps { targetDesc._textureDesc._width, targetDesc._textureDesc._height };

		Techniques::ParsingContext parsingContext{techniqueContext};
		parsingContext.GetProjectionDesc() = BuildProjectionDesc(camera, UInt2{targetDesc._textureDesc._width, targetDesc._textureDesc._height});
		
		auto& stitchingContext = parsingContext.GetFragmentStitchingContext();
		stitchingContext._workingProps = fbProps;
		for (const auto&a:preregisteredAttachments)
			stitchingContext.DefineAttachment(a._semantic, a._desc, a._state, a._layoutFlags);
		return parsingContext;
	}

}


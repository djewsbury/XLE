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
#include "../../../RenderCore/Techniques/PipelineAcceleratorInternal.h"
#include "../../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../../RenderCore/Techniques/TechniqueDelegates.h"
#include "../../../RenderCore/Techniques/ParsingContext.h"
#include "../../../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../../../RenderCore/Techniques/Techniques.h"
#include "../../../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../../../RenderCore/Techniques/CommonResources.h"
#include "../../../RenderCore/Assets/TextureLoaders.h"
#include "../../../RenderCore/Assets/MaterialCompiler.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/Format.h"
#include "../../../Assets/AssetServices.h"
#include "../../../Assets/IFileSystem.h"
#include "../../../Assets/OSFileSystem.h"
#include "../../../Assets/MountingTree.h"
#include "../../../Assets/MemoryFile.h"
#include "../../../Assets/AssetSetManager.h"
#include "../../../Assets/Assets.h"
#include "../../../Math/MathSerialization.h"
#include "../../../Math/Transformations.h"
#include "../../../Tools/ToolsRig/VisualisationGeo.h"
#include "../../../ConsoleRig/Console.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/StringFormat.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <regex>
#include <thread>
#include <chrono>

using namespace Catch::literals;
using namespace std::chrono_literals;
namespace UnitTests
{
	static const char* s_basicTexturingGraph = R"--(
		import templates = "xleres/Nodes/Templates.pixel.sh"
		import output = "xleres/Nodes/Output.sh"
		import texture = "xleres/Nodes/Texture.sh"
		import basic = "xleres/Nodes/Basic.sh"
		import materialParam = "xleres/Nodes/MaterialParam.sh"

		GBufferValues Bind_PerPixel(VSOUT geo) implements templates::PerPixel
		{
			captures MaterialUniforms = ( float3 Multiplier = "{1,1,1}", float Adder = "{0,0,0}", float2 CoordFreq = "{.1, .1}", Texture2D BoundTexture, SamplerState BoundSampler );
			node samplingCoords = basic::Multiply2(lhs:texture::GetPixelCoords(geo:geo).result, rhs:MaterialUniforms.CoordFreq);
			node loadFromTexture = texture::SampleWithSampler(
				inputTexture:MaterialUniforms.BoundTexture, 
				inputSampler:MaterialUniforms.BoundSampler,
				texCoord:samplingCoords.result);
			node multiply = basic::Multiply3(lhs:loadFromTexture.result, rhs:MaterialUniforms.Multiplier);
			node add = basic::Add3(lhs:multiply.result, rhs:MaterialUniforms.Adder);
			node mat = materialParam::CommonMaterialParam_Make(roughness:"1", specular:"1", metal:"1");
			return output::Output_PerPixel(
				diffuseAlbedo:add.result, 
				material:mat.result).result;
		}
	)--";

	static const char s_patchCollectionBasicTexturing[] = R"--(
		main=~
			ut-data/basicTexturingGraph.graph::Bind_PerPixel
	)--";

	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("basic.tech", ::Assets::AsBlob(s_basicTechniqueFile)),
		std::make_pair("basicTexturingGraph.graph", ::Assets::AsBlob(s_basicTexturingGraph))
	};

	static std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> GetPatchCollectionFromText(StringSection<> techniqueText)
	{
		using namespace RenderCore;

		InputStreamFormatter<utf8> formattr { techniqueText.Cast<utf8>() };
		return std::make_shared<RenderCore::Assets::ShaderPatchCollection>(formattr, ::Assets::DirectorySearchRules{}, ::Assets::DependencyValidation{});
	}

	template<typename Type>
		static void StallForDescriptorSet(RenderCore::IThreadContext& threadContext, ::Assets::Marker<Type>& descriptorSetFuture)
	{
		auto state = descriptorSetFuture.StallWhilePending();
		if (state.has_value() && state.value() == ::Assets::AssetState::Ready)
			RenderCore::Techniques::Services::GetBufferUploads().StallUntilCompletion(threadContext, descriptorSetFuture.Actualize().GetCompletionCommandList());
	}

	template<typename Type>
		void RequireReady(::Assets::Marker<Type>& future)
	{
		INFO(::Assets::AsString(future.GetActualizationLog()));
		REQUIRE(future.GetAssetState() == ::Assets::AssetState::Ready);
	}

	static RenderCore::Techniques::GlobalTransformConstants MakeGlobalTransformConstants(const RenderCore::ResourceDesc& targetDesc)
	{
		using namespace RenderCore;
		Techniques::CameraDesc cameraDesc;
		Float3 fwd = Normalize(Float3 { 1.0f, -1.0f, 1.0f });
		cameraDesc._cameraToWorld = MakeCameraToWorld(fwd, Float3{0.f, 1.f, 0.f}, -5.0f * fwd);
		cameraDesc._projection = Techniques::CameraDesc::Projection::Orthogonal;
		cameraDesc._left = -2.0f; cameraDesc._top = -2.0f;
		cameraDesc._right = 2.0f; cameraDesc._bottom = 2.0f;
		auto projDesc = Techniques::BuildProjectionDesc(cameraDesc, UInt2{ targetDesc._textureDesc._width, targetDesc._textureDesc._height });
		return Techniques::BuildGlobalTransformConstants(projDesc);
	}

	class UnitTestGlobalUniforms : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			switch (idx) {
			case 0:
				*(RenderCore::Techniques::GlobalTransformConstants*)dst.begin() = MakeGlobalTransformConstants(_targetDesc);
				break;
			case 1:
				*(RenderCore::Techniques::LocalTransformConstants*)dst.begin() = RenderCore::Techniques::MakeLocalTransform(Identity<Float4x4>(), Zero<Float3>());
				break;
			}
		}

		size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			switch (idx) {
			case 0:
				return sizeof(RenderCore::Techniques::GlobalTransformConstants);
			case 1:
				return sizeof(RenderCore::Techniques::LocalTransformConstants);
			default:
				return 0;
			}
		}

		UnitTestGlobalUniforms(const RenderCore::ResourceDesc& targetDesc) : _targetDesc(targetDesc)
		{
			BindImmediateData(0, Hash64("GlobalTransform"));
			BindImmediateData(1, Hash64("LocalTransform"));
		}

		RenderCore::ResourceDesc _targetDesc;
	};

	static unsigned s_sphereVertexCount = 0;

	TEST_CASE( "Drawables-RenderImages", "[rendercore_techniques]" )
	{
		using namespace RenderCore;
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto xlresmnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", UnitTests::CreateEmbeddedResFileSystem());
		auto utdatamnt = ::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData, s_defaultFilenameRules, ::Assets::FileSystemMemoryFlags::UseModuleModificationTime));
		auto testHelper = MakeTestHelper();

		TechniqueTestApparatus techniqueTestApparatus(*testHelper);
		auto pipelineAcceleratorPool = techniqueTestApparatus._pipelineAccelerators;
		auto& compilers = ::Assets::Services::GetAsyncMan().GetIntermediateCompilers();

		auto threadContext = testHelper->_device->GetImmediateContext();
		auto targetDesc = CreateDesc(
			BindFlag::RenderTarget | BindFlag::TransferSrc, 0, GPUAccess::Write,
			TextureDesc::Plain2D(256, 256, Format::R8G8B8A8_UNORM),
			"temporary-out");
		UnitTestFBHelper fbHelper(*testHelper->_device, *threadContext, targetDesc);
		
		/////////////////////////////////////////////////////////////////

		SECTION("Draw Basic Sphere")
		{
			auto sphereGeo = ToolsRig::BuildGeodesicSphere();
			auto sphereVb = testHelper->CreateVB(sphereGeo);
			auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
			drawableGeo->_vertexStreams[0]._resource = sphereVb;
			drawableGeo->_vertexStreamCount = 1;
			s_sphereVertexCount = sphereGeo.size();

			auto patches = GetPatchCollectionFromText(s_patchCollectionBasicTexturing);

			ParameterBox constantBindings;
			constantBindings.SetParameter("CoordFreq", Float2{.025f, .025f});
			ParameterBox resourceBindings;
			resourceBindings.SetParameter("BoundTexture", "xleres/DefaultResources/waternoise.png");
			std::vector<std::pair<uint64_t, SamplerDesc>> samplerBindings;
			samplerBindings.push_back(std::make_pair(Hash64("BoundSampler"), SamplerDesc{}));
			auto descriptorSetAccelerator = pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
				patches,
				ParameterBox {}, constantBindings, resourceBindings, MakeIteratorRange(samplerBindings));

			auto techniqueSetFile = ::Assets::MakeAssetPtr<Techniques::TechniqueSetFile>("ut-data/basic.tech");
			auto cfgId = pipelineAcceleratorPool->CreateSequencerConfig(
				"test",
				Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile),
				ParameterBox {},
				fbHelper.GetDesc());

			auto pipelineWithTexCoord = pipelineAcceleratorPool->CreatePipelineAccelerator(
				patches,
				ParameterBox {},
				ToolsRig::Vertex3D_InputLayout,
				Topology::TriangleList,
				RenderCore::Assets::RenderStateSet{});

			auto descSetFuture = pipelineAcceleratorPool->GetDescriptorSetMarker(*descriptorSetAccelerator);
			REQUIRE(descSetFuture);
			StallForDescriptorSet(*threadContext, *descSetFuture);
			RequireReady(*descSetFuture);

			auto pipelineFuture = pipelineAcceleratorPool->GetPipelineMarker(*pipelineWithTexCoord, *cfgId);
			REQUIRE(pipelineFuture);
			pipelineFuture->StallWhilePending();
			RequireReady(*pipelineFuture);

			pipelineAcceleratorPool->RebuildAllOutOfDatePipelines();		// must call this to flip completed pipelines, etc, to visible

			struct CustomDrawable : public Techniques::Drawable { unsigned _vertexCount; };
			Techniques::DrawablesPacket pkt;
			auto* drawable = pkt._drawables.Allocate<CustomDrawable>();
			drawable->_pipeline = pipelineWithTexCoord;
			drawable->_descriptorSet = descriptorSetAccelerator;
			drawable->_geo = drawableGeo;
			drawable->_vertexCount = sphereGeo.size();
			drawable->_drawFn = [](Techniques::ParsingContext&, const Techniques::ExecuteDrawableContext& drawFnContext, const Techniques::Drawable& drawable)
				{
					drawFnContext.Draw(((CustomDrawable&)drawable)._vertexCount);
				};

			auto globalDelegate = std::make_shared<UnitTestGlobalUniforms>(targetDesc);

			{
				auto rpi = fbHelper.BeginRenderPass(*threadContext);
				Techniques::ParsingContext parsingContext{*techniqueTestApparatus._techniqueContext, *threadContext};
				parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(globalDelegate);
				parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
				auto prepare = Techniques::PrepareResources(*pipelineAcceleratorPool, *cfgId, pkt);
				if (prepare) {
					prepare->StallWhilePending();
					REQUIRE(prepare->GetAssetState() == ::Assets::AssetState::Ready);
				}
				Techniques::Draw(
					parsingContext, 
					*pipelineAcceleratorPool,
					*cfgId,
					pkt);
			}
			fbHelper.SaveImage(*threadContext, "drawables-render-sphere");
		}

		SECTION("Draw model file")
		{
			testHelper->BeginFrameCapture();

			auto matRegistration = RenderCore::Assets::RegisterMaterialCompiler(compilers);
			#if defined(_DEBUG)
				auto discoveredCompilations = ::Assets::DiscoverCompileOperations(compilers, "ColladaConversion.dll");
			#else
				auto discoveredCompilations = ::Assets::DiscoverCompileOperations(compilers, "ColladaConversion.dll");
			#endif
			REQUIRE(!discoveredCompilations.empty());

			auto techniqueSetFile = ::Assets::MakeAssetPtr<Techniques::TechniqueSetFile>("ut-data/basic.tech");
			auto cfgId = pipelineAcceleratorPool->CreateSequencerConfig(
				"test",
				Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile),
				ParameterBox {},
				fbHelper.GetDesc());

			auto renderer = ::Assets::MakeAssetPtr<Techniques::SimpleModelRenderer>(
				pipelineAcceleratorPool, nullptr,
				"xleres/DefaultResources/materialsphere.dae",
				"xleres/DefaultResources/materialsphere.material");
			INFO(::Assets::AsString(renderer->GetActualizationLog()));
			renderer->StallWhilePending();
			REQUIRE(renderer->GetAssetState() == ::Assets::AssetState::Ready);

			Techniques::DrawablesPacket pkts[(unsigned)Techniques::BatchFilter::Max];
			Techniques::DrawablesPacket* drawablePktsPtrs[] = { &pkts[0], &pkts[1], &pkts[2], &pkts[3] };
			static_assert(dimof(pkts) == dimof(drawablePktsPtrs));
			renderer->Actualize()->BuildDrawables(MakeIteratorRange(drawablePktsPtrs));
				
			auto globalDelegate = std::make_shared<UnitTestGlobalUniforms>(targetDesc);

			for (const auto&pkt:pkts) {
				auto prepare = Techniques::PrepareResources(*pipelineAcceleratorPool, *cfgId, pkt);
				if (prepare) {
					prepare->StallWhilePending();
					REQUIRE(prepare->GetAssetState() == ::Assets::AssetState::Ready);
				}
			}

			pipelineAcceleratorPool->RebuildAllOutOfDatePipelines();		// must call this to flip completed pipelines, etc, to visible

			for (unsigned c=0; c<1; ++c) {
				{
					auto rpi = fbHelper.BeginRenderPass(*threadContext);
					Techniques::ParsingContext parsingContext{*techniqueTestApparatus._techniqueContext, *threadContext};
					parsingContext.GetViewport() = fbHelper.GetDefaultViewport();
					parsingContext.GetUniformDelegateManager()->AddShaderResourceDelegate(globalDelegate);
					
					auto* d = (Techniques::Drawable*)pkts[0]._drawables.begin().get();
					auto future = pipelineAcceleratorPool->GetPipelineMarker(*d->_pipeline, *cfgId);
					if (future) {
						future->StallWhilePending();
						INFO(::Assets::AsString(future->GetActualizationLog()));
						REQUIRE(future->GetAssetState() == ::Assets::AssetState::Ready);
					}

					for (const auto&pkt:pkts)
						Techniques::Draw(parsingContext, *pipelineAcceleratorPool, *cfgId, pkt);

					if (parsingContext._requiredBufferUploadsCommandList)
						techniqueTestApparatus._bufferUploads->StallUntilCompletion(*threadContext, parsingContext._requiredBufferUploadsCommandList);
				}
				fbHelper.SaveImage(*threadContext, "drawables-render-model");
			}

			testHelper->EndFrameCapture();
		}

		/////////////////////////////////////////////////////////////////

		::Assets::MainFileSystem::GetMountingTree()->Unmount(utdatamnt);
		::Assets::MainFileSystem::GetMountingTree()->Unmount(xlresmnt);
	}

	using namespace RenderCore;
	using namespace RenderCore::Techniques;

	class ShaderResourceDel : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
        virtual void WriteResourceViews(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			++_resViewQueryCount;
			REQUIRE(bindingFlags == 1ull<<uint64_t(_realTextureSlot));
			REQUIRE(dst.size() == _interface._resourceViewBindings.size());
			dst[_realTextureSlot] = _textureResource.get();
		}

        virtual void WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst) override
		{
			++_samplerQueryCount;
			REQUIRE(bindingFlags == 1ull<<uint64_t(_realSamplerSlot));
			REQUIRE(dst.size() == _interface._samplerBindings.size());
			dst[0] = _sampler.get();
		}

		virtual void WriteImmediateData(ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			++_immediateDataQueryCount;
			REQUIRE(idx == _realImmediateDataSlot);
			REQUIRE(dst.size() == 134);
			std::memset(dst.begin(), 0xff, dst.size());
		}

		virtual size_t GetImmediateDataSize(ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			if (idx != _realImmediateDataSlot) return 0;
			return 134;
		}

		ShaderResourceDel(IDevice& dev, std::string name, unsigned dummySlots)
		{
			for (unsigned c=0; c<dummySlots; ++c)
				BindResourceView(c, Hash64("slot-doesnt-exist-" + std::to_string(c)));
			_realTextureSlot = dummySlots;
			BindResourceView(_realTextureSlot, Hash64("SeqTex0"));
			_realSamplerSlot = 0;
			BindSampler(_realSamplerSlot, Hash64("SeqSampler0"));
			for (unsigned c=0; c<dummySlots; ++c)
				BindImmediateData(c, Hash64("imm-slot-doesnt-exist-" + std::to_string(c)));
			_realImmediateDataSlot = dummySlots;
			BindImmediateData(_realImmediateDataSlot, Hash64("SeqBuffer0"));

			std::vector<uint8_t> dummyData(32*32, 0);
			auto textureResource = dev.CreateResource(
				CreateDesc(
					BindFlag::ShaderResource, 
					0, GPUAccess::Read,
					TextureDesc::Plain2D(32, 32, RenderCore::Format::R8G8B8A8_UNORM),
					name + "-tex0"),
				SubResourceInitData{MakeIteratorRange(dummyData)});
			_textureResource = textureResource->CreateTextureView();

			_sampler = dev.CreateSampler(SamplerDesc{});
		}

		std::shared_ptr<IResourceView> _textureResource;
		std::shared_ptr<ISampler> _sampler;

		unsigned _realTextureSlot = 0, _realSamplerSlot = 0, _realImmediateDataSlot = 0;

		unsigned _resViewQueryCount = 0;
		unsigned _samplerQueryCount = 0;
		unsigned _immediateDataQueryCount = 0;
	};

	class UniformDel : public RenderCore::Techniques::IUniformBufferDelegate
	{
	public:
		virtual void WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst) override
		{
			std::memset(dst.begin(), 0xff, dst.size());
			++_queryCount;
		}

        virtual size_t GetSize() override { return 36; }		// odd size should get rounded up

		unsigned _queryCount = 0;
	};

	TEST_CASE( "Drawables-SequencerDescriptorSet", "[rendercore_techniques]" )
	{
		auto globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
		auto testHelper = MakeTestHelper();
		TechniqueTestApparatus testApparatus(*testHelper);

		auto del0 = std::make_shared<ShaderResourceDel>(*testHelper->_device, "del0", 6);
		auto del1 = std::make_shared<ShaderResourceDel>(*testHelper->_device, "del1", 3);
		auto del2 = std::make_shared<ShaderResourceDel>(*testHelper->_device, "del2", 8);
		auto udel0 = std::make_shared<UniformDel>();
		auto udel1 = std::make_shared<UniformDel>();
		
		auto threadContext = testHelper->_device->GetImmediateContext();
		ParsingContext parsingContext(*testApparatus._techniqueContext, *threadContext);
		
		{
			auto helper0 = Techniques::CreateUniformDelegateManager();
			helper0->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *testApparatus._sequencerDescSetLayout.GetLayout(), *testHelper->_device);
			helper0->AddShaderResourceDelegate(del0);
			helper0->AddShaderResourceDelegate(del1);
			helper0->AddUniformDelegate(Hash64("slot-doesnt-exist-0"), udel0);
			helper0->AddUniformDelegate(Hash64("slot-doesnt-exist-1"), udel0);
			helper0->AddUniformDelegate(Hash64("GlobalTransform"), udel0);
			helper0->AddUniformDelegate(Hash64("LocalTransform"), udel0);
			helper0->AddUniformDelegate(Hash64("slot-doesnt-exist-2"), udel0);

			helper0->AddShaderResourceDelegate(del2);
			helper0->AddUniformDelegate(Hash64("LocalTransform"), udel1);

			helper0->BringUpToDateGraphics(parsingContext);
			REQUIRE(del2->_resViewQueryCount == 1);
			REQUIRE(del1->_resViewQueryCount == 0);
			REQUIRE(del0->_resViewQueryCount == 0);
			REQUIRE(del2->_samplerQueryCount == 1);
			REQUIRE(del1->_samplerQueryCount == 0);
			REQUIRE(del0->_samplerQueryCount == 0);
			REQUIRE(del2->_immediateDataQueryCount == 1);
			REQUIRE(del1->_immediateDataQueryCount == 0);
			REQUIRE(del0->_immediateDataQueryCount == 0);
			REQUIRE(udel0->_queryCount == 1);		// once for GlobalTransform
			REQUIRE(udel1->_queryCount == 1);		// once for LocalTransform
		}

		{
			auto helper1 = Techniques::CreateUniformDelegateManager();
			helper1->AddSemiConstantDescriptorSet(Hash64("Sequencer"), *testApparatus._sequencerDescSetLayout.GetLayout(), *testHelper->_device);
			helper1->AddShaderResourceDelegate(del0);
			helper1->AddShaderResourceDelegate(del1);
			helper1->AddUniformDelegate(Hash64("slot-doesnt-exist-0"), udel0);
			helper1->AddUniformDelegate(Hash64("slot-doesnt-exist-1"), udel0);
			helper1->AddUniformDelegate(Hash64("GlobalTransform"), udel0);
			helper1->AddUniformDelegate(Hash64("LocalTransform"), udel0);
			helper1->AddUniformDelegate(Hash64("slot-doesnt-exist-2"), udel0);

			helper1->BringUpToDateGraphics(parsingContext);
			REQUIRE(del2->_resViewQueryCount == 1);
			REQUIRE(del1->_resViewQueryCount == 1);
			REQUIRE(del0->_resViewQueryCount == 0);
			REQUIRE(del2->_samplerQueryCount == 1);
			REQUIRE(del1->_samplerQueryCount == 1);
			REQUIRE(del0->_samplerQueryCount == 0);
			REQUIRE(del2->_immediateDataQueryCount == 1);
			REQUIRE(del1->_immediateDataQueryCount == 1);
			REQUIRE(del0->_immediateDataQueryCount == 0);
			REQUIRE(udel0->_queryCount == 3);		// twice more for GlobalTransform & LocalTransform
			REQUIRE(udel1->_queryCount == 1);		// removed from binding
		}
	}
}

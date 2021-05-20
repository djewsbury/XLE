// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeferredLightingResolve.h"
#include "DeferredLightingDelegate.h"
#include "LightUniforms.h"
#include "LightScene_Internal.h"
#include "ShadowPreparer.h"

#include "../Techniques/CommonResources.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineCollection.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/InputLayout.h"
#include "../IAnnotator.h"

#include "../../Assets/AssetFuture.h"
#include "../../Assets/Assets.h"
#include "../../Utility/StringFormat.h"
#include "../../xleres/FileList.h"

#include "../ConsoleRig/Console.h"

static Int2 GetCursorPos();

namespace RenderCore { namespace LightingEngine
{

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct CB
	{
		enum 
		{
			GlobalTransform,
			LightBuffer,
			Debugging,
			Max
		};
	};

	struct SR
	{
		enum
		{
			GBuffer_Diffuse,
			GBuffer_Normals,
			GBuffer_Parameters,
			DepthTexture,
			Max
		};
	};

	class ShadowFilteringTable
	{
	public:
		Float4      _filterKernel[32];

		ShadowFilteringTable()
		{
			_filterKernel[ 0] = Float4(-0.1924249f, -0.5685654f,0,0);
			_filterKernel[ 1] = Float4(0.0002287195f, -0.830722f,0,0);
			_filterKernel[ 2] = Float4(-0.6227817f, -0.676464f,0,0);
			_filterKernel[ 3] = Float4(-0.3433303f, -0.8954138f,0,0);
			_filterKernel[ 4] = Float4(-0.3087259f, 0.0593961f,0,0);
			_filterKernel[ 5] = Float4(0.4013956f, 0.005351349f,0,0);
			_filterKernel[ 6] = Float4(0.6675568f, 0.2226908f,0,0);
			_filterKernel[ 7] = Float4(0.4703487f, 0.4219977f,0,0);
			_filterKernel[ 8] = Float4(-0.865732f, -0.1704932f,0,0);
			_filterKernel[ 9] = Float4(0.4836336f, -0.7363456f,0,0);
			_filterKernel[10] = Float4(-0.8455518f, 0.429606f,0,0);
			_filterKernel[11] = Float4(0.2486194f, 0.7276461f,0,0);
			_filterKernel[12] = Float4(0.01841145f, 0.581219f,0,0);
			_filterKernel[13] = Float4(0.9428069f, 0.2151681f,0,0);
			_filterKernel[14] = Float4(-0.2937738f, 0.8432091f,0,0);
			_filterKernel[15] = Float4(0.01657544f, 0.9762882f,0,0);

			_filterKernel[16] = Float4(0.03878351f, -0.1410931f,0,0);
			_filterKernel[17] = Float4(-0.3663213f, -0.348966f,0,0);
			_filterKernel[18] = Float4(0.2333971f, -0.5178556f,0,0);
			_filterKernel[19] = Float4(-0.6433204f, -0.3284476f,0,0);
			_filterKernel[20] = Float4(0.1255225f, 0.3221043f,0,0);
			_filterKernel[21] = Float4(0.4051761f, -0.299208f,0,0);
			_filterKernel[22] = Float4(0.8829983f, -0.1718857f,0,0);
			_filterKernel[23] = Float4(0.6724088f, -0.3562584f,0,0);
			_filterKernel[24] = Float4(-0.826445f, 0.1214067f,0,0);
			_filterKernel[25] = Float4(-0.386752f, 0.406546f,0,0);
			_filterKernel[26] = Float4(-0.5869312f, -0.01993746f,0,0);
			_filterKernel[27] = Float4(0.7842119f, 0.5549603f,0,0);
			_filterKernel[28] = Float4(0.5801646f, 0.7416336f,0,0);
			_filterKernel[29] = Float4(0.7366455f, -0.6388465f,0,0);
			_filterKernel[30] = Float4(-0.6067169f, 0.6372176f,0,0);
			_filterKernel[31] = Float4(0.2743046f, -0.9303559f,0,0);
		}
	};

	static const uint32_t StencilSky = 1<<7;
	static const uint32_t StencilSampleCount = 1<<6;

	static DepthStencilDesc s_dsWritePixelFrequencyPixel {
		CompareOp::Always, false,
		true, StencilSky|StencilSampleCount, 0xff, 
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Equal},
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Less}};

	static DepthStencilDesc s_dsWriteNonSky {
		CompareOp::Always, false,
		true, StencilSky, 0xff, 
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Equal}};

	::Assets::FuturePtr<Metal::GraphicsPipeline> BuildLightResolveOperator(
		Techniques::GraphicsPipelineCollection& pipelineCollection,
		const LightSourceOperatorDesc& desc,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		unsigned shadowResolveModel,
		Shadowing shadowing,
		GBufferType gbufferType)
	{
		auto sampleCount = TextureSamples::Create();
		auto mainOutputAttachment = fbDesc.GetSubpasses()[subpassIdx].GetOutputs()[0]._resourceName;
		if (fbDesc.GetAttachments()[mainOutputAttachment]._desc._flags & AttachmentDesc::Flags::Multisampled)
			sampleCount = fbDesc.GetProperties()._samples;
		
		StringMeld<256, ::Assets::ResChar> definesTable;
		definesTable << "GBUFFER_TYPE=" << (unsigned)gbufferType;
		definesTable << ";MSAA_SAMPLES=" << ((sampleCount._sampleCount<=1)?0:sampleCount._sampleCount);
		// if (desc._msaaSamplers) definesTable << ";MSAA_SAMPLERS=1";

		if (shadowing != Shadowing::NoShadows) {
			if (shadowing == Shadowing::OrthShadows || shadowing == Shadowing::OrthShadowsNearCascade || shadowing == Shadowing::OrthHybridShadows) {
				definesTable << ";SHADOW_CASCADE_MODE=" << 2u;
			} else if (shadowing == Shadowing::CubeMapShadows) {
				definesTable << ";SHADOW_CASCADE_MODE=" << 3u;
			} else
				definesTable << ";SHADOW_CASCADE_MODE=" << 1u;
			definesTable << ";SHADOW_ENABLE_NEAR_CASCADE=" << (shadowing == Shadowing::OrthShadowsNearCascade ? 1u : 0u);
			definesTable << ";SHADOW_RESOLVE_MODEL=" << unsigned(shadowResolveModel);
			definesTable << ";SHADOW_RT_HYBRID=" << unsigned(shadowing == Shadowing::OrthHybridShadows);
		}
		definesTable << ";LIGHT_SHAPE=" << unsigned(desc._shape);
		definesTable << ";DIFFUSE_METHOD=" << unsigned(desc._diffuseModel);
		definesTable << ";HAS_SCREENSPACE_AO=" << unsigned(hasScreenSpaceAO);

		const bool flipDirection = false;
		const char* vertexShader_viewFrustumVector = 
			flipDirection
				? BASIC2D_VERTEX_HLSL ":fullscreen_flip_viewfrustumvector"
				: BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector"
				;

		auto stencilRefValue = 0;

		Techniques::VertexInputStates inputStates;
		inputStates._inputLayout = {};
		inputStates._topology = Topology::TriangleStrip;

		Techniques::PixelOutputStates outputStates;
		const bool doSampleFrequencyOptimisation = Tweakable("SampleFrequencyOptimisation", true);
		if (doSampleFrequencyOptimisation && sampleCount._sampleCount > 1) {
			outputStates._rasterization = Techniques::CommonResourceBox::s_rsCullDisable;
			outputStates._depthStencil = s_dsWritePixelFrequencyPixel;
			stencilRefValue = StencilSampleCount;
		} else {
			outputStates._depthStencil = s_dsWriteNonSky;
			stencilRefValue = 0x0;
		}

		AttachmentBlendDesc blends[] { Techniques::CommonResourceBox::s_abAdditive };
		outputStates._attachmentBlend = MakeIteratorRange(blends);
		outputStates._fbTarget = {&fbDesc, subpassIdx};

		return pipelineCollection.CreatePipeline(
			vertexShader_viewFrustumVector, {},
			DEFERRED_RESOLVE_LIGHT ":main", definesTable.AsStringSection(),
			inputStates, outputStates);
	}

	::Assets::FuturePtr<IDescriptorSet> BuildFixedLightResolveDescriptorSet(
		std::shared_ptr<IDevice> device,
		const DescriptorSetSignature& descSetLayout)
	{
		auto balancedNoiseFuture = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/balanced_noise.dds:LT");

		auto result = std::make_shared<::Assets::AssetFuture<IDescriptorSet>>();
		::Assets::WhenAll(balancedNoiseFuture).ThenConstructToFuture<IDescriptorSet>(
			*result,
			[device, descSetLayout=descSetLayout](std::shared_ptr<RenderCore::Techniques::DeferredShaderResource> balancedNoise) {
				SamplerDesc shadowComparisonSamplerDesc { FilterMode::ComparisonBilinear, AddressMode::Clamp, AddressMode::Clamp, CompareOp::LessEqual };
				SamplerDesc shadowDepthSamplerDesc { FilterMode::Bilinear, AddressMode::Clamp, AddressMode::Clamp };
				auto shadowComparisonSampler = device->CreateSampler(shadowComparisonSamplerDesc);
				auto shadowDepthSampler = device->CreateSampler(shadowDepthSamplerDesc);

				DescriptorSetInitializer::BindTypeAndIdx bindTypes[4];
				bindTypes[0] = { DescriptorSetInitializer::BindType::ImmediateData, 0 };
				bindTypes[1] = { DescriptorSetInitializer::BindType::ResourceView, 0 };
				bindTypes[2] = { DescriptorSetInitializer::BindType::Sampler, 0 };
				bindTypes[3] = { DescriptorSetInitializer::BindType::Sampler, 1 };
				ShadowFilteringTable shdParam;
				ImmediateDataStream immDatas { shdParam };
				IResourceView* srv[1] { balancedNoise->GetShaderResource().get() };
				ISampler* samplers[2] { shadowComparisonSampler.get(), shadowDepthSampler.get() };
				DescriptorSetInitializer inits;
				inits._slotBindings = MakeIteratorRange(bindTypes);
				inits._bindItems = immDatas;
				inits._bindItems._resourceViews = MakeIteratorRange(srv);
				inits._bindItems._samplers = MakeIteratorRange(samplers);
				inits._signature = &descSetLayout;
				return device->CreateDescriptorSet(inits);
			});
		return result;
	}

	::Assets::FuturePtr<LightResolveOperators> BuildLightResolveOperators(
		Techniques::GraphicsPipelineCollection& pipelineCollection,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		unsigned shadowResolveModel,
		Shadowing shadowing,
		GBufferType gbufferType)
	{
		using PipelineFuture = ::Assets::FuturePtr<Metal::GraphicsPipeline>;
		std::vector<PipelineFuture> pipelineFutures;
		pipelineFutures.reserve(resolveOperators.size());
		for (const auto&desc:resolveOperators)
			pipelineFutures.push_back(
				BuildLightResolveOperator(
					pipelineCollection, desc,
					fbDesc, subpassIdx,
					hasScreenSpaceAO, shadowResolveModel,
					shadowing, gbufferType));

		auto finalResult = std::make_shared<LightResolveOperators>();
		finalResult->_pipelineLayout = pipelineCollection.GetPipelineLayout();
		finalResult->_debuggingOn = false;

		const RenderCore::DescriptorSetSignature* sig = nullptr;
		auto layoutInitializer = finalResult->_pipelineLayout->GetInitializer();
		for (const auto& descSet:layoutInitializer.GetDescriptorSets())
			if (descSet._name == "SharedDescriptors")
				sig = &descSet._signature;
		if (!sig)
			Throw(std::runtime_error("No SharedDescriptors descriptor set in lighting operator pipeline layout"));

		auto fixedDescSetFuture = BuildFixedLightResolveDescriptorSet(pipelineCollection.GetDevice(), *sig);

		auto result = std::make_shared<::Assets::AssetFuture<LightResolveOperators>>("light-operators");
		result->SetPollingFunction(
			[pipelineFutures=std::move(pipelineFutures), fixedDescSetFuture, finalResult=std::move(finalResult)](::Assets::AssetFuture<LightResolveOperators>& future) -> bool {
				using namespace ::Assets;
				std::vector<std::shared_ptr<Metal::GraphicsPipeline>> actualized;
				actualized.resize(pipelineFutures.size());
				auto a=actualized.begin();
				Blob queriedLog;
				DependencyValidation queriedDepVal;
				for (const auto& p:pipelineFutures) {
					auto state = p->CheckStatusBkgrnd(*a, queriedDepVal, queriedLog);
					if (state != AssetState::Ready) {
						if (state == AssetState::Invalid) {
							future.SetInvalidAsset(queriedDepVal, queriedLog);
							return false;
						} else 
							return true;
					}
					++a;
				}

				{
					auto state = fixedDescSetFuture->CheckStatusBkgrnd(finalResult->_fixedDescriptorSet, queriedDepVal, queriedLog);
					if (state != AssetState::Ready) {
						if (state == AssetState::Invalid) {
							future.SetInvalidAsset(queriedDepVal, queriedLog);
							return false;
						} else 
							return true;
					}
				}

				finalResult->_operators.reserve(actualized.size());
				for (auto& a:actualized)
					finalResult->_operators.push_back({std::move(a), LightSourceOperatorDesc{}});

				UniformsStreamInterface sharedUsi;
				sharedUsi.BindFixedDescriptorSet(0, Utility::Hash64("SharedDescriptors"));

				UniformsStreamInterface usi;
				usi.BindFixedDescriptorSet(0, Utility::Hash64("ShadowTemplate"));
				usi.BindImmediateData(CB::GlobalTransform, Utility::Hash64("GlobalTransform"));
				usi.BindImmediateData(CB::LightBuffer, Utility::Hash64("LightBuffer"));
				usi.BindImmediateData(CB::Debugging, Utility::Hash64("Debugging"));
				usi.BindResourceView(SR::GBuffer_Diffuse, Utility::Hash64("GBuffer_Diffuse"));
				usi.BindResourceView(SR::GBuffer_Normals, Utility::Hash64("GBuffer_Normals"));
				usi.BindResourceView(SR::GBuffer_Parameters, Utility::Hash64("GBuffer_Parameters"));
				usi.BindResourceView(SR::DepthTexture, Utility::Hash64("DepthTexture"));
				finalResult->_boundUniforms = Metal::BoundUniforms {
					*finalResult->_pipelineLayout,
					usi, sharedUsi};

				future.SetAsset(decltype(finalResult){finalResult}, nullptr);
				return false;
			});
		return result;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void ResolveLights(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		Techniques::RenderPassInstance& rpi,
		const LightResolveOperators& lightResolveOperators,
		StandardLightScene& lightScene,
		IteratorRange<const std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>*> preparedShadows)
	{
		GPUAnnotation anno(threadContext, "Lights");

		IteratorRange<const void*> cbvs[CB::Max];

		const IResourceView* srvs[] = { nullptr, nullptr, nullptr, nullptr };
		static_assert(dimof(srvs)==SR::Max, "Shader resource array incorrect size");
		
		struct DebuggingGlobals
		{
			UInt2 viewportSize; 
			Int2 MousePosition;
		} debuggingGlobals;
		if (lightResolveOperators._debuggingOn) {
			auto vdesc = parsingContext.GetViewport();
			debuggingGlobals = { UInt2(unsigned(vdesc._width), unsigned(vdesc._height)), GetCursorPos() };
			cbvs[CB::Debugging] = MakeOpaqueIteratorRange(debuggingGlobals);
		}

		auto globalTransformUniforms = Techniques::BuildGlobalTransformConstants(parsingContext.GetProjectionDesc());
		cbvs[CB::GlobalTransform] = MakeOpaqueIteratorRange(globalTransformUniforms);
		srvs[SR::GBuffer_Diffuse] = rpi.GetInputAttachmentSRV(0);
		srvs[SR::GBuffer_Normals] = rpi.GetInputAttachmentSRV(1);
		srvs[SR::GBuffer_Parameters] = rpi.GetInputAttachmentSRV(2);
		srvs[SR::DepthTexture] = rpi.GetInputAttachmentSRV(3);

			////////////////////////////////////////////////////////////////////////

			//-------- do lights --------
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder(lightResolveOperators._pipelineLayout);
		auto& boundUniforms = lightResolveOperators._boundUniforms;

		IDescriptorSet* fixedDescSets[] = { lightResolveOperators._fixedDescriptorSet.get() };
		boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(fixedDescSets), 1);

		auto lightCount = lightScene._lights.size();
		auto shadowIterator = preparedShadows.begin();
		for (unsigned l=0; l<lightCount; ++l) {
			const auto& i = lightScene._lights[l];

			auto lightUniforms = Internal::MakeLightUniforms(i._desc);
			cbvs[CB::LightBuffer] = MakeOpaqueIteratorRange(lightUniforms);
			float debuggingDummy[4] = {};
			cbvs[CB::Debugging] = MakeIteratorRange(debuggingDummy);

			// while (shadowIterator != preparedShadows.end() && shadowIterator->first < l) ++shadowIterator;
			if (shadowIterator != preparedShadows.end() && shadowIterator->first == l) {
				IDescriptorSet* shadowDescSets[] = { shadowIterator->second->GetDescriptorSet().get() };
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(shadowDescSets));
				++shadowIterator;
			}

			auto& op = lightResolveOperators._operators[0];
			UniformsStream uniformsStream;
			uniformsStream._immediateData = MakeIteratorRange(cbvs);
			uniformsStream._resourceViews = MakeIteratorRange(srvs);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);
			encoder.Draw(*op._pipeline, 4);
		}
	}
   
}}


#include "../../Core/SelectConfiguration.h"
#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
	#include "../../OSServices/WinAPI/IncludeWindows.h"
	Int2 GetCursorPos()
	{
		POINT cursorPos;
		GetCursorPos(&cursorPos);
		ScreenToClient((HWND)::GetActiveWindow(), &cursorPos);
		return Int2(cursorPos.x, cursorPos.y);
	}
#endif

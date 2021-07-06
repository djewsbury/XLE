// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeferredLightingResolve.h"
#include "DeferredLightingDelegate.h"
#include "LightUniforms.h"
#include "StandardLightScene.h"
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
#include "../BufferView.h"

#include "../../Assets/AssetFuture.h"
#include "../../Assets/Assets.h"
#include "../../Utility/StringFormat.h"
#include "../../xleres/FileList.h"

#include "../../ConsoleRig/Console.h"

#include "../../Tools/ToolsRig/VisualisationGeo.h"

static Int2 GetCursorPos();

namespace RenderCore { namespace LightingEngine
{
	std::unique_ptr<ILightBase> LightResolveOperators::CreateLightSource(ILightScene::LightOperatorId opId)
	{
		StandardLightDesc::Flags::BitField flags = 0;
		if (_pipelines[opId]._stencilingGeoShape != LightSourceShape::Directional && !(_pipelines[opId]._flags & LightSourceOperatorDesc::Flags::NeverStencil))
			flags |= StandardLightDesc::Flags::SupportFiniteRange;
		return std::make_unique<StandardLightDesc>(flags);
	}

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

	::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> BuildLightResolveOperator(
		Techniques::GraphicsPipelinePool& pipelineCollection,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const LightSourceOperatorDesc& desc,
		const Internal::ShadowResolveParam& shadowResolveParam,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		GBufferType gbufferType)
	{
		auto sampleCount = TextureSamples::Create();
		auto mainOutputAttachment = fbDesc.GetSubpasses()[subpassIdx].GetOutputs()[0]._resourceName;
		if (fbDesc.GetAttachments()[mainOutputAttachment]._flags & AttachmentDesc::Flags::Multisampled)
			sampleCount = fbDesc.GetProperties()._samples;
		
		Techniques::VertexInputStates inputStates;
		MiniInputElementDesc inputElements[] = { {Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT} };

		Techniques::FrameBufferTarget fbTarget {&fbDesc, subpassIdx};

		Techniques::GraphicsPipelineDesc pipelineDesc;

		ParameterBox selectors;
		selectors.SetParameter("GBUFFER_TYPE", unsigned(gbufferType));
		selectors.SetParameter("MSAA_SAMPLES", (sampleCount._sampleCount<=1)?0:sampleCount._sampleCount);
		selectors.SetParameter("LIGHT_SHAPE", unsigned(desc._shape));
		selectors.SetParameter("DIFFUSE_METHOD", unsigned(desc._diffuseModel));
		selectors.SetParameter("HAS_SCREENSPACE_AO", unsigned(hasScreenSpaceAO));
		shadowResolveParam.WriteShaderSelectors(selectors);

		auto stencilRefValue = 0;

		const bool doSampleFrequencyOptimisation = Tweakable("SampleFrequencyOptimisation", true);
		if (doSampleFrequencyOptimisation && sampleCount._sampleCount > 1) {
			pipelineDesc._depthStencil = s_dsWritePixelFrequencyPixel;
			stencilRefValue = StencilSampleCount;
		} else {
			pipelineDesc._depthStencil = s_dsWriteNonSky;
			stencilRefValue = 0x0;
		}

		if ((desc._flags & LightSourceOperatorDesc::Flags::NeverStencil) || desc._shape == LightSourceShape::Directional) {
			inputStates._inputLayout = {};
			pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
			inputStates._topology = Topology::TriangleStrip;
		} else {
			inputStates._inputLayout = MakeIteratorRange(inputElements);
			pipelineDesc._shaders[(unsigned)ShaderStage::Vertex] = DEFERRED_RESOLVE_LIGHT_VERTEX_HLSL ":main";
			pipelineDesc._shaders[(unsigned)ShaderStage::Geometry] = BASIC_GEO_HLSL ":PT_viewfrustumVector_clipToNear";
			inputStates._topology = Topology::TriangleList;
			pipelineDesc._depthStencil._depthBoundsTestEnable = true;
		}

		pipelineDesc._rasterization = Techniques::CommonResourceBox::s_rsDefault;
		pipelineDesc._blend.push_back(Techniques::CommonResourceBox::s_abAdditive);
		pipelineDesc._shaders[(unsigned)ShaderStage::Pixel] = DEFERRED_RESOLVE_LIGHT_PIXEL_HLSL ":main";

		return pipelineCollection.CreatePipeline(
			pipelineLayout, pipelineDesc, selectors,
			inputStates, fbTarget);
	}

	::Assets::PtrToFuturePtr<IDescriptorSet> BuildFixedLightResolveDescriptorSet(
		std::shared_ptr<IDevice> device,
		const DescriptorSetSignature& descSetLayout)
	{
		auto balancedNoiseFuture = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/balanced_noise.dds:LT");

		auto result = std::make_shared<::Assets::FuturePtr<IDescriptorSet>>();
		::Assets::WhenAll(balancedNoiseFuture).ThenConstructToFuture(
			*result,
			[device, descSetLayout=descSetLayout](std::shared_ptr<RenderCore::Techniques::DeferredShaderResource> balancedNoise) {
				SamplerDesc shadowComparisonSamplerDesc { FilterMode::ComparisonBilinear, AddressMode::Border, AddressMode::Border, CompareOp::LessEqual };
				SamplerDesc shadowDepthSamplerDesc { FilterMode::Bilinear, AddressMode::Border, AddressMode::Border };
				auto shadowComparisonSampler = device->CreateSampler(shadowComparisonSamplerDesc);
				auto shadowDepthSampler = device->CreateSampler(shadowDepthSamplerDesc);

				DescriptorSetInitializer::BindTypeAndIdx bindTypes[4];
				bindTypes[0] = { DescriptorSetInitializer::BindType::Empty };
				bindTypes[1] = { DescriptorSetInitializer::BindType::ResourceView, 0 };
				bindTypes[2] = { DescriptorSetInitializer::BindType::Sampler, 0 };
				bindTypes[3] = { DescriptorSetInitializer::BindType::Sampler, 1 };
				IResourceView* srv[1] { balancedNoise->GetShaderResource().get() };
				ISampler* samplers[2] { shadowComparisonSampler.get(), shadowDepthSampler.get() };
				DescriptorSetInitializer inits;
				inits._slotBindings = MakeIteratorRange(bindTypes);
				inits._bindItems._resourceViews = MakeIteratorRange(srv);
				inits._bindItems._samplers = MakeIteratorRange(samplers);
				inits._signature = &descSetLayout;
				return device->CreateDescriptorSet(inits);
			});
		return result;
	}

	static bool IsCompatible(const LightSourceOperatorDesc& lightSource, const ShadowOperatorDesc& shadowOp)
	{
		switch (shadowOp._projectionMode) {
		case ShadowProjectionMode::Arbitrary:
		case ShadowProjectionMode::Ortho:
			return lightSource._shape == LightSourceShape::Directional || lightSource._shape == LightSourceShape::Rectangle || lightSource._shape == LightSourceShape::Disc;
		case ShadowProjectionMode::ArbitraryCubeMap:
			return lightSource._shape == LightSourceShape::Sphere || lightSource._shape == LightSourceShape::Tube;
		default:
			assert(0);
			return false;
		}
	}

	::Assets::PtrToFuturePtr<LightResolveOperators> BuildLightResolveOperators(
		Techniques::GraphicsPipelinePool& pipelineCollection,
		const std::shared_ptr<ICompiledPipelineLayout>& lightingOperatorLayout,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowOperators,
		const FrameBufferDesc& fbDesc,
		unsigned subpassIdx,
		bool hasScreenSpaceAO,
		GBufferType gbufferType)
	{
		struct AttachedData
		{
			LightSourceOperatorDesc::Flags::BitField _flags = 0;
			LightSourceShape _stencilingShape = LightSourceShape::Sphere;
		};
		using PipelineFuture = ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline>;
		std::vector<PipelineFuture> pipelineFutures;
		std::vector<AttachedData> attachedData;
		std::vector<std::tuple<ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned>> operatorToPipelineMap;
		
		pipelineFutures.reserve(resolveOperators.size() * (1+shadowOperators.size()));
		operatorToPipelineMap.reserve(resolveOperators.size() * (1+shadowOperators.size()));
		for (unsigned lightOperatorId=0; lightOperatorId!=resolveOperators.size(); ++lightOperatorId) {
			operatorToPipelineMap.push_back({lightOperatorId, ~0u, (unsigned)pipelineFutures.size()});
			pipelineFutures.push_back(
				BuildLightResolveOperator(
					pipelineCollection, lightingOperatorLayout,
					resolveOperators[lightOperatorId], Internal::ShadowResolveParam {}, 
					fbDesc, subpassIdx,
					hasScreenSpaceAO, 
					gbufferType));
			attachedData.push_back(AttachedData{resolveOperators[lightOperatorId]._flags, resolveOperators[lightOperatorId]._shape});
		}

		for (unsigned lightOperatorId=0; lightOperatorId!=resolveOperators.size(); ++lightOperatorId) {
			
			Internal::ShadowResolveParam shadowParams[shadowOperators.size()+1];
			unsigned shadowParamCount = 0;
			shadowParams[shadowParamCount++] = Internal::ShadowResolveParam { Internal::ShadowResolveParam::Shadowing::NoShadows };
			auto basePipelineIdx = (unsigned)pipelineFutures.size();

			for (unsigned shadowOperatorId=0; shadowOperatorId!=shadowOperators.size(); ++shadowOperatorId) {
				const auto& shadowOp = shadowOperators[shadowOperatorId];
				if (!IsCompatible(resolveOperators[lightOperatorId], shadowOp)) {
					operatorToPipelineMap.push_back({lightOperatorId, shadowOperatorId, lightOperatorId});
					continue;
				}

				auto param = Internal::MakeShadowResolveParam(shadowOp);
				bool foundExisting = false;
				for (unsigned c=0; c<shadowParamCount; ++c)
					if (shadowParams[c] == param) {
						foundExisting = true;
						operatorToPipelineMap.push_back({lightOperatorId, shadowOperatorId, basePipelineIdx+c});
						break;
					}
				if (!foundExisting) {
					operatorToPipelineMap.push_back({lightOperatorId, shadowOperatorId, basePipelineIdx+shadowParamCount});
					shadowParams[shadowParamCount++] = param;
				}
			}

			for (unsigned c=0; c<shadowParamCount; ++c) {
				pipelineFutures.push_back(
					BuildLightResolveOperator(
						pipelineCollection, lightingOperatorLayout,
						resolveOperators[lightOperatorId], shadowParams[c], 
						fbDesc, subpassIdx,
						hasScreenSpaceAO, 
						gbufferType));
				attachedData.push_back(AttachedData{resolveOperators[lightOperatorId]._flags, resolveOperators[lightOperatorId]._shape});
			}
		}

		auto finalResult = std::make_shared<LightResolveOperators>();
		finalResult->_pipelineLayout = lightingOperatorLayout;
		finalResult->_debuggingOn = false;

		const RenderCore::DescriptorSetSignature* sig = nullptr;
		auto layoutInitializer = finalResult->_pipelineLayout->GetInitializer();
		for (const auto& descSet:layoutInitializer.GetDescriptorSets())
			if (descSet._name == "SharedDescriptors")
				sig = &descSet._signature;
		if (!sig)
			Throw(std::runtime_error("No SharedDescriptors descriptor set in lighting operator pipeline layout"));

		auto fixedDescSetFuture = BuildFixedLightResolveDescriptorSet(pipelineCollection.GetDevice(), *sig);
		auto device = pipelineCollection.GetDevice();

		auto result = std::make_shared<::Assets::FuturePtr<LightResolveOperators>>("light-operators");
		result->SetPollingFunction(
			[pipelineFutures=std::move(pipelineFutures), fixedDescSetFuture, finalResult=std::move(finalResult), operatorToPipelineMap=std::move(operatorToPipelineMap), attachedData=std::move(attachedData), device=std::move(device)](::Assets::FuturePtr<LightResolveOperators>& future) -> bool {
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
					assert(*a);
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

				finalResult->_depVal = ::Assets::GetDepValSys().Make();
				finalResult->_pipelines.reserve(actualized.size());
				assert(actualized.size() == attachedData.size());
				for (unsigned c=0; c<actualized.size(); ++c) {
					finalResult->_depVal.RegisterDependency(actualized[c]->GetDependencyValidation());
					finalResult->_pipelines.push_back({std::move(actualized[c]), attachedData[c]._flags, attachedData[c]._stencilingShape});
				}
				finalResult->_operatorToPipelineMap = operatorToPipelineMap; 

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

				{
					auto sphereGeo = ToolsRig::BuildRoughGeodesicHemiSphereP(4);
					auto cubeGeo = ToolsRig::BuildCubeP();
					std::vector<uint8_t> geoInitBuffer;
					geoInitBuffer.resize((sphereGeo.size() + cubeGeo.size()) * sizeof(Float3));
					std::memcpy(geoInitBuffer.data(), cubeGeo.data(), cubeGeo.size() * sizeof(Float3));
					std::memcpy(PtrAdd(geoInitBuffer.data(), cubeGeo.size() * sizeof(Float3)), sphereGeo.data(), sphereGeo.size() * sizeof(Float3));
					finalResult->_stencilingGeometry = device->CreateResource(
						CreateDesc(BindFlag::VertexBuffer, 0, 0, LinearBufferDesc::Create(geoInitBuffer.size()), "light-stenciling-geometry"),
						SubResourceInitData{MakeIteratorRange(geoInitBuffer)});
					finalResult->_cubeOffsetAndCount = {0u, (unsigned)cubeGeo.size()};
					finalResult->_sphereOffsetAndCount = {(unsigned)cubeGeo.size(), (unsigned)sphereGeo.size()};
				} 

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

		const auto& projectionDesc = parsingContext.GetProjectionDesc();
		auto globalTransformUniforms = Techniques::BuildGlobalTransformConstants(projectionDesc);
		cbvs[CB::GlobalTransform] = MakeOpaqueIteratorRange(globalTransformUniforms);
		srvs[SR::GBuffer_Diffuse] = rpi.GetView(0).get();
		srvs[SR::GBuffer_Normals] = rpi.GetView(1).get();
		srvs[SR::GBuffer_Parameters] = rpi.GetView(2).get();
		srvs[SR::DepthTexture] = rpi.GetView(3).get();

			////////////////////////////////////////////////////////////////////////

			//-------- do lights --------
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder(lightResolveOperators._pipelineLayout);
		auto& boundUniforms = lightResolveOperators._boundUniforms;

		IDescriptorSet* fixedDescSets[] = { lightResolveOperators._fixedDescriptorSet.get() };
		boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(fixedDescSets), 1);

		VertexBufferView vbvs[] = {
			VertexBufferView { lightResolveOperators._stencilingGeometry.get() }
		};
		encoder.Bind(MakeIteratorRange(vbvs), {});

		AccurateFrustumTester frustumTester(projectionDesc._worldToProjection, Techniques::GetDefaultClipSpaceType());

		auto cameraForward = ExtractForward_Cam(projectionDesc._cameraToWorld);
		auto cameraRight = ExtractRight_Cam(projectionDesc._cameraToWorld);
		auto cameraUp = ExtractUp_Cam(projectionDesc._cameraToWorld);
		auto cameraPosition = ExtractTranslation(projectionDesc._cameraToWorld);
		assert(Equivalent(MagnitudeSquared(cameraForward), 1.0f, 1e-3f));

		auto lightCount = lightScene._lights.size();
		auto shadowIterator = preparedShadows.begin();
		for (unsigned l=0; l<lightCount; ++l) {
			const auto& i = lightScene._lights[l];

			assert(i._desc->QueryInterface(typeid(StandardLightDesc).hash_code()) == i._desc.get());
			auto& standardLightDesc = *(StandardLightDesc*)i._desc.get();

			auto lightShape = lightResolveOperators._pipelines[i._operatorId]._stencilingGeoShape;
			if (lightShape == LightSourceShape::Sphere) {
				// Lights can require a bit of setup and fiddling around on the GPU; so we'll try to
				// do an accurate culling check for them here... 
				auto cullResult = frustumTester.TestSphere(standardLightDesc._position, standardLightDesc._cutoffRange);
				if (cullResult == AABBIntersection::Culled)
					continue;
			}

			auto lightUniforms = Internal::MakeLightUniforms(standardLightDesc);
			cbvs[CB::LightBuffer] = MakeOpaqueIteratorRange(lightUniforms);
			float debuggingDummy[4] = {};
			cbvs[CB::Debugging] = MakeIteratorRange(debuggingDummy);

			const LightResolveOperators::Pipeline* pipeline;
			assert(i._operatorId < lightResolveOperators._pipelines.size());

			if (shadowIterator != preparedShadows.end() && shadowIterator->first == lightScene._lights[l]._id) {
				IDescriptorSet* shadowDescSets[] = { shadowIterator->second->GetDescriptorSet().get() };
				boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(shadowDescSets));

				auto shadowOperatorId = shadowIterator->second->GetShadowOperatorId();
				auto q = std::find_if(
					lightResolveOperators._operatorToPipelineMap.begin(), lightResolveOperators._operatorToPipelineMap.end(),
					[operatorId=i._operatorId, shadowOperatorId](const auto& c) {
						return std::get<0>(c) == operatorId && std::get<1>(c) == shadowOperatorId;
					});
				if (q != lightResolveOperators._operatorToPipelineMap.end()) {
					pipeline = &lightResolveOperators._pipelines[std::get<2>(*q)];
				} else {
					assert(0);		// couldn't find the mapping for this light operator and shadow operator pair
					pipeline = &lightResolveOperators._pipelines[i._operatorId];
				}
				++shadowIterator;
			} else {
				// If you hit the following assert it probably means the preparedShadows are not sorted by lightId,
				// or the lights in the light scene are not sorted in id order, or there's a prepared shadow
				// generated for a light that doesn't exist
				assert(shadowIterator == preparedShadows.end() || shadowIterator->first > lightScene._lights[l]._id);
				pipeline = &lightResolveOperators._pipelines[i._operatorId];
			}

			UniformsStream uniformsStream;
			uniformsStream._immediateData = MakeIteratorRange(cbvs);
			uniformsStream._resourceViews = MakeIteratorRange(srvs);
			boundUniforms.ApplyLooseUniforms(metalContext, encoder, uniformsStream);

			if ((pipeline->_flags & LightSourceOperatorDesc::Flags::NeverStencil) || pipeline->_stencilingGeoShape == LightSourceShape::Directional) {
				encoder.Draw(*pipeline->_pipeline, 4);
			} else {
				if (pipeline->_stencilingGeoShape == LightSourceShape::Sphere) {
					// We need to calculate the correct min and max depth of sphere projected into clip space
					// Here the smallest and largest depth value aren't necessarily at the points that are closest and furtherest
					// from the camera; but rather by the pointer intersected by the camera forward direction through the center
					// (at least assuming the entire sphere is onscreen)
					// I suppose we could reduce this depth range when we know that the center point is onscreen
					Float4 extremePoint0 = projectionDesc._worldToProjection * Float4{standardLightDesc._position + cameraForward * standardLightDesc._cutoffRange, 1.0f};
					Float4 extremePoint1 = projectionDesc._worldToProjection * Float4{standardLightDesc._position - cameraForward * standardLightDesc._cutoffRange, 1.0f};
					float d0 = extremePoint0[2] / std::abs(extremePoint0[3]), d1 = extremePoint1[2] / std::abs(extremePoint1[3]);
					encoder.SetDepthBounds(std::max(0.f, std::min(d0, d1)), std::min(1.f, std::max(d0, d1)));

					// We only need the front faces of the sphere. There are some special problems when the camera is inside of the sphere,
					// though, but in that case we can flatten the front of the sphere to the near clip plane
					encoder.Draw(*pipeline->_pipeline, lightResolveOperators._sphereOffsetAndCount.second, lightResolveOperators._sphereOffsetAndCount.first);
				} else {
					assert(0);
				}
			}
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

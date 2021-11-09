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
	std::unique_ptr<Internal::ILightBase> LightResolveOperators::CreateLightSource(ILightScene::LightOperatorId opId)
	{
		Internal::StandardPositionalLight::Flags::BitField flags = 0;
		if (_pipelines[opId]._stencilingGeoShape != LightSourceShape::Directional && !(_pipelines[opId]._flags & LightSourceOperatorDesc::Flags::NeverStencil))
			flags |= Internal::StandardPositionalLight::Flags::SupportFiniteRange;
		return std::make_unique<Internal::StandardPositionalLight>(flags);
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
		CompareOp::GreaterEqual, false,
		true, StencilSky|StencilSampleCount, 0xff, 
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Equal},
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Less}};

	static DepthStencilDesc s_dsWriteNonSky {
		CompareOp::GreaterEqual, false,
		true, StencilSky, 0xff, 
		StencilDesc{StencilOp::DontWrite, StencilOp::DontWrite, StencilOp::DontWrite, CompareOp::Equal}};

	static std::shared_ptr<::Assets::Future<Techniques::GraphicsPipelineAndLayout>> BuildLightResolveOperator(
		Techniques::PipelineCollection& pipelineCollection,
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

		auto pipelineDesc = std::make_shared<Techniques::GraphicsPipelineDesc>();

		ParameterBox selectors;
		selectors.SetParameter("GBUFFER_TYPE", unsigned(gbufferType));
		selectors.SetParameter("MSAA_SAMPLES", (sampleCount._sampleCount<=1)?0:sampleCount._sampleCount);
		selectors.SetParameter("LIGHT_SHAPE", unsigned(desc._shape));
		selectors.SetParameter("DIFFUSE_METHOD", unsigned(desc._diffuseModel));
		selectors.SetParameter("HAS_SCREENSPACE_AO", unsigned(hasScreenSpaceAO));
		selectors.SetParameter("LIGHT_RESOLVE_SHADER", 1);
		shadowResolveParam.WriteShaderSelectors(selectors);

		auto stencilRefValue = 0;

		const bool doSampleFrequencyOptimisation = Tweakable("SampleFrequencyOptimisation", true);
		if (doSampleFrequencyOptimisation && sampleCount._sampleCount > 1) {
			pipelineDesc->_depthStencil = s_dsWritePixelFrequencyPixel;
			stencilRefValue = StencilSampleCount;
		} else {
			pipelineDesc->_depthStencil = s_dsWriteNonSky;
			stencilRefValue = 0x0;
		}

		if ((desc._flags & LightSourceOperatorDesc::Flags::NeverStencil) || desc._shape == LightSourceShape::Directional) {
			inputStates._inputAssembly = {};
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector";
			inputStates._topology = Topology::TriangleStrip;
		} else {
			inputStates._miniInputAssembly = MakeIteratorRange(inputElements);
			pipelineDesc->_shaders[(unsigned)ShaderStage::Vertex] = DEFERRED_LIGHT_OPERATOR_VERTEX_HLSL ":main";
			pipelineDesc->_shaders[(unsigned)ShaderStage::Geometry] = BASIC_GEO_HLSL ":ClipToNear";
			inputStates._topology = Topology::TriangleList;
			pipelineDesc->_depthStencil._depthBoundsTestEnable = true;
			pipelineDesc->_manualSelectorFiltering._setValues.SetParameter("GS_FVF", 1);
		}

		pipelineDesc->_rasterization = Techniques::CommonResourceBox::s_rsDefault;
		pipelineDesc->_blend.push_back(Techniques::CommonResourceBox::s_abAdditive);
		pipelineDesc->_shaders[(unsigned)ShaderStage::Pixel] = DEFERRED_LIGHT_OPERATOR_PIXEL_HLSL ":main";

		const ParameterBox* selectorList[] { &selectors };
		return pipelineCollection.CreateGraphicsPipeline(
			pipelineLayout, pipelineDesc, MakeIteratorRange(selectorList),
			inputStates, fbTarget);
	}

	::Assets::PtrToFuturePtr<IDescriptorSet> BuildFixedLightResolveDescriptorSet(
		std::shared_ptr<IDevice> device,
		const DescriptorSetSignature& descSetLayout)
	{
		auto balancedNoiseFuture = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>("xleres/DefaultResources/balanced_noise.dds:LT");

		auto result = std::make_shared<::Assets::FuturePtr<IDescriptorSet>>();
		::Assets::WhenAll(balancedNoiseFuture).ThenConstructToPromise(
			result->AdoptPromise(),
			[device, descSetLayout=descSetLayout](std::shared_ptr<RenderCore::Techniques::DeferredShaderResource> balancedNoise) {
				DescriptorSetInitializer::BindTypeAndIdx bindTypes[4];
				bindTypes[0] = { DescriptorSetInitializer::BindType::Empty };
				bindTypes[1] = { DescriptorSetInitializer::BindType::ResourceView, 0 };
				IResourceView* srv[1] { balancedNoise->GetShaderResource().get() };
				DescriptorSetInitializer inits;
				inits._slotBindings = MakeIteratorRange(bindTypes);
				inits._bindItems._resourceViews = MakeIteratorRange(srv);
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
		Techniques::PipelineCollection& pipelineCollection,
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
		using PipelineFuture = std::shared_ptr<::Assets::Future<Techniques::GraphicsPipelineAndLayout>>;
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
		finalResult->_operatorDescs = {resolveOperators.begin(), resolveOperators.end()};

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
				std::vector<Techniques::GraphicsPipelineAndLayout> actualized;
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
					assert(a->_pipeline);
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
					finalResult->_depVal.RegisterDependency(actualized[c].GetDependencyValidation());
					finalResult->_pipelines.push_back({std::move(actualized[c]._pipeline), attachedData[c]._flags, attachedData[c]._stencilingShape});
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

				finalResult->_stencilingGeometry = LightStencilingGeometry(*device);

				future.SetAsset(decltype(finalResult){finalResult});
				return false;
			});
		return result;
	}

	LightStencilingGeometry::LightStencilingGeometry(IDevice& device)
	{
		auto sphereGeo = ToolsRig::BuildRoughGeodesicHemiSphereP(4);
		auto cubeGeo = ToolsRig::BuildCubeP();
		std::vector<uint8_t> geoInitBuffer;
		geoInitBuffer.resize((sphereGeo.size() + cubeGeo.size()) * sizeof(Float3));
		std::memcpy(geoInitBuffer.data(), cubeGeo.data(), cubeGeo.size() * sizeof(Float3));
		std::memcpy(PtrAdd(geoInitBuffer.data(), cubeGeo.size() * sizeof(Float3)), sphereGeo.data(), sphereGeo.size() * sizeof(Float3));
		_geo = device.CreateResource(
			CreateDesc(BindFlag::VertexBuffer, 0, 0, LinearBufferDesc::Create(geoInitBuffer.size()), "light-stenciling-geometry"),
			SubResourceInitData{MakeIteratorRange(geoInitBuffer)});
		_cubeOffsetAndCount = {0u, (unsigned)cubeGeo.size()};
		_sphereOffsetAndCount = {(unsigned)cubeGeo.size(), (unsigned)sphereGeo.size()};

		auto lowDetailHemi = ToolsRig::BuildIndexedRoughGeodesicHemiSphereP(0);
		// float b = tan(pi/6)/tan(2*pi/6);
		// float c = 1.0/cos(pi/6);
		// float underestimationFactor = c*sin(pi/3)/(1+b);
		//								= sin(pi/3)*cos(pi/6);
		float underestimationFactor = std::sin(gPI/3.0f) * std::cos(gPI/6.0f);
		for (auto& pt:lowDetailHemi.second) pt /= underestimationFactor;
		_lowDetailHemiSphereVB = device.CreateResource(
			CreateDesc(BindFlag::VertexBuffer, 0, 0, LinearBufferDesc::Create(sizeof(decltype(lowDetailHemi.second)::value_type)*lowDetailHemi.second.size()), "light-stenciling-geometry"),
			SubResourceInitData{MakeIteratorRange(lowDetailHemi.second)});
		std::vector<uint16_t> ib; ib.reserve(lowDetailHemi.first.size());
		std::copy(lowDetailHemi.first.begin(), lowDetailHemi.first.end(), std::back_inserter(ib));
		_lowDetailHemiSphereIB = device.CreateResource(
			CreateDesc(BindFlag::IndexBuffer, 0, 0, LinearBufferDesc::Create(sizeof(uint16_t)*ib.size()), "light-stenciling-geometry"),
			SubResourceInitData{MakeIteratorRange(ib)});
		_lowDetailHemiSphereIndexCount = (unsigned)ib.size();
	} 

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void ResolveLights(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		Techniques::RenderPassInstance& rpi,
		const LightResolveOperators& lightResolveOperators,
		Internal::StandardLightScene& lightScene,
		IteratorRange<const PreparedShadow*> preparedShadows)
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
		srvs[SR::GBuffer_Diffuse] = rpi.GetInputAttachmentView(0).get();
		srvs[SR::GBuffer_Normals] = rpi.GetInputAttachmentView(1).get();
		srvs[SR::GBuffer_Parameters] = rpi.GetInputAttachmentView(2).get();
		srvs[SR::DepthTexture] = rpi.GetInputAttachmentView(3).get();

			////////////////////////////////////////////////////////////////////////

			//-------- do lights --------
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder(lightResolveOperators._pipelineLayout);
		auto& boundUniforms = lightResolveOperators._boundUniforms;

		IDescriptorSet* fixedDescSets[] = { lightResolveOperators._fixedDescriptorSet.get() };
		boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(fixedDescSets), 1);

		VertexBufferView vbvs[] = {
			VertexBufferView { lightResolveOperators._stencilingGeometry._geo.get() }
		};
		encoder.Bind(MakeIteratorRange(vbvs), {});

		AccurateFrustumTester frustumTester(projectionDesc._worldToProjection, Techniques::GetDefaultClipSpaceType());

		auto cameraForward = ExtractForward_Cam(projectionDesc._cameraToWorld);
		auto cameraRight = ExtractRight_Cam(projectionDesc._cameraToWorld);
		auto cameraUp = ExtractUp_Cam(projectionDesc._cameraToWorld);
		auto cameraPosition = ExtractTranslation(projectionDesc._cameraToWorld);
		assert(Equivalent(MagnitudeSquared(cameraForward), 1.0f, 1e-3f));

		for (const auto&set:lightScene._tileableLightSets) {
			auto lightShape = lightResolveOperators._pipelines[set._operatorId]._stencilingGeoShape;
			auto shadowOperatorId = set._shadowOperatorId;
			auto lightOperatorId = set._operatorId;
		
			const LightResolveOperators::Pipeline* pipeline;
			if (shadowOperatorId != ~0u) {
				auto q = std::find_if(
					lightResolveOperators._operatorToPipelineMap.begin(), lightResolveOperators._operatorToPipelineMap.end(),
					[lightOperatorId, shadowOperatorId](const auto& c) {
						return std::get<0>(c) == lightOperatorId && std::get<1>(c) == shadowOperatorId;
					});
				if (q != lightResolveOperators._operatorToPipelineMap.end()) {
					pipeline = &lightResolveOperators._pipelines[std::get<2>(*q)];
				} else {
					assert(0);		// couldn't find the mapping for this light operator and shadow operator pair
					pipeline = &lightResolveOperators._pipelines[lightOperatorId];
				}
			} else {
				pipeline = &lightResolveOperators._pipelines[lightOperatorId];
			}

			auto shadowIterator = preparedShadows.begin();
			for (unsigned l=0; l<set._lights.size(); ++l) {
				const auto& i = set._lights[l];

				assert(i._desc->QueryInterface(typeid(Internal::StandardPositionalLight).hash_code()) == i._desc.get());
				auto& standardLightDesc = *(Internal::StandardPositionalLight*)i._desc.get();

				if (lightShape == LightSourceShape::Sphere) {
					// Lights can require a bit of setup and fiddling around on the GPU; so we'll try to
					// do an accurate culling check for them here... 
					auto cullResult = frustumTester.TestSphere(standardLightDesc._position, standardLightDesc._cutoffRange);
					if (cullResult == CullTestResult::Culled)
						continue;
				}

				auto lightUniforms = Internal::MakeLightUniforms(standardLightDesc, lightResolveOperators._operatorDescs[set._operatorId]);
				cbvs[CB::LightBuffer] = MakeOpaqueIteratorRange(lightUniforms);
				float debuggingDummy[4] = {};
				cbvs[CB::Debugging] = MakeIteratorRange(debuggingDummy);
				
				assert(set._operatorId < lightResolveOperators._pipelines.size());

				while (shadowIterator != preparedShadows.end() && shadowIterator->_lightId < i._id) ++shadowIterator;
				if (shadowIterator != preparedShadows.end() && shadowIterator->_lightId == i._id) {
					IDescriptorSet* shadowDescSets[] = { shadowIterator->_preparedResult->GetDescriptorSet().get() };
					boundUniforms.ApplyDescriptorSets(metalContext, encoder, MakeIteratorRange(shadowDescSets));
					++shadowIterator;
				} else {
					// If you hit the following assert it probably means the preparedShadows are not sorted by lightId,
					// or the lights in the light scene are not sorted in id order, or there's a prepared shadow
					// generated for a light that doesn't exist
					assert(shadowIterator == preparedShadows.end() || shadowIterator->_lightId > i._id);
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
						float d0 = extremePoint0[2] / extremePoint0[3], d1 = extremePoint1[2] / extremePoint1[3];
						encoder.SetDepthBounds(std::max(0.f, std::min(d0, d1)), std::min(1.f, std::max(d0, d1)));

						// We only need the front faces of the sphere. There are some special problems when the camera is inside of the sphere,
						// though, but in that case we can flatten the front of the sphere to the near clip plane
						encoder.Draw(*pipeline->_pipeline, lightResolveOperators._stencilingGeometry._sphereOffsetAndCount.second, lightResolveOperators._stencilingGeometry._sphereOffsetAndCount.first);
					} else {
						assert(0);
					}
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

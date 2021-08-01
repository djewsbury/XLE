// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ForwardLightingDelegate.h"
#include "LightingEngineInternal.h"
#include "LightingEngineApparatus.h"
#include "LightUniforms.h"
#include "ShadowPreparer.h"
#include "RenderStepFragments.h"
#include "StandardLightScene.h"
#include "HierarchicalDepths.h"
#include "ScreenSpaceReflections.h"
#include "LightTiler.h"
#include "SHCoefficients.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Assets/TextureCompiler.h"
#include "../IThreadContext.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../xleres/FileList.h"

#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Shader.h"

namespace RenderCore { namespace LightingEngine
{
	class AmbientLight
	{
	public:
		::Assets::PtrToFuturePtr<Techniques::DeferredShaderResource> _specularIBL;
		::Assets::PtrToFuturePtr<Techniques::DeferredShaderResource> _ambientRawCubemap;
		::Assets::PtrToFuturePtr<SHCoefficientsAsset> _diffuseIBL;

		enum class SourceImageType { Equirectangular };
		SourceImageType _sourceImageType = SourceImageType::Equirectangular;
		std::string _sourceImage;

		AmbientLightOperatorDesc _ambientLightOperator;

		void SetEquirectangularSource(StringSection<> input)
		{
			if (XlEqString(input, _sourceImage)) return;
			_sourceImage = input.AsString();
			_sourceImageType = SourceImageType::Equirectangular;
			_diffuseIBL = ::Assets::MakeAsset<SHCoefficientsAsset>(input);

			Assets::TextureCompilationRequest request;
			request._operation = Assets::TextureCompilationRequest::Operation::EquiRectFilterGlossySpecular; 
			request._srcFile = _sourceImage;
			request._format = Format::BC6H_UF16;
			request._faceDim = 512;
			_specularIBL = ::Assets::MakeFuture<std::shared_ptr<Techniques::DeferredShaderResource>>(request);

			Assets::TextureCompilationRequest request2;
			request2._operation = Assets::TextureCompilationRequest::Operation::EquRectToCubeMap; 
			request2._srcFile = _sourceImage;
			request2._format = Format::BC6H_UF16;
			request2._faceDim = 256;
			_ambientRawCubemap = ::Assets::MakeFuture<std::shared_ptr<Techniques::DeferredShaderResource>>(request2);
		}
	};

	class BuildGBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		virtual const UniformsStreamInterface& GetInterface() { return _interf; }

        virtual void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst)
		{
			assert(bindingFlags == 1<<0);
			dst[0] = _normalsFitting.get();
		}

		BuildGBufferResourceDelegate(Techniques::DeferredShaderResource& normalsFittingResource)
		{
			_interf.BindResourceView(0, Utility::Hash64("NormalsFittingTexture"));
			_normalsFitting = normalsFittingResource.GetShaderResource();
		}
		UniformsStreamInterface _interf;
		std::shared_ptr<IResourceView> _normalsFitting;
	};

	static const uint64_t s_shadowTemplate = Utility::Hash64("ShadowTemplate");

	class ForwardPlusLightScene : public Internal::StandardLightScene, public IDistantIBLSource, public ISSAmbientOcclusion, public Techniques::IShaderResourceDelegate, public std::enable_shared_from_this<ForwardPlusLightScene>
	{
	public:
		std::vector<LightSourceOperatorDesc> _positionalLightOperators;
		std::shared_ptr<ScreenSpaceReflectionsOperator> _ssrOperator;
		std::shared_ptr<RasterizationLightTileOperator> _lightTiler;
		std::shared_ptr<HierarchicalDepthsOperator> _hierarchicalDepthsOperator;
		std::shared_ptr<IDevice> _device;

		std::shared_ptr<ShadowPreparationOperators> _shadowPreparationOperators;
		std::vector<std::pair<unsigned, std::shared_ptr<IPreparedShadowResult>>> _preparedShadows;

		AmbientLight _ambientLight;
		bool _ambientLightEnabled = false;
		AmbientLightOperatorDesc _ambientLightOperator;

		LightOperatorId _dominantLightOperatorId = ~0u;
		ShadowOperatorId _dominantLightShadowOperator = ~0u;

		Float4 _diffuseSHCoefficients[25];

		BufferUploads::CommandListID _completionCommandListID = 0;

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

		void FinalizeConfiguration()
		{
			auto tilerConfig = _lightTiler->GetConfiguration();
			for (unsigned c=0; c<dimof(_uniforms); c++) {
				_uniforms[c]._propertyCB = _device->CreateResource(
					CreateDesc(BindFlag::ConstantBuffer, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_EnvironmentProps)), "env-props"));
				_uniforms[c]._propertyCBView = _uniforms[c]._propertyCB->CreateBufferView(BindFlag::ConstantBuffer);

				_uniforms[c]._lightList = _device->CreateResource(
					CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(Internal::CB_Light)*tilerConfig._maxLightsPerView, sizeof(Internal::CB_Light)), "light-list"));
				_uniforms[c]._lightListUAV = _uniforms[c]._lightList->CreateBufferView(BindFlag::UnorderedAccess);

				_uniforms[c]._lightDepthTable = _device->CreateResource(
					CreateDesc(BindFlag::UnorderedAccess, CPUAccess::Write, 0, LinearBufferDesc::Create(sizeof(unsigned)*tilerConfig._depthLookupGradiations, sizeof(unsigned)), "light-depth-table"));
				_uniforms[c]._lightDepthTableUAV = _uniforms[c]._lightDepthTable->CreateBufferView(BindFlag::UnorderedAccess);
			}
			_pingPongCounter = 0;

			// Default to using the first light operator & first shadow operator for the dominant light
			if (!_positionalLightOperators.empty()) _dominantLightOperatorId = 0;
			if (!_shadowPreparationOperators->_operators.empty()) _dominantLightShadowOperator = 0;

			_usi.BindResourceView(0, Utility::Hash64("LightDepthTable"));
			_usi.BindResourceView(1, Utility::Hash64("LightList"));
			_usi.BindResourceView(2, Utility::Hash64("EnvironmentProps"));
			_usi.BindResourceView(3, Utility::Hash64("TiledLightBitField"));
			_usi.BindResourceView(4, Utility::Hash64("SSR"));
		}

		virtual LightSourceId CreateLightSource(ILightScene::LightOperatorId opId) override
		{
			if (opId == _positionalLightOperators.size()) {
				if (_ambientLightEnabled)
					Throw(std::runtime_error("Attempting to create multiple ambient light sources. Only one is supported at a time"));
				_ambientLightEnabled = true;
				return 0;
			}
			auto desc = std::make_unique<Internal::StandardLightDesc>(Internal::StandardLightDesc::Flags::SupportFiniteRange);
			return AddLightSource(opId, std::move(desc));
		}

		virtual void DestroyLightSource(LightSourceId sourceId) override
		{
			if (sourceId == 0) {
				if (!_ambientLightEnabled)
					Throw(std::runtime_error("Attempting to destroy the ambient light source, but it has not been created"));
				_ambientLightEnabled = false;
			} else {
				Internal::StandardLightScene::DestroyLightSource(sourceId);
			}
		}

		virtual void Clear() override
		{
			_ambientLightEnabled = false;
			Internal::StandardLightScene::Clear();
		}

		virtual ShadowProjectionId CreateShadowProjection(ShadowOperatorId opId, LightSourceId associatedLight) override
		{
			auto desc = _shadowPreparationOperators->CreateShadowProjection(opId);
			return AddShadowProjection(opId, associatedLight, std::move(desc));
		}

		virtual void* TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode) override
		{
			if (sourceId == 0) {
				if (interfaceTypeCode == typeid(IDistantIBLSource).hash_code()) return (IDistantIBLSource*)this;
				if (interfaceTypeCode == typeid(ISSAmbientOcclusion).hash_code()) return (ISSAmbientOcclusion*)this;
				return nullptr;
			} else {
				return Internal::StandardLightScene::TryGetLightSourceInterface(sourceId, interfaceTypeCode);
			}
		}

		virtual void SetEquirectangularSource(StringSection<> input) override
		{
			if (XlEqString(input, _ambientLight._sourceImage)) return;
			_ambientLight.SetEquirectangularSource(input);
			auto weakThis = weak_from_this();
			::Assets::WhenAll(_ambientLight._specularIBL, _ambientLight._diffuseIBL, _ambientLight._ambientRawCubemap).Then(
				[weakThis](auto specularIBLFuture, auto diffuseIBLFuture, auto ambientRawCubemapFuture) {
					auto l = weakThis.lock();
					if (!l) return;
					if (specularIBLFuture->GetAssetState() != ::Assets::AssetState::Ready
						|| diffuseIBLFuture->GetAssetState() != ::Assets::AssetState::Ready) {
						l->_ssrOperator->SetSpecularIBL(nullptr);
						std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
					} else {
						auto ambientRawCubemap = ambientRawCubemapFuture->Actualize();
						l->_ssrOperator->SetSpecularIBL(ambientRawCubemap->GetShaderResource());
						auto actualDiffuse = diffuseIBLFuture->Actualize();
						std::memset(l->_diffuseSHCoefficients, 0, sizeof(l->_diffuseSHCoefficients));
						std::memcpy(l->_diffuseSHCoefficients, actualDiffuse->GetCoefficients().begin(), sizeof(Float4*)*std::min(actualDiffuse->GetCoefficients().size(), dimof(l->_diffuseSHCoefficients)));
						l->_completionCommandListID = std::max(l->_completionCommandListID, ambientRawCubemap->GetCompletionCommandList());
					}
				});
		}

		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
		{
			/////////////////
			_pingPongCounter = (_pingPongCounter+1)%dimof(_uniforms);

			auto& uniforms = _uniforms[_pingPongCounter];
			auto& tilerOutputs = _lightTiler->_outputs;
			{
				Metal::ResourceMap map(
					*_device, *uniforms._lightDepthTable,
					Metal::ResourceMap::Mode::WriteDiscardPrevious, 
					0, sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
				std::memcpy(map.GetData().begin(), tilerOutputs._lightDepthTable.data(), sizeof(unsigned)*tilerOutputs._lightDepthTable.size());
			}
			if (tilerOutputs._lightCount) {
				Metal::ResourceMap map(
					*_device, *uniforms._lightList,
					Metal::ResourceMap::Mode::WriteDiscardPrevious, 
					0, sizeof(Internal::CB_Light)*tilerOutputs._lightCount);
				auto* i = (Internal::CB_Light*)map.GetData().begin();
				auto end = tilerOutputs._lightOrdering.begin() + tilerOutputs._lightCount;
				for (auto idx=tilerOutputs._lightOrdering.begin(); idx!=end; ++idx, ++i) {
					auto set = *idx >> 16, light = (*idx)&0xffff;
					auto op = _lightSets[set]._operatorId;
					*i = MakeLightUniforms(
						*(Internal::StandardLightDesc*)_lightSets[set]._lights[light]._desc.get(),
						_positionalLightOperators[op]);
				}
			}

			{
				Metal::ResourceMap map(
					*_device, *uniforms._propertyCB,
					Metal::ResourceMap::Mode::WriteDiscardPrevious);
				auto* i = (Internal::CB_EnvironmentProps*)map.GetData().begin();
				i->_dominantLight = {};

				if (_dominantLightOperatorId != ~0u) {
					auto& dominantLightSet = GetLightSet(_dominantLightOperatorId, _dominantLightShadowOperator);
					if (dominantLightSet._lights.size() > 1)
						Throw(std::runtime_error("Multiple lights in the non-tiled dominant light category. There can be only one dominant light, but it can support more features than the tiled lights"));
					if (!dominantLightSet._lights.empty())
						i->_dominantLight = Internal::MakeLightUniforms(
							*checked_cast<Internal::StandardLightDesc*>(dominantLightSet._lights[0]._desc.get()),
							_positionalLightOperators[_dominantLightOperatorId]);
				}

				i->_lightCount = tilerOutputs._lightCount;
				std::memcpy(i->_diffuseSHCoefficients, _diffuseSHCoefficients, sizeof(_diffuseSHCoefficients));
			}

			if (_completionCommandListID)
				parsingContext.RequireCommandList(_completionCommandListID);
			// parsingContext.AddShaderResourceDelegate(shared_from_this());

			if (_dominantLightShadowOperator != ~0u) {
				assert(!parsingContext._extraSequencerDescriptorSet.second);
				parsingContext._extraSequencerDescriptorSet = {s_shadowTemplate, _preparedShadows[0].second->GetDescriptorSet().get()};
			}
		}

		void SetupProjection(Techniques::ParsingContext& parsingContext)
		{
			if (_hasPrevProjection) {
				parsingContext.GetPrevProjectionDesc() = _prevProjDesc;
				parsingContext.GetEnablePrevProjectionDesc() = true;
			}
			_prevProjDesc = parsingContext.GetProjectionDesc();
			_hasPrevProjection = true;
		}

		const UniformsStreamInterface& GetInterface() override { return _usi; }
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			assert(dst.size() >= 4);
			auto& uniforms = _uniforms[_pingPongCounter];
			dst[0] = uniforms._lightDepthTableUAV.get();
			dst[1] = uniforms._lightListUAV.get();
			dst[2] = uniforms._propertyCBView.get();
			dst[3] = _lightTiler->_outputs._tiledLightBitFieldSRV.get();
			if (bindingFlags & (1ull<<4ull)) {
				assert(context._rpi);
				dst[4] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
			}
		}

		RenderStepFragmentInterface CreateDepthMotionFragment(
			std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionDelegate)
		{
			RenderStepFragmentInterface result { PipelineType::Graphics };
			Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
			preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion, LoadStore::Clear));
			preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Clear));
			preDepthSubpass.SetName("PreDepth");
			result.AddSubpass(std::move(preDepthSubpass), depthMotionDelegate, Techniques::BatchFilter::General);
			return result;
		}

		RenderStepFragmentInterface CreateDepthMotionNormalFragment(
			std::shared_ptr<Techniques::ITechniqueDelegate> depthMotionNormalDelegate)
		{
			RenderStepFragmentInterface result { PipelineType::Graphics };
			Techniques::FrameBufferDescFragment::SubpassDesc preDepthSubpass;
			preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion, LoadStore::Clear));
			preDepthSubpass.AppendOutput(result.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal, LoadStore::Clear));
			preDepthSubpass.SetDepthStencil(result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth, LoadStore::Clear));
			preDepthSubpass.SetName("PreDepth");

			auto normalsFittingTexture = ::Assets::MakeAsset<Techniques::DeferredShaderResource>(NORMALS_FITTING_TEXTURE);
			normalsFittingTexture->StallWhilePending();
			auto srDelegate = std::make_shared<BuildGBufferResourceDelegate>(*normalsFittingTexture->Actualize());

			result.AddSubpass(std::move(preDepthSubpass), depthMotionNormalDelegate, Techniques::BatchFilter::General, {}, std::move(srDelegate));
			return result;
		}

		RenderStepFragmentInterface CreateForwardSceneFragment(
			std::shared_ptr<Techniques::ITechniqueDelegate> forwardIllumDelegate)
		{
			RenderStepFragmentInterface result { PipelineType::Graphics };
			auto lightResolve = result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Clear);
			auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth);

			Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
			mainSubpass.AppendOutput(lightResolve);
			mainSubpass.SetDepthStencil(depth);

			const bool hasSSR = true;
			if (hasSSR) {
				auto ssr = result.DefineAttachment(Utility::Hash64("SSRReflections"), LoadStore::Retain, LoadStore::DontCare);
				mainSubpass.AppendNonFrameBufferAttachmentView(ssr);
			}
			mainSubpass.SetName("MainForward");

			ParameterBox box;
			if (_dominantLightOperatorId != ~0u) {
				box.SetParameter("DOMINANT_LIGHT_SHAPE", (unsigned)_positionalLightOperators[_dominantLightOperatorId]._shape);
				if (_dominantLightShadowOperator != ~0u) {
					auto resolveParam = Internal::MakeShadowResolveParam(_shadowPreparationOperators->_operators[_dominantLightShadowOperator]._desc);
					resolveParam.WriteShaderSelectors(box);
				}
			}

			result.AddSubpass(std::move(mainSubpass), forwardIllumDelegate, Techniques::BatchFilter::General, std::move(box), shared_from_this());
			return result;
		}

		ForwardPlusLightScene(const AmbientLightOperatorDesc& ambientLightOperator)
		: _ambientLightOperator(ambientLightOperator)
		{
			// We'll maintain the first few ids for system lights (ambient surrounds, etc)
			ReserveLightSourceIds(32);
			std::memset(_diffuseSHCoefficients, 0, sizeof(_diffuseSHCoefficients));
		}

	private:
		UniformsStreamInterface _usi;
		bool _hasPrevProjection = false;
		Techniques::ProjectionDesc _prevProjDesc;
	};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class ForwardLightingCaptures
	{
	public:
		std::shared_ptr<Techniques::FrameBufferPool> _shadowGenFrameBufferPool;
		std::shared_ptr<Techniques::AttachmentPool> _shadowGenAttachmentPool;
		std::shared_ptr<ForwardPlusLightScene> _lightScene;

		/*class UniformsDelegate : public Techniques::IShaderResourceDelegate
		{
		public:
			virtual const UniformsStreamInterface& GetInterface() override { return _interface; }
			void WriteImmediateData(Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
			{
				assert(idx==0);
				assert(dst.size() == sizeof(Internal::CB_BasicEnvironment));
				*(Internal::CB_BasicEnvironment*)dst.begin() = Internal::MakeBasicEnvironmentUniforms(EnvironmentalLightingDesc{});
			}

			size_t GetImmediateDataSize(Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
			{
				assert(idx==0);
				return sizeof(Internal::CB_BasicEnvironment);
			}
		
			UniformsDelegate(ForwardLightingCaptures& captures) : _captures(&captures)
			{
				_interface.BindImmediateData(0, Utility::Hash64("BasicLightingEnvironment"), {});
			}
			UniformsStreamInterface _interface;
			ForwardLightingCaptures* _captures;
		};
		std::shared_ptr<UniformsDelegate> _uniformsDelegate;*/

		void DoShadowPrepare(LightingTechniqueIterator& iterator);
		void DoToneMap(LightingTechniqueIterator& iterator);
	};

	static std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		Internal::ILightBase& proj,
		ICompiledShadowPreparer& preparer,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool)
	{
		auto res = preparer.CreatePreparedShadowResult();
		iterator.PushFollowingStep(
			[&preparer, &proj, &shadowGenFrameBufferPool, &shadowGenAttachmentPool](LightingTechniqueIterator& iterator) {
				iterator._rpi = preparer.Begin(
					*iterator._threadContext,
					*iterator._parsingContext,
					proj,
					shadowGenFrameBufferPool,
					shadowGenAttachmentPool);
			});
		iterator.PushFollowingStep(Techniques::BatchFilter::General);
		auto cfg = preparer.GetSequencerConfig();
		iterator.PushFollowingStep(std::move(cfg.first), std::move(cfg.second));
		iterator.PushFollowingStep(
			[res, &preparer](LightingTechniqueIterator& iterator) {
				iterator._rpi.End();
				preparer.End(
					*iterator._threadContext,
					*iterator._parsingContext,
					iterator._rpi,
					*res);
			});
		return res;
	}

	void ForwardLightingCaptures::DoShadowPrepare(LightingTechniqueIterator& iterator)
	{
		if (_lightScene->_shadowPreparationOperators->_operators.empty()) return;

		_lightScene->_preparedShadows.reserve(_lightScene->_shadowProjections.size());
		ILightScene::LightSourceId prevLightId = ~0u; 
		for (unsigned c=0; c<_lightScene->_shadowProjections.size(); ++c) {
			auto shadowOperatorId = _lightScene->_shadowProjections[c]._operatorId;
			auto& shadowPreparer = *_lightScene->_shadowPreparationOperators->_operators[shadowOperatorId]._preparer;
			_lightScene->_preparedShadows.push_back(std::make_pair(
				_lightScene->_shadowProjections[c]._lightId,
				SetupShadowPrepare(iterator, *_lightScene->_shadowProjections[c]._desc, shadowPreparer, *_shadowGenFrameBufferPool, *_shadowGenAttachmentPool)));

			// shadow entries must be sorted by light id
			assert(prevLightId == ~0u || prevLightId < _lightScene->_shadowProjections[c]._lightId);
			prevLightId = _lightScene->_shadowProjections[c]._lightId;
		}
	}

	void ForwardLightingCaptures::DoToneMap(LightingTechniqueIterator& iterator)
	{
		// Very simple stand-in for tonemap -- just use a copy shader to write the HDR values directly to the LDR texture
		auto& pipelineLayout = ::Assets::Actualize<Techniques::CompiledPipelineLayoutAsset>(
			iterator._threadContext->GetDevice(), 
			LIGHTING_OPERATOR_PIPELINE ":LightingOperator");
		auto& copyShader = *::Assets::Actualize<Metal::ShaderProgram>(
			pipelineLayout->GetPipelineLayout(),
			BASIC2D_VERTEX_HLSL ":fullscreen",
			BASIC_PIXEL_HLSL ":copy_inputattachment");
		auto& metalContext = *Metal::DeviceContext::Get(*iterator._threadContext);
		auto encoder = metalContext.BeginGraphicsEncoder_ProgressivePipeline(pipelineLayout->GetPipelineLayout());
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Utility::Hash64("SubpassInputAttachment"));
		Metal::BoundUniforms uniforms(copyShader, usi);
		encoder.Bind(copyShader);
		encoder.Bind(Techniques::CommonResourceBox::s_dsDisable);
		encoder.Bind({&Techniques::CommonResourceBox::s_abOpaque, &Techniques::CommonResourceBox::s_abOpaque+1});
		UniformsStream us;
		IResourceView* srvs[] = { iterator._rpi.GetInputAttachmentView(0).get() };
		us._resourceViews = MakeIteratorRange(srvs);
		uniforms.ApplyLooseUniforms(metalContext, encoder, us);
		encoder.Bind({}, Topology::TriangleStrip);
		encoder.Draw(4);
	}

	static RenderStepFragmentInterface CreateToneMapFragment(
		std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Graphics };
		auto hdrInput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR, LoadStore::Retain, LoadStore::DontCare);
		auto ldrOutput = fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR, LoadStore::DontCare, LoadStore::Retain);

		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.AppendOutput(ldrOutput);
		subpass.AppendInput(hdrInput);
		subpass.SetName("tonemap");
		fragment.AddSubpass(std::move(subpass), std::move(fn));
		return fragment;
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, bool precisionTargets = false)
	{
		UInt2 fbSize{stitchingContext._workingProps._outputWidth, stitchingContext._workingProps._outputHeight};
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::UnorderedAccess | BindFlag::InputAttachment, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::D24_UNORM_S8_UINT),
					"main-depth")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::ColorHDR,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::InputAttachment | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], (!precisionTargets) ? Format::R16G16B16A16_FLOAT : Format::R32G32B32A32_FLOAT),
					"color-hdr")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferNormal,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8B8A8_SNORM),
					"gbuffer-normal")
			},
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::GBufferMotion,
				CreateDesc(
					BindFlag::RenderTarget | BindFlag::ShaderResource | BindFlag::UnorderedAccess, 0, 0, 
					TextureDesc::Plain2D(fbSize[0], fbSize[1], RenderCore::Format::R8G8_SINT),
					"gbuffer-motion")
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<LightingEngineApparatus>& apparatus,
		IteratorRange<const LightSourceOperatorDesc*> resolveOperators,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps)
	{
		return CreateForwardLightingTechnique(
			apparatus->_device, apparatus->_pipelineAccelerators, apparatus->_lightingOperatorCollection, apparatus->_sharedDelegates, 
			apparatus->_dmShadowDescSetTemplate,
			resolveOperators, shadowGenerators, ambientLightOperator,
			preregisteredAttachments, fbProps);
	}

	::Assets::PtrToFuturePtr<CompiledLightingTechnique> CreateForwardLightingTechnique(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAccelerators,
		const std::shared_ptr<Techniques::PipelinePool>& pipelinePool,
		const std::shared_ptr<SharedTechniqueDelegateBox>& techDelBox,
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& shadowDescSet,
		IteratorRange<const LightSourceOperatorDesc*> positionalLightOperatorsInit,
		IteratorRange<const ShadowOperatorDesc*> shadowGenerators,
		const AmbientLightOperatorDesc& ambientLightOperator,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachmentsInit,
		const FrameBufferProperties& fbProps)
	{
		auto shadowPreparationOperatorsFuture = CreateShadowPreparationOperators(shadowGenerators, pipelineAccelerators, techDelBox, shadowDescSet);

		auto hierarchicalDepthsOperatorFuture = ::Assets::MakeFuture<std::shared_ptr<HierarchicalDepthsOperator>>(pipelinePool);
		RasterizationLightTileOperator::Configuration tilingConfig;
		auto lightTilerFuture = ::Assets::MakeFuture<std::shared_ptr<RasterizationLightTileOperator>>(pipelinePool, tilingConfig);
		auto ssrFuture = ::Assets::MakeFuture<std::shared_ptr<ScreenSpaceReflectionsOperator>>(pipelinePool);

		auto result = std::make_shared<::Assets::FuturePtr<CompiledLightingTechnique>>("forward-lighting-technique");
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { preregisteredAttachmentsInit.begin(), preregisteredAttachmentsInit.end() };
		std::vector<LightSourceOperatorDesc> positionalLightOperators { positionalLightOperatorsInit.begin(), positionalLightOperatorsInit.end() };
		::Assets::WhenAll(shadowPreparationOperatorsFuture, hierarchicalDepthsOperatorFuture, lightTilerFuture, ssrFuture).ThenConstructToFuture(
			*result,
			[device, techDelBox, preregisteredAttachments=std::move(preregisteredAttachments), fbProps, pipelineAccelerators, positionalLightOperators, ambientLightOperator]
			(auto shadowPreparationOperators, auto hierarchicalDepthsOperator, auto lightTiler, auto ssr) {

				auto lightScene = std::make_shared<ForwardPlusLightScene>(ambientLightOperator);
				lightScene->_positionalLightOperators = std::move(positionalLightOperators);
				lightScene->_shadowPreparationOperators = shadowPreparationOperators;
				lightScene->_device = device;
				lightScene->_ssrOperator = ssr;
				lightScene->_lightTiler = lightTiler;
				lightScene->_hierarchicalDepthsOperator = hierarchicalDepthsOperator;
				lightScene->FinalizeConfiguration();

				auto captures = std::make_shared<ForwardLightingCaptures>();
				captures->_shadowGenAttachmentPool = std::make_shared<Techniques::AttachmentPool>(device);
				captures->_shadowGenFrameBufferPool = Techniques::CreateFrameBufferPool();
				captures->_lightScene = lightScene;
				// captures->_uniformsDelegate = std::make_shared<ForwardLightingCaptures::UniformsDelegate>(*captures.get());

				lightTiler->SetLightScene(lightScene);

				Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, fbProps };
				PreregisterAttachments(stitchingContext);
				hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
				lightTiler->PreregisterAttachments(stitchingContext);
				ssr->PreregisterAttachments(stitchingContext);

				auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(pipelineAccelerators, stitchingContext, lightScene);

				// Reset captures
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						// iterator._parsingContext->AddShaderResourceDelegate(captures->_uniformsDelegate);
						auto& stitchingContext = iterator._parsingContext->GetFragmentStitchingContext();
						PreregisterAttachments(stitchingContext);
						captures->_lightScene->_hierarchicalDepthsOperator->PreregisterAttachments(stitchingContext);
						captures->_lightScene->_lightTiler->PreregisterAttachments(stitchingContext);
						captures->_lightScene->_ssrOperator->PreregisterAttachments(stitchingContext);
						captures->_lightScene->SetupProjection(*iterator._parsingContext);
					});

				// Prepare shadows
				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoShadowPrepare(iterator);
					});

				// Pre depth
				// lightingTechnique->CreateStep_RunFragments(lightScene->CreateDepthMotionFragment(techDelBox->_depthMotionDelegate));
				lightingTechnique->CreateStep_RunFragments(lightScene->CreateDepthMotionNormalFragment(techDelBox->_depthMotionNormalDelegate));

				// Build hierarchical depths
				lightingTechnique->CreateStep_RunFragments(hierarchicalDepthsOperator->CreateFragment(fbProps));

				// Light tiling & configure lighting descriptors
				lightingTechnique->CreateStep_RunFragments(lightTiler->CreateInitFragment(fbProps));
				lightingTechnique->CreateStep_RunFragments(lightTiler->CreateFragment(fbProps));

				// Calculate SSRs
				lightingTechnique->CreateStep_RunFragments(ssr->CreateFragment(fbProps));

				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						captures->_lightScene->ConfigureParsingContext(*iterator._parsingContext);
					});

				// Draw main scene
				lightingTechnique->CreateStep_RunFragments(lightScene->CreateForwardSceneFragment(techDelBox->_forwardIllumDelegate_DisableDepthWrite));

				// Post processing
				auto toneMapFragment = CreateToneMapFragment(
					[captures](LightingTechniqueIterator& iterator) {
						captures->DoToneMap(iterator);
					});
				lightingTechnique->CreateStep_RunFragments(std::move(toneMapFragment));

				lightingTechnique->CreateStep_CallFunction(
					[captures](LightingTechniqueIterator& iterator) {
						// iterator._parsingContext->RemoveShaderResourceDelegate(*captures->_lightScene);
						iterator._parsingContext->_extraSequencerDescriptorSet = {0ull, nullptr};
						// iterator._parsingContext->RemoveShaderResourceDelegate(*captures->_uniformsDelegate);
						captures->_lightScene->_preparedShadows.clear();
					});

				lightingTechnique->CompleteConstruction();

				lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
				lightingTechnique->_depVal.RegisterDependency(hierarchicalDepthsOperator->GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(lightTiler->GetDependencyValidation());
				lightingTechnique->_depVal.RegisterDependency(ssr->GetDependencyValidation());
					
				return lightingTechnique;
			});
		return result;
	}

}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkyOperator.h"
#include "SequenceIterator.h"
#include "SHCoefficients.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"
#include "../Techniques/DescriptorSetAccelerator.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/Services.h"
#include "../Techniques/CommonResources.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/TextureCompiler.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/FunctionUtils.h"
#include "../../xleres/FileList.h"
#include <future>

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	void SkyOperator::Execute(Techniques::ParsingContext& parsingContext)
	{
		assert(_secondStageConstructionState == 2);
		assert(_shader);

		const IDescriptorSet* descSets[] = { _descSet.get() };
		_shader->Draw(
			parsingContext,
			{},
			MakeIteratorRange(descSets));

		parsingContext.RequireCommandList(_completionCommandList);
	}

	void SkyOperator::Execute(SequenceIterator& iterator)
	{
		Execute(*iterator._parsingContext);
	}

	void SkyOperator::SetResource(std::shared_ptr<IResourceView> texture, BufferUploads::CommandListID completionCommandList)
	{
		assert(_secondStageConstructionState == 2);
		auto& pipelineLayout = _shader->GetPredefinedPipelineLayout();
		auto* descSetLayout = pipelineLayout.FindDescriptorSet("SkyDS");
		assert(descSetLayout);
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Sky"_h);
		auto& commonRes = *Techniques::Services::GetCommonResources();
		if (texture) {
			_descSet = Techniques::ConstructDescriptorSetHelper{_device, &commonRes._samplerPool}
				.ConstructImmediately(
					*descSetLayout, usi, 
					ResourceViewStream{*texture},
					"SkyOperator");
		} else {
			auto dummy = Techniques::Services::GetCommonResources()->_blackCubeSRV;
			_descSet = Techniques::ConstructDescriptorSetHelper{_device, &commonRes._samplerPool}
				.ConstructImmediately(
					*descSetLayout, usi, 
					ResourceViewStream{*dummy},
					"SkyOperator");
		}

		_completionCommandList = completionCommandList;
	}

	::Assets::DependencyValidation SkyOperator::GetDependencyValidation() const { assert(_secondStageConstructionState == 2); return _shader->GetDependencyValidation(); }

	SkyOperator::SkyOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const SkyOperatorDesc& desc)
	: _secondStageConstructionState(0)
	, _desc(desc)
	{
		_device = pipelinePool->GetDevice();
		_pool = std::move(pipelinePool);
	}

	SkyOperator::~SkyOperator()
	{}

	void SkyOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<SkyOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		UniformsStreamInterface usi;
		usi.BindFixedDescriptorSet(0, "SkyDS"_h);

		ParameterBox params;
		params.SetParameter("SKY_PROJECTION", 5);
		Techniques::PixelOutputStates po;
		po.Bind(*fbTarget._fbDesc, fbTarget._subpassIdx);
		auto depthStencil = Techniques::CommonResourceBox::s_dsReadOnly;
		depthStencil._stencilEnable = true;
		depthStencil._stencilReadMask = 1<<7;
		depthStencil._frontFaceStencil._comparisonOp = CompareOp::Equal;		// assuming stencil ref value == 0
		po.Bind(depthStencil);
		AttachmentBlendDesc blendDescs[] {Techniques::CommonResourceBox::s_abOpaque};
		po.Bind(MakeIteratorRange(blendDescs));
		auto futureShader = CreateFullViewportOperator(
			_pool,
			Techniques::FullViewportOperatorSubType::MaxDepth,
			SKY_PIXEL_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":Sky",
			po, usi);
		::Assets::WhenAll(futureShader).ThenConstructToPromise(
			std::move(promise),
			[strongThis=shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				strongThis->SetResource(nullptr, 0);		// initial blocked out state
				return strongThis;
			});
	}

	uint64_t SkyOperatorDesc::GetHash(uint64_t seed) const
	{
		return rotr64(seed, (uint8_t)_textureType);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class SkyTextureProcessor : public ISkyTextureProcessor, public std::enable_shared_from_this<SkyTextureProcessor>
	{
	public:
		void SetEquirectangularSource(std::shared_ptr<::Assets::OperationContext> loadingContext, StringSection<> src) override;

		void SetSkyResource(std::shared_ptr<IResourceView> resource, BufferUploads::CommandListID completionCommandList) override
		{
			ScopedLock(_pendingUpdatesLock);
			_pendingAmbientRawCubemap = std::move(resource);
			_pendingAmbientRawCubemapCompletion = completionCommandList;
			_pendingUpdate = true;
		}

		void SetIBL(std::shared_ptr<IResourceView> specular, BufferUploads::CommandListID specularCompletion, SHCoefficients& diffuse) override
		{
			ScopedLock(_pendingUpdatesLock);
			_pendingSpecularIBL = std::move(specular);
			_pendingSpecularIBLCompletion = specularCompletion;
			_pendingDiffuseIBL = diffuse;
			_pendingUpdate = true;
		}

		unsigned BindOnChangeSkyTexture(std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID)>&& fn)
		{
			// if we don't have a pending update (ie, if we're not expecting to call the function at the start of next render, anyway, we must
			// call it now with most recently configured texture)
			{
				ScopedLock(_pendingUpdatesLock);
				if (!_pendingUpdate)
					fn(_pendingAmbientRawCubemap, _pendingAmbientRawCubemapCompletion);
			}
			return _onChangeSkyTexture.Bind(std::move(fn));
		}

		void UnbindOnChangeSkyTexture(unsigned bindId)
		{
			_onChangeSkyTexture.Unbind(bindId);
		}

		unsigned BindOnChangeIBL(std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID, SHCoefficients&)>&& fn)
		{
			{
				ScopedLock(_pendingUpdatesLock);
				if (!_pendingUpdate)
					fn(_pendingSpecularIBL, _pendingSpecularIBLCompletion, _pendingDiffuseIBL);
			}
			return _onChangeIBL.Bind(std::move(fn));
		}

		void UnbindOnChangeIBL(unsigned bindId)
		{
			_onChangeIBL.Unbind(bindId);
		}

		void Prerender();

		SkyTextureProcessor(
			const SkyTextureProcessorDesc& desc,
			std::shared_ptr<SkyOperator> skyOperator,
			std::shared_ptr<BufferUploads::IManager> bufferUploads)
		: _desc(desc), _skyOperator(std::move(skyOperator)), _bufferUploads(std::move(bufferUploads))
		{}
		~SkyTextureProcessor() = default;
	private:
		SkyTextureProcessorDesc _desc;
		std::string _sourceImage;
		std::shared_ptr<SkyOperator> _skyOperator;

		std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _specularIBL;
		std::shared_future<SHCoefficientsAsset> _diffuseIBL;
		std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _skyCubemap;

		std::shared_future<void> _activeUpdate;

		Signal<std::shared_ptr<IResourceView>, BufferUploads::CommandListID> _onChangeSkyTexture;
		Signal<std::shared_ptr<IResourceView>, BufferUploads::CommandListID, SHCoefficients&> _onChangeIBL;

		Threading::Mutex _pendingUpdatesLock;
		bool _pendingUpdate = true;
		SHCoefficients _pendingDiffuseIBL;
		std::shared_ptr<IResourceView> _pendingSpecularIBL;
		BufferUploads::CommandListID _pendingSpecularIBLCompletion = 0;
		std::shared_ptr<IResourceView> _pendingAmbientRawCubemap;
		BufferUploads::CommandListID _pendingAmbientRawCubemapCompletion = 0;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
	};

	void SkyTextureProcessor::SetEquirectangularSource(std::shared_ptr<::Assets::OperationContext> loadingContext, StringSection<> input)
	{
		if (XlEqString(input, _sourceImage)) return;
		_sourceImage = input.AsString();

		_diffuseIBL = {};
		_specularIBL = {};
		_skyCubemap = {};
		auto weakThis = weak_from_this();

		if (_skyOperator || _onChangeSkyTexture.AtLeastOneBind()) {
			Assets::TextureCompilationRequest request2;
			request2._operation = Assets::TextureCompilationRequest::Operation::EquirectToCubeMap; 
			request2._srcFile = _sourceImage;
			request2._format = _desc._cubemapFormat;
			request2._faceDim = _desc._cubemapFaceDimension;
			request2._mipMapFilter = Assets::TextureCompilationRequest::MipMapFilter::FromSource;

			if (_desc._blurBackground) {
				// Use the "Bokeh" mode to blur out the background image, almost as if it's a depth of field effect
				request2._operation = Assets::TextureCompilationRequest::Operation::EquirectToCubeMapBokeh;
				request2._sampleCount = 2048u;
			}

			Techniques::DeferredShaderResource::ProgressiveResultFn progressiveResultsFn;
			if (_desc._progressiveCompilation && !_desc._useProgressiveSpecularAsBackground) {
				progressiveResultsFn =
					[weakbu=std::weak_ptr<BufferUploads::IManager>{_bufferUploads}, weakThis](auto dataSource) {
						auto bu = weakbu.lock();
						auto strongThis = weakThis.lock();
						if (!bu || !strongThis) return;

						auto transaction = bu->Begin(dataSource);
						auto locator = transaction._future.get();		// note -- stall here, maybe some alignment with frame beat
						
						ScopedLock(strongThis->_pendingUpdatesLock);
						strongThis->_pendingUpdate = true;
						TRY {
							strongThis->_pendingAmbientRawCubemap = locator.CreateTextureView();
							strongThis->_pendingAmbientRawCubemapCompletion = locator.GetCompletionCommandList();
						} CATCH(...) {
							// suppress bad texture errors
							strongThis->_pendingAmbientRawCubemap = nullptr;
							strongThis->_pendingAmbientRawCubemapCompletion = 0;
						} CATCH_END
					};
			}

			_skyCubemap = ::Assets::ConstructToFuturePtr<Techniques::DeferredShaderResource>(loadingContext, request2, std::move(progressiveResultsFn));
		}

		if (_onChangeIBL.AtLeastOneBind()) {
			_diffuseIBL = ::Assets::MakeAsset<SHCoefficientsAsset>(loadingContext, input);

			Assets::TextureCompilationRequest request;
			request._operation = Assets::TextureCompilationRequest::Operation::EquirectFilterGlossySpecular;
			// request._operation = Assets::TextureCompilationRequest::Operation::EquirectFilterGlossySpecularReference;
			// request._operation = Assets::TextureCompilationRequest::Operation::EquirectFilterDiffuseReference;
			request._srcFile = _sourceImage;
			request._format = _desc._specularCubemapFormat;
			request._faceDim = _desc._specularCubemapFaceDimension;
			request._sampleCount = 32u*1024u;

			Techniques::DeferredShaderResource::ProgressiveResultFn progressiveResultsFn;
			if (_desc._progressiveCompilation) {
				request._commandListIntervalMS = 250;	// some overhead created by splitting cmd lists when we want progressive results

				progressiveResultsFn =
					[weakbu=std::weak_ptr<BufferUploads::IManager>{_bufferUploads}, weakThis, setBkgrnd=_desc._useProgressiveSpecularAsBackground](auto dataSource) {
						auto bu = weakbu.lock();
						auto strongThis = weakThis.lock();
						if (!bu || !strongThis) return;

						auto transaction = bu->Begin(dataSource);
						auto locator = transaction._future.get();		// note -- stall here, maybe some alignment with frame beat
						
						ScopedLock(strongThis->_pendingUpdatesLock);
						strongThis->_pendingUpdate = true;
						TRY {
							strongThis->_pendingSpecularIBL = locator.CreateTextureView();
							strongThis->_pendingSpecularIBLCompletion = locator.GetCompletionCommandList();
							if (setBkgrnd) {
								strongThis->_pendingAmbientRawCubemap = locator.CreateTextureView();
								strongThis->_pendingAmbientRawCubemapCompletion = locator.GetCompletionCommandList();
							}
						} CATCH(...) {
							// suppress bad texture errors
							strongThis->_pendingSpecularIBL = nullptr;
							strongThis->_pendingSpecularIBLCompletion = 0;
						} CATCH_END
					};
			}

			_specularIBL = ::Assets::ConstructToFuturePtr<Techniques::DeferredShaderResource>(loadingContext, request, std::move(progressiveResultsFn));
		}

		if (!_specularIBL.valid() && !_diffuseIBL.valid() && !_skyCubemap.valid())
			return;

		std::promise<void> promisedUpdate;
		_activeUpdate = promisedUpdate.get_future();

		struct Helper
		{
			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _specularIBL;
			std::shared_future<SHCoefficientsAsset> _diffuseIBL;
			std::shared_future<std::shared_ptr<Techniques::DeferredShaderResource>> _skyCubemap;
		};
		auto helper = std::make_shared<Helper>();
		helper->_specularIBL = _specularIBL;
		helper->_diffuseIBL = _diffuseIBL;
		helper->_skyCubemap = _skyCubemap;

		::Assets::PollToPromise(
			std::move(promisedUpdate),
			[weakThis, helper](auto timeout) {
				auto strongThis = weakThis.lock();
				if (!strongThis) return ::Assets::PollStatus::Finish;
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (helper->_specularIBL.valid() && helper->_specularIBL.wait_until(timeoutTime) == std::future_status::timeout)
					return ::Assets::PollStatus::Continue;
				if (helper->_diffuseIBL.valid() && helper->_diffuseIBL.wait_until(timeoutTime) == std::future_status::timeout)
					return ::Assets::PollStatus::Continue;
				if (helper->_skyCubemap.valid() && helper->_skyCubemap.wait_until(timeoutTime) == std::future_status::timeout)
					return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[weakThis, helper]() {
				auto l = weakThis.lock();
				if (!l) return;

				ScopedLock(l->_pendingUpdatesLock);
				l->_pendingUpdate = true;
				TRY {
					l->_pendingDiffuseIBL = {};
					l->_pendingSpecularIBL = nullptr;
					l->_pendingSpecularIBLCompletion = 0;
					l->_pendingAmbientRawCubemap = nullptr;
					l->_pendingAmbientRawCubemapCompletion = 0;
					if (helper->_diffuseIBL.valid())
						l->_pendingDiffuseIBL = helper->_diffuseIBL.get();
					if (helper->_specularIBL.valid()) {
						auto d = helper->_specularIBL.get();
						l->_pendingSpecularIBL = d->GetShaderResource();
						l->_pendingSpecularIBLCompletion = d->GetCompletionCommandList();
					}
					if (helper->_skyCubemap.valid()) {
						auto d = helper->_skyCubemap.get();
						l->_pendingAmbientRawCubemap = d->GetShaderResource();
						l->_pendingAmbientRawCubemapCompletion = d->GetCompletionCommandList();
					}
				} CATCH(...) {
					// suppress bad texture errors
					l->_pendingDiffuseIBL = {};
					l->_pendingSpecularIBL = nullptr;
					l->_pendingSpecularIBLCompletion = 0;
					l->_pendingAmbientRawCubemap = nullptr;
					l->_pendingAmbientRawCubemapCompletion = 0;
				} CATCH_END
			});
	}

	void SkyTextureProcessor::Prerender()
	{
		// certain updates processed one per render, in the main rendering thread
		ScopedLock(_pendingUpdatesLock);
		if (_pendingUpdate) {

			if (_activeUpdate.valid() && _activeUpdate.wait_for(std::chrono::seconds(0)) != std::future_status::timeout) {
				_activeUpdate.get();		// propagate any exceptions
				_activeUpdate = {};
			}

			if (_skyOperator)
				_skyOperator->SetResource(_pendingAmbientRawCubemap, _pendingAmbientRawCubemapCompletion);

			_onChangeSkyTexture(_pendingAmbientRawCubemap, _pendingAmbientRawCubemapCompletion);
			_onChangeIBL(_pendingSpecularIBL, _pendingSpecularIBLCompletion, _pendingDiffuseIBL);

			_pendingUpdate = false;
		}
	}

	std::shared_ptr<ISkyTextureProcessor> CreateSkyTextureProcessor(
		const SkyTextureProcessorDesc& desc,
		std::shared_ptr<SkyOperator> skyOperator,
		std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID)>&& onSkyTextureUpdate,
		std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID, SHCoefficients&)>&& onIBLUpdate)
	{
		auto result = std::make_shared<SkyTextureProcessor>(desc, std::move(skyOperator), Techniques::Services::GetBufferUploadsPtr());
		if (onSkyTextureUpdate)
			result->BindOnChangeSkyTexture(std::move(onSkyTextureUpdate));
		if (onIBLUpdate)
			result->BindOnChangeIBL(std::move(onIBLUpdate));
		return result;
	}

	void SkyTextureProcessorPrerender(ISkyTextureProcessor& processor)
	{
		checked_cast<SkyTextureProcessor*>(&processor)->Prerender();
	}

	ISkyTextureProcessor::~ISkyTextureProcessor() {}


	void FillBackgroundOperator::Execute(Techniques::ParsingContext& parsingContext)
	{
		assert(_secondStageConstructionState == 2);
		assert(_shader);
		_shader->Draw(parsingContext, {});
	}

	::Assets::DependencyValidation FillBackgroundOperator::GetDependencyValidation() const
	{
		assert(_secondStageConstructionState == 2);
		return _shader->GetDependencyValidation();
	}

	void FillBackgroundOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<FillBackgroundOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		Techniques::PixelOutputStates outputStates;
		outputStates.Bind(*fbTarget._fbDesc, fbTarget._subpassIdx);
		outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
		AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abOpaque };
		outputStates.Bind(MakeIteratorRange(blendStates));
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "SubpassInputAttachment"_h);
		auto shaderFuture = Techniques::CreateFullViewportOperator(
			_pool, Techniques::FullViewportOperatorSubType::DisableDepth,
			BASIC_PIXEL_HLSL ":fill_background",
			{}, GENERAL_OPERATOR_PIPELINE ":GraphicsMain",
			outputStates, usi);
		::Assets::WhenAll(std::move(shaderFuture)).ThenConstructToPromise(
			std::move(promise),
			[strongThis=shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				return strongThis;
			});
	}

	FillBackgroundOperator::FillBackgroundOperator(std::shared_ptr<Techniques::PipelineCollection> pipelinePool)
	: _pool(std::move(pipelinePool)) {}
	FillBackgroundOperator::~FillBackgroundOperator() {}

}}


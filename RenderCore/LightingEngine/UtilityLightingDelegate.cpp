// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UtilityLightingDelegate.h"
#include "SkyOperator.h"
#include "Sequence.h"
#include "SequenceIterator.h"
#include "StandardLightScene.h"
#include "LightingDelegateUtil.h"
#include "LightingEngineApparatus.h"
#include "ForwardLightingDelegate.h"		// for MultiSampleOperatorDesc
#include "../Techniques/RenderPass.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/PipelineAccelerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/DeformAccelerator.h"
#include "../IDevice.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/Assets.h"
#include "../Utility/MemoryUtils.h"

namespace RenderCore { namespace LightingEngine
{
	struct UtilityOperatorDigest;

	class UtilityLightingCaptures : public std::enable_shared_from_this<UtilityLightingCaptures>
	{
	public:
		std::shared_ptr<Internal::StandardLightScene> _lightScene;
		std::shared_ptr<SkyOperator> _skyOperator;
		std::shared_ptr<FillBackgroundOperator> _fillBackgroundOperator;
		std::shared_ptr<ISkyTextureProcessor> _skyTextureProcessor;

		void ConfigureParsingContext(Techniques::ParsingContext& parsingContext);
		void ReleaseParsingContext(Techniques::ParsingContext& parsingContext);

		struct SecondStageConstructionOperators;
		auto ConstructMainSequence(
			CompiledLightingTechnique& lightingTechnique,
			std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
			IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
			const FrameBufferProperties& fbProps,
			const UtilityOperatorDigest& digest,
			std::shared_ptr<Techniques::ITechniqueDelegate> mainTechniqueDelegate) -> std::shared_ptr<SecondStageConstructionOperators>;
	};

	void UtilityLightingCaptures::ConfigureParsingContext(Techniques::ParsingContext& parsingContext)
	{
	}

	void UtilityLightingCaptures::ReleaseParsingContext(Techniques::ParsingContext& parsingContext)
	{
	}

	static RenderStepFragmentInterface CreateUtilitySceneFragment(
		std::shared_ptr<UtilityLightingCaptures> captures,
		std::shared_ptr<Techniques::ITechniqueDelegate> mainDelegate)
	{
		RenderStepFragmentInterface result { PipelineType::Graphics };
		auto output = result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState();
		auto depth = result.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).Clear().FinalState(BindFlag::DepthStencil);

		{
			Techniques::FrameBufferDescFragment::SubpassDesc skySubpass;
			skySubpass.AppendOutput(output);
			skySubpass.SetDepthStencil(depth);
			skySubpass.SetName("Sky");
			if (captures->_skyOperator) {
				result.AddSubpass(
					std::move(skySubpass),
					[weakCaptures = std::weak_ptr<UtilityLightingCaptures>{captures}](SequenceIterator& iterator) {
						auto l = weakCaptures.lock();
						if (l) l->_skyOperator->Execute(iterator);
					});
			} else {
				result.AddSubpass(
					std::move(skySubpass),
					[weakCaptures = std::weak_ptr<UtilityLightingCaptures>{captures}](SequenceIterator& iterator) {
						auto l = weakCaptures.lock();
						if (l) l->_fillBackgroundOperator->Execute(*iterator._parsingContext);
					});
			}
		}

		{
			Techniques::FrameBufferDescFragment::SubpassDesc mainSubpass;
			mainSubpass.AppendOutput(output);
			mainSubpass.SetDepthStencil(depth);
			mainSubpass.SetName("Utility");

			ParameterBox box;
			result.AddSubpass(std::move(mainSubpass), mainDelegate, Techniques::BatchFlags::Opaque|Techniques::BatchFlags::Decal|Techniques::BatchFlags::Blending);
		}

		return result;
	}

	static void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		UInt2 fbSize{fbProps._width, fbProps._height};
		auto samples = fbProps._samples;
		Techniques::PreregisteredAttachment attachments[] {
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource | BindFlag::InputAttachment,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil), samples)),
				"main-depth"
			}
		};
		for (const auto& a:attachments)
			stitchingContext.DefineAttachment(a);
	}

	struct UtilityOperatorDigest
	{
		UtilityLightingTechniqueDesc _globalTechniqueDesc;
		std::optional<MultiSampleOperatorDesc> _msaa;
		std::optional<SkyTextureProcessorDesc> _skyTextureProcessor;
		std::optional<SkyOperatorDesc> _sky;

		UtilityOperatorDigest(
			const ChainedOperatorDesc* globalOperatorsChain)
		{
			bool gotGlobalTechniqueDesc = false;
			auto* chain = globalOperatorsChain;
			while (chain) {
				switch(chain->_structureType) {
				case TypeHashCode<UtilityLightingTechniqueDesc>:
					if (gotGlobalTechniqueDesc)
						Throw(std::runtime_error("Multiple UtilityLightingTechniqueDesc operators found, where only one expected"));
					_globalTechniqueDesc = Internal::ChainedOperatorCast<UtilityLightingTechniqueDesc>(*chain);
					gotGlobalTechniqueDesc = true;
					break;

				case TypeHashCode<MultiSampleOperatorDesc>:
					if (_msaa)
						Throw(std::runtime_error("Multiple antialiasing operators found, where only one expected"));
					_msaa = Internal::ChainedOperatorCast<MultiSampleOperatorDesc>(*chain);
					break;

				case TypeHashCode<SkyOperatorDesc>:
					if (_sky)
						Throw(std::runtime_error("Multiple sky operators found, where only one expected"));
					_sky = Internal::ChainedOperatorCast<SkyOperatorDesc>(*chain);
					break;

				case TypeHashCode<SkyTextureProcessorDesc>:
					if (_skyTextureProcessor)
						Throw(std::runtime_error("Multiple sky operators found, where only one expected"));
					_skyTextureProcessor = Internal::ChainedOperatorCast<SkyTextureProcessorDesc>(*chain);
					break;
				}
				chain = chain->_next;
			}
			if (!gotGlobalTechniqueDesc)
				Throw(std::runtime_error("Missing UtilityLightingTechniqueDesc when constructing utility lighting technique"));
		}
	};

	struct UtilityLightingCaptures::SecondStageConstructionOperators
	{
		std::future<std::shared_ptr<SkyOperator>> _futureSky;
		std::future<std::shared_ptr<FillBackgroundOperator>> _futureFillBackground;
	};

	auto UtilityLightingCaptures::ConstructMainSequence(
		CompiledLightingTechnique& lightingTechnique,
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAccelerators,
		IteratorRange<const Techniques::PreregisteredAttachment*> preregisteredAttachments,
		const FrameBufferProperties& fbProps,
		const UtilityOperatorDigest& digest,
		std::shared_ptr<Techniques::ITechniqueDelegate> mainTechniqueDelegate) -> std::shared_ptr<SecondStageConstructionOperators>
	{
		Techniques::FragmentStitchingContext stitchingContext { preregisteredAttachments, Techniques::CalculateDefaultSystemFormats(*pipelineAccelerators->GetDevice()) };
		PreregisterAttachments(stitchingContext, fbProps);

		auto& mainSequence = lightingTechnique.CreateSequence();
		mainSequence.CreateStep_CallFunction(
			[](SequenceIterator& iterator) {
				if (iterator._parsingContext->GetTechniqueContext()._deformAccelerators)
					iterator._parsingContext->GetTechniqueContext()._deformAccelerators->SetVertexInputBarrier(*iterator._threadContext);
			});

		mainSequence.CreateStep_InvalidateUniforms();
		mainSequence.CreateStep_BringUpToDateUniforms();

		mainSequence.CreateStep_CallFunction(
			[captures=shared_from_this()](SequenceIterator& iterator) {
				captures->ConfigureParsingContext(*iterator._parsingContext);
			});

		// Draw main scene
		auto mainSceneFragmentRegistration = mainSequence.CreateStep_RunFragments(
			CreateUtilitySceneFragment(shared_from_this(), mainTechniqueDelegate));

		mainSequence.CreateStep_CallFunction(
			[captures=shared_from_this()](SequenceIterator& iterator) {
				captures->ReleaseParsingContext(*iterator._parsingContext);	// almost need a "finally" step for this, because it may not be called on exception
			});
		mainSequence.CreateStep_BringUpToDateUniforms();

		lightingTechnique.CompleteConstruction(std::move(pipelineAccelerators), stitchingContext, fbProps);

		auto ops = std::make_shared<SecondStageConstructionOperators>();
		if (_skyOperator)
			ops->_futureSky = Internal::SecondStageConstruction(*_skyOperator, Internal::AsFrameBufferTarget(mainSequence, mainSceneFragmentRegistration));
		ops->_futureFillBackground = Internal::SecondStageConstruction(*_fillBackgroundOperator, Internal::AsFrameBufferTarget(mainSequence, mainSceneFragmentRegistration));
		return ops;
	}

	void CreateUtilityLightingTechnique(
		std::promise<std::shared_ptr<CompiledLightingTechnique>>&& promise,
		CreationUtility& utility,
		const ChainedOperatorDesc* globalOperators,
		CreationUtility::OutputTarget outputTarget)
	{
		struct ConstructionHelper
		{
			SharedTechniqueDelegateBox::TechniqueDelegateFuture _techniqueDelegate;
			std::future<std::shared_ptr<Internal::StandardLightScene>> _lightSceneFuture;
		};
		auto helper = std::make_shared<ConstructionHelper>();

		UtilityOperatorDigest digest { globalOperators };

		helper->_techniqueDelegate = utility._techDelBox->GetUtilityDelegate(digest._globalTechniqueDesc._type);

		helper->_lightSceneFuture = ::Assets::ConstructToFuturePtr<Internal::StandardLightScene>();

		auto resolution = Internal::ExtractOutputResolution(outputTarget._preregisteredAttachments);
		std::vector<Techniques::PreregisteredAttachment> preregisteredAttachments { outputTarget._preregisteredAttachments.begin(), outputTarget._preregisteredAttachments.end() };

		::Assets::PollToPromise(
			std::move(promise),
			[helper](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				if (Internal::MarkerTimesOut(helper->_techniqueDelegate, timeoutTime)) return ::Assets::PollStatus::Continue;
				return ::Assets::PollStatus::Finish;
			},
			[helper, utility=utility, preregisteredAttachments=std::move(preregisteredAttachments), resolution, digest=std::move(digest)]
			(std::promise<std::shared_ptr<CompiledLightingTechnique>>&& thatPromise) {

				TRY {
					auto captures = std::make_shared<UtilityLightingCaptures>();
					captures->_lightScene = helper->_lightSceneFuture.get();

					// operators
					auto msaaSamples = digest._msaa ? digest._msaa->_samples : TextureSamples::Create(); 
					if (digest._sky)
						captures->_skyOperator = std::make_shared<SkyOperator>(utility._pipelinePool, *digest._sky);
					captures->_fillBackgroundOperator = std::make_shared<FillBackgroundOperator>(utility._pipelinePool);

					if (digest._skyTextureProcessor) {
						captures->_skyTextureProcessor = CreateSkyTextureProcessor(
							*digest._skyTextureProcessor, captures->_skyOperator,
							nullptr, nullptr);
					}

					auto techniqueDelegate = helper->_techniqueDelegate.get();

					auto lightingTechnique = std::make_shared<CompiledLightingTechnique>(captures->_lightScene);
					lightingTechnique->_depVal = ::Assets::GetDepValSys().Make();
					// lightingTechnique->_depVal.RegisterDependency(captures->_lightScene->GetDependencyValidation());
					lightingTechnique->_depVal.RegisterDependency(techniqueDelegate->GetDependencyValidation());

					// main sequence & setup second stage construction
					FrameBufferProperties fbProps { resolution[0], resolution[1], msaaSamples };
					auto secondStageHelper = captures->ConstructMainSequence(
						*lightingTechnique,
						utility._pipelineAccelerators,
						preregisteredAttachments, fbProps, digest, techniqueDelegate);

					::Assets::PollToPromise(
						std::move(thatPromise),
						[secondStageHelper](auto timeout) {
							auto timeoutTime = std::chrono::steady_clock::now() + timeout;
							if (secondStageHelper->_futureSky.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureSky, timeoutTime)) return ::Assets::PollStatus::Continue;
							if (secondStageHelper->_futureFillBackground.valid() && Internal::MarkerTimesOut(secondStageHelper->_futureFillBackground, timeoutTime)) return ::Assets::PollStatus::Continue;
							return ::Assets::PollStatus::Finish;
						},
						[secondStageHelper, lightingTechnique, captures]() {
							// Shake out any exceptions
							if (secondStageHelper->_futureSky.valid()) secondStageHelper->_futureSky.get();
							if (secondStageHelper->_futureFillBackground.valid()) secondStageHelper->_futureFillBackground.get();

							// register dep vals for operators after we've done their second-stage-construction 
							if (captures->_skyOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_skyOperator->GetDependencyValidation());
							if (captures->_fillBackgroundOperator)
								lightingTechnique->_depVal.RegisterDependency(captures->_fillBackgroundOperator->GetDependencyValidation());

							// Everything finally finished
							return lightingTechnique;
						});
				} CATCH(...) {
					thatPromise.set_exception(std::current_exception());
				} CATCH_END
			});
	}

}}


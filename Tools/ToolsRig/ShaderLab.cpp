// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../Tools/EntityInterface/FormatterAdapters.h"
#include "../../RenderOverlays/SimpleVisualization.h"
#include "../../RenderCore/LightingEngine/LightingEngineInitialization.h"
#include "../../RenderCore/LightingEngine/LightingEngineIterator.h"
#include "../../RenderCore/LightingEngine/LightingDelegateUtil.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/PipelineOperators.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/UniformsStream.h"
#include "../../RenderCore/FrameBufferDesc.h"
#include "../../SceneEngine/Noise.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Marker.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../xleres/FileList.h"
#include <sstream>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
	#include "../../OSServices/WinAPI/IncludeWindows.h"
#endif

using namespace Utility::Literals;

namespace ToolsRig
{
	class GlobalStateDelegate : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		struct State
		{
			float _currentTime = 0.f;
			unsigned _dummy[3] = {0,0,0};
		};
		State _state;

		void WriteImmediateData(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override
		{
			assert(idx == 0);
			assert(dst.size() == sizeof(_state));
			std::memcpy(dst.begin(), &_state, sizeof(_state));
		}

		size_t GetImmediateDataSize(RenderCore::Techniques::ParsingContext& context, const void* objectContext, unsigned idx) override
		{
			assert(idx == 0);
			return sizeof(_state);
		}

		GlobalStateDelegate()
		{
			_interface.BindImmediateData(0, "GlobalState"_h);
		}
	};

	class CompiledTechnique : public ShaderLab::ICompiledOperation
	{
	public:
		virtual RenderCore::LightingEngine::CompiledLightingTechnique& GetLightingTechnique() const override { assert(_operation); return *_operation; }
		const ::Assets::DependencyValidation& GetDependencyValidation() const override  { return _depVal; }
		unsigned GetCompletionCommandList() const override { return _completionCommandList; }
		void AdvanceTime(float time) override { _globalStateDelegate->_state._currentTime += time; }
		std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _operation;
		std::shared_ptr<GlobalStateDelegate> _globalStateDelegate;
		::Assets::DependencyValidation _depVal;
		unsigned _completionCommandList = 0;
	};

	template<typename Function, typename... Args>
		void AsyncConstructToPromise(
			std::promise<
				std::decay_t<std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...>>
			>&& promise,
			Function&& function, Args&&... args)
	{
		ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[function=std::move(function), promise=std::move(promise)](Args&&... args) mutable -> void {
				TRY {
					auto object = function(std::forward<Args>(args)...);
					promise.set_value(std::move(object));
				} CATCH (...) {
					promise.set_exception(std::current_exception());
				} CATCH_END
			},
			std::forward<Args>(args)...);
	}

	static void ParseSequenceOperators(
		Formatters::IDynamicFormatter& formatter,
		ShaderLab::OperationConstructorContext& constructorContext,
		RenderCore::LightingEngine::LightingTechniqueSequence& sequence,
		const std::vector<std::pair<std::string, ShaderLab::OperationConstructor>>& operationConstructors)
	{
		assert(constructorContext._sequenceFinalizers.empty());
		assert(constructorContext._postStitchFunctions.empty());

		StringSection<> keyname;
		while (formatter.TryKeyedItem(keyname)) {
			auto constructor = std::find_if(operationConstructors.begin(), operationConstructors.end(),
				[keyname](const auto& p) { return XlEqString(keyname, p.first); });
			if (constructor == operationConstructors.end()) {
				std::stringstream str;
				str << "Unknown operation (" << keyname << ")." << std::endl << "Try one of the following: ";
				bool first = true;
				for (const auto& c:operationConstructors) {
					if (!first) str << ", ";
					first = false;
					str << c.first;
				}
				Throw(FormatException(str.str().c_str(), formatter.GetLocation()));
			}

			RequireBeginElement(formatter);
			constructor->second(formatter, constructorContext, &sequence);
			RequireEndElement(formatter);
		}

		for (auto fn=constructorContext._sequenceFinalizers.rbegin(); fn!=constructorContext._sequenceFinalizers.rend(); ++fn)
			(*fn)(constructorContext, &sequence);
		constructorContext._sequenceFinalizers.clear();
	}
	
	::Assets::PtrToMarkerPtr<ShaderLab::ICompiledOperation> ShaderLab::BuildCompiledTechnique(
		std::future<std::shared_ptr<Formatters::IDynamicFormatter>> futureFormatter,
		::Assets::PtrToMarkerPtr<IVisualizeStep> visualizeStep,
		::Assets::PtrToMarkerPtr<RenderCore::LightingEngine::ILightScene> futureLightScene,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachmentsInit,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormatsInit)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<ShaderLab::ICompiledOperation>>();
		std::vector<RenderCore::Techniques::PreregisteredAttachment> preregAttachments { preregAttachmentsInit.begin(), preregAttachmentsInit.end() };
		std::vector<RenderCore::Format> systemAttachmentsFormat { systemAttachmentFormatsInit.begin(), systemAttachmentFormatsInit.end() };
		auto noiseDelegateFuture = SceneEngine::CreatePerlinNoiseResources();
		auto weakThis = weak_from_this();
		AsyncConstructToPromise(
			result->AdoptPromise(),
			[preregAttachments=std::move(preregAttachments), futureFormatter=std::move(futureFormatter), futureLightScene=std::move(futureLightScene), visualizeStep=std::move(visualizeStep), systemAttachmentsFormat=std::move(systemAttachmentsFormat), noiseDelegateFuture=std::move(noiseDelegateFuture), weakThis]() mutable {
				std::shared_ptr<Formatters::IDynamicFormatter> formatter;
				TRY {
					auto l = weakThis.lock();
					if (!l) Throw(std::runtime_error("ShaderLab shutdown before construction finished"));

					YieldToPool(futureFormatter);
					formatter = futureFormatter.get();

					std::shared_ptr<RenderCore::LightingEngine::ILightScene> lightScene;
					if (futureLightScene) {
						futureLightScene->StallWhilePending();
						lightScene = futureLightScene->Actualize();
					}

					auto noiseDelegate = noiseDelegateFuture.get();	// stall 
				
					OperationConstructorContext constructorContext;
					auto outputRes = RenderCore::LightingEngine::Internal::ExtractOutputResolution(preregAttachments);
					RenderCore::FrameBufferProperties fbProps { outputRes[0], outputRes[1] };
					constructorContext._stitchingContext = { preregAttachments, fbProps, MakeIteratorRange(systemAttachmentsFormat) };
					constructorContext._depVal = ::Assets::GetDepValSys().Make();
					constructorContext._drawingApparatus = l->_drawingApparatus;
					constructorContext._bufferUploads = l->_bufferUploads;
					constructorContext._lightScene = lightScene;

					auto technique = std::make_shared<RenderCore::LightingEngine::CompiledLightingTechnique>();
					constructorContext._technique = technique.get();

					auto globalStateDelegate = std::make_shared<GlobalStateDelegate>();

					std::vector<std::pair<
						RenderCore::LightingEngine::LightingTechniqueSequence*, 
						std::vector<OperationConstructorContext::SetupFunction>>> registeredSequences;

					StringSection<> keyname;
					while (formatter->TryKeyedItem(keyname)) {
						auto constructor = std::find_if(l->_operationConstructors.begin(), l->_operationConstructors.end(),
							[keyname](const auto& p) { return XlEqString(keyname, p.first); });
						if (constructor != l->_operationConstructors.end()) {
							RequireBeginElement(*formatter);
							// out-of-sequence constructor. This should normally be used for operations that need to create their own
							// sequence (for example, dynamic shader preparation)
							constructor->second(*formatter, constructorContext, nullptr);
							assert(constructorContext._sequenceFinalizers.empty());
							registeredSequences.emplace_back(
								nullptr,
								std::move(constructorContext._postStitchFunctions));
							RequireEndElement(*formatter);
						} else if (XlEqString(keyname, "Sequence")) {
							// sequence
							RequireBeginElement(*formatter);

							auto& sequence = technique->CreateSequence();
							sequence.CreateStep_BindDelegate(globalStateDelegate);
							sequence.CreateStep_BindDelegate(noiseDelegate);
							sequence.CreateStep_InvalidateUniforms();
							sequence.CreateStep_BringUpToDateUniforms();

							ParseSequenceOperators(*formatter, constructorContext, sequence, l->_operationConstructors);

							registeredSequences.emplace_back(
								&sequence,
								std::move(constructorContext._postStitchFunctions));

							RequireEndElement(*formatter);
						} else {
							Throw(FormatException(StringMeld<256>() << "Unknown top level instruction: " << keyname, formatter->GetLocation()));
						}
					}

					if (visualizeStep) {
						visualizeStep->StallWhilePending();
						auto reqAttachments = visualizeStep->ActualizeBkgrnd()->GetRequiredAttachments();
						for (auto& seq:registeredSequences)
							if (seq.first)
								for (const auto& r:reqAttachments)
									seq.first->ForceRetainAttachment(r.first, r.second);
					}

					for (auto fn=constructorContext._techniqueFinalizers.rbegin(); fn!=constructorContext._techniqueFinalizers.rend(); ++fn)
						(*fn)(constructorContext, nullptr);

					technique->CompleteConstruction(
						l->_drawingApparatus->_pipelineAccelerators,
						constructorContext._stitchingContext);

					for (auto& seqAndFns:registeredSequences)
						for (auto& fn:seqAndFns.second)
							fn(constructorContext, seqAndFns.first);

					auto result = std::make_shared<CompiledTechnique>();
					result->_operation = std::move(technique);
					constructorContext._depVal.RegisterDependency(formatter->GetDependencyValidation());
					if (result->_operation->GetDependencyValidation())
						constructorContext._depVal.RegisterDependency(result->_operation->GetDependencyValidation());
					result->_depVal = std::move(constructorContext._depVal);
					result->_completionCommandList = constructorContext._completionCommandList;
					result->_globalStateDelegate = std::move(globalStateDelegate);
					return std::static_pointer_cast<ICompiledOperation>(result);
				} CATCH (const ::Assets::Exceptions::ConstructionError& e) {
					if (formatter) Throw(::Assets::Exceptions::ConstructionError(e, formatter->GetDependencyValidation()));
					throw;
				} CATCH (const ::Assets::Exceptions::InvalidAsset& e) {
					auto depVel = ::Assets::GetDepValSys().Make();
					if (formatter)
						depVel.RegisterDependency(formatter->GetDependencyValidation());
					if (e.GetDependencyValidation())
						depVel.RegisterDependency(e.GetDependencyValidation());
					Throw(::Assets::Exceptions::ConstructionError(e, depVel));
				} CATCH (const std::exception& e) {
					if (formatter) Throw(::Assets::Exceptions::ConstructionError(e, formatter->GetDependencyValidation()));
					throw;
				} CATCH_END
			});

		return result;
	}

	::Assets::PtrToMarkerPtr<ShaderLab::IVisualizeStep> ShaderLab::BuildVisualizeStep(
		std::future<std::shared_ptr<Formatters::IDynamicFormatter>> futureFormatter)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<ShaderLab::IVisualizeStep>>();
		auto weakThis = weak_from_this();
		AsyncConstructToPromise(
			result->AdoptPromise(),
			[futureFormatter=std::move(futureFormatter), weakThis]() mutable {
				auto l = weakThis.lock();
				if (!l) Throw(std::runtime_error("ShaderLab shutdown before construction finished"));

				YieldToPool(futureFormatter);
				auto formatter = futureFormatter.get();

				OperationConstructorContext constructorContext;
				constructorContext._depVal = ::Assets::GetDepValSys().Make();

				auto type = RequireKeyedItem(*formatter);

				auto constructor = std::find_if(l->_visualizeStepConstructors.begin(), l->_visualizeStepConstructors.end(),
					[type](const auto& p) { return XlEqString(type, p.first); });
				if (constructor == l->_visualizeStepConstructors.end()) {
					std::stringstream str;
					str << "Unknown visualize step (" << type << ")." << std::endl << "Try one of the following: ";
					bool first = true;
					for (const auto& c:l->_visualizeStepConstructors) {
						if (!first) str << ", ";
						first = false;
						str << c.first;
					}
					Throw(FormatException(str.str().c_str(), formatter->GetLocation()));
				}

				RequireBeginElement(*formatter);
				auto step = constructor->second(*formatter, constructorContext);
				RequireEndElement(*formatter);

				return step;
			});
		return result;
	}

	void ShaderLab::RegisterOperation(
		StringSection<> name,
		OperationConstructor&& constructor)
	{
		auto existing = std::find_if(_operationConstructors.begin(), _operationConstructors.end(),
			[name](const auto& p) { return XlEqString(name, p.first); });
		assert(existing == _operationConstructors.end());
		_operationConstructors.emplace_back(name.AsString(), std::move(constructor));
	}

	void ShaderLab::RegisterVisualizeStep(
		StringSection<> name,
		VisualizeStepConstructor&& constructor)
	{
		auto existing = std::find_if(_visualizeStepConstructors.begin(), _visualizeStepConstructors.end(),
			[name](const auto& p) { return XlEqString(name, p.first); });
		assert(existing == _visualizeStepConstructors.end());
		_visualizeStepConstructors.emplace_back(name.AsString(), std::move(constructor));
	}

	ShaderLab::ShaderLab(std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus, std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads)
	: _drawingApparatus(std::move(drawingApparatus))
	, _bufferUploads(std::move(bufferUploads))
	{}

	ShaderLab::~ShaderLab(){}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
		static Int2 GetCursorPos()
		{
			POINT cursorPos;
			GetCursorPos(&cursorPos);
			ScreenToClient((HWND)::GetActiveWindow(), &cursorPos);
			return Int2(cursorPos.x, cursorPos.y);
		}
	#else
		static Int2 GetCursorPos() { return {0,0}; }
	#endif

	class VisualizeAttachment : public ToolsRig::ShaderLab::IVisualizeStep
	{
	public:
		void Execute(
			RenderCore::Techniques::ParsingContext& parsingContext,
			RenderCore::Techniques::DrawingApparatus& drawingApparatus,
			RenderOverlays::OverlayApparatus& immediateDrawingApparatus) override
		{
			using namespace RenderCore;

			// since we're writing to ColorLDR, never attempt to copy this onto itself
			auto attachmentSemantic = Hash64(AsPointer(_attachmentName.begin()), AsPointer(_attachmentName.end()));
			if (attachmentSemantic == RenderCore::Techniques::AttachmentSemantics::ColorLDR) return;

			// update graphics descriptor set, because we've probably just done bunch of unbind operations
			parsingContext.GetUniformDelegateManager()->BringUpToDateGraphics(parsingContext);

			Techniques::FrameBufferDescFragment fragment;

			auto preRegAttachments = parsingContext.GetFragmentStitchingContext().GetPreregisteredAttachments();
			auto i = std::find_if(preRegAttachments.begin(), preRegAttachments.end(),
				[attachmentSemantic](const auto& c) { return c._semantic == attachmentSemantic; });
			if (i != preRegAttachments.end()) {
				TRY {
					Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
					spDesc.AppendOutput(fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).Clear());
					spDesc.AppendNonFrameBufferAttachmentView(fragment.DefineAttachment(attachmentSemantic));
					spDesc.SetName("visualize");
					fragment.AddSubpass(std::move(spDesc));

					Techniques::RenderPassInstance rpi{parsingContext, fragment};
					auto attachmentSRV = rpi.GetNonFrameBufferAttachmentView(0);

					UniformsStreamInterface usi;
					usi.BindResourceView(0, "VisualizeInput"_h);
					usi.BindImmediateData(0, "DebuggingGlobals"_h);
					UniformsStream us;
					IResourceView* srvs[] = { attachmentSRV.get() };
					us._resourceViews = MakeIteratorRange(srvs);

					struct DebuggingGlobals
					{
						UInt2 _viewportDimension;
						UInt2 _mousePosition;
					} debuggingGlobals { UInt2 { parsingContext.GetViewport()._width, parsingContext.GetViewport()._height }, GetCursorPos() };
					UniformsStream::ImmediateData immData[] = {
						MakeOpaqueIteratorRange(debuggingGlobals)
					};
					us._immediateData = MakeIteratorRange(immData);

					Techniques::PixelOutputStates outputStates;
					outputStates.Bind(rpi);
					outputStates.Bind(Techniques::CommonResourceBox::s_dsDisable);
					AttachmentBlendDesc blendStates[] { Techniques::CommonResourceBox::s_abStraightAlpha };
					outputStates.Bind(MakeIteratorRange(blendStates));
					auto op = Techniques::CreateFullViewportOperator(
						drawingApparatus._graphicsPipelinePool,
						Techniques::FullViewportOperatorSubType::DisableDepth,
						VISUALIZE_ATTACHMENT_PIXEL_HLSL ":main", _shaderSelectors, 
						GENERAL_OPERATOR_PIPELINE ":GraphicsMain", outputStates,
						usi);
					op->Actualize()->Draw(parsingContext, us);
				} CATCH (::Assets::Exceptions::InvalidAsset& e) {
					std::stringstream str;
					str << "Error in visualize shader:" << std::endl;
					str << ::Assets::AsString(e.GetActualizationLog());
					RenderOverlays::FillScreenWithMsg(parsingContext, immediateDrawingApparatus, str.str());
				} CATCH_END
			} else {
				std::stringstream str;
				str << "Attachment with semantic (" << _attachmentName << ") was not found. Try the following:" << std::endl;
				bool first = true;
				for (const auto& attachment:preRegAttachments) {
					if (!first) str << ", ";
					first = false;
					auto* dehash = RenderCore::Techniques::AttachmentSemantics::TryDehash(attachment._semantic);
					if (dehash) str << dehash;
					else str << std::hex << attachment._semantic;
				}
				RenderOverlays::FillScreenWithMsg(parsingContext, immediateDrawingApparatus, str.str());
			}
		}

		virtual const ::Assets::DependencyValidation& GetDependencyValidation() const override
		{ 
			static ::Assets::DependencyValidation s_dummy;
			return s_dummy;
		};

		virtual auto GetRequiredAttachments() const -> std::vector<std::pair<uint64_t, RenderCore::BindFlag::BitField>> override
		{
			auto attachmentHash = Hash64(AsPointer(_attachmentName.begin()), AsPointer(_attachmentName.end()));
			if (attachmentHash == RenderCore::Techniques::AttachmentSemantics::ColorLDR) return {};
			return {std::make_pair(attachmentHash, RenderCore::BindFlag::ShaderResource)};
		}

		VisualizeAttachment(
			StringSection<> attachmentName,
			VisualizeAttachmentShader shader)
		: _attachmentName(attachmentName.AsString())
		{
			_shaderSelectors.SetParameter("VISUALIZE_TYPE", (unsigned)shader);
		}

		std::string _attachmentName;
		ParameterBox _shaderSelectors;
	};

	const char* AsString(VisualizeAttachmentShader shader)
	{
		switch (shader) {
		case VisualizeAttachmentShader::Color: return "Color";
		case VisualizeAttachmentShader::Normal: return "Normal";
		case VisualizeAttachmentShader::Depth: return "Depth";
		case VisualizeAttachmentShader::Motion: return "Motion";
		case VisualizeAttachmentShader::Alpha: return "Alpha";
		case VisualizeAttachmentShader::GreyScale: return "GreyScale";
		case VisualizeAttachmentShader::GBufferNormals: return "GBufferNormals";
		default: return nullptr;
		}
	}
	
	std::optional<VisualizeAttachmentShader> AsVisualizeAttachmentShader(StringSection<> shader)
	{
		if (XlEqString(shader, "Color")) return VisualizeAttachmentShader::Color;
		if (XlEqString(shader, "Normal")) return VisualizeAttachmentShader::Normal;
		if (XlEqString(shader, "Depth")) return VisualizeAttachmentShader::Depth;
		if (XlEqString(shader, "Motion")) return VisualizeAttachmentShader::Motion;
		if (XlEqString(shader, "Alpha")) return VisualizeAttachmentShader::Alpha;
		if (XlEqString(shader, "GreyScale")) return VisualizeAttachmentShader::GreyScale;
		if (XlEqString(shader, "GBufferNormals")) return VisualizeAttachmentShader::GBufferNormals;
		return {};
	}

	static VisualizeAttachmentShader DefaultVisualizeAttachmentShader(StringSection<> attachmentName)
	{
		using namespace RenderCore::Techniques::AttachmentSemantics;
		auto semantic = Hash64(AsPointer(attachmentName.begin()), AsPointer(attachmentName.end()));
		if (semantic == GBufferNormal) return VisualizeAttachmentShader::GBufferNormals;
		else if (semantic == GBufferMotion) return VisualizeAttachmentShader::Motion;
		else if (semantic == Depth) return VisualizeAttachmentShader::Depth;
		else if (semantic == ShadowDepthMap) return VisualizeAttachmentShader::Depth;
		else if (semantic == HierarchicalDepths) return VisualizeAttachmentShader::Depth;
		return VisualizeAttachmentShader::Color;
	}

	void RegisterVisualizeAttachment(ShaderLab& shaderLab)
	{
		shaderLab.RegisterVisualizeStep(
			"VisualizeAttachment",
			[](Formatters::IDynamicFormatter& formatter, ToolsRig::ShaderLab::OperationConstructorContext& context) {
				std::optional<VisualizeAttachmentShader> shader;
				StringSection<> attachmentName;
				StringSection<> keyname;
				while (formatter.TryKeyedItem(keyname)) {
					if (XlEqString(keyname, "Attachment")) {
						attachmentName = RequireStringValue(formatter);
						if (!shader)
							shader = DefaultVisualizeAttachmentShader(attachmentName);
					} else if (XlEqString(keyname, "Shader")) {
						shader = RequireEnum<VisualizeAttachmentShader, AsVisualizeAttachmentShader>(formatter);
					} else
						formatter.SkipValueOrElement();
				}
				if (attachmentName.IsEmpty())
					Throw(FormatException("Expecting 'Attachment' key", formatter.GetLocation()));
				return std::make_shared<VisualizeAttachment>(attachmentName, shader.value());
			});
	}

}


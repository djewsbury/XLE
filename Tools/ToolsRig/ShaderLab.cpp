// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLab.h"
#include "../../Tools/ToolsRig/ToolsRigServices.h"
#include "../../Tools/EntityInterface/EntityInterface.h"
#include "../../Tools/EntityInterface/FormatterAdapters.h"
#include "../../RenderOverlays/SimpleVisualization.h"
#include "../../RenderCore/LightingEngine/LightingEngineInternal.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/PipelineOperators.h"
#include "../../RenderCore/Techniques/CommonBindings.h"
#include "../../RenderCore/Techniques/DrawableDelegates.h"
#include "../../RenderCore/UniformsStream.h"
#include "../../RenderCore/FrameBufferDesc.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Assets/AssetsCore.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../xleres/FileList.h"
#include <sstream>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
	#include "../../OSServices/WinAPI/IncludeWindows.h"
#endif

namespace ToolsRig
{
	class CompiledTechnique : public ShaderLab::ICompiledOperation
	{
	public:
		virtual RenderCore::LightingEngine::CompiledLightingTechnique& GetLightingTechnique() const override { assert(_operation); return *_operation; }
		const ::Assets::DependencyValidation& GetDependencyValidation() const override  { return _depVal; }
		virtual unsigned GetCompletionCommandList() const override { return _completionCommandList; }
		std::shared_ptr<RenderCore::LightingEngine::CompiledLightingTechnique> _operation;
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
	
	::Assets::PtrToMarkerPtr<ShaderLab::ICompiledOperation> ShaderLab::BuildCompiledTechnique(
		::Assets::PtrToMarkerPtr<Formatters::IDynamicFormatter> futureFormatter,
		::Assets::PtrToMarkerPtr<IVisualizeStep> visualizeStep,
		IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachmentsInit,
		const RenderCore::FrameBufferProperties& fBProps,
		IteratorRange<const RenderCore::Format*> systemAttachmentFormatsInit)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<ShaderLab::ICompiledOperation>>();
		std::vector<RenderCore::Techniques::PreregisteredAttachment> preregAttachments { preregAttachmentsInit.begin(), preregAttachmentsInit.end() };
		std::vector<RenderCore::Format> systemAttachmentsFormat { systemAttachmentFormatsInit.begin(), systemAttachmentFormatsInit.end() };
		auto weakThis = weak_from_this();
		AsyncConstructToPromise(
			result->AdoptPromise(),
			[preregAttachments=std::move(preregAttachments), fBProps=fBProps, futureFormatter=std::move(futureFormatter), visualizeStep=std::move(visualizeStep), systemAttachmentsFormat=std::move(systemAttachmentsFormat), weakThis]() {
				auto l = weakThis.lock();
				if (!l) Throw(std::runtime_error("ShaderLab shutdown before construction finished"));

				futureFormatter->StallWhilePending();
				auto formatter = futureFormatter->Actualize();

				TRY {
					OperationConstructorContext constructorContext;
					constructorContext._stitchingContext = { preregAttachments, fBProps, MakeIteratorRange(systemAttachmentsFormat) };
					constructorContext._depVal = ::Assets::GetDepValSys().Make();
					constructorContext._drawingApparatus = l->_drawingApparatus;
					constructorContext._bufferUploads = l->_bufferUploads;

					{
						StringSection<> keyname;
						while (formatter->TryKeyedItem(keyname)) {
							auto constructor = std::find_if(l->_operationConstructors.begin(), l->_operationConstructors.end(),
								[keyname](const auto& p) { return XlEqString(keyname, p.first); });
							if (constructor == l->_operationConstructors.end()) {
								std::stringstream str;
								str << "Unknown operation (" << keyname << ")." << std::endl << "Try one of the following: ";
								bool first = true;
								for (const auto& c:l->_operationConstructors) {
									if (!first) str << ", ";
									first = false;
									str << c.first;
								}
								Throw(FormatException(str.str().c_str(), formatter->GetLocation()));
							}

							RequireBeginElement(*formatter);
							constructor->second(*formatter, constructorContext);
							RequireEndElement(*formatter);
						}
					}

					auto technique = std::make_shared<RenderCore::LightingEngine::CompiledLightingTechnique>();
					auto& sequence = technique->CreateSequence();
					sequence.CreateStep_CallFunction(
						[](RenderCore::LightingEngine::LightingTechniqueIterator& iterator) {
							iterator._parsingContext->GetUniformDelegateManager()->InvalidateUniforms();
							iterator._parsingContext->GetUniformDelegateManager()->BringUpToDateGraphics(*iterator._parsingContext);
						});
					for (auto& fn:constructorContext._setupFunctions) fn(sequence);

					if (visualizeStep) {
						visualizeStep->StallWhilePending();
						auto reqAttachments = visualizeStep->ActualizeBkgrnd()->GetRequiredAttachments();
						for (const auto& r:reqAttachments)
							sequence.ForceRetainAttachment(r.first, r.second);
					}
					
					technique->CompleteConstruction(
						l->_drawingApparatus->_pipelineAccelerators,
						constructorContext._stitchingContext);

					auto result = std::make_shared<CompiledTechnique>();
					result->_operation = std::move(technique);
					constructorContext._depVal.RegisterDependency(formatter->GetDependencyValidation());
					if (result->_operation->GetDependencyValidation())
						constructorContext._depVal.RegisterDependency(result->_operation->GetDependencyValidation());
					result->_depVal = std::move(constructorContext._depVal);
					result->_completionCommandList = constructorContext._completionCommandList;
					return std::static_pointer_cast<ICompiledOperation>(result);
				} CATCH (const ::Assets::Exceptions::ConstructionError& e) {
					Throw(::Assets::Exceptions::ConstructionError(e, formatter->GetDependencyValidation()));
				} CATCH (const ::Assets::Exceptions::InvalidAsset& e) {
					auto depVel = ::Assets::GetDepValSys().Make();
					depVel.RegisterDependency(formatter->GetDependencyValidation());
					depVel.RegisterDependency(e.GetDependencyValidation());
					Throw(::Assets::Exceptions::ConstructionError(e, depVel));
				} CATCH (const std::exception& e) {
					Throw(::Assets::Exceptions::ConstructionError(e, formatter->GetDependencyValidation()));
				} CATCH_END
			});

		return result;
	}

	::Assets::PtrToMarkerPtr<ShaderLab::IVisualizeStep> ShaderLab::BuildVisualizeStep(
		::Assets::PtrToMarkerPtr<Formatters::IDynamicFormatter> futureFormatter)
	{
		auto result = std::make_shared<::Assets::MarkerPtr<ShaderLab::IVisualizeStep>>();
		auto weakThis = weak_from_this();
		AsyncConstructToPromise(
			result->AdoptPromise(),
			[futureFormatter=std::move(futureFormatter), weakThis]() {
				auto l = weakThis.lock();
				if (!l) Throw(std::runtime_error("ShaderLab shutdown before construction finished"));

				futureFormatter->StallWhilePending();
				auto formatter = futureFormatter->Actualize();

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

	ShaderLab::ShaderLab(std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus, std::shared_ptr<BufferUploads::IManager> bufferUploads)
	: _drawingApparatus(std::move(drawingApparatus))
	, _bufferUploads(std::move(bufferUploads))
	{
	}

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
			RenderCore::Techniques::ImmediateDrawingApparatus& immediateDrawingApparatus) override
		{
			using namespace RenderCore;

			Techniques::FrameBufferDescFragment fragment;

			auto attachmentSemantic = ConstHash64FromString(AsPointer(_attachmentName.begin()), AsPointer(_attachmentName.end()));
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
					usi.BindResourceView(0, Hash64("VisualizeInput"));
					usi.BindImmediateData(0, Hash64("DebuggingGlobals"));
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
			return {std::make_pair(ConstHash64FromString(AsPointer(_attachmentName.begin()), AsPointer(_attachmentName.end())), RenderCore::BindFlag::ShaderResource)};
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
		auto semantic = ConstHash64FromString(AsPointer(attachmentName.begin()), AsPointer(attachmentName.end()));
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


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../Assets/AssetsCore.h"
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace LightingEngine { class CompiledLightingTechnique; class LightingTechniqueSequence; class LightingTechniqueIterator; class ILightScene; }}
namespace RenderCore { namespace Techniques { class PreregisteredAttachment; class DrawingApparatus; class FragmentStitchingContext; class ImmediateDrawingApparatus; }}
namespace RenderCore { class FrameBufferProperties; }
namespace BufferUploads { class IManager; }
namespace Formatters { class IDynamicFormatter; }

namespace ToolsRig
{
	class ShaderLab : public std::enable_shared_from_this<ShaderLab>
	{
	public:
		class IVisualizeStep
		{
		public:
			virtual void Execute(
				RenderCore::Techniques::ParsingContext& parsingContext,
				RenderCore::Techniques::DrawingApparatus& drawingApparatus,
				RenderCore::Techniques::ImmediateDrawingApparatus& immediateDrawingApparatus) = 0;
			virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;
			virtual auto GetRequiredAttachments() const -> std::vector<std::pair<uint64_t, RenderCore::BindFlag::BitField>> = 0;
			virtual ~IVisualizeStep() = default;
		};

		class ICompiledOperation
		{
		public:
			virtual RenderCore::LightingEngine::CompiledLightingTechnique& GetLightingTechnique() const = 0;
			virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;
			virtual unsigned GetCompletionCommandList() const = 0;
			virtual void AdvanceTime(float) = 0;
			virtual ~ICompiledOperation() = default;
		};

		::Assets::PtrToMarkerPtr<ICompiledOperation> BuildCompiledTechnique(
			::Assets::PtrToMarkerPtr<Formatters::IDynamicFormatter> futureFormatter,
			::Assets::PtrToMarkerPtr<IVisualizeStep> visualizeStep,
			::Assets::PtrToMarkerPtr<RenderCore::LightingEngine::ILightScene> lightScene,
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachmentsInit,
			const RenderCore::FrameBufferProperties& fBProps,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats);

		::Assets::PtrToMarkerPtr<IVisualizeStep> BuildVisualizeStep(
			::Assets::PtrToMarkerPtr<Formatters::IDynamicFormatter> futureFormatter);

		struct OperationConstructorContext
		{
			using SetupFunction = std::function<void(RenderCore::LightingEngine::LightingTechniqueSequence&)>;
			using DynamicSequenceFunction = std::function<void(RenderCore::LightingEngine::LightingTechniqueIterator&, RenderCore::LightingEngine::LightingTechniqueSequence&)>;
			std::vector<SetupFunction> _setupFunctions;
			std::vector<DynamicSequenceFunction> _dynamicSequenceFunctions;
			RenderCore::Techniques::FragmentStitchingContext _stitchingContext;
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
			std::shared_ptr<BufferUploads::IManager> _bufferUploads;
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> _lightScene;
			unsigned _completionCommandList = 0;
			::Assets::DependencyValidation _depVal;
		};
		using OperationConstructor = std::function<void(Formatters::IDynamicFormatter&, OperationConstructorContext&)>;
		void RegisterOperation(
			StringSection<> name,
			OperationConstructor&& constructor);

		using VisualizeStepConstructor = std::function<std::shared_ptr<IVisualizeStep>(Formatters::IDynamicFormatter&, OperationConstructorContext&)>;
		void RegisterVisualizeStep(
			StringSection<> name,
			VisualizeStepConstructor&& constructor);

		ShaderLab(
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
			std::shared_ptr<BufferUploads::IManager> bufferUploads);
		~ShaderLab();
	private:
		std::vector<std::pair<std::string, OperationConstructor>> _operationConstructors;
		std::vector<std::pair<std::string, VisualizeStepConstructor>> _visualizeStepConstructors;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
	};

	void RegisterVisualizeAttachment(ShaderLab&);

	enum class VisualizeAttachmentShader { Color, Depth, Normal, Motion, Alpha, GreyScale, GBufferNormals };
	const char* AsString(VisualizeAttachmentShader);
	std::optional<VisualizeAttachmentShader> AsVisualizeAttachmentShader(StringSection<>);
}


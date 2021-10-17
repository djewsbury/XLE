// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../Assets/AssetsCore.h"
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace LightingEngine { class CompiledLightingTechnique; }}
namespace RenderCore { namespace Techniques { class PreregisteredAttachment; class DrawingApparatus; class FragmentStitchingContext; class ImmediateDrawingApparatus; }}
namespace RenderCore { class FrameBufferProperties; }
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
			virtual ~IVisualizeStep() = default;
		};

		class ICompiledOperation
		{
		public:
			virtual RenderCore::LightingEngine::CompiledLightingTechnique& GetLightingTechnique() const = 0;
			virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;
			virtual unsigned GetCompletionCommandList() const = 0;
			virtual ~ICompiledOperation() = default;
		};

		::Assets::PtrToFuturePtr<ICompiledOperation> BuildCompiledTechnique(
			::Assets::PtrToFuturePtr<Formatters::IDynamicFormatter> futureFormatter,
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachmentsInit,
			const RenderCore::FrameBufferProperties& fBProps);

		::Assets::PtrToFuturePtr<IVisualizeStep> BuildVisualizeStep(
			::Assets::PtrToFuturePtr<Formatters::IDynamicFormatter> futureFormatter);

		struct OperationConstructorContext
		{
			using SetupFunctionList = std::vector<std::function<void(RenderCore::LightingEngine::CompiledLightingTechnique&)>>;
			SetupFunctionList _setupFunctions;
			RenderCore::Techniques::FragmentStitchingContext _stitchingContext;
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
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

		ShaderLab(std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus);
		~ShaderLab();
	private:
		std::vector<std::pair<std::string, OperationConstructor>> _operationConstructors;
		std::vector<std::pair<std::string, VisualizeStepConstructor>> _visualizeStepConstructors;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
	};

	void RegisterVisualizeAttachment(ShaderLab&);

	enum class VisualizeAttachmentShader { Color, Depth, Normal, Motion, Alpha, GreyScale };
	const char* AsString(VisualizeAttachmentShader);
	std::optional<VisualizeAttachmentShader> AsVisualizeAttachmentShader(StringSection<>);
}


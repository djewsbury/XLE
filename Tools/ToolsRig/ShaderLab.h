// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../Assets/AssetsCore.h"
#include "../../Math/Matrix.h"
#include <memory>
#include <vector>
#include <functional>

namespace RenderCore { namespace LightingEngine { class CompiledLightingTechnique; class Sequence; class SequenceIterator; class ILightScene; }}
namespace RenderCore { namespace Techniques { struct PreregisteredAttachment; class DrawingApparatus; class FragmentStitchingContext; struct DoubleBufferAttachment; class DrawablesPacket; }}
namespace RenderCore { class FrameBufferProperties; }
namespace RenderCore { namespace BufferUploads { class IManager; }}
namespace RenderOverlays { class OverlayApparatus; }
namespace Formatters { class IDynamicInputFormatter; }
namespace Assets { class OperationContext; }
namespace std { template<typename T> class future; }

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
				RenderOverlays::OverlayApparatus& immediateDrawingApparatus) = 0;
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

		class IResource
		{
		public:
			virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;
			virtual ~IResource() = default;
		};

		class IBuildDrawablesResource
		{
		public:
			virtual void BuildDrawables(
				RenderCore::Techniques::ParsingContext& parsingContext,
				IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
				const Float4x4& localToWorld,
				uint32_t viewMask=1,
				uint64_t cmdStream=0) = 0;
			virtual ~IBuildDrawablesResource() = default;
		};

		class ResourceSet
		{
		public:
			std::shared_ptr<IResource> TryGetResource(uint64_t) const;
			std::vector<std::pair<uint64_t, std::shared_ptr<IResource>>> _constructedResources;
			friend class ShaderLab;
		};

		::Assets::PtrToMarkerPtr<ICompiledOperation> BuildCompiledTechnique(
			std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> futureFormatter,
			::Assets::PtrToMarkerPtr<IVisualizeStep> visualizeStep,
			::Assets::PtrToMarkerPtr<ResourceSet> resourceSet,
			::Assets::PtrToMarkerPtr<RenderCore::LightingEngine::ILightScene> lightScene,
			IteratorRange<const RenderCore::Techniques::PreregisteredAttachment*> preregAttachmentsInit,
			IteratorRange<const RenderCore::Format*> systemAttachmentFormats);

		::Assets::PtrToMarkerPtr<IVisualizeStep> BuildVisualizeStep(
			std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> futureFormatter);

		::Assets::PtrToMarkerPtr<ResourceSet> BuildResourceSet(
			std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> futureFormatter);

		struct OperationConstructorContext
		{
			using SetupFunction = std::function<void(OperationConstructorContext&, RenderCore::LightingEngine::Sequence*)>;
			std::vector<SetupFunction> _sequenceFinalizers;
			std::vector<SetupFunction> _postStitchFunctions;

			std::vector<SetupFunction> _techniqueFinalizers;

			RenderCore::LightingEngine::CompiledLightingTechnique* _technique = nullptr;
			ResourceSet* _resourceSet = nullptr;
			RenderCore::Techniques::FragmentStitchingContext _stitchingContext;
			RenderCore::FrameBufferProperties _fbProps;
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
			std::shared_ptr<RenderCore::BufferUploads::IManager> _bufferUploads;
			std::shared_ptr<RenderCore::LightingEngine::ILightScene> _lightScene;
			std::shared_ptr<::Assets::OperationContext> _loadingContext;
			unsigned _completionCommandList = 0;
			::Assets::DependencyValidation _depVal;
		};
		using OperationConstructor = std::function<void(Formatters::IDynamicInputFormatter&, OperationConstructorContext&, RenderCore::LightingEngine::Sequence*)>;
		void RegisterOperation(
			StringSection<> name,
			OperationConstructor&& constructor);

		using VisualizeStepConstructor = std::function<std::shared_ptr<IVisualizeStep>(Formatters::IDynamicInputFormatter&, OperationConstructorContext&)>;
		void RegisterVisualizeStep(
			StringSection<> name,
			VisualizeStepConstructor&& constructor);

		struct ResourceConstructorContext
		{
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
			std::shared_ptr<RenderCore::BufferUploads::IManager> _bufferUploads;
			std::shared_ptr<::Assets::OperationContext> _loadingContext;
		};
		using ResourceConstructor = std::function<std::shared_ptr<IResource>(Formatters::IDynamicInputFormatter&, ResourceConstructorContext&)>;
		void RegisterResource(
			StringSection<> name, uint64_t resourceTypeCode,
			ResourceConstructor&& constructor);

		ShaderLab(
			std::shared_ptr<RenderCore::Techniques::DrawingApparatus> drawingApparatus,
			std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
			std::shared_ptr<::Assets::OperationContext> loadingContext);
		~ShaderLab();
	private:
		std::vector<std::pair<std::string, OperationConstructor>> _operationConstructors;
		std::vector<std::pair<std::string, VisualizeStepConstructor>> _visualizeStepConstructors;
		struct RC { ResourceConstructor _constructor; uint64_t _typeCode; };
		std::vector<std::pair<std::string, RC>> _resourceConstructors;
		std::shared_ptr<RenderCore::Techniques::DrawingApparatus> _drawingApparatus;
		std::shared_ptr<RenderCore::BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<::Assets::OperationContext> _loadingContext;
	};

	void RegisterVisualizeAttachment(ShaderLab&);

	enum class VisualizeAttachmentShader { Color, Depth, Normal, Motion, Alpha, GreyScale, GBufferNormals };
	const char* AsString(VisualizeAttachmentShader);
	std::optional<VisualizeAttachmentShader> AsVisualizeAttachmentShader(StringSection<>);
}


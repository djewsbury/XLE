// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/Drawables.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../BufferUploads/IBufferUploads.h"
#include "../../../Math/Vector.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "../../../ConsoleRig/GlobalServices.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"

namespace RenderCore { namespace Techniques
{
	class TechniqueContext;
	class CameraDesc;
	class CommonResourceBox;
	class PipelineCollection;
}}

namespace RenderCore { namespace LightingEngine
{
	class SharedTechniqueDelegateBox;
	class LightingTechniqueInstance;
}}

namespace Assets { class CompilerRegistration; }
namespace ToolsRig { class IDrawablesWriter; }

namespace UnitTests
{
	class LightingEngineTestApparatus
	{
	public:
		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
		uint32_t _xleresmnt;
		std::unique_ptr<MetalTestHelper> _metalTestHelper;
		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;

		std::vector<::Assets::CompilerRegistration> _compilerRegistrations;

		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::PipelineCollection> _pipelinePool;
		std::shared_ptr<RenderCore::LightingEngine::SharedTechniqueDelegateBox> _sharedDelegates;
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox> _commonResources;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
		
		LightingEngineTestApparatus();
		~LightingEngineTestApparatus();
	};

	struct LightingOperatorsPipelineLayout
	{
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> _pipelineLayoutFile;
		std::shared_ptr<RenderCore::ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _dmShadowDescSetTemplate;
		std::shared_ptr<RenderCore::SamplerPool> _samplerPool;

		LightingOperatorsPipelineLayout(const MetalTestHelper& testHelper);
	};

	void ParseScene(RenderCore::LightingEngine::LightingTechniqueInstance& lightingIterator, ToolsRig::IDrawablesWriter& drawableWriter);

	RenderCore::Techniques::ParsingContext InitializeParsingContext(
		RenderCore::Techniques::TechniqueContext& techniqueContext,
		const RenderCore::ResourceDesc& targetDesc,
		const RenderCore::Techniques::CameraDesc& camera,
		RenderCore::IThreadContext& threadContext);
}

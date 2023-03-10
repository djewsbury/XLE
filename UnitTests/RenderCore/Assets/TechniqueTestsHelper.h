// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../RenderCore/BufferUploads/IBufferUploads.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/AssetServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"

namespace RenderCore { namespace Techniques { class TechniqueContext; class IPipelineLayoutDelegate; class IDrawablesPool; class DrawablesPacket; struct PreparedResourcesVisibility; class PipelineCollection; }}

namespace UnitTests
{
	class TechniqueTestApparatus
	{
	public:
		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
		std::shared_ptr<RenderCore::BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox> _commonResources;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;
		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
		std::shared_ptr<RenderCore::Techniques::PipelineCollection> _pipelineCollection;
		std::shared_ptr<RenderCore::Techniques::IPipelineLayoutDelegate> _pipelineLayoutDelegate;

		::Assets::CompilerRegistration _filteringRegistration;
		::Assets::CompilerRegistration _shaderCompilerRegistration;
		::Assets::CompilerRegistration _shaderCompiler2Registration;

		static const char UnitTestPipelineLayout[];

		TechniqueTestApparatus(MetalTestHelper& testHelper);
		~TechniqueTestApparatus();
	};

	RenderCore::Techniques::PreparedResourcesVisibility PrepareAndStall(
		TechniqueTestApparatus& testApparatus,
		const RenderCore::Techniques::SequencerConfig& sequencerConfig,
		const RenderCore::Techniques::DrawablesPacket& drawablePkt);

	RenderCore::Techniques::ParsingContext BeginParsingContext(TechniqueTestApparatus& testApparatus, RenderCore::IThreadContext& threadContext);
}

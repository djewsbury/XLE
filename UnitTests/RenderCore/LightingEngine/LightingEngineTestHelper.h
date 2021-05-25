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
	class TechniqueSharedResources;
	class TechniqueContext;
	class CameraDesc;
}}

namespace RenderCore { namespace LightingEngine
{
	class SharedTechniqueDelegateBox;
	class LightingTechniqueInstance;
}}

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

		std::shared_ptr<thousandeyes::futures::DefaultExecutor> _futureExecutor;
		std::unique_ptr<thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter> _futureExecSetter;

		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::TechniqueSharedResources> _techniquesSharedResources;
		std::shared_ptr<RenderCore::LightingEngine::SharedTechniqueDelegateBox> _techDelBox;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;

		LightingEngineTestApparatus();
		~LightingEngineTestApparatus();
	};

	class IDrawablesWriter
	{
	public:
		virtual void WriteDrawables(RenderCore::Techniques::DrawablesPacket& pkt) = 0;
		virtual ~IDrawablesWriter() = default;
	};

	std::shared_ptr<IDrawablesWriter> CreateSphereDrawablesWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateShapeStackDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateStonehengeDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateFlatPlaneDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateSharpContactDrawableWriter(MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool);
	std::shared_ptr<IDrawablesWriter> CreateShapeWorldDrawableWriter(
		MetalTestHelper& testHelper, RenderCore::Techniques::IPipelineAcceleratorPool& pipelineAcceleratorPool,
		const Float2& worldMins, const Float2& worldMaxs);

	void ParseScene(RenderCore::LightingEngine::LightingTechniqueInstance& lightingIterator, IDrawablesWriter& drawableWriter);

	RenderCore::Techniques::ParsingContext InitializeParsingContext(
		RenderCore::Techniques::TechniqueContext& techniqueContext,
		const RenderCore::ResourceDesc& targetDesc,
		const RenderCore::Techniques::CameraDesc& camera);
}

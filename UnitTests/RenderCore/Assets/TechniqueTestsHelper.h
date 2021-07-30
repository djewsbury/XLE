// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../BufferUploads/IBufferUploads.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include <regex>

namespace RenderCore { namespace Techniques { class TechniqueContext; }}

namespace UnitTests
{
	class TechniqueTestApparatus
	{
	public:
		thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter _futureSetter;

		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox> _commonResources;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;

		RenderCore::Techniques::DescriptorSetLayoutAndBinding _materialDescSetLayout;
		RenderCore::Techniques::DescriptorSetLayoutAndBinding _sequencerDescSetLayout;

		::Assets::CompilerRegistration _filteringRegistration;
		::Assets::CompilerRegistration _shaderCompilerRegistration;
		::Assets::CompilerRegistration _shaderCompiler2Registration;

		static const char UnitTestPipelineLayout[];

		TechniqueTestApparatus(MetalTestHelper& testHelper);
		~TechniqueTestApparatus();
	};
}

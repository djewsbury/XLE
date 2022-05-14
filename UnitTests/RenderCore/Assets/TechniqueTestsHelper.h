// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Metal/MetalTestHelper.h"
#include "../../../RenderCore/Techniques/Services.h"
#include "../../../RenderCore/Techniques/PipelineOperators.h"
#include "../../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../../RenderCore/MinimalShaderSource.h"
#include "../../../BufferUploads/IBufferUploads.h"
#include "../../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../../Assets/CompileAndAsyncManager.h"
#include "../../../Assets/AssetServices.h"
#include "../../../ConsoleRig/AttachablePtr.h"
#include <regex>

namespace RenderCore { namespace Techniques { class TechniqueContext; class ICompiledLayoutPool; }}

namespace UnitTests
{
	class TechniqueTestApparatus
	{
	public:
		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniqueServices;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<RenderCore::Techniques::CommonResourceBox> _commonResources;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;
		std::shared_ptr<RenderCore::Techniques::ICompiledLayoutPool> _compiledLayoutPool;

		std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> _materialDescSetLayout;
		std::shared_ptr<RenderCore::Techniques::DescriptorSetLayoutAndBinding> _sequencerDescSetLayout;

		::Assets::CompilerRegistration _filteringRegistration;
		::Assets::CompilerRegistration _shaderCompilerRegistration;
		::Assets::CompilerRegistration _shaderCompiler2Registration;

		static const char UnitTestPipelineLayout[];

		TechniqueTestApparatus(MetalTestHelper& testHelper);
		~TechniqueTestApparatus();
	};
}

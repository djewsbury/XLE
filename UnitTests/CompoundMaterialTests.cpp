// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UnitTestHelper.h"
#include "ReusableDataFiles.h"
#include "MetalUnitTest.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Techniques/DrawableDelegates.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Techniques/CompiledShaderPatchCollection.h"
#include "../RenderCore/Techniques/DescriptorSetAccelerator.h"
#include "../RenderCore/Techniques/Services.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/PipelineLayout.h"
#include "../RenderCore/Assets/Services.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../RenderCore/ResourceDesc.h"
#include "../RenderCore/FrameBufferDesc.h"
#include "../BufferUploads/BufferUploads_Manager.h"
#include "../Assets/AssetServices.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/OSFileSystem.h"
#include "../Assets/MountingTree.h"
#include "../Assets/MemoryFile.h"
#include "../Assets/DepVal.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/AssetSetManager.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../xleres/FileList.h"
#include <map>

#if !defined(XC_TEST_ADAPTER)
    #include <CppUnitTest.h>
    using namespace Microsoft::VisualStudio::CppUnitTestFramework;
	#define ThrowsException ExpectException<const std::exception&>
#endif

static const char s_exampleCompoundMaterialFile[] = R"**(
	// CompoundDocument:1

	DeformedVertex DeformVertex(VSIN input)
	{
		DeformedVertex dv;
		dv.position = float3(1, 0, 1);
		dv.tangentFrame.basisVector0 = 0.0.xxx;
		dv.tangentFrame.basisVector1 = 0.0.xxx;
		dv.tangentFrame.handiness = 0;
		dv.coordinateSpace = 1;
		return dv;
	}

	/* <<Chunk:RawMaterial:main>>--(

	~sphere
		~Patches
			~main
				<.>::DeformVertex
		~ShaderParams
			SHAPE=1

	)--*/


)**";

namespace UnitTests
{
	static std::unordered_map<std::string, ::Assets::Blob> s_utData {
		std::make_pair("compoundmaterial.hlsl", ::Assets::AsBlob(s_exampleCompoundMaterialFile))
	};

	TEST_CLASS(CompoundMaterialTests)
	{
	public:
		TEST_METHOD(ConstructPipelineAccelerator)
		{
			using namespace RenderCore;

			auto mainPool = Techniques::CreatePipelineAcceleratorPool();
			std::shared_ptr<Techniques::TechniqueSetFile> techniqueSetFile = ::Assets::AutoConstructAsset<Techniques::TechniqueSetFile>(ILLUM_TECH);
			auto techniqueDelegate = Techniques::CreateTechniqueDelegate_Deferred(techniqueSetFile);

			// compoundmaterial.hlsl contains a material definition and some shader code embedded alongside
			// Ensure that we can load it and access the material properties 
			auto sphereMat = *::Assets::AutoConstructAsset<RenderCore::Assets::RawMaterial>("ut-data/compoundmaterial.hlsl:sphere");

			auto compiledPatchCollection = std::make_shared<RenderCore::Techniques::CompiledShaderPatchCollection>(sphereMat._patchCollection);
			auto accelerator = mainPool->CreatePipelineAccelerator(
				compiledPatchCollection,
				sphereMat._matParamBox,
				{},
				Topology::TriangleList,
				sphereMat._stateSet);

			::Assert::IsNotNull(accelerator.get());
			::Assert::IsTrue(compiledPatchCollection->GetInterface().HasPatchType(Hash64("DeformVertex")));
			::Assert::AreEqual(sphereMat._matParamBox.GetParameter<unsigned>("SHAPE").value(), 1u);
		}

		ConsoleRig::AttachablePtr<ConsoleRig::GlobalServices> _globalServices;
		ConsoleRig::AttachablePtr<::Assets::Services> _assetServices;

		std::shared_ptr<RenderCore::IDevice> _device;
		ConsoleRig::AttachablePtr<RenderCore::Assets::Services> _renderCoreAssetServices;
		ConsoleRig::AttachablePtr<RenderCore::Techniques::Services> _techniquesServices;

		CompoundMaterialTests()
		{
			UnitTest_SetWorkingDirectory();
			_globalServices = ConsoleRig::MakeAttachablePtr<ConsoleRig::GlobalServices>(GetStartupConfig());
			::Assets::MainFileSystem::GetMountingTree()->Mount("xleres", ::Assets::CreateFileSystem_OS("Game/xleres"));
			::Assets::MainFileSystem::GetMountingTree()->Mount("ut-data", ::Assets::CreateFileSystem_Memory(s_utData));
			_assetServices = ConsoleRig::MakeAttachablePtr<::Assets::Services>(0);

			#if GFXAPI_TARGET == GFXAPI_APPLEMETAL
				auto api = RenderCore::UnderlyingAPI::AppleMetal;
			#elif GFXAPI_TARGET == GFXAPI_DX11
				auto api = RenderCore::UnderlyingAPI::DX11;
			#else
				auto api = RenderCore::UnderlyingAPI::OpenGLES;
			#endif

			_device = RenderCore::CreateDevice(api);

			_renderCoreAssetServices = ConsoleRig::MakeAttachablePtr<RenderCore::Assets::Services>(_device);
			_techniquesServices = ConsoleRig::MakeAttachablePtr<RenderCore::Techniques::Services>(_device);
		}

		~CompoundMaterialTests()
		{
			_techniquesServices.reset();
			_renderCoreAssetServices.reset();
			_device.reset();
			_assetServices.reset();
			_globalServices.reset();
		}
	};

}


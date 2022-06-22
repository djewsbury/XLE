// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EngineDevice.h"
#include "UITypesBinding.h"
#include "GUILayerUtil.h"
#include "MarshalString.h"
#include "NativeEngineDevice.h"
#include "CLIXAutoPtr.h"

#include "../ToolsRig/MaterialVisualisation.h"
#include "../ToolsRig/ModelVisualisation.h"
#include "../ToolsRig/VisualisationUtils.h"

#include "../../SceneEngine/IScene.h"
// #include "../../SceneEngine/RenderStep.h"

#include "../../RenderCore/Techniques/Techniques.h"
#include "../../RenderCore/Techniques/ParsingContext.h"
#include "../../RenderCore/Techniques/RenderPass.h"
#include "../../RenderCore/Techniques/PipelineAccelerator.h"
#include "../../RenderCore/Techniques/Services.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../RenderCore/MinimalShaderSource.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/ResourceUtils.h"

// #include "../../BufferUploads/IBufferUploads.h"

#include "../../Assets/AssetServices.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/IAsyncMarker.h"
#include "../../ConsoleRig/ResourceBox.h"
#include "../../Utility/PtrUtils.h"

#include <memory>

using System::Collections::Generic::Dictionary;
using System::String;
using System::Object;

namespace GUILayer
{
	using System::Drawing::Size;

	static std::shared_ptr<RenderCore::Assets::MaterialScaffoldMaterial> CreatePreviewMaterial(const std::string& materialNames, const ::Assets::DirectorySearchRules& searchRules)
	{
		auto result = std::make_shared<RenderCore::Assets::MaterialScaffoldMaterial>();
		RenderCore::Assets::ShaderPatchCollection patchCollectionResult;

        // Our default material settings come from the "Document" object. This
        // give us our starting material and shader properties.
		auto start = materialNames.begin();
		std::vector<::Assets::DependentFileState> finalMatDeps;
		while (start != materialNames.end()) {
			auto end = std::find(start, materialNames.end(), ';');
			RenderCore::Assets::MergeIn_Stall(
				*result,
				patchCollectionResult,
				MakeStringSection(start, end),
				searchRules,
				finalMatDeps);
			if (end == materialNames.end()) break;
			start = end+1;
        }

        result->_matParams.SetParameter("SHADER_NODE_EDITOR", MakeStringSection("1"));

        /*
		for each(auto i in doc->ShaderParameters)
            result->_matParams.SetParameter(
                MakeStringSection(clix::marshalString<clix::E_UTF8>(i.Key)).Cast<utf8>(),
                MakeStringSection(clix::marshalString<clix::E_UTF8>(i.Value)));
		*/

		return result;
	}

	class PreviewImagePreparer
	{
	public:
		std::shared_ptr<SceneEngine::IScene> _scene;
		std::shared_ptr<RenderCore::IResource> _resource;
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::shared_ptr<RenderCore::Techniques::TechniqueContext> _globalTechniqueContext;
		std::shared_ptr<SceneEngine::IRenderStep> _customRenderStep;
		ToolsRig::VisCameraSettings _camSettings;

		::Assets::AssetState GetAssetState()
		{
			if (_resource)
				return ::Assets::AssetState::Ready;

			auto* asyncScene = dynamic_cast<::Assets::IAsyncMarker*>(_scene.get());
			if (asyncScene) {
				return asyncScene->GetAssetState();
			}
			return ::Assets::AssetState::Ready;
		}

		std::string _statusMessage;
		MaterialVisSettings::GeometryType _geometry;
		unsigned _width, _height;

		std::pair<ToolsRig::DrawPreviewResult, std::shared_ptr<RenderCore::IResource>> GetResource(RenderCore::IThreadContext& threadContext)
		{
			if (!_scene)
				return std::make_pair(ToolsRig::DrawPreviewResult::Error, std::shared_ptr<RenderCore::IResource>{});

			using namespace RenderCore;
			if (!_resource) {
				if (_geometry == MaterialVisSettings::GeometryType::Model) {
					auto* visContent = dynamic_cast<ToolsRig::IVisContent*>(_scene.get());
					if (visContent) {
						_camSettings = 
							ToolsRig::AlignCameraToBoundingBox(
								_camSettings._verticalFieldOfView,
								visContent->GetBoundingBox());
					}
				}

				_resource = threadContext.GetDevice()->CreateResource(
					CreateDesc(
						BindFlag::RenderTarget,
						TextureDesc::Plain2D(_width, _height, Format::R8G8B8A8_UNORM_SRGB),
						"PreviewBuilderTarget"));

				FrameBufferProperties fbProps{(unsigned)_width, (unsigned)_height, TextureSamples::Create()};

				ToolsRig::VisEnvSettings envSettings;
				envSettings._lightingType = ToolsRig::VisEnvSettings::LightingType::Direct;

				Techniques::ParsingContext parserContext { *_globalTechniqueContext };

				// Can no longer render to multiple output targets using this path. We only get to input the single "presentation target"
				// to the lighting parser.
				auto result = DrawPreview(
					threadContext, _resource, parserContext, _pipelineAcceleratorPool,
					_camSettings, envSettings,
					*_scene, _customRenderStep);
				if (result.first == ToolsRig::DrawPreviewResult::Error) {
					_statusMessage = result.second;
					// return GenerateErrorBitmap(result.second.c_str(), size);
					return std::make_pair(ToolsRig::DrawPreviewResult::Error, std::shared_ptr<RenderCore::IResource>{});
				} else if (result.first == ToolsRig::DrawPreviewResult::Pending) {
					return std::make_pair(ToolsRig::DrawPreviewResult::Pending, std::shared_ptr<RenderCore::IResource>{});
				}
			}

			return std::make_pair(ToolsRig::DrawPreviewResult::Success, _resource);
		}

		PreviewImagePreparer(
			GUILayer::MaterialVisSettings^ visSettings,
			const std::shared_ptr<RenderCore::IDevice>& device,
			const std::shared_ptr<ToolsRig::DeferredCompiledShaderPatchCollection>& patchCollection,
			const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& customTechniqueDelegate,
			const std::string& materialNames,
			unsigned width, unsigned height)
		{
			using namespace RenderCore;

				// We use a short-lived pipeline accelerator pool here, because
				// everything we put into it is temporary
			_pipelineAcceleratorPool = EngineDevice::GetInstance()->GetNative().GetMainPipelineAcceleratorPool();
			_globalTechniqueContext = std::make_shared<RenderCore::Techniques::TechniqueContext>();
			_globalTechniqueContext->_attachmentPool = std::make_shared<RenderCore::Techniques::AttachmentPool>(device);
			_globalTechniqueContext->_frameBufferPool = RenderCore::Techniques::CreateFrameBufferPool();
			_globalTechniqueContext->_commonResources = EngineDevice::GetInstance()->GetNative().GetDrawingApparatus()->_commonResources;

				////////////

			_camSettings._position = Float3(-1.42f, 0, 0);  // note that the position of the camera affects the apparent color of normals when previewing world space normals
			_camSettings._verticalFieldOfView = 90.f;

				// Select the geometry type to use.
				// In the "chart" mode, we are just going to run a pixel shader for every
				// output pixel, so we want to use a pretransformed quad covering the viewport
			_geometry = visSettings->Geometry;
			if (_geometry != MaterialVisSettings::GeometryType::Model) {
				bool pretransformed = false;
				ToolsRig::MaterialVisSettings nativeVisSettings;
				switch (_geometry) {
				case MaterialVisSettings::GeometryType::Plane2D:
				case MaterialVisSettings::GeometryType::Chart:
					nativeVisSettings._geometryType = ToolsRig::MaterialVisSettings::GeometryType::Plane2D;
					pretransformed = true;
					break;

				case MaterialVisSettings::GeometryType::Cube:
					nativeVisSettings._geometryType = ToolsRig::MaterialVisSettings::GeometryType::Cube;
					_camSettings._position = Float3(-2.1f, 0, 0);
					break;

				default:
				case MaterialVisSettings::GeometryType::Sphere:
					nativeVisSettings._geometryType = ToolsRig::MaterialVisSettings::GeometryType::Sphere;
					break;
				}

				auto material = CreatePreviewMaterial(materialNames, ::Assets::DirectorySearchRules{});

				if (pretransformed)
					material->_matParams.SetParameter("GEO_PRETRANSFORMED", MakeStringSection("1"));

				_scene = ToolsRig::MakeScene(_pipelineAcceleratorPool, nativeVisSettings, material);
			} else {
				ToolsRig::ModelVisSettings modelSettings;
				// modelSettings._modelName = clix::marshalString<clix::E_UTF8>(doc->PreviewModelFile);
				// modelSettings._materialName = clix::marshalString<clix::E_UTF8>(doc->PreviewModelFile);
				_scene = ToolsRig::MakeScene(_pipelineAcceleratorPool, modelSettings);
			}

			auto* patchScene = dynamic_cast<ToolsRig::IPatchCollectionVisualizationScene*>(_scene.get());
			if (patchScene)
				patchScene->SetPatchCollection(patchCollection->GetFuture());

			if (customTechniqueDelegate) {
				assert(0);
				// _customRenderStep = SceneEngine::CreateRenderStep_Direct(customTechniqueDelegate);
			}

			_width = width;
			_height = height;
		}
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	static System::Drawing::Bitmap^ GenerateWindowsBitmap(RenderCore::IThreadContext& threadContext, const std::shared_ptr<RenderCore::IResource>& res)
	{
		auto readback = res->ReadBackSynchronized(threadContext);
		if (!readback.empty()) {
			auto desc = res->GetDesc();
            using System::Drawing::Bitmap;
            using namespace System::Drawing;
            Bitmap^ newBitmap = gcnew Bitmap(desc._textureDesc._width, desc._textureDesc._height, Imaging::PixelFormat::Format32bppArgb);
            auto data = newBitmap->LockBits(
                System::Drawing::Rectangle(0, 0, desc._textureDesc._width, desc._textureDesc._height), 
                Imaging::ImageLockMode::WriteOnly, 
                Imaging::PixelFormat::Format32bppArgb);
			auto pitches = RenderCore::MakeTexturePitches(desc._textureDesc);
            try
            {
                    // we have to flip ABGR -> ARGB!
                for (unsigned y=0; y<desc._textureDesc._height; ++y) {
                    void* sourcePtr = PtrAdd(readback.data(), y * pitches._rowPitch);
                    System::IntPtr destinationPtr = data->Scan0 + y * desc._textureDesc._width * sizeof(unsigned);
                    for (unsigned x=0; x<desc._textureDesc._width; ++x) {
                        ((unsigned*)(void*)destinationPtr)[x] = 
                            (RenderCore::ARGBtoABGR(((unsigned*)sourcePtr)[x]) & 0x00ffffff) | 0xff000000;
                    }
                }
            }
            finally
            {
                newBitmap->UnlockBits(data);
            }
            return newBitmap;
        }
		return nullptr;
	}

	static System::Drawing::Bitmap^    GenerateErrorBitmap(const char str[], Size^ size)
    {
            //      Previously, we got an error while rendering this item.
            //      Render some text to the bitmap with an error string. Just
            //      use the gdi for this (don't bother rendering via D3D)

        using System::Drawing::Bitmap;
        using namespace System::Drawing;
        Bitmap^ newBitmap = gcnew Bitmap(size->Width, size->Height, Imaging::PixelFormat::Format32bppArgb);

        Graphics^ dc = Graphics::FromImage(newBitmap);
        dc->FillRectangle(gcnew SolidBrush(Color::Black), 0, 0, newBitmap->Width, newBitmap->Height);
        dc->DrawString(gcnew String(str), gcnew Font("Arial", 9), gcnew SolidBrush(Color::White), RectangleF(0.f, 0.f, float(newBitmap->Width), float(newBitmap->Height)));
        delete dc;

        return newBitmap;
    }

    public ref class PreviewBuilder
    {
    public:
		ref class PreviewImage
		{
		public:
			property System::Drawing::Bitmap^ Bitmap
			{
				System::Drawing::Bitmap^ get()
				{
					if (_bitmap)
						return _bitmap;

					auto state = _preparer->GetAssetState();
					if (state != ::Assets::AssetState::Ready)
						return nullptr;

					auto engineDevice = EngineDevice::GetInstance();
					auto device = engineDevice->GetNative().GetRenderDevice();
					auto& context = *device->GetImmediateContext();
					auto res = _preparer->GetResource(context);

					if (res.second) {
						_bitmap = GenerateWindowsBitmap(context, res.second);
					} else {
						_bitmap = GenerateErrorBitmap(_preparer->_statusMessage.c_str(), this->Size);
					}

					return _bitmap;
				}
			}
			property System::String^ StatusMessage
			{ 
				System::String^ get()
				{
					return clix::marshalString<clix::E_UTF8>(_preparer->_statusMessage);
				}
			}
			property bool IsPending
			{ 
				bool get()
				{
					return _preparer->GetAssetState() == ::Assets::AssetState::Pending;
				}
			}
			property bool HasError
			{ 
				bool get()
				{
					return !_preparer->_statusMessage.empty() || (_preparer->GetAssetState() == ::Assets::AssetState::Invalid);
				}
			}

			property System::Drawing::Size Size
			{
				System::Drawing::Size get()
				{
					return System::Drawing::Size((int)_preparer->_width, (int)_preparer->_height);
				}
			}

			PreviewImage(const std::shared_ptr<PreviewImagePreparer>& preparer)
			{
				_bitmap = nullptr;
				_preparer = preparer;
			}

		private:
			System::Drawing::Bitmap^ _bitmap;
			clix::shared_ptr<PreviewImagePreparer> _preparer;
		};

		virtual PreviewImage^ BuildPreviewImage(
            GUILayer::MaterialVisSettings^ visSettings,
			String^ materialNames,
			GUILayer::TechniqueDelegateWrapper^ techniqueDelegate,
			GUILayer::CompiledShaderPatchCollectionWrapper^ patchCollection,
			System::Drawing::Size^ size);

        PreviewBuilder();
		~PreviewBuilder();
    };

	PreviewBuilder::PreviewImage^ PreviewBuilder::BuildPreviewImage(
		GUILayer::MaterialVisSettings^ visSettings,
		String^ materialNames,
		GUILayer::TechniqueDelegateWrapper^ techniqueDelegate,
		GUILayer::CompiledShaderPatchCollectionWrapper^ patchCollection,
		System::Drawing::Size^ size)
    {
		// We have to pump some services, or assets will never complete loading/compiling
		assert(0);
		/*
		::Assets::Services::GetAsyncMan().Update();
		::Assets::Services::GetAssetSets().OnFrameBarrier();
		auto& uploads = RenderCore::Techniques::Services::GetBufferUploads();
		uploads.Update(*EngineDevice::GetInstance()->GetNative().GetRenderDevice()->GetImmediateContext());
		*/

            ////////////

		auto preparer = std::make_shared<PreviewImagePreparer>(
			visSettings,
			EngineDevice::GetInstance()->GetNative().GetRenderDevice(),
			patchCollection->_patchCollection.GetNativePtr(),
			techniqueDelegate->_techniqueDelegate.GetNativePtr(),
			clix::marshalString<clix::E_UTF8>(materialNames),
			size->Width, size->Height);

		return gcnew PreviewImage(preparer);
    }

    PreviewBuilder::PreviewBuilder()
    {
    }

    PreviewBuilder::~PreviewBuilder()
    {
    }

}


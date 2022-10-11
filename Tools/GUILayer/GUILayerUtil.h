// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CLIXAutoPtr.h"
#include <functional>

namespace RenderCore { namespace Techniques { class TechniqueContext; class ITechniqueDelegate; } }
namespace SceneEngine 
{
    class TerrainManager;
    class PlacementsEditor;
    class PlacementsRenderer;
    class PlacementCellSet;
    class IIntersectionScene;
}

namespace ConsoleRig { class IProgress; }
namespace ToolsRig { class MessageRelay; class DeferredCompiledShaderPatchCollection; }

namespace std { template <typename R> class future; }

namespace GUILayer
{
	public enum class CompilationTargetFlag { Model = 1<<0, Animation = 1<<1, Skeleton = 1<<2, Material = 1<<3 };

	public ref class Utils
	{
	public:
		static System::String^ MakeAssetName(System::String^ input);
		static System::UInt64 HashID(System::String^ string);

		ref struct AssetExtension
		{
			System::String^ Extension;
			System::String^ Description;
		};
		static System::Collections::Generic::IEnumerable<AssetExtension^>^ GetModelExtensions();
		static System::Collections::Generic::IEnumerable<AssetExtension^>^ GetAnimationSetExtensions();

		static System::Collections::Generic::IEnumerable<System::String^>^ EnumeratePreviewScenes();

		static uint32_t FindCompilationTargets(System::String^ Extension);
	};

    public ref class TechniqueContextWrapper
    {
    public:
        clix::shared_ptr<RenderCore::Techniques::TechniqueContext> _techniqueContext;

        TechniqueContextWrapper(const std::shared_ptr<RenderCore::Techniques::TechniqueContext>& techniqueContext);
        ~TechniqueContextWrapper();
    };

	public ref class TechniqueDelegateWrapper
	{
	public:
		clix::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _techniqueDelegate;

        TechniqueDelegateWrapper(const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& techniqueDelegate);
		TechniqueDelegateWrapper(RenderCore::Techniques::ITechniqueDelegate* techniqueDelegate);
        ~TechniqueDelegateWrapper();
	};

	public ref class CompiledShaderPatchCollectionWrapper
	{
	public:
		clix::shared_ptr<ToolsRig::DeferredCompiledShaderPatchCollection> _patchCollection;

        CompiledShaderPatchCollectionWrapper(std::unique_ptr<ToolsRig::DeferredCompiledShaderPatchCollection>&& patchCollection);
		CompiledShaderPatchCollectionWrapper(ToolsRig::DeferredCompiledShaderPatchCollection* patchCollection);
        ~CompiledShaderPatchCollectionWrapper();
	};

	public ref class MessageRelayWrapper
	{
	public:
		property System::String^ Messages { System::String^ get(); };

		delegate void OnChangeEventHandler(System::Object^ sender, System::EventArgs^ args);
		property OnChangeEventHandler^ OnChangeEvent;

		clix::shared_ptr<ToolsRig::MessageRelay> _native;
		unsigned _callbackId;

		MessageRelayWrapper(const std::shared_ptr<ToolsRig::MessageRelay>& techniqueDelegate);
		MessageRelayWrapper(ToolsRig::MessageRelay* techniqueDelegate);
		MessageRelayWrapper();
        ~MessageRelayWrapper();
	};

	public ref class IntersectionTestSceneWrapper
	{
	public:
		clix::shared_ptr<SceneEngine::IIntersectionScene> _scene;

		SceneEngine::IIntersectionScene& GetNative();
		IntersectionTestSceneWrapper(
			const std::shared_ptr<SceneEngine::IIntersectionScene>& scene);
        ~IntersectionTestSceneWrapper();
        !IntersectionTestSceneWrapper();
	};

    public ref class PlacementsEditorWrapper
	{
	public:
		clix::shared_ptr<SceneEngine::PlacementsEditor> _editor;

		SceneEngine::PlacementsEditor& GetNative();
		PlacementsEditorWrapper(std::shared_ptr<SceneEngine::PlacementsEditor> scene);
        ~PlacementsEditorWrapper();
        !PlacementsEditorWrapper();
	};

    public ref class PlacementsRendererWrapper
	{
	public:
		clix::shared_ptr<SceneEngine::PlacementsRenderer> _renderer;

		SceneEngine::PlacementsRenderer& GetNative();
		PlacementsRendererWrapper(std::shared_ptr<SceneEngine::PlacementsRenderer> scene);
        ~PlacementsRendererWrapper();
        !PlacementsRendererWrapper();
	};

    public interface class IStep
    {
        virtual void SetProgress(unsigned progress);
        virtual void Advance();
        virtual bool IsCancelled();
        virtual void EndStep();
    };

    public interface class IProgress
    {
    public:
        virtual IStep^ BeginStep(System::String^ name, unsigned progressMax, bool cancellable);

        typedef std::unique_ptr<ConsoleRig::IProgress, std::function<void(ConsoleRig::IProgress*)>> ProgressPtr;
        static ProgressPtr CreateNative(IProgress^ managed);
        static void DeleteNative(ConsoleRig::IProgress* native);
    };
}

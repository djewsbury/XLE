// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CLIXAutoPtr.h"
#include "../../RenderCore/Techniques/TechniqueUtils.h"     // for CameraDesc destructor
#include <vector>
#include <utility>

namespace SceneEngine { class PlacementsEditor; }
namespace ToolsRig { class VisCameraSettings; }

namespace GUILayer
{
    ref class PlacementsEditorWrapper;

    public ref class ObjectSet
    {
    public:
        void Add(System::Tuple<uint64_t, uint64_t>^ nativeHighlightableId);
        void Clear();
        bool IsEmpty();

        typedef std::vector<std::pair<uint64_t, uint64_t>> NativePlacementSet;
		clix::auto_ptr<NativePlacementSet> _nativePlacements;

        ObjectSet();
        ~ObjectSet();
    };

    ref class EditorSceneManager;

    public ref class EnvironmentSettingsSet
    {
    public:
        property System::Collections::Generic::IEnumerable<System::String^>^ Names { System::Collections::Generic::IEnumerable<System::String^>^ get(); }

        void AddDefault();

        EnvironmentSettingsSet(EditorSceneManager^ scene);
        ~EnvironmentSettingsSet();
    };

	public ref class CameraDescWrapper
    {
    public:
        clix::auto_ptr<RenderCore::Techniques::CameraDesc> _native;

		CameraDescWrapper(ToolsRig::VisCameraSettings& camSettings);
        ~CameraDescWrapper();
    };
}

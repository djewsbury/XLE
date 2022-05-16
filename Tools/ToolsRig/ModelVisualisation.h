// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <string>

namespace SceneEngine { class IScene; }
namespace RenderCore { namespace Techniques { class IDrawablesPool; class IPipelineAcceleratorPool; class IDeformAcceleratorPool; } }

namespace ToolsRig
{
    class VisCameraSettings;
    class VisEnvSettings;

    /// <summary>Settings related to the visualisation of a model</summary>
    /// This is a "model" part of a MVC pattern related to the way a model
    /// is presented in a viewport. Typically some other controls might 
    /// write to this when something changes (for example, if a different
    /// model is selected to be viewed).
    /// The settings could come from anywhere though -- it's purposefully
    /// kept free of dependencies so that it can be driven by different sources.
    /// We have a limited set of different rendering options for special
    /// visualisation modes, etc.
    class ModelVisSettings
    {
    public:
        std::string		_modelName;
        std::string		_materialName;
        std::string		_supplements;
        unsigned		_levelOfDetail;
		std::string		_animationFileName;
		std::string		_skeletonFileName;
		uint64_t		_materialBindingFilter;

        uint64_t GetHash() const;

		ModelVisSettings();
	};

	Assets::PtrToMarkerPtr<SceneEngine::IScene> MakeScene(
        std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
		std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
        std::shared_ptr<RenderCore::Techniques::IDeformAcceleratorPool> deformAcceleratorPool,
		const ModelVisSettings& settings);
}


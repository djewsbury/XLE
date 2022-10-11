// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UITypesBinding.h"
#include "ExportedNativeTypes.h"
#include "EditorInterfaceUtils.h"
#include "GUILayerUtil.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/MaterialVisualisation.h"
#include "../../RenderCore/Assets/RawMaterial.h"

#pragma warning(disable:4505)		// 'GUILayer::AsNative': unreferenced local function has been removed)

namespace GUILayer
{
	/*static ToolsRig::VisEnvSettings::LightingType AsNative(
         MaterialVisSettings::LightingType input)
    {
        switch (input) {
        case MaterialVisSettings::LightingType::Deferred:
            return ToolsRig::VisEnvSettings::LightingType::Deferred;
        case MaterialVisSettings::LightingType::Forward:
            return ToolsRig::VisEnvSettings::LightingType::Forward;
        default:
            return ToolsRig::VisEnvSettings::LightingType::Direct;
        }
    }

	RenderCore::Assets::MaterialScaffoldMaterial ResolveNativeMaterial(
		System::Collections::Generic::IEnumerable<RawMaterial^>^ rawMaterials,
		const ::Assets::DirectorySearchRules& searchRules)
	{
		RenderCore::Assets::MaterialScaffoldMaterial result;
		RenderCore::Assets::ShaderPatchCollection patchCollection;
		for each(auto c in rawMaterials)
			RenderCore::Assets::MergeIn_Stall(result, patchCollection, *c->GetUnderlying(), searchRules);
		return result;
	}*/
    
    static MaterialVisSettings::GeometryType AsManaged(
        ToolsRig::MaterialVisSettings::GeometryType input)
    {
        switch (input) {
        case ToolsRig::MaterialVisSettings::GeometryType::Sphere:
            return MaterialVisSettings::GeometryType::Sphere;
        case ToolsRig::MaterialVisSettings::GeometryType::Cube:
            return MaterialVisSettings::GeometryType::Cube;
        default:
            return MaterialVisSettings::GeometryType::Plane2D;
        }
    }

    static ToolsRig::MaterialVisSettings::GeometryType AsNative(
         MaterialVisSettings::GeometryType input)
    {
        switch (input) {
        case MaterialVisSettings::GeometryType::Sphere:
            return ToolsRig::MaterialVisSettings::GeometryType::Sphere;
        case MaterialVisSettings::GeometryType::Cube:
            return ToolsRig::MaterialVisSettings::GeometryType::Cube;
        default:
            return ToolsRig::MaterialVisSettings::GeometryType::Plane2D;
        }
    }

	MaterialVisSettings::MaterialVisSettings()
	{
		Geometry = GeometryType::Sphere;
		Lighting = LightingType::Deferred;
	}

	std::shared_ptr<ToolsRig::MaterialVisSettings> MaterialVisSettings::ConvertToNative()
	{
		auto result = std::make_shared<ToolsRig::MaterialVisSettings>();
		result->_geometryType = AsNative(Geometry);
		return result;
	}

	MaterialVisSettings^ MaterialVisSettings::ConvertFromNative(const ToolsRig::MaterialVisSettings& input)
	{
		MaterialVisSettings^ result = gcnew MaterialVisSettings();
		result->Geometry = AsManaged(input._geometryType);
		return result;
	}

	MaterialVisSettings^ MaterialVisSettings::ShallowCopy()
	{
		return (MaterialVisSettings^)this->MemberwiseClone();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

    void ModelVisSettings::ModelName::set(System::String^ value)
    {
            //  we need to make a filename relative to the current working
            //  directory                
        _modelName = Utils::MakeAssetName(value);

            // also set the material name (the old material file probably won't match the new model file)
        auto nativeName = clix::marshalString<clix::E_UTF8>(value);
        auto split = MakeFileNameSplitter(nativeName);
        nativeName = split.DrivePathAndFilename().AsString() + ".material";
        _materialName = Utils::MakeAssetName(clix::marshalString<clix::E_UTF8>(nativeName));
    }

    void ModelVisSettings::MaterialName::set(System::String^ value)
    {
        _materialName = Utils::MakeAssetName(value);
    }

	void ModelVisSettings::AnimationFileName::set(System::String^ value)
    {
        _animationFileName = Utils::MakeAssetName(value);
    }

	void ModelVisSettings::SkeletonFileName::set(System::String^ value)
    {
        _skeletonFileName = Utils::MakeAssetName(value);
    }

	ModelVisSettings^ ModelVisSettings::CreateDefault()
    {
		return ConvertFromNative(ToolsRig::ModelVisSettings{});
    }

	ModelVisSettings^ ModelVisSettings::FromCommandLine(array<System::String^>^ args)
	{
		return CreateDefault();
	}

	ModelVisSettings::ModelVisSettings() {}

	ModelVisSettings^ ModelVisSettings::ShallowCopy()
	{
		return (ModelVisSettings^)this->MemberwiseClone();
	}

	std::shared_ptr<ToolsRig::ModelVisSettings> ModelVisSettings::ConvertToNative()
	{
		auto result = std::make_shared<ToolsRig::ModelVisSettings>();
		result->_modelName = clix::marshalString<clix::E_UTF8>(_modelName);
		result->_materialName = clix::marshalString<clix::E_UTF8>(_materialName);
        result->_supplements = clix::marshalString<clix::E_UTF8>(this->Supplements);
        result->_levelOfDetail = this->LevelOfDetail;
		result->_animationFileName = clix::marshalString<clix::E_UTF8>(_animationFileName);
		result->_skeletonFileName = clix::marshalString<clix::E_UTF8>(_skeletonFileName);
		result->_materialBindingFilter = this->MaterialBindingFilter;
		return result;
	}

	ModelVisSettings^ ModelVisSettings::ConvertFromNative(const ToolsRig::ModelVisSettings& input)
	{
		auto result = gcnew ModelVisSettings();
		result->_modelName = clix::marshalString<clix::E_UTF8>(input._modelName);
		result->_materialName = clix::marshalString<clix::E_UTF8>(input._materialName);
        result->Supplements = clix::marshalString<clix::E_UTF8>(input._supplements);
        result->LevelOfDetail = input._levelOfDetail;
		result->_animationFileName = clix::marshalString<clix::E_UTF8>(input._animationFileName);
		result->_skeletonFileName = clix::marshalString<clix::E_UTF8>(input._skeletonFileName);
		result->MaterialBindingFilter = input._materialBindingFilter;
		return result;
	}

}

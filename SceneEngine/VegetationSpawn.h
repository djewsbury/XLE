// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Metal/Forward.h"
#include "../FixedFunctionModel/DelayedDrawCall.h"       // for DelayStep
#include "../Assets/AssetsCore.h"
#include "../Assets/AssetUtils.h"
#include "../Utility/UTFUtils.h"

namespace FixedFunctionModel { class ModelCache; }
namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques { class ParsingContext; }}
namespace Utility
{
    template<typename Type> class TextInputFormatter;
    class TextOutputFormatter;
}

namespace SceneEngine
{
    class VegetationSpawnConfig
    {
    public:
        class ObjectType
        {
        public:
            ::Assets::rstring _modelName, _materialName;

            ObjectType();
        };

        class Bucket
        {
        public:
            unsigned _objectType;
            float _maxDrawDistance;
            float _frequencyWeight;
            Bucket();
        };

        class Material
        {
        public:
            std::vector<Bucket> _buckets;
            float _noSpawnWeight;
            float _suppressionThreshold;
            float _suppressionNoise;
            float _suppressionGain;
            float _suppressionLacunarity;
            unsigned _materialId;
            Material();
        };

        float _baseGridSpacing;
        float _jitterAmount;
        bool _alignToTerrainUp;
        std::vector<Material> _materials;
        std::vector<ObjectType> _objectTypes;

        ::Assets::DirectorySearchRules _searchRules;

        VegetationSpawnConfig(
            Utility::TextInputFormatter<utf8>& formatter,
            const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
        VegetationSpawnConfig();
        ~VegetationSpawnConfig();

        void        Write(TextOutputFormatter& formatter) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

	private:
		::Assets::DependencyValidation _depVal;
    };

#if 0
    class LightingParserContext;
    class VegetationSpawnResources;
    class PreparedScene;

    void VegetationSpawn_Prepare(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
		LightingParserContext& lightingParserContext,
		ISceneParser& sceneParser,
        PreparedScene& preparedScene,
        const VegetationSpawnConfig& cfg,
        VegetationSpawnResources& resources);

	class ILightingParserPlugin;

    class VegetationSpawnManager
    {
    public:
        void Render(
            RenderCore::IThreadContext& context, 
            RenderCore::Techniques::ParsingContext& parsingContext,
            unsigned techniqueIndex,
            FixedFunctionModel::DelayStep delayStep);
        bool HasContent(FixedFunctionModel::DelayStep delayStep) const;

        std::shared_ptr<ILightingParserPlugin> GetParserPlugin();

        void Load(const VegetationSpawnConfig& cfg);
        void Reset();

        const VegetationSpawnConfig& GetConfig() const;

        VegetationSpawnManager(
            std::shared_ptr<FixedFunctionModel::ModelCache> modelCache);
        ~VegetationSpawnManager();
    protected:
        class Pimpl;
        std::unique_ptr<Pimpl> _pimpl;
        friend class VegetationSpawnPlugin;
    };
#else
	class ILightingParserPlugin;
	class VegetationSpawnManager
    {
	public:
		const VegetationSpawnConfig& GetConfig() const;
		std::shared_ptr<ILightingParserPlugin> GetParserPlugin() { return nullptr; }
		VegetationSpawnManager(
            std::shared_ptr<FixedFunctionModel::ModelCache> modelCache);
        ~VegetationSpawnManager();
	};
#endif


}


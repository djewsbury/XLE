// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Assets/AssetUtils.h"
#include "../Math/Vector.h"
#include <vector>

namespace Formatters
{ 
    template<typename CharType> class TextInputFormatter;
    class TextOutputFormatter;
}

namespace SceneEngine
{
    class TerrainMaterialConfig
    {
    public:
        class StrataMaterial
        {
        public:
            class Strata
            {
            public:
                ::Assets::rstring _texture[3];
                float _mappingConstant[3];
                float _endHeight;

                Strata();
            };
            std::vector<Strata> _strata;
            unsigned _id;

            StrataMaterial();
        };

        class GradFlagMaterial
        {
        public:
            ::Assets::rstring _texture[5];
            float _mappingConstant[5];
            unsigned _id;

            GradFlagMaterial();
        };

        class ProcTextureSetting
        {
        public:
            ::Assets::rstring _name;
            ::Assets::rstring _texture[2];
            float _hgrid, _gain;

            ProcTextureSetting();
        };

        UInt2   _diffuseDims;
        UInt2   _normalDims;
        UInt2   _paramDims;
        std::vector<StrataMaterial>     _strataMaterials;
        std::vector<GradFlagMaterial>   _gradFlagMaterials;
        std::vector<ProcTextureSetting> _procTextures;

        float _specularParameter;
        float _roughnessMin, _roughnessMax;
        float _shadowSoftness;

        ::Assets::DirectorySearchRules  _searchRules;

        void Write(Utility::OutputStreamFormatter& formatter) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

        TerrainMaterialConfig();
        TerrainMaterialConfig(
            TextInputFormatter<utf8>& formatter,
            const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
        ~TerrainMaterialConfig();

            // the following constructor is intended for performance comparisons only
        TerrainMaterialConfig(
            TextInputFormatter<utf8>& formatter,
            const ::Assets::DirectorySearchRules& searchRules,
            bool);

	private:
		::Assets::DependencyValidation _depVal;
    };
}

using namespace Utility;


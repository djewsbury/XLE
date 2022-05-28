// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Scaffold.h"
#include "ScaffoldParsingUtil.h"
#include "ConversionConfig.h"
#include "../RenderCore/Assets/RawMaterial.h"
#include "../OSServices/Log.h"
#include "../Math/MathSerialization.h"
#include "../Utility/Conversion.h"
#include "../Utility/Streams/PathUtils.h"
#include <string>

namespace ColladaConversion
{
    static const ParameterSet::SamplerParameter* FindSamplerParameter(
        const ParameterSet& paramSet, Section sid)
    {
        for (unsigned c=0; c<paramSet.GetSamplerParameterCount(); ++c)
            if (XlEqString(paramSet.GetSamplerParameter(c)._sid, sid))
                return &paramSet.GetSamplerParameter(c);
        return nullptr;
    }

    static const ParameterSet::SurfaceParameter* FindSurfaceParameter(
        const ParameterSet& paramSet, Section sid)
    {
        for (unsigned c=0; c<paramSet.GetSurfaceParameterCount(); ++c)
            if (XlEqString(paramSet.GetSurfaceParameter(c)._sid, sid))
                return &paramSet.GetSurfaceParameter(c);
        return nullptr;
    }

    void AddSamplerBinding(
        RenderCore::Assets::RawMaterial& dest,
        const ParameterSet& paramSet, const utf8 bindingName[], 
        const StringSection<utf8> samplerRef,
        const URIResolveContext& resolveContext)
    {
        auto* samplerParam = FindSamplerParameter(paramSet, samplerRef);
        if (samplerParam) {

                // the "init_from" member of <surface> seems to reference
                // an image in the <library_images> section.
                // in Collada 1.5, there should be an instance_image into the sampler
                // object, skipping the "surface" param.
            Section imageRef = samplerParam->_instanceImage;
            if (imageRef._end == imageRef._start) {
                auto* surfaceParam = FindSurfaceParameter(paramSet, samplerParam->_source);
                if (surfaceParam) {
                    imageRef = surfaceParam->_initFrom;                    
                }
            }

            if (imageRef._end != imageRef._start) {
                GuidReference ref(imageRef);
                auto* file = resolveContext.FindFile(ref._fileHash);
                if (file) {
                    auto* image = file->FindImage(ref._id);
                    if (image) {
                        utf8 rebuiltPath[MaxPath];
                        SplitPath<utf8>(image->GetInitFrom()).Simplify().Rebuild(rebuiltPath, dimof(rebuiltPath));

                        dest._resources.SetParameter(bindingName, (const char*)rebuiltPath);
                        return;
                    }
                }
            }
    
            Log(Warning) << "Could not resolve surface reference (" << AsString(imageRef) << ")" << std::endl;
        } else {
            Log(Warning) << "Could not resolve sampler reference (" << AsString(samplerRef) << ")" << std::endl;
        }
    }

    static void ParseExtraForTextures(
        RenderCore::Assets::RawMaterial& matSettings,
        const Utility::StreamDOM<Formatter>& extra, const ParameterSet& params,
        const URIResolveContext& pubEles,
        const ImportConfiguration& cfg, const std::string& effectName)
    {
        auto extraValues = extra.RootElement().Element("technique");
        if (extraValues) {
            for (auto techValue:extraValues.children()) {
                auto n = techValue.Name();
                if (cfg.GetResourceBindings().IsSuppressed(n)) continue;

                auto texture = techValue.Element("texture");
                if (texture) {
                    auto samplerRef = texture.Attribute("texture").Value();
                    if (!samplerRef.IsEmpty()) {
                        auto binding = cfg.GetResourceBindings().AsNative(n);
                        AddSamplerBinding(
                            matSettings, params, 
                            binding.c_str(), samplerRef, pubEles);
                    }
                } else if (techValue.Element("color") || techValue.Element("float") || techValue.Element("param")) {
                    Log(Warning) << "Color, float and param type technique values not supported in <extra> part in effect (" << effectName << ")" << std::endl;
                }
            }
        }
    }

    RenderCore::Assets::RawMaterial Convert(
        const Effect& effect, const URIResolveContext& pubEles,
        const ImportConfiguration& cfg)
    {
        RenderCore::Assets::RawMaterial matSettings;

            //  Any settings from the Collada file should override what we read
            //  in the material settings file. This means that we have 
            //  clear out the settings in the Collada file if we want the .material
            //  file to show through. This pattern works best if we can use 
            //  the .material files to specify the default settings, and then use
            //  the collada data to specialise the settings of specific parts of geometry.

            // bind the texture in the "common" effects part
        auto* profile = effect.FindProfile("COMMON");
        if (!profile)
            Throw(::Exceptions::BasicLabel("Missing common profile in effect (%s)", AsString(effect.GetName()).c_str()));

        for (const auto& t:profile->_values) {
            auto& value = t.second;
            
            switch (value._type) {
            case TechniqueValue::Type::Color:
            case TechniqueValue::Type::Float:
                {
                    if (cfg.GetConstantBindings().IsSuppressed(t.first)) continue;
                    auto binding = cfg.GetConstantBindings().AsNative(t.first);

                    if (value._type == TechniqueValue::Type::Color) {
                        matSettings._uniforms.SetParameter(
                            binding.c_str(), value._value);
                    } else {
                        matSettings._uniforms.SetParameter(binding.c_str(), value._value[0]);
                    }
                    break;
                }

            case TechniqueValue::Type::Texture:
                {
                    if (cfg.GetResourceBindings().IsSuppressed(t.first)) continue;
                    auto binding = cfg.GetResourceBindings().AsNative(t.first);

                    AddSamplerBinding(
                        matSettings, profile->GetParams(), 
                        binding.c_str(), value._reference, pubEles);
                    break;
                }

            default:
            case TechniqueValue::Type::Param:
                Log(Warning) << "Param type technique values are not supported. In effect (" << AsString(effect.GetName()) << ")" << std::endl;
                break;
            }
        }
            
            //  Many exporters write some extra texture information into the <extra> part of the
            //  profile. We should get some more texture and parameter binding information from there
            //  We have to parse the "extra" section both in the "profile" element and in the "technique" element

        ParseExtraForTextures(matSettings, profile->_techniqueExtra, profile->GetParams(), pubEles, cfg, AsString(effect.GetName()));
        ParseExtraForTextures(matSettings, profile->_extra, profile->GetParams(), pubEles, cfg, AsString(effect.GetName()));

        return std::move(matSettings);
    }

}

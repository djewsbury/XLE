// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ConversionConfig.h"
#include "../../Assets/Assets.h"       // (for RegisterFileDependency)
#include "../../OSServices/Log.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/FormatterUtils.h"

namespace ColladaConversion
{
    BindingConfig::BindingConfig(Formatters::TextInputFormatter<utf8>& formatter)
    {
        StringSection<> kn;
        while (formatter.TryKeyedItem(kn)) {
            if (XlEqString(kn, "Rename")) {
                Formatters::RequireBeginElement(formatter);
                while (formatter.TryKeyedItem(kn))
                    _exportNameToBinding.push_back(std::make_pair(kn.AsString(), Formatters::RequireStringValue(formatter).AsString()));
                Formatters::RequireEndElement(formatter);
            } else if (XlEqString(kn, "Suppress")) {
                Formatters::RequireBeginElement(formatter);
                while (formatter.TryStringValue(kn))
                    _bindingSuppressed.push_back(kn.AsString());
                Formatters::RequireEndElement(formatter);
            } else if (XlEqString(kn, "ForceLinear")) {
                Formatters::RequireBeginElement(formatter);
                while (formatter.TryStringValue(kn))
                    _forceLinear.push_back(kn.AsString());
                Formatters::RequireEndElement(formatter);
            } else
                Formatters::SkipValueOrElement(formatter);
        }
    }

    BindingConfig::BindingConfig() {}
    BindingConfig::~BindingConfig() {}

    std::basic_string<utf8> BindingConfig::AsNative(StringSection<utf8> input) const
    {
            //  we need to define a mapping between the names used by the max exporter
            //  and the native XLE shader names. The meaning might not match perfectly
            //  but let's try to get as close as possible
        auto i = std::find_if(
            _exportNameToBinding.cbegin(), _exportNameToBinding.cend(),
            [=](const std::pair<String, String>& e) 
            { return XlEqString(input, e.first); });

        if (i != _exportNameToBinding.cend()) 
            return i->second;
        return input.AsString();
    }

    bool BindingConfig::IsSuppressed(StringSection<utf8> input) const
    {
        auto i = std::find_if(
            _bindingSuppressed.cbegin(), _bindingSuppressed.cend(),
            [=](const String& e) { return XlEqString(input, e); });

        return (i != _bindingSuppressed.cend());
    }

    bool BindingConfig::IsForceLinear(StringSection<utf8> input) const
    {
        auto i = std::find_if(
            _forceLinear.cbegin(), _forceLinear.cend(),
            [=](const String& e) { return XlEqString(input, e); });

        return (i != _forceLinear.cend());
    }

    ImportConfiguration::ImportConfiguration(Formatters::TextInputFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules&, const ::Assets::DependencyValidation& depVal)
    : _depVal(depVal)
    {
        StringSection<> kn;
        while (formatter.TryKeyedItem(kn)) {
            if (XlEqString(kn, "Resources")) {
                Formatters::RequireBeginElement(formatter);
                _resourceBindings = BindingConfig(formatter);
                Formatters::RequireEndElement(formatter);
            } else if (XlEqString(kn, "Constants")) {
                Formatters::RequireBeginElement(formatter);
                _constantsBindings = BindingConfig(formatter);
                Formatters::RequireEndElement(formatter);
            } else if (XlEqString(kn, "VertexSemantics")) {
                Formatters::RequireBeginElement(formatter);
                _vertexSemanticBindings = BindingConfig(formatter);
                Formatters::RequireEndElement(formatter);
            } else if (XlEqString(kn, "DefaultMaterialInherit")) {
                _defaultMaterialInherit = Formatters::RequireStringValue(formatter);
            } else
                Formatters::SkipValueOrElement(formatter);
        }
    }
    ImportConfiguration::ImportConfiguration() {}
    ImportConfiguration::~ImportConfiguration()
    {}

}


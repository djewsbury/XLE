// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderPatcher.h"
#include "NodeGraph.h"
#include "NodeGraphSignature.h"
#include "ShaderSignatureParser.h"
#include "../RenderCore/ShaderLangUtil.h"
#include "../RenderCore/Assets/PredefinedCBLayout.h"
#include "../RenderCore/Format.h"
#include "../Assets/AssetUtils.h"
#include "../Assets/ConfigFileContainer.h"
#include "../Assets/DepVal.h"
#include "../Assets/Assets.h"
#include "../Assets/IFileSystem.h"
#include "../Utility/StringFormat.h"
#include "../Utility/Conversion.h"
#include <regex>
#include <tuple>
#include <set>

#include "plustache/template.hpp"

namespace ShaderSourceParser
{
	using namespace GraphLanguage;

	class TemplateItem
    {
    public:
        std::basic_string<utf8> _item;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

        TemplateItem(
            InputStreamFormatter<utf8>& formatter,
            const ::Assets::DirectorySearchRules&,
			const ::Assets::DependencyValidation& depVal)
        {
            _item = RequireValue(formatter).AsString();
			_depVal = depVal;
        }
        TemplateItem() {}
	private:
		::Assets::DependencyValidation _depVal;
    };

	static std::string ToPlustache(bool value)
    {
        static std::string T = "true", F = "false";
        return value ? T : F;
    }

	static std::string GetPreviewTemplate(const char templateName[])
    {
        StringMeld<MaxPath, Assets::ResChar> str;
        str << "xleres/TechniqueLibrary/ToolsRig/MaterialTool/PreviewTemplates.hlsl:" << templateName;
		return ::Assets::Legacy::GetAssetDep<TemplateItem>(str.AsStringSection())._item;
    }

	struct VaryingParamsFlags
    {
        enum Enum { WritesVSOutput = 1<<0 };
        using BitField = unsigned;
    };

    class ParameterMachine
    {
    public:
        auto GetBuildInterpolator(const NodeGraphSignature::Parameter& param) const
            -> std::pair<std::string, VaryingParamsFlags::BitField>;

        auto GetBuildSystem(const NodeGraphSignature::Parameter& param) const -> std::string;

        ParameterMachine();
        ~ParameterMachine();
    private:
        GraphLanguage::ShaderFragmentSignature _systemHeader;
    };

    auto ParameterMachine::GetBuildInterpolator(const NodeGraphSignature::Parameter& param) const
        -> std::pair<std::string, VaryingParamsFlags::BitField>
    {
        std::string searchName = "BuildInterpolator_" + param._semantic;
        auto i = std::find_if(
            _systemHeader._functions.cbegin(),
            _systemHeader._functions.cend(),
            [searchName](const std::pair<std::string, GraphLanguage::NodeGraphSignature>& sig) { return sig.first == searchName; });

        if (i == _systemHeader._functions.cend()) {
            searchName = "BuildInterpolator_" + param._name;
            i = std::find_if(
                _systemHeader._functions.cbegin(),
                _systemHeader._functions.cend(),
                [searchName](const std::pair<std::string, GraphLanguage::NodeGraphSignature>& sig) { return sig.first == searchName; });
        }

        if (i == _systemHeader._functions.cend()) {
            searchName = "BuildInterpolator_" + param._type;
            i = std::find_if(
                _systemHeader._functions.cbegin(),
                _systemHeader._functions.cend(),
                [searchName](const std::pair<std::string, GraphLanguage::NodeGraphSignature>& sig) { return sig.first == searchName; });
        }

        if (i != _systemHeader._functions.cend()) {
			auto p = std::find_if(i->second.GetParameters().begin(), i->second.GetParameters().end(),
				[](const GraphLanguage::NodeGraphSignature::Parameter& p) { 
					return p._direction == GraphLanguage::ParameterDirection::Out 
						&& XlEqString(MakeStringSection(p._name), MakeStringSection(GraphLanguage::s_resultName));
				});
            VaryingParamsFlags::BitField flags = 0;
            if (p != i->second.GetParameters().end() && !p->_semantic.empty()) {
                    // using regex, convert the semantic value into a series of flags...
                static std::regex FlagsParse(R"--(NE(?:_([^_]*))*)--");
                std::smatch match;
                if (std::regex_match(p->_semantic.begin(), p->_semantic.end(), match, FlagsParse))
                    for (unsigned c=1; c<match.size(); ++c)
                        if (XlEqString(MakeStringSection(AsPointer(match[c].first), AsPointer(match[c].second)), "WritesVSOutput"))
                            flags |= VaryingParamsFlags::WritesVSOutput;
            }

            return std::make_pair(i->first, flags);
        }

        return std::make_pair(std::string(), 0);
    }

    auto ParameterMachine::GetBuildSystem(const NodeGraphSignature::Parameter& param) const -> std::string
    {
        std::string searchName = "BuildSystem_" + param._type;
        auto i = std::find_if(
            _systemHeader._functions.cbegin(), _systemHeader._functions.cend(),
            [searchName](const std::pair<std::string, GraphLanguage::NodeGraphSignature>& sig) { return sig.first == searchName; });
        if (i != _systemHeader._functions.cend())
            return i->first;
        return std::string();
    }

    ParameterMachine::ParameterMachine()
    {
		size_t fileSize = 0;
        auto buildInterpolatorsSource = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock("xleres/TechniqueLibrary/ToolsRig/MaterialTool/BuildInterpolators.h", &fileSize);
        _systemHeader = ShaderSourceParser::ParseHLSL(
			MakeStringSection(
				(const char*)buildInterpolatorsSource.get(),
				(const char*)PtrAdd(buildInterpolatorsSource.get(), fileSize)));
    }

    ParameterMachine::~ParameterMachine() {}

    class ParameterGenerator
    {
    public:
        unsigned Count() const              { return (unsigned)_parameters.size(); };
        std::string VSOutputMember() const  { return _vsOutputMember; }

        std::string VaryingStructSignature(unsigned index) const;
        std::string VSInitExpression(unsigned index);
        std::string PSExpression(unsigned index, const char vsOutputName[], const char varyingParameterStruct[]) const;
        bool IsGlobalResource(unsigned index) const;
        const NodeGraphSignature::Parameter& Param(unsigned index) const { return _parameters[index]; }

        bool IsInitializedBySystem(unsigned index) const { return !_buildSystemFunctions[index].empty(); }

        ParameterGenerator(const NodeGraphSignature& interf, const PreviewOptions& previewOptions);
        ~ParameterGenerator();
    private:
        std::vector<NodeGraphSignature::Parameter>  _parameters;
        std::vector<std::string>            _buildSystemFunctions;
        std::string                         _vsOutputMember;
        ParameterMachine                    _paramMachine;

		const PreviewOptions* _previewOptions;
    };

	static bool IsStructType(StringSection<char> typeName)
    {
        // If it's not recognized as a built-in shader language type, then we
        // need to assume this is a struct type. There is no typedef in HLSL, but
        // it could be a #define -- but let's assume it isn't.
        return RenderCore::ShaderLangTypeNameAsTypeDesc(typeName)._type == ImpliedTyping::TypeCat::Void;
    }

    std::string ParameterGenerator::VaryingStructSignature(unsigned index) const
    {
        if (!_buildSystemFunctions[index].empty()) return std::string();
        if (IsGlobalResource(index)) return std::string();
        const auto& p = _parameters[index];

        std::stringstream result;
        result << "\t" << p._type << " " << p._name;
        /*if (!p._semantic.empty()) {
            result << " : " << p._semantic;
        } else */ {
            char smallBuffer[128];
            if (!IsStructType(MakeStringSection(p._type)))  // (struct types don't get a semantic)
                result << " : " << "VARYING_" << XlI32toA(index, smallBuffer, dimof(smallBuffer), 10);
        }
        return result.str();
    }

    std::string ParameterGenerator::VSInitExpression(unsigned index)
    {
        const auto& p = _parameters[index];
        if (!_buildSystemFunctions[index].empty()) return std::string();

        // Here, we have to sometimes look for parameters that we understand.
        // First, we should look at the semantic attached.
        // We can look for translator functions in the "BuildInterpolators.h" system header
        // If there is a function signature there that can generate the interpolator
        // we're interested in, then we should use that function.
        //
        // If the parameter is actually a structure, we need to look inside of the structure
        // and bind the individual elements. We should do this recursively incase we have
        // structures within structures.
        //
        // Even if we know that the parameter is a structure, it might be hard to find the
        // structure within the shader source code... It would require following through
        // #include statements, etc. That could potentially create some complications here...
        // Maybe we just need a single BuildInterpolator_ that returns a full structure for
        // things like VSOutput...?

        std::string buildInterpolator;
        VaryingParamsFlags::BitField flags;
        std::tie(buildInterpolator, flags) = _paramMachine.GetBuildInterpolator(p);

        if (!buildInterpolator.empty()) {
            if (flags & VaryingParamsFlags::WritesVSOutput)
                _vsOutputMember = p._name;
            return buildInterpolator + "(vsInput)";
        } else {
            if (!IsStructType(MakeStringSection(p._type))) {

					// Look for a "restriction" applied to this variable.
				auto r = std::find_if(
					_previewOptions->_variableRestrictions.cbegin(), _previewOptions->_variableRestrictions.cend(),
					[&p](const std::pair<std::string, std::string>& v) { return XlEqStringI(v.first, p._name); });
				if (r != _previewOptions->_variableRestrictions.cend()) {
                     if (XlBeginsWith(MakeStringSection(r->second), MakeStringSection("Function:"))) {
                            // This is actually a function name. It would be great if we could chose to
                            // run this function in the VS or PS, dependant on the parameters.
                        _buildSystemFunctions[index] = r->second.substr(9);
                        return std::string();
                    } else {
					    static std::regex pattern("([^:]*):([^:]*):([^:]*)");
					    std::smatch match;
					    if (std::regex_match(r->second, match, pattern) && match.size() >= 4) {
						    return std::string("InterpolateVariable_") + match[1].str() + "(" + match[2].str() + ", " + match[3].str() + ", localPosition)";
					    } else {
						    return r->second;	// interpret as a constant
					    }
                     }
				} else {
					// attempt to set values
					auto type = RenderCore::ShaderLangTypeNameAsTypeDesc(MakeStringSection(p._type));
					int dimensionality = type._arrayCount;
					if (dimensionality == 1) {
						return "localPosition.x * 0.5 + 0.5.x";
					} else if (dimensionality == 2) {
						return "float2(localPosition.x * 0.5 + 0.5, localPosition.y * -0.5 + 0.5)";
					} else if (dimensionality == 3) {
						return "worldPosition.xyz";
					} else if (dimensionality == 4) {
						return "float4(worldPosition.xyz, 1.0)";
					}
				}
            }
        }

        return std::string();
    }

    std::string ParameterGenerator::PSExpression(unsigned index, const char vsOutputName[], const char varyingParameterStruct[]) const
    {
        if (IsGlobalResource(index))
            return _parameters[index]._name;

        auto buildSystemFunction = _buildSystemFunctions[index];
        if (!buildSystemFunction.empty()) {
            if (!_vsOutputMember.empty())
                return buildSystemFunction + "(" + varyingParameterStruct + "." + _vsOutputMember + ", sys)";
            return buildSystemFunction + "(" + vsOutputName + ", sys)";
        } else {
            return std::string(varyingParameterStruct) + "." + _parameters[index]._name;
        }
    }

    bool ParameterGenerator::IsGlobalResource(unsigned index) const
    {
            // Resource types (eg, texture, etc) can't be handled like scalars
            // they must become globals in the shader.
		auto descriptor = RenderCore::ShaderLangTypeNameAsDescriptorType(MakeStringSection(_parameters[index]._type));
		return descriptor != RenderCore::DescriptorType::Unknown;
    }

    ParameterGenerator::ParameterGenerator(const NodeGraphSignature& interf, const PreviewOptions& previewOptions)
    {
		for (const auto&p:interf.GetParameters())
			if (p._direction == ParameterDirection::In)
				_parameters.push_back(p);
        for (auto i=_parameters.cbegin(); i!=_parameters.cend(); ++i)
            _buildSystemFunctions.push_back(_paramMachine.GetBuildSystem(*i));
		_previewOptions = &previewOptions;
    }

    ParameterGenerator::~ParameterGenerator() {}

    std::string         GenerateStructureForPreview(
        const StringSection<char> graphName, const NodeGraphSignature& interf,
        const PreviewOptions& previewOptions)
    {
            //
            //      Generate the shader structure that will surround the main
            //      shader generated from "graph"
            //
            //      We have to analyse the inputs and output.
            //
            //      The type of structure should be determined by the dimensionality
            //      of the outputs, and whether the shader takes position inputs.
            //
            //      We must then look at the inputs, and try to determine which
            //      inputs (if any) should vary over the surface of the preview.
            //
            //      For example, if our preview is a basic 2d or 1d preview, then
            //      the x and y axes will represent some kind of varying parameter.
            //      But for a 3d preview window, there are no varying parameters
            //      (all parameters must be fixed over the surface of the preview
            //      window)
            //

        ParameterGenerator mainParams(interf, previewOptions);

            //
            //      All varying parameters must have semantics
            //      so, look for free TEXCOORD slots, and set all unset semantics
            //      to a default
            //

        std::stringstream result;
        result << std::endl;
        result << "\t//////// Structure for preview ////////" << std::endl;

        const bool renderAsChart = previewOptions._type == PreviewOptions::Type::Chart;
        if (renderAsChart)
            result << "#define SHADER_NODE_EDITOR_CHART 1" << std::endl;
        result << "#include \"xleres/TechniqueLibrary/ToolsRig/MaterialTool/BuildInterpolators.h\"" << std::endl;

            //
            //      First write the "varying" parameters
            //      The varying parameters are always written in the vertex shader and
            //      read by the pixel shader. They will "vary" over the geometry that
            //      we're rendering -- hence the name.
            //      We could use a Mustache template for this, if we were using the
            //      more general implementation of Mustache for C++. But unfortunately
            //      there's no practical way with Plustasche.
            //
        result << "struct NE_Varying" << std::endl << "{" << std::endl;
        for (unsigned index=0; index<mainParams.Count(); ++index) {
            auto sig = mainParams.VaryingStructSignature(index);
            if (sig.empty()) continue;
            result << sig << ";" << std::endl;
        }
        result << "};" << std::endl << std::endl;

            //
            //      Write "_Output" structure. This contains all of the values that are output
            //      from the main function
            //
        result << "struct NE_" << graphName.AsString() << "_Output" << std::endl << "{" << std::endl;
        unsigned svTargetCounter = 0;
        for (const auto& i:interf.GetParameters())
			if (i._direction == ParameterDirection::Out)
				result << "\t" << i._type << " " << i._name << ": SV_Target" << (svTargetCounter++) << ";" << std::endl;
        result << "};" << std::endl << std::endl;

            //
            //      Write all of the global resource types
            //
        for (unsigned index=0; index<mainParams.Count(); ++index)
            if (mainParams.IsGlobalResource(index))
                result << mainParams.Param(index)._type << " " << mainParams.Param(index)._name << ";" << std::endl;
        result << std::endl;

            //
            //      Calculate the code that will fill in the varying parameters from the vertex
            //      shader. We need to do this now because it will effect some of the structure
            //      later.
            //
        std::stringstream varyingInitialization;

        for (unsigned index=0; index<mainParams.Count(); ++index) {
            auto initString = mainParams.VSInitExpression(index);
            if (initString.empty()) continue;
            varyingInitialization << "\tOUT.varyingParameters." << mainParams.Param(index)._name << " = " << initString << ";" << std::endl;
        }

        result << "struct NE_PSInput" << std::endl << "{" << std::endl;
        if (mainParams.VSOutputMember().empty())
            result << "\tVSOutput geo;" << std::endl;
        result << "\tNE_Varying varyingParameters;" << std::endl;
        result << "};" << std::endl << std::endl;

        std::string parametersToMainFunctionCall;

            //  Pass each member of the "varyingParameters" struct as a separate input to
            //  the main function
        for (unsigned index=0; index<mainParams.Count(); ++index) {
            if (!parametersToMainFunctionCall.empty())
                parametersToMainFunctionCall += ", ";
            parametersToMainFunctionCall += mainParams.PSExpression(index, "input.geo", "input.varyingParameters");
        }

            //  Also pass each output as a parameter to the main function
        for (const auto& i:interf.GetParameters()) {
			if (i._direction == ParameterDirection::Out) {
				if (!parametersToMainFunctionCall.empty())
					parametersToMainFunctionCall += ", ";
				parametersToMainFunctionCall += "functionResult." + i._name;
			}
        }

        Plustache::template_t preprocessor;

        // Render the ps_main template
        {
            Plustache::Context context;
            context.add("GraphName", graphName.AsString());
            context.add("ParametersToMainFunctionCall", parametersToMainFunctionCall);
            context.add("PreviewOutput", previewOptions._outputToVisualize);

                // Collect all of the output values into a flat array of floats.
                // This is needed for "charts"
            if (renderAsChart) {
                std::vector<PlustacheTypes::ObjectType> chartLines;

                if (!previewOptions._outputToVisualize.empty()) {
                    // When we have an "outputToVisualize" we only show the
                    // chart for that single output.
                    chartLines.push_back(
                        PlustacheTypes::ObjectType { std::make_pair("Item", previewOptions._outputToVisualize) });
                } else {
                    // Find all of the scalar values written out from main function,
                    // including searching through parameter strructures.
                    for (const auto& i:interf.GetParameters()) {
						if (i._direction != ParameterDirection::Out) continue;

                        auto type = RenderCore::ShaderLangTypeNameAsTypeDesc(MakeStringSection(i._type));
                        auto dim = type._arrayCount;
                        for (unsigned c=0; c<dim; ++c) {
                            std::stringstream str;
                            str << i._name;
                            if (dim != 1) str << "[" << c << "]";
                            chartLines.push_back(
                                PlustacheTypes::ObjectType { std::make_pair("Item", str.str()) });
                        }
                    }
                }

                context.add("ChartLines", chartLines);
                context.add("ChartLineCount", (StringMeld<64>() << unsigned(chartLines.size())).get());
            }

            if (renderAsChart) {
                result << preprocessor.render(GetPreviewTemplate("ps_main_chart"), context);
            } else if (!previewOptions._outputToVisualize.empty()) {
                result << preprocessor.render(GetPreviewTemplate("ps_main_explicit"), context);
            } else
                result << preprocessor.render(GetPreviewTemplate("ps_main"), context);
        }

        // Render the vs_main template
        result << preprocessor.render(GetPreviewTemplate("vs_main"),
            PlustacheTypes::ObjectType
            {
                {"InitGeo", ToPlustache(mainParams.VSOutputMember().empty())},
                {"VaryingInitialization", varyingInitialization.str()}
            });

        return result.str();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::string GenerateDescriptorVariables(
		const RenderCore::Assets::PredefinedDescriptorSetLayout& descriptorSet, 
        unsigned descriptorSetSlotIdx,
		IteratorRange<const GraphLanguage::NodeGraphSignature::Parameter*> captures)
	{
		std::stringstream result;

		for (auto i=descriptorSet._slots.begin(); i!=descriptorSet._slots.end(); ++i) {
            if (i->_type != RenderCore::DescriptorType::SampledTexture || i->_name.empty())
                continue;
			auto descriptorIdx = std::distance(descriptorSet._slots.begin(), i);
			auto cap = std::find_if(
				captures.begin(), captures.end(),
				[i](const GraphLanguage::NodeGraphSignature::Parameter&p) 
				{ 
					auto dot = p._name.find_first_of('.');
					if (dot != std::string::npos) {	// we must ignore the captures name before the dot, if it exists
						return XlEqString(MakeStringSection(p._name.begin()+dot+1, p._name.end()), i->_name);
					} else {
						return p._name == i->_name; 
					}
				});
			if (cap != captures.end()) {
    			result << cap->_type << " " << i->_name << " : register(t" << descriptorIdx << ", space" << descriptorSetSlotIdx << ");" << std::endl;
            }
		}

		for (auto i=descriptorSet._slots.begin(); i!=descriptorSet._slots.end(); ++i) {
            if (i->_type != RenderCore::DescriptorType::Sampler || i->_name.empty())
                continue;

			auto descriptorIdx = std::distance(descriptorSet._slots.begin(), i);
			auto cap = std::find_if(
				captures.begin(), captures.end(),
				[i](const GraphLanguage::NodeGraphSignature::Parameter&p)
				{ 
					auto dot = p._name.find_first_of('.');	// we must ignore the captures name before the dot, if it exists
					if (dot != std::string::npos) {
						return XlEqString(MakeStringSection(p._name.begin()+dot+1, p._name.end()), i->_name);
					} else {
						return p._name == i->_name; 
					}
				});
			if (cap != captures.end()) {
                result << cap->_type << " " << i->_name << " : register(s" << descriptorIdx << ", space" << descriptorSetSlotIdx << ");" << std::endl;
            }
		}

		for (auto cb=descriptorSet._slots.begin(); cb!=descriptorSet._slots.end(); ++cb) {
            if (cb->_type != RenderCore::DescriptorType::UniformBuffer || cb->_name.empty())
                continue;

			result << "cbuffer " << cb->_name << " : register (b" << std::distance(descriptorSet._slots.begin(), cb)  << ", space" << descriptorSetSlotIdx << ")" << std::endl;
            result << "{" << std::endl;

            if (cb->_cbIdx != ~0u) {
                const auto& layout = *descriptorSet._constantBuffers[cb->_cbIdx];
                for (auto ele=layout._elements.begin(); ele!=layout._elements.end(); ++ele) {
                    auto idx = std::distance(layout._elements.begin(), ele);
                    result << "\t" << RenderCore::AsShaderLangTypeName(ele->_type, RenderCore::ShaderLanguage::HLSL) << " " << layout._elements[idx]._name << ";" << std::endl;
                }
            }

			result << "}" << std::endl;
		}

		return result.str();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	static std::string GetTechniqueTemplate(const char templateName[])
    {
        StringMeld<MaxPath, Assets::ResChar> str;
        str << "xleres/System/TechniqueTemplates.hlsl:" << templateName;
        return ::Assets::Legacy::GetAssetDep<TemplateItem>(str.AsStringSection())._item;
    }

	static void MaybeComma(std::stringstream& stream) { if (stream.tellp() != std::stringstream::pos_type(0)) stream << ", "; }

	std::string GenerateStructureForTechniqueConfig(const NodeGraphSignature& interf, StringSection<char> graphName)
	{
		std::stringstream mainFunctionParameterSignature;
		std::stringstream forwardMainParameters;
		for (const auto& p:interf.GetParameters()) {
			if (p._direction != ParameterDirection::In) continue;

			MaybeComma(mainFunctionParameterSignature);
			mainFunctionParameterSignature << p._type << " " << p._name;

			MaybeComma(forwardMainParameters);
			forwardMainParameters << p._name;
		}

		Plustache::Context context;
		context.add("MainFunctionParameterSignature", mainFunctionParameterSignature.str());
		context.add("ForwardMainParameters", forwardMainParameters.str());
		context.add("GraphName", graphName.AsString());

		std::stringstream result;
		Plustache::template_t preprocessor;
		result << preprocessor.render(GetTechniqueTemplate("forward_main"), context);
        result << preprocessor.render(GetTechniqueTemplate("deferred_main"), context);
		result << preprocessor.render(GetTechniqueTemplate("oi_main"), context);
        result << preprocessor.render(GetTechniqueTemplate("stochastic_main"), context);
        result << preprocessor.render(GetTechniqueTemplate("depthonly_main"), context);
		return result.str();
	}
}

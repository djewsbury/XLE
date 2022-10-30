// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderPatcher.h"
#include "NodeGraph.h"
#include "NodeGraphProvider.h"
#include "NodeGraphSignature.h"
#include "ShaderInstantiation.h"
#include "GraphSyntax.h"		// (for GenerateSignature)
#include "../RenderCore/ShaderLangUtil.h"
#include "../Assets/DepVal.h"
#include "../Assets/Assets.h"
#include "../Core/Exceptions.h"
#include "../OSServices/Log.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Conversion.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/PreprocessorInterpreter.h"
#include <sstream>
#include <set>
#include <assert.h>
#include <algorithm>
#include <tuple>
#include <map>

namespace ShaderSourceParser
{
	using namespace GraphLanguage;

///////////////////////////////////////////////////////////////////////////////////////////////////

	struct DependencyTrackers
	{
		std::set<::Assets::DependencyValidation> _depVals;
		std::set<::Assets::DependentFileState> _depFileStates;
	};

    static std::string AsString(uint32_t i)
    {
        char buffer[128];
        XlI64toA(i, buffer, dimof(buffer), 10);
        return buffer;
    }

	static std::string SantizeIdentifier(const std::string& input)
	{
		std::string result;
		result.reserve(input.size());
		for (auto c:input)
			if ((c >= '0' && c <= '9') || c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
				result.push_back(c);
			} else
				result.push_back('_');
		return result;
	}

    static std::string OutputTemporaryForNode(NodeId nodeId, const std::string& outputName)
    {
        return std::string("Output_") + AsString(nodeId) + "_" + SantizeIdentifier(outputName);
    }

    template<typename Connection>
        const Connection* FindConnectionThatOutputsTo(
            IteratorRange<const Connection*> connections,
            NodeId nodeId, const std::string& parameterName)
    {
        return std::find_if(
            connections.cbegin(), connections.cend(),
            [nodeId, &parameterName](const Connection& connection)
            { return    connection.OutputNodeId() == nodeId &&
                        connection.OutputParameterName() == parameterName; } );
    }

    struct ExpressionString { std::string _expression, _type; };

    static std::string WriteCastExpression(std::stringstream& result, const ExpressionString& expression, const std::string& dstType)
    {
        if (!expression._type.empty() && !dstType.empty() && expression._type != dstType && !XlEqStringI(dstType, "auto") && !XlEqStringI(expression._type, "auto")) {
            result << "Cast_" << expression._type << "_to_" << dstType << "(" << expression._expression << ")";
			return dstType;
        } else {
            result << expression._expression;
			return expression._type;
		}
    }

    static std::string TypeFromShaderFragment(
        StringSection<> archiveName, StringSection<> paramName, ParameterDirection direction,
        INodeGraphProvider& sigProvider,
		DependencyTrackers& depVals)
    {
            // Go back to the shader fragments to find the current type for the given parameter
		std::optional<INodeGraphProvider::Signature> sigResult;

		// Some typenames contain a template type in angle brackets. For example, when 
		// graphs are passed in as parameters, they are given a template signature, in the form:
		//		parameterName<signatureTemplate>
		auto typenameBegin = std::find(archiveName.begin(), archiveName.end(), '<');
		if (typenameBegin != archiveName.end()) {
			auto typenameEnd = std::find(typenameBegin+1, archiveName.end(), '>');
			sigResult = sigProvider.FindSignature(MakeStringSection(typenameBegin+1, typenameEnd));
		} else {
			sigResult = sigProvider.FindSignature(archiveName);
		}
        if (!sigResult)
            return std::string();
            // Throw(::Exceptions::BasicLabel("Couldn't find signature for (%s)", archiveName.AsString().c_str()));

        const auto& sig = sigResult.value()._signature;
		assert(sigResult.value()._depVal);
		depVals._depVals.insert(sigResult.value()._depVal);
		assert(!sigResult.value()._fileState._filename.empty());
		depVals._depFileStates.insert(sigResult.value()._fileState);

            // find a parameter with the right direction & name
        for (const auto& p:sig.GetParameters())
            if (p._direction == direction && XlEqString(paramName, p._name))
                return p._type;

        return std::string();
    }

	class GraphInterfaceContext
	{
	public:
		IteratorRange<const NodeGraphSignature::Parameter*>		_predefinedParameters;
		std::vector<NodeGraphSignature::Parameter>				_dangingParameters;		// these are input/outputs from nodes in the graph that aren't connected to anything
		std::vector<NodeGraphSignature::Parameter>				_additionalParameters;	// these are interface parameters referenced in the graph that don't match anything in the fixed interface
		std::vector<NodeGraphSignature::Parameter>				_capturedParameters;

		std::vector<std::pair<std::string, NodeGraphSignature::Parameter>>	_curriedParameters;
	};

	static bool HasParameterWithName(const std::string& name, IteratorRange<const NodeGraphSignature::Parameter*> params)
	{
		auto i = std::find_if(params.begin(), params.end(), [&name](const NodeGraphSignature::Parameter&p) { return p._name == name; } );
		return i != params.end();
	}

	static std::string UniquifyName(const std::string& name, const GraphInterfaceContext& interfContext)
    {
		std::string testName = name;
		unsigned suffix = 0;
		for (;;) {
			if (	!HasParameterWithName(testName, MakeIteratorRange(interfContext._additionalParameters))
				&&	!HasParameterWithName(testName, MakeIteratorRange(interfContext._dangingParameters))
				&&	!HasParameterWithName(testName, MakeIteratorRange(interfContext._capturedParameters))) {
				return testName;
			} else {
				std::stringstream str;
				str << name << suffix;
				testName = str.str();
				++suffix;
			}
		}
    }

	static std::string RemoveTemplateRestrictions(const std::string& input)
	{
		// for strings like "something<parameters>", remove the part in angle brackets (ie, just return "something")
		if (input.empty() || *(input.end()-1) != '>') return input;
		auto i = input.find_first_of('<');
		if (i != std::string::npos)
			return input.substr(i+1, input.size()-i-2);
		return input;
	}

	static std::string TypeOfConstant(StringSection<> constantValue)
	{
		// todo --	we can get the constant type with the implied typing API, but we then need
		//			a way to convert that into a string typename. Different languages will use
		//			different names for basic types, and the rest of the code doesn't currently
		//			make many assumptions about what kinds of type names are used.
		// auto type = ImpliedTyping::ParseFullMatch(constantValue);
		return {};
	}

    static ExpressionString QueryExpression(const NodeGraph& nodeGraph, const Connection& connection, const std::string& expectedType, GraphInterfaceContext& interfContext, INodeGraphProvider& sigProvider, DependencyTrackers& depVals)
    {
		std::stringstream str;
		std::string finalType;
		if (connection.InputNodeId() == NodeId_Constant) {

				//  we have a "constant connection" value here
			auto value = connection.InputParameterName();
			auto type = TypeOfConstant(MakeStringSection(value));
			finalType = WriteCastExpression(str, {value, type}, expectedType);

		} else if (connection.InputNodeId() == NodeId_Interface) {

			NodeGraphSignature::Parameter param { expectedType, connection.InputParameterName() };

			// If we have an entry for this param in the "predefinedParameters" list, then we must
			// take the type and semantic from there.
			auto predefined = std::find_if(
				interfContext._predefinedParameters.begin(), interfContext._predefinedParameters.end(),
				[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::In && XlEqString(MakeStringSection(connection.InputParameterName()), param._name); });
			if (predefined != interfContext._predefinedParameters.end()) {
				param = *predefined;
				if (param._type.empty() || XlEqString(param._type, "auto"))	// fallback to the expectedType if the predefined param has no type sets
					param._type = expectedType;
			}

			auto existing = std::find_if(
				interfContext._additionalParameters.begin(), interfContext._additionalParameters.end(),
				[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::In && XlEqString(MakeStringSection(connection.InputParameterName()), param._name); });
			if (existing == interfContext._additionalParameters.end()) {
				existing = interfContext._additionalParameters.insert(interfContext._additionalParameters.end(), param);
			} else {
				assert(XlEqString(MakeStringSection(param._type), existing->_type));
			}

			finalType = WriteCastExpression(str, {param._name, param._type}, expectedType);

		} else {

			auto n = nodeGraph.GetNode(connection.InputNodeId());
			if (n) {
				ExpressionString expr;
				if (n->GetType() == Node::Type::Procedure) {
					auto type = TypeFromShaderFragment(n->ArchiveName(), connection.InputParameterName(), ParameterDirection::Out, sigProvider, depVals);
					expr = {OutputTemporaryForNode(connection.InputNodeId(), connection.InputParameterName()), type};
				} else {
					assert(n->GetType() == Node::Type::Captures);

					NodeGraphSignature::Parameter param { expectedType, connection.InputParameterName() };

					auto predefined = std::find_if(
						interfContext._predefinedParameters.begin(), interfContext._predefinedParameters.end(),
						[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::In && XlEqString(MakeStringSection(connection.InputParameterName()), param._name); });
					if (predefined != interfContext._predefinedParameters.end()) {
						param = *predefined;
						if (param._type.empty() || XlEqString(param._type, "auto"))
							param._type = expectedType;
					}

					auto existing = std::find_if(
						interfContext._capturedParameters.begin(), interfContext._capturedParameters.end(),
						[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::In && XlEqString(MakeStringSection(connection.InputParameterName()), param._name); });
					if (existing == interfContext._capturedParameters.end()) {
						existing = interfContext._capturedParameters.insert(interfContext._capturedParameters.end(), param);
					} else {
						assert(XlEqString(MakeStringSection(param._type), existing->_type));
					}

					expr = ExpressionString { param._name, param._type };
				}
				finalType = WriteCastExpression(str, expr, expectedType);
			} else {
				// str << "// ERROR: could not find node for parameter expression. Looking for node id (" << connection.InputNodeId() << ") and input parameter (" << connection.InputParameterName() << ")" << std::endl;
				str << "DefaultValue_" << expectedType << "()";
				finalType = expectedType;
			}

		}

		return {str.str(), finalType};
    }

    static ExpressionString ParameterExpression(
		const NodeGraph& nodeGraph, NodeId nodeId, const NodeGraphSignature::Parameter& signatureParam,
		GraphInterfaceContext& interfContext, 
		bool generateDanglingInputs,
		INodeGraphProvider& sigProvider,
		DependencyTrackers& depVals)
    {
		auto expectedType = signatureParam._type;
        auto i = FindConnectionThatOutputsTo(nodeGraph.GetConnections(), nodeId, signatureParam._name);
        if (i!=nodeGraph.GetConnections().cend()) {
            return QueryExpression(nodeGraph, *i, expectedType, interfContext, sigProvider, depVals);
		}

		// We must add this request as some kind of input to the function (ie, a parameter input or a global input)
		// auto uniqueName = UniquifyName(signatureParam._name, interfContext);
		if (generateDanglingInputs) {
			auto uniqueName = "in_" + std::to_string(nodeId) + "_" + signatureParam._name;
			interfContext._dangingParameters.push_back({expectedType, uniqueName, ParameterDirection::In, signatureParam._semantic});
			return {uniqueName, expectedType};
		} else {
			return {"DefaultValue_" + expectedType + "()", expectedType};
		}
    }

    template<typename Connection>
        static void FillDirectOutputParameters(
            std::stringstream& result,
            const NodeGraph& graph,
            IteratorRange<const Connection*> range,
            GraphInterfaceContext& interfContext,
            INodeGraphProvider& sigProvider,
			DependencyTrackers& depVals)
    {
        for (const auto& connection:range) {
			auto* destinationNode = graph.GetNode(connection.OutputNodeId());
            if (!destinationNode) {
				// This is not connected to anything -- so we just have to add it as a
				// unique output from the interface.
				//
				// If the parameter is already in the interface, then use that -- otherwise we
				// must create an "additional parameter" for it

				std::string inputType;
				auto* srcNode = graph.GetNode(connection.InputNodeId());
				if (srcNode)
					inputType = TypeFromShaderFragment(srcNode->ArchiveName(), connection.InputParameterName(), ParameterDirection::Out, sigProvider, depVals);

				auto p = std::find_if(
					interfContext._additionalParameters.begin(), interfContext._additionalParameters.end(),
					[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::Out && XlEqString(MakeStringSection(connection.OutputParameterName()), param._name); });
				if (p == interfContext._additionalParameters.end()) {
					auto predefined = std::find_if(
						interfContext._predefinedParameters.begin(), interfContext._predefinedParameters.end(),
						[&connection](const NodeGraphSignature::Parameter& param) { return param._direction == ParameterDirection::Out && XlEqString(MakeStringSection(connection.OutputParameterName()), param._name); });
					if (predefined != interfContext._predefinedParameters.end()) {
						NodeGraphSignature::Parameter newParam = *p;
						if (newParam._type.empty() || XlEqString(newParam._type, "auto"))
							newParam._type = inputType;
						p = interfContext._additionalParameters.insert(interfContext._additionalParameters.end(), newParam);
					} else {
						p = interfContext._additionalParameters.insert(interfContext._additionalParameters.end(), {inputType, connection.OutputParameterName(), ParameterDirection::Out});
					}
				}

				result << "\t" << p->_name << " = " << QueryExpression(graph, connection, p->_type, interfContext, sigProvider, depVals)._expression << ";" << std::endl;
			}
        }
    }

    struct ResolvedFunction
    {
    public:
        std::string _name;
        std::string _finalArchiveName;
        NodeGraphSignature _signature;
		InstantiationRequest _instantiationParameters;
		bool _isGraphSyntaxFile;
		std::shared_ptr<INodeGraphProvider> _customProvider;
    };

    static ResolvedFunction ResolveFunction(
        const std::string& archiveName, 
        const InstantiationRequest& instantiationParameters, 
        INodeGraphProvider& sigProvider,
		DependencyTrackers& depVals)
    {
        ResolvedFunction result;

        // Check to see if this function call is using a template parameter. Templated function names look like this:
        //      name '<' restriction '>'
        // Where name is the name of the parameter, and restriction is a function signature we're expecting it to
        // match.
        // If the template parameter has not been assigned to anything, we'll use the restriction as a kind of default.
        auto marker = std::find(archiveName.begin(), archiveName.end(), '<');
        if (marker != archiveName.end()) {
            auto parameterName = MakeStringSection(archiveName.begin(), marker);
            auto restriction = MakeStringSection(marker+1, std::find(archiveName.begin(), archiveName.end(), '>'));

            auto i = instantiationParameters._parameterBindings.find(parameterName.AsString());
            if (i!=instantiationParameters._parameterBindings.end()) {
				result._finalArchiveName = i->second->_archiveName;
				result._instantiationParameters = *i->second;
				result._customProvider = i->second->_customProvider;
            } else {
				result._finalArchiveName = restriction.AsString();
            }

			std::optional<INodeGraphProvider::Signature> sigProviderResult;
			if (result._customProvider) {
				// Search the "custom provider" for both the archive name & the restriction
				// If they both fail, we'll still fallback to searching the main provider
				// (but only for the restriction)
				sigProviderResult = result._customProvider->FindSignature(result._finalArchiveName);
				if (!sigProviderResult)
					sigProviderResult = result._customProvider->FindSignature(restriction);
			} else {
				sigProviderResult = sigProvider.FindSignature(result._finalArchiveName);
			}
			if (!sigProviderResult)
				sigProviderResult = sigProvider.FindSignature(restriction);
            if (!sigProviderResult)
                Throw(::Exceptions::BasicLabel("Couldn't find signature for (%s)", result._finalArchiveName.c_str()));
			result._signature = sigProviderResult.value()._signature;

			auto p = result._finalArchiveName.find_last_of(':');
			if (p != std::string::npos) {
				result._name = result._finalArchiveName.substr(p+1);
			} else
				result._name = result._finalArchiveName;

			result._isGraphSyntaxFile = sigProviderResult.value()._isGraphSyntax;

			assert(sigProviderResult.value()._depVal);
			depVals._depVals.insert(sigProviderResult.value()._depVal);
			assert(!sigProviderResult.value()._fileState._filename.empty());
			depVals._depFileStates.insert(sigProviderResult.value()._fileState);

            return result;
        }

        auto sigProviderResult = sigProvider.FindSignature(archiveName);
        if (!sigProviderResult)
            Throw(::Exceptions::BasicLabel("Couldn't find signature for (%s)", archiveName.c_str()));
		result._signature = sigProviderResult.value()._signature;        
        result._name = sigProviderResult.value()._name;
        // result._finalArchiveName = sigProviderResult.value()._sourceFile.empty() ? archiveName : sigProviderResult.value()._sourceFile + ":" + result._name;
		result._finalArchiveName = archiveName;
		result._isGraphSyntaxFile = sigProviderResult.value()._isGraphSyntax;
		assert(sigProviderResult.value()._depVal);
		depVals._depVals.insert(sigProviderResult.value()._depVal);
		assert(!sigProviderResult.value()._fileState._filename.empty());
		depVals._depFileStates.insert(sigProviderResult.value()._fileState);
        return result;
    }

    static std::pair<std::stringstream, ResolvedFunction> GenerateFunctionCall(
        GraphInterfaceContext& interfContext,
        DependencyTable& workingDependencyTable,    // this is the dependency table into which we'll append this function call
        const Node& node, 
        const NodeGraph& nodeGraph,
        const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions,
        INodeGraphProvider& sigProvider,
		DependencyTrackers& depVals)
    {
            //
            //      Parse the fragment again, to get the correct function
            //      signature at this time.
            //
            //      It's possible that the function signature has changed
            //      since the node graph was generated. In this case, we
            //      have to try to adapt to match the node graph as closely
            //      as possible. This might require:
            //          * ignoring some connections
            //          * adding casting operations
            //          * adding some default values
            //
            //      \todo --    avoid parsing the same shader file twice (parse
            //                  when writing the #include statements to the file)
            //


        auto sigRes = ResolveFunction(node.ArchiveName(), instantiationParameters, sigProvider, depVals);
        auto functionName = sigRes._name;
        auto& sig = sigRes._signature;

            //
            //  There are template parameters in the signature, and if we are passing values
            //  for those parameters, we must select a specific instantiation of the function
            //
        InstantiationRequest callInstantiation;
		for (const auto&param:sigRes._instantiationParameters._parameterBindings)
			callInstantiation._parameterBindings.insert(
				std::make_pair(
					param.first,
					std::make_unique<InstantiationRequest>(*param.second)));

		// the node graph can override any instantiation parameters
        for (const auto& tp:sig.GetTemplateParameters()) {
            auto connection = std::find_if(
                nodeGraph.GetConnections().begin(),
                nodeGraph.GetConnections().end(),
                [tp, &node](const Connection& p) {
                    return p.OutputNodeId() == node.NodeId()
                        && p.OutputParameterName() == tp._name;
                });
            if (connection!=nodeGraph.GetConnections().end() && XlEqString(MakeStringSection(connection->InputParameterName()), ParameterName_NodeInstantiation)) {
				// this connection must be used as a template parameter
				// The connected node is called an "instantiation" node -- it represents an instantiation of some function
				// There can be values attached as inputs to the instantiation node; they act like curried parameters.
				auto* instantiationNode = nodeGraph.GetNode(connection->InputNodeId());
				assert(instantiationNode);
				auto param = std::make_unique<InstantiationRequest>();
				param->_archiveName = instantiationNode->ArchiveName();

				// Any input parameters to this node that aren't part of the restriction signature should become curried
				auto restrictionSignature = sigProvider.FindSignature(tp._restriction);
				if (restrictionSignature) {
					assert(restrictionSignature.value()._depVal);
					depVals._depVals.insert(restrictionSignature.value()._depVal);
					assert(!restrictionSignature.value()._fileState._filename.empty());
					depVals._depFileStates.insert(restrictionSignature.value()._fileState);
				}
				for (const auto&c:nodeGraph.GetConnections()) {
					if (c.OutputNodeId() == instantiationNode->NodeId()) {
						auto paramName = c.OutputParameterName();
						bool isPartOfRestriction = false;
						if (restrictionSignature) {
							auto i = std::find_if(
								restrictionSignature.value()._signature.GetParameters().begin(),
								restrictionSignature.value()._signature.GetParameters().end(),
								[paramName](const NodeGraphSignature::Parameter&param) {
									return XlEqString(MakeStringSection(param._semantic), paramName);
								});
							isPartOfRestriction = i != restrictionSignature.value()._signature.GetParameters().end();
						}
						if (!isPartOfRestriction) {
							param->_parametersToCurry.push_back(paramName);
						}
					}
				}

				callInstantiation._parameterBindings.insert({tp._name, std::move(param)});
            }
        }

        std::stringstream result, warnings;

        auto callInstHash = callInstantiation.CalculateInstanceHash();
        if (!callInstantiation._parameterBindings.empty()) {
            for (const auto& c:callInstantiation._parameterBindings) {
                result << "\t// Instantiating " << functionName << " with " << c.first << " set to " << c.second->_archiveName << std::endl;
				for (const auto& p:c.second->_parametersToCurry)
					result << "\t//     Curried parameter: " << p << std::endl;
			}
            functionName += "_" + std::to_string(callInstHash);
        }

            //      1.  Declare output variable (made unique by node id)
            //      2.  Call the function, assigning the output variable as appropriate
            //          and passing in the parameters (as required)
        std::string returnType;
        for (const auto& i:sig.GetParameters())
            if (i._direction == ParameterDirection::Out) {
                if (i._name == s_resultName) {
                    returnType = i._type;
                    continue;
                }
                result << "\t" << i._type << " " << OutputTemporaryForNode(node.NodeId(), i._name) << ";" << std::endl;
            }

        if (!returnType.empty()) {
            auto outputName = OutputTemporaryForNode(node.NodeId(), s_resultName);
            result << "\t" << returnType << " " << outputName << " = " << functionName << "( ";
        } else {
            result << "\t" << functionName << "( ";
        }

		auto n = RemoveTemplateRestrictions(node.ArchiveName());
		auto parameterBindingsForThisNode = instantiationParameters._parameterBindings.find(n);

        bool pendingComma = false;
        for (auto p=sig.GetParameters().cbegin(); p!=sig.GetParameters().cend(); ++p) {
            if (p->_direction == ParameterDirection::Out && p->_name == s_resultName)
                continue;

            if (pendingComma) result << ", ";
            pendingComma = true;

                // note -- problem here for in/out parameters
            if (p->_direction == ParameterDirection::Out) {
                result << OutputTemporaryForNode(node.NodeId(), p->_name);
                continue;
            }

			if (p->_direction == ParameterDirection::In) {
				// Check if this parameter is marked to be "curried" by a template instantiation
				// When this happens, the value must be passed through the interface from the caller
				if (parameterBindingsForThisNode != instantiationParameters._parameterBindings.end()) {
					auto i = std::find_if(
						parameterBindingsForThisNode->second->_parametersToCurry.begin(),
						parameterBindingsForThisNode->second->_parametersToCurry.end(),
						[p](const std::string& str) { return XlEqString(MakeStringSection(str), p->_name);});
					if (i != parameterBindingsForThisNode->second->_parametersToCurry.end()) {
						interfContext._curriedParameters.push_back({n, *p});
						result << "curried_" << n << "_" << p->_name;
						continue;
					}
				}
			}

            result << ParameterExpression(nodeGraph, node.NodeId(), *p, interfContext, generateOptions._generateDanglingInputs, sigProvider, depVals)._expression;
        }

		// If the call instantiation itself has curried parameters, they won't appear in the 
		// signature returned from ResolveFunction. We must append their values here
		for (const auto& tp:callInstantiation._parameterBindings) {
			if (tp.second->_parametersToCurry.empty()) continue;

			// First, find the instantiation node
			auto connection = std::find_if(
				nodeGraph.GetConnections().begin(),
				nodeGraph.GetConnections().end(),
				[&tp, &node](const Connection& p) {
					return p.OutputNodeId() == node.NodeId()
						&& p.OutputParameterName() == tp.first;
				});
			if (connection!=nodeGraph.GetConnections().end() && XlEqString(MakeStringSection(connection->InputParameterName()), ParameterName_NodeInstantiation)) {
				auto* instantiationNode = nodeGraph.GetNode(connection->InputNodeId());
				assert(instantiationNode);

				// Now generate the syntax as if we're passing the parameter into the instantiation node
				for (const auto& c:tp.second->_parametersToCurry) {
					if (pendingComma) result << ", ";
					pendingComma = true;

					NodeGraphSignature::Parameter param{"auto", c};
					result << ParameterExpression(nodeGraph, instantiationNode->NodeId(), param, interfContext, generateOptions._generateDanglingInputs, sigProvider, depVals)._expression;
				}
			}
		}

        result << " );" << std::endl;
        if (warnings.tellp()) {
            result << "\t// Warnings in function call: " << std::endl;
            result << warnings.str();
        }

        // Append the function call to the dependency table
        DependencyTable::Dependency dep;
		dep._instantiation = std::move(callInstantiation);
		dep._instantiation._archiveName = sigRes._finalArchiveName;
		dep._instantiation._customProvider = sigRes._customProvider;
		dep._isGraphSyntaxFile = sigRes._isGraphSyntaxFile;

		auto depHash = dep._instantiation.CalculateInstanceHash();
        auto existing = std::find_if(
            workingDependencyTable._dependencies.begin(), workingDependencyTable._dependencies.end(),
            [&dep, depHash](const DependencyTable::Dependency& d) 
                { return d._instantiation._archiveName == dep._instantiation._archiveName && d._instantiation.CalculateInstanceHash() == depHash; });
        if (existing == workingDependencyTable._dependencies.end())
            workingDependencyTable._dependencies.emplace_back(std::move(dep));

        return std::make_pair(std::move(result), std::move(sigRes));
    }

	static bool HasConnectionStartingAt(const NodeGraph& nodeGraph, NodeId inputNodeId, StringSection<> parameterName)
	{
		for (const auto&c:nodeGraph.GetConnections())
			if (c.InputNodeId() == inputNodeId && XlEqString(parameterName, c.InputParameterName()))
				return true;
		return false;
	}

    static std::tuple<std::string, NodeGraphSignature, DependencyTable> GenerateMainFunctionBody(
        const NodeGraph& graph,
		IteratorRange<const NodeGraphSignature::Parameter*> predefinedParameters,
        const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions,
        INodeGraphProvider& sigProvider,
		DependencyTrackers& depVals)
    {
        std::stringstream result;

		bool acyclic = false;
		auto sortedNodes = SortNodes(graph, acyclic);

            //
            //      Now the function calls can be ordered by walking through the
            //      directed graph.
            //

        if (!acyclic) {
            result << "// Warning! found a cycle in the graph of nodes. Result will be incomplete!" << std::endl;
        }

        DependencyTable depTable;
		GraphInterfaceContext interfContext;
		interfContext._predefinedParameters = predefinedParameters;
		std::stringstream dangingOutBlock;

        for (auto i=sortedNodes.cbegin(); i!=sortedNodes.cend(); ++i) {
            auto i2 = std::find_if( graph.GetNodes().cbegin(),
                                    graph.GetNodes().cend(), [i](const Node& n) { return n.NodeId() == *i; } );
            if (i2 != graph.GetNodes().cend()) {
                if (i2->GetType() == Node::Type::Procedure) {
					auto fnCall = GenerateFunctionCall(interfContext, depTable, *i2, graph, instantiationParameters, generateOptions, sigProvider, depVals);
                    result << fnCall.first.str();

					// Look for "dangling outputs". These are outputs that have been generated by GenerateFunctionCall, but
					// are not attached to any other nodes as inputs. If the flag is set, these will become part of the 
					// function interface (and can be used for preview shaders, etc)
					if (i2->NodeId() == generateOptions._generateDanglingOutputs) {
						const auto& fnSig = fnCall.second._signature;
						for (const auto& p:fnSig.GetParameters()) {
							if (p._direction == ParameterDirection::Out) {
								if (!HasConnectionStartingAt(graph, i2->NodeId(), p._name)) {
									auto uniqueName = UniquifyName(p._name, interfContext);
									interfContext._dangingParameters.push_back({p._type, uniqueName, ParameterDirection::Out});
									dangingOutBlock << "\t" << uniqueName << " = " << OutputTemporaryForNode(i2->NodeId(), p._name) << ";" << std::endl;
								}
							}
						}
					}
                }
            }
        }

        for (const auto& dep:depTable._dependencies)
            result << "\t//Dependency: " << dep._instantiation._archiveName << " inst hash: " << dep._instantiation.CalculateInstanceHash() << std::endl;

		FillDirectOutputParameters(result, graph, graph.GetConnections(), interfContext, sigProvider, depVals);

		// todo -- any outputs in the fixed interface that we didn't write to in FillDirectOutputParameters should get default valuess

		result << dangingOutBlock.str();

		// Generate the final interface based on the fixed interface, any additional / dangling parameters we added
		NodeGraphSignature finalInterface;
		for (auto& p:interfContext._additionalParameters) finalInterface.AddParameter(p);
		for (auto& p:interfContext._dangingParameters) finalInterface.AddParameter(p);
		for (auto& p:interfContext._curriedParameters) {
			auto param = p.second;
			param._name = "curried_" + p.first + "_" + p.second._name;
			finalInterface.AddParameter(param);
		}

        return std::make_tuple(result.str(), std::move(finalInterface), std::move(depTable));
    }

	static NodeGraph FilterGraphWithSelectors(
		const NodeGraph& graph, const ParameterBox& selectors)
	{
		const ParameterBox* selectorsAsArray[] = { &selectors };

		NodeGraph filteredGraph;
		for (const auto&n:graph.GetNodes())
			filteredGraph.Add(Node{n});
		for (const auto&c:graph.GetConnections())
			if (c._condition.empty() || EvaluatePreprocessorExpression(c._condition, MakeIteratorRange(selectorsAsArray)))
				filteredGraph.Add(Connection{c});
		return filteredGraph;
	}

    GenerateFunctionResult GenerateFunction(
        const NodeGraph& graph, StringSection<char> name,
        const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions,
        INodeGraphProvider& sigProvider)
    {
		// We must filter the connections in the graph based on the selectors in the instantiation request
		// The most straightforward way to do this is to just filter them out here, before we call GenerateMainFunctionBody.
		// We will just remove any connections that have a condition that won't pass given the input selectors
		NodeGraph filteredGraph;
		if (generateOptions._filterWithSelectors) {
			filteredGraph = FilterGraphWithSelectors(graph, generateOptions._selectors);
		} else {
			filteredGraph = graph;
		}

		std::string mainBody;
		NodeGraphSignature interf;
        DependencyTable depTable;
		DependencyTrackers depTrackers;
		std::tie(mainBody, interf, depTable) = GenerateMainFunctionBody(filteredGraph, {}, instantiationParameters, generateOptions, sigProvider, depTrackers);

			//
            //      Our graph function is always a "void" function, and all of the output
            //      parameters are just function parameters with the "out" keyword. This is
            //      convenient for writing out generated functions
            //      We don't want to put the "node id" in the name -- because node ids can
            //      change from time to time, and that would invalidate any other shaders calling
            //      this function. But ideally we need some way to guarantee uniqueness.
            //
        std::stringstream result;     
        result << GenerateSignature(interf, name, false) << std::endl;
        result << "{" << std::endl;
		result << mainBody;
        result << "}" << std::endl;

		std::vector<std::string> fragments;
		fragments.emplace_back(result.str());

        return GenerateFunctionResult {
			std::move(fragments), 
			ShaderEntryPoint { name.AsString(), std::move(interf) },
			std::move(depTable),
			std::vector<GraphLanguage::NodeGraphSignature::Parameter> { interf.GetCapturedParameters().begin(), interf.GetCapturedParameters().end() },
			std::move(depTrackers._depVals),
			std::move(depTrackers._depFileStates) };
    }

	static void MaybeComma(std::stringstream& stream) { if (stream.tellp() != std::stringstream::pos_type(0)) stream << ", "; }

	std::string GenerateScaffoldFunction(
		const NodeGraphSignature& inputSlotSignature, 
		const NodeGraphSignature& generatedFunctionSignature, 
		StringSection<char> scaffoldFunctionName,
		StringSection<char> implementationFunctionName,
		ScaffoldFunctionFlags::BitField flags)
	{
			//
			//	Generate the scaffolding function that conforms to the given signature in "slotSignature", but that calls the implementation
			//	function that has the signature in "generatedFunctionSignature"
			//
			//	This is used to tie a node graph to a "slot signature". The slot signature is defined in the shader source code (as a normal function
			//	declaration). But the two may not match exactly (such as different parameter ordering, or some parameters missing in one or the
			//	other, etc). Here, we have to create a function that ties then together, and tries to make the best of mis-matches.
			//

		NodeGraphSignature slotSignature = inputSlotSignature;
		std::stringstream result;
		result << "{" << std::endl;

		// make temporaries for all outputs
		for (auto& p:generatedFunctionSignature.GetParameters()) {
			if (p._direction == ParameterDirection::Out) {
				result << "\t" << p._type << " temp_" << p._name << ";" << std::endl;
			}
		}

		std::string temporaryForCatchingReturn;

		std::stringstream paramStream;
		for (const auto& p:generatedFunctionSignature.GetParameters()) {
            MaybeComma(paramStream);

			if (p._direction == ParameterDirection::Out) {
				if ((flags & ScaffoldFunctionFlags::ScaffoldeeUsesReturnSlot) && p._name == s_resultName) {
					temporaryForCatchingReturn = "temp_" + p._name;
				} else {
					paramStream << "temp_" << p._name;
				}
				continue;
			}

			// How do we pass a value to this parameter?
			// first, check to see if this parameter is in "slotSignature"
			auto i = std::find_if(slotSignature.GetParameters().begin(), slotSignature.GetParameters().end(),
				[&p](const NodeGraphSignature::Parameter& test) -> bool
				{
					if (test._direction != ParameterDirection::In) return false;
					return test._name == p._name;
				});
			if (i != slotSignature.GetParameters().end()) {
				if (XlEqString(i->_type, "auto"))
					i->_type = p._type;
				WriteCastExpression(paramStream, {i->_name, i->_type}, p._type);
				continue;
			}

			// second, check for a default value specified on the parameter itself
			if (!p._default.empty()) {
				paramStream << p._default;
				continue;
			}

			// third, just pass a default value
			paramStream << "DefaultValue_" << p._type << "()";
		}

		result << "\t";
		if (!temporaryForCatchingReturn.empty())
			result << temporaryForCatchingReturn << " = ";
		result << implementationFunctionName << "(" << paramStream.str() << ");" << std::endl;

		// Map the output parameters to their final destination.
		std::stringstream returnExpression;
		for (auto&p:slotSignature.GetParameters()) {
			if (p._direction != ParameterDirection::Out)
                continue;
            
			// First, look for an output from the generated function
			auto i = std::find_if(generatedFunctionSignature.GetParameters().begin(), generatedFunctionSignature.GetParameters().end(),
				[&p](const NodeGraphSignature::Parameter& test) -> bool
				{
					if (test._direction != ParameterDirection::Out) return false;
					return test._name == p._name;
				});
			if (i != generatedFunctionSignature.GetParameters().end()) {
				if (XlEqString(p._type, "auto"))
					p._type = i->_type;

				if (p._name == s_resultName) {
					WriteCastExpression(returnExpression, {std::string("temp_") + i->_name, i->_type}, p._type);
				} else {
					result << "\t" << p._name << " = ";
					WriteCastExpression(result, {std::string("temp_") + i->_name, i->_type}, p._type);
					result << ";" << std::endl;
				}
				continue;
			}

			// Second, just use a default value
			if (p._name == s_resultName) {
				returnExpression << "DefaultValue_" << p._type << "()";
			} else {
				result << "\t" << p._name << " = ";
				result << "DefaultValue_" << p._type << "();" << std::endl;
			}
		}

		auto returnExprStr = returnExpression.str();
		if (!returnExprStr.empty()) {
			result << "\treturn " << returnExprStr << ";" << std::endl;
		}

		result << "}" << std::endl;

		{
			std::stringstream header;
			header << "/////// Scaffold function for: " << implementationFunctionName << " ///////" << std::endl;
			header << GenerateSignature(slotSignature, scaffoldFunctionName) << std::endl;

			return header.str() + result.str();
		}
	}
}

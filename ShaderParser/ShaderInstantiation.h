// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NodeGraphProvider.h"
#include "NodeGraphSignature.h"
#include "../Assets/AssetsCore.h"
#include "../RenderCore/ShaderLangUtil.h"
#include "../Utility/StringUtils.h"
#include <vector>
#include <string>
#include <set>
#include <unordered_map>
#include <memory>

namespace RenderCore { namespace Assets { class PredefinedCBLayout; class PredefinedDescriptorSetLayout; } }
namespace GraphLanguage { class INodeGraphProvider; class NodeGraph; }

namespace ShaderSourceParser
{
	/// <summary>Parameters used in a shader instantiation operation</summary>
	/// See the InstantiateShader functions for different ways to use this class.
	/// The instantiation request must be attached to some root instantiation (since this
	/// class is used to fill in the parameters for that instantiation. 
	/// The root instantiation can either be referenced by an archive name, or it can be
	/// a NodeGraph object; but the way the parameters are assigned is the same in either
	/// case
	class InstantiationRequest
	{
	public:
		std::string																_archiveName;
		std::shared_ptr<GraphLanguage::INodeGraphProvider>						_customProvider;
		std::unordered_map<std::string, std::unique_ptr<InstantiationRequest>>	_parameterBindings;
		std::vector<std::string>												_parametersToCurry;

		/// when "implements" is not provided by the instantiation itself (eg, in the case of
		/// a HLSL patch) we can specify the patch that will be implemented explicitly:
		std::string																_implementsArchiveName;	 

		uint64_t CalculateInstanceHash() const; ///< Calculate hash value for the parameter bindings (& curried parameters) in the request

		InstantiationRequest(InstantiationRequest&&) = default;
		InstantiationRequest& operator=(InstantiationRequest&&) = default;
		InstantiationRequest(const InstantiationRequest&);
		InstantiationRequest& operator=(const InstantiationRequest&);
		InstantiationRequest(const std::string& archiveName = std::string{});
		~InstantiationRequest();
	};

	class ShaderEntryPoint
	{
	public:
		std::string _name;
		GraphLanguage::NodeGraphSignature _signature;

		std::string _implementsName;
		GraphLanguage::NodeGraphSignature _implementsSignature;
	};

	class InstantiatedShader
	{
	public:
		/// These are the source fragments that make up the instantiated shader
		/// Generally from here they can be fed into the shader compiler
		std::vector<std::string> _sourceFragments;

		/// <summary>Describes an entry point function in the instantiated shader</summary>
		/// A given instantiation can have multiple entry points (for example, for binding
		/// with different techniques).
		/// These are like "exported" functions if we think of the instantiated shader
		/// as a kind of library.
		std::vector<ShaderEntryPoint> _entryPoints;

		/// Instantiated shaders can have a "uniform input" interface. This takes the
		/// form of a descriptor set, and generally will be filled in with parameters
		/// from a material file.
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _descriptorSet;

		/// Relevance table for selectors. This describes what selectors influence the
		/// shader graph instantiation, and under what circumstances.
		/// Note that this only contains relevance information for selectors used
		/// by shader graph files -- not selector used by the pure shader files that
		/// were included.
		std::unordered_map<std::string, std::string> _selectorRelevance;

		/// List of included pure shader files.
		/// Note that this doesn't include any shader graph files that were used
		/// during the instantiation.
		/// It will include "root" instantiation -- that is, shader files that were
		/// part of  the initial request
		std::set<std::string> _rawShaderFileIncludes;

		/// Preprocessor prefix useful with HAS_INSTANTIATION_XXX defines useful for the
		/// shader to know what instantiations are present 
		std::set<std::string> _instantiationPrefix;

		/// List of dependency validations, which can be used for change tracking.
		std::set<::Assets::DependencyValidation> _depVals;
		std::set<::Assets::DependentFileState> _depFileStates;
	};

	class GenerateFunctionOptions
	{
	public:
		ParameterBox _selectors;
		bool _filterWithSelectors = false;

		RenderCore::ShaderLanguage _shaderLanguage = RenderCore::ShaderLanguage::HLSL; // Note -- we pass the shader language here to control how the CB layouts are optimized
		RenderCore::Assets::PredefinedDescriptorSetLayout* _pipelineLayoutMaterialDescriptorSet = nullptr;
		unsigned _materialDescriptorSetIndex = ~0u;

		bool _generateDanglingInputs = false;
		GraphLanguage::NodeId _generateDanglingOutputs = GraphLanguage::NodeId_Interface;
	};

	InstantiatedShader InstantiateShader(
		const GraphLanguage::INodeGraphProvider::NodeGraph& initialGraph,
		bool useScaffoldFunction,
		const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions);

	InstantiatedShader InstantiateShader(
		IteratorRange<const InstantiationRequest*> request,
		const GenerateFunctionOptions& generateOptions);

        ///////////////////////////////////////////////////////////////

	class DependencyTable
    {
    public:
        struct Dependency 
		{ 
			InstantiationRequest _instantiation;
			bool _isGraphSyntaxFile;
		};
        std::vector<Dependency> _dependencies;
    };

	class GenerateFunctionResult
	{
	public:
		std::vector<std::string> _sourceFragments;
		ShaderEntryPoint _entryPoint;
		DependencyTable _dependencies;
		std::vector<GraphLanguage::NodeGraphSignature::Parameter> _captures;
		std::set<::Assets::DependencyValidation> _depVals;
		std::set<::Assets::DependentFileState> _depFileStates;
	};

    GenerateFunctionResult GenerateFunction(
        const GraphLanguage::NodeGraph& graph, StringSection<char> name, 
        const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions,
        GraphLanguage::INodeGraphProvider& sigProvider);

	namespace Internal
	{
		/// <summary>Build a selector relevance map from a node graph</summary>
		/// Intended for internal use and testing only. Normally the selector relevance
		/// can be collected as a by-product of the InstantiateShader() method
		void ExtractSelectorRelevance(
			std::unordered_map<std::string, std::string>& result,
			const GraphLanguage::NodeGraph& graph);
	}

}

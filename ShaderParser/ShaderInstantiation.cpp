// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderInstantiation.h"
#include "GraphSyntax.h"
#include "DescriptorSetInstantiation.h"
#include "NodeGraphSignature.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include <stack>
#include <sstream>
#include <regex>

namespace ShaderSourceParser
{
	static std::string MakeGraphName(const std::string& baseName, uint64_t instantiationHash = 0)
    {
        if (!instantiationHash) return baseName;
        return baseName + "_" + std::to_string(instantiationHash);
    }

	static std::pair<StringSection<>, StringSection<>> SplitArchiveName(StringSection<> input)
    {
        auto pos = std::find(input.begin(), input.end(), ':');
        if (pos != input.end())
			if ((pos+1) != input.end() && *(pos+1) == ':')
				return std::make_pair(MakeStringSection(input.begin(), pos), MakeStringSection(pos+2, input.end()));

		return std::make_pair(input, StringSection<>{});
    }

	namespace Internal
	{
		static const std::string s_alwaysRelevant { "1" };

		void ExtractSelectorRelevance(
			std::unordered_map<std::string, std::string>& result,
			const GraphLanguage::NodeGraph& graph)
		{
			std::regex regex(R"(defined\(([a-zA-Z]\w*)\))");
			for (const auto& connection:graph.GetConnections()) {
				if (connection._condition.empty())
					continue;

				// Find everything with "defined()" commands
				auto words_begin = 
					std::sregex_iterator(connection._condition.begin(), connection._condition.end(), regex);
				auto words_end = std::sregex_iterator();

				for (auto i = words_begin; i != words_end; ++i) {
					// We don't have to worry about combining this with other relevance conditions, because
					// we can just set it to be always relevant
					result[(*i)[1].str()] = s_alwaysRelevant;
				}
			}
		}

		struct PendingInstantiation
		{
			GraphLanguage::INodeGraphProvider::NodeGraph _graph;
			bool _useScaffoldFunction = false;
			bool _isRootInstantiation = true;
			InstantiationRequest _instantiationParams;
		};

		class PendingInstantiationsHelper
		{
		public:
			std::stack<PendingInstantiation> _instantiations;
			std::set<std::pair<std::string, uint64_t>> _previousInstantiation;
			std::set<std::string> _rawShaderFileIncludes;
			std::vector<ShaderEntryPoint> _entryPointsFromRawShaders;
			
			std::set<::Assets::DependencyValidation> _depVals;		// dependencies created in the QueueUp() method
			std::set<::Assets::DependentFileState> _fileStates;

			void QueueUp(
				IteratorRange<const DependencyTable::Dependency*> dependencies,
				GraphLanguage::INodeGraphProvider& provider,
				bool isRootInstantiation = false)
			{
				if (dependencies.empty())
					return;

				// Add to the stack in reverse order, so that the first item in rootInstantiations appears highest in
				// the output file
				for (auto i=dependencies.end()-1; i>=dependencies.begin(); --i) {
					// if it's a graph file, then we must create a specific instantiation
					auto& dep = *i;
					auto instHash = dep._instantiation.CalculateInstanceHash();
					if (dep._isGraphSyntaxFile) {
						// todo -- not taking into account the custom provider on the following line (ie, incase the new load is using a different provider to the new load)
						if (_previousInstantiation.find({dep._instantiation._archiveName, instHash}) == _previousInstantiation.end()) {

							std::optional<GraphLanguage::INodeGraphProvider::NodeGraph> nodeGraph;
							if (dep._instantiation._customProvider) {
								nodeGraph = dep._instantiation._customProvider->FindGraph(dep._instantiation._archiveName);
							} else {
								nodeGraph = provider.FindGraph(dep._instantiation._archiveName);
							}

							if (!nodeGraph)
								Throw(::Exceptions::BasicLabel("Failed loading graph with archive name (%s)", dep._instantiation._archiveName.c_str()));

							_instantiations.emplace(
								PendingInstantiation{nodeGraph.value(), true, isRootInstantiation, dep._instantiation});
							_previousInstantiation.insert({dep._instantiation._archiveName, instHash});

						}
					} else {
						// This is just an include of a normal shader header
						if (instHash!=0) {
							auto filename = SplitArchiveName(dep._instantiation._archiveName).first;
							_rawShaderFileIncludes.insert(std::string(StringMeld<MaxPath>() << filename.AsString() + "_" << instHash));
						} else {
							GraphLanguage::INodeGraphProvider::Signature sig;
							if (dep._instantiation._customProvider) {
								sig = dep._instantiation._customProvider->FindSignature(dep._instantiation._archiveName).value();
								_rawShaderFileIncludes.insert(sig._sourceFile);
							} else {
								sig = provider.FindSignature(dep._instantiation._archiveName).value();
								_rawShaderFileIncludes.insert(sig._sourceFile);
							}

							if (isRootInstantiation) {
								// If this is a root instantiation, we can include this function as an entry point
								ShaderEntryPoint entryPoint;
								entryPoint._name = entryPoint._implementsName = sig._name;
								entryPoint._signature = entryPoint._implementsSignature = sig._signature;
								_entryPointsFromRawShaders.emplace_back(std::move(entryPoint));
							}
						}
					}
				}
			}
		};
	}

	static InstantiatedShader InstantiateShader(
		Internal::PendingInstantiationsHelper& pendingInst,
		const GenerateFunctionOptions& generateOptions)
	{
		std::vector<GraphLanguage::NodeGraphSignature::Parameter> mergedCaptures;
        InstantiatedShader result;
		
        while (!pendingInst._instantiations.empty()) {
            auto inst = std::move(pendingInst._instantiations.top());
            pendingInst._instantiations.pop();

			result._depVals.insert(inst._graph._depVal);
			result._depFileStates.insert(inst._graph._fileState);

			// Slightly different rules for function name generation with inst._scope is not null. inst._scope is
			// only null for the original instantiation request -- in that case, we want the outer most function
			// to have the same name as the original request
			auto scaffoldName = MakeGraphName(inst._graph._name, inst._instantiationParams.CalculateInstanceHash());
			auto implementationName = inst._useScaffoldFunction ? (scaffoldName + "_impl") : scaffoldName;
			auto instFn = GenerateFunction(
				inst._graph._graph, implementationName, 
				inst._instantiationParams, generateOptions, *inst._graph._subProvider);

			if (inst._useScaffoldFunction) {
				auto scaffoldSignature = inst._graph._signature;
				for (const auto&tp:inst._instantiationParams._parameterBindings) {
					for (const auto&c:tp.second->_parametersToCurry) {
						auto name = "curried_" + tp.first + "_" + c;
						auto instP = std::find_if(
							instFn._entryPoint._signature.GetParameters().begin(), instFn._entryPoint._signature.GetParameters().end(),
							[name](const GraphLanguage::NodeGraphSignature::Parameter& p) { return XlEqString(MakeStringSection(name), p._name); });
						if (instP != instFn._entryPoint._signature.GetParameters().end())
							scaffoldSignature.AddParameter(*instP);
					}
				}

				result._sourceFragments.push_back(ShaderSourceParser::GenerateScaffoldFunction(scaffoldSignature, instFn._entryPoint._signature, scaffoldName, implementationName));

				if (inst._isRootInstantiation) {
					ShaderEntryPoint entryPoint { scaffoldName, scaffoldSignature };
					if (!scaffoldSignature.GetImplements().empty()) {
						auto implementsSig = inst._graph._subProvider->FindSignature(scaffoldSignature.GetImplements());
						if (implementsSig) {
							entryPoint._implementsName = implementsSig.value()._name;
							entryPoint._implementsSignature = implementsSig.value()._signature;
							assert(implementsSig.value()._depVal);
							result._depVals.insert(implementsSig.value()._depVal);
							result._depFileStates.insert(implementsSig.value()._fileState);
						}
					}
					result._entryPoints.emplace_back(std::move(entryPoint));
				}
			}
			else
			{
				if (inst._isRootInstantiation)
					result._entryPoints.push_back(instFn._entryPoint);
			}

			result._sourceFragments.insert(
				result._sourceFragments.end(),
				instFn._sourceFragments.begin(), instFn._sourceFragments.end());

			// We need to collate a little more information from the generated function
			//  - dep vals
			//  - captured parameters
			//  - selector relevance table

			result._depVals.insert(instFn._depVals.begin(), instFn._depVals.end());
			result._depFileStates.insert(instFn._depFileStates.begin(), instFn._depFileStates.end());

			{
				for (const auto&c:inst._graph._signature.GetCapturedParameters()) {
					auto existing = std::find_if(
						mergedCaptures.begin(), mergedCaptures.end(),
						[c](const GraphLanguage::NodeGraphSignature::Parameter& p) { return XlEqString(MakeStringSection(p._name), c._name); });
					if (existing != mergedCaptures.end()) {
						if (existing->_type != c._type || existing->_direction != c._direction)
							Throw(::Exceptions::BasicLabel("Type mismatch detected for capture (%s). Multiple fragments have this capture, but they are not compatible types.", existing->_name.c_str()));
						continue;
					}
					mergedCaptures.push_back(c);
				}
			}

			Internal::ExtractSelectorRelevance(
				result._selectorRelevance,
				inst._graph._graph);
                
			// Queue up all of the dependencies that we got out of the GenerateFunction() call
			pendingInst.QueueUp(MakeIteratorRange(instFn._dependencies._dependencies), *inst._graph._subProvider);
        }

		// Write the merged captures as a cbuffers in the material descriptor set
		if (!mergedCaptures.empty()) {
			std::stringstream warningMessages;
			result._descriptorSet = MakeMaterialDescriptorSet(
				MakeIteratorRange(mergedCaptures),
				generateOptions._shaderLanguage,
				warningMessages);

			// Link to a fixed pipeline layout descriptor set, if that's provided
			if (generateOptions._pipelineLayoutMaterialDescriptorSet)
				result._descriptorSet = LinkToFixedLayout(
					*result._descriptorSet, 
					*generateOptions._pipelineLayoutMaterialDescriptorSet);

			auto fragment = GenerateDescriptorVariables(*result._descriptorSet, generateOptions._materialDescriptorSetIndex, MakeIteratorRange(mergedCaptures));
			if (!fragment.empty())
				result._sourceFragments.push_back(fragment);

			fragment = warningMessages.str();
			if (!fragment.empty())
				result._sourceFragments.push_back(fragment);
		}

		// Reverse the source fragments, because we wrote everything in reverse dependency order
		std::reverse(result._sourceFragments.begin(), result._sourceFragments.end());

		// Build a fragment containing all of the #include statements needed
		{
			std::stringstream str;
			str << "#include \"xleres/TechniqueLibrary/System/Prefix.hlsl\"" << std::endl;
			for (const auto&i:pendingInst._rawShaderFileIncludes)
				str << "#include \"" << i << "\"" << std::endl;
			result._sourceFragments.insert(result._sourceFragments.begin(), str.str());
		}

		// append any entry points that came from raw shader includes
		result._entryPoints.insert(
			result._entryPoints.end(),
			pendingInst._entryPointsFromRawShaders.begin(),
			pendingInst._entryPointsFromRawShaders.end());

		result._rawShaderFileIncludes = std::move(pendingInst._rawShaderFileIncludes);
		result._depVals.insert(pendingInst._depVals.begin(), pendingInst._depVals.end());
		result._depFileStates.insert(pendingInst._fileStates.begin(), pendingInst._fileStates.end());

		return result;
	}

	InstantiatedShader InstantiateShader(
        const GraphLanguage::INodeGraphProvider::NodeGraph& initialGraph,
		bool useScaffoldFunction,
		const InstantiationRequest& instantiationParameters,
		const GenerateFunctionOptions& generateOptions)
	{
		// Note that we end up with a few extra copies of initialGraph, because PendingInstantiation
		// contains a complete copy of the node graph
		Internal::PendingInstantiationsHelper pendingInst;
		pendingInst._instantiations.push(Internal::PendingInstantiation { initialGraph, useScaffoldFunction, true, instantiationParameters });
		return InstantiateShader(pendingInst, generateOptions);
	}

	InstantiatedShader InstantiateShader(
		IteratorRange<const InstantiationRequest*> request,
		const GenerateFunctionOptions& generateOptions)
	{
		GraphLanguage::BasicNodeGraphProvider defaultProvider(::Assets::DirectorySearchRules{});

		assert(!request.empty());
		std::vector<DependencyTable::Dependency> pendingInst;
		pendingInst.reserve(request.size());
		for (const auto&r:request) {

			// We can either be instantiating from a full graph file, or from a specific graph within that file
			// When the request name has an archive name divider (ie, "::"), we will pull out only a single
			// graph from the file.
			// Otherwise we will load every graph from within the file

			auto split = SplitArchiveName(r._archiveName);
			if (split.second.IsEmpty()) {
				// this is a full filename, we should load all of the node graphs within the given
				// file
				std::vector<GraphLanguage::INodeGraphProvider::Signature> signatures;
				if (r._customProvider) {
					signatures = r._customProvider->FindSignatures(r._archiveName);
				} else {
					signatures = defaultProvider.FindSignatures(r._archiveName);
				}

				if (signatures.empty())
					Throw(::Exceptions::BasicLabel("Did not find any node graph signatures for instantiation request (%s)", r._archiveName.c_str()));

				for (const auto&s:signatures) {
					DependencyTable::Dependency dep;
					dep._instantiation = r;
					dep._instantiation._archiveName =  r._archiveName + "::" + s._name;
					dep._isGraphSyntaxFile = s._isGraphSyntax;
					pendingInst.emplace_back(dep);
				}

			} else {
				// this refers to a specific item in graph within an outer graph file
				// Just check to make sure it's a graph file
				auto sig = (r._customProvider ? r._customProvider.get() : &defaultProvider)->FindSignature(r._archiveName);
				if (!sig.has_value())
					Throw(::Exceptions::BasicLabel("Failed while reading signatures for instantiation request (%s). This might have been caused by a shader language parsing failure", r._archiveName.c_str()));

				DependencyTable::Dependency dep;
				dep._instantiation = r;
				dep._isGraphSyntaxFile = sig.value()._isGraphSyntax;
				pendingInst.emplace_back(dep);
			}
			
		}

		Internal::PendingInstantiationsHelper pendingInstHelper;
		pendingInstHelper.QueueUp(MakeIteratorRange(pendingInst), defaultProvider, true);
		return InstantiateShader(pendingInstHelper, generateOptions);
	}

	static uint64_t CalculateDepHash(const InstantiationRequest& dep, uint64_t seed = DefaultSeed64)
	{
		uint64_t result = Hash64(dep._archiveName);
		// todo -- ordering of parameters matters to the hash here
		for (const auto& d:dep._parameterBindings)
			result = Hash64(d.first, CalculateDepHash(*d.second, result));
		return result;
	}

    uint64_t InstantiationRequest::CalculateInstanceHash() const
    {
        if (_parameterBindings.empty()) return 0;
        uint64 result = DefaultSeed64;
		// todo -- ordering of parameters matters to the hash here
        for (const auto&p:_parameterBindings) {
            result = Hash64(p.first, CalculateDepHash(*p.second, result));
			for (const auto&pc:p.second->_parametersToCurry)
				result = Hash64(pc, result);
		}
        return result;
    }

	InstantiationRequest::InstantiationRequest(const InstantiationRequest& copyFrom)
	: _archiveName(copyFrom._archiveName)
	, _customProvider(copyFrom._customProvider)
	, _parametersToCurry(copyFrom._parametersToCurry)
	{
		for (const auto&src:copyFrom._parameterBindings)
			_parameterBindings.insert(std::make_pair(src.first, std::make_unique<InstantiationRequest>(*src.second)));
	}

	InstantiationRequest& InstantiationRequest::operator=(const InstantiationRequest& copyFrom)
	{
		_archiveName = copyFrom._archiveName;
		_customProvider = copyFrom._customProvider;
		_parametersToCurry = copyFrom._parametersToCurry;
		_parameterBindings.clear();
		for (const auto&src:copyFrom._parameterBindings)
			_parameterBindings.insert(std::make_pair(src.first, std::make_unique<InstantiationRequest>(*src.second)));
		return *this;
	}

	InstantiationRequest::InstantiationRequest(const std::string& archiveName) : _archiveName(archiveName) {}
	InstantiationRequest::~InstantiationRequest() {}
}

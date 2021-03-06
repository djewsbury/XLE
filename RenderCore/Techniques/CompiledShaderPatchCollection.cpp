// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompiledShaderPatchCollection.h"
#include "TechniqueUtils.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Assets/IntermediatesStore.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ShaderParser/ShaderPatcher.h"
#include "../../ShaderParser/NodeGraphProvider.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
	CompiledShaderPatchCollection::CompiledShaderPatchCollection(const RenderCore::Assets::ShaderPatchCollection& src)
	: _src(src)
	{
		_depVal = std::make_shared<::Assets::DependencyValidation>();
		_guid = src.GetHash();

		// With the given shader patch collection, build the source code and the 
		// patching functions associated
		
		if (!src.GetPatches().empty()) {
			std::vector<ShaderSourceParser::InstantiationRequest> finalInstRequests;
			finalInstRequests.reserve(src.GetPatches().size());
			for (const auto&i:src.GetPatches()) finalInstRequests.push_back(i.second);

			ShaderSourceParser::GenerateFunctionOptions generateOptions;
			auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(finalInstRequests), generateOptions, GetDefaultShaderLanguage());
			BuildFromInstantiatedShader(inst);
		}

		if (!_interface._descriptorSet && !src.GetDescriptorSet().empty()) {
			auto descriptorSetFuture = ::Assets::MakeAsset<RenderCore::Assets::PredefinedDescriptorSetLayout>(src.GetDescriptorSet());
			descriptorSetFuture->StallWhilePending();
			_interface._descriptorSet = descriptorSetFuture->Actualize();

			::Assets::RegisterAssetDependency(_depVal, _interface._descriptorSet->GetDependencyValidation());
		}
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const ShaderSourceParser::InstantiatedShader& inst)
	{
		_depVal = std::make_shared<::Assets::DependencyValidation>();
		_guid = 0;
		BuildFromInstantiatedShader(inst);
	}

	void CompiledShaderPatchCollection::BuildFromInstantiatedShader(const ShaderSourceParser::InstantiatedShader& inst)
	{
			// Note -- we can build the patches interface here, because we assume that this will not
			//		even change with selectors

		_interface._patches.reserve(inst._entryPoints.size());
		for (const auto&patch:inst._entryPoints) {

			Interface::Patch p;
			if (!patch._implementsName.empty()) {
				p._implementsHash = Hash64(patch._implementsName);

				if (patch._implementsName != patch._name) {
					p._scaffoldInFunction = ShaderSourceParser::GenerateScaffoldFunction(
						patch._implementsSignature, patch._signature,
						patch._implementsName, patch._name, 
						ShaderSourceParser::ScaffoldFunctionFlags::ScaffoldeeUsesReturnSlot);
				}
			}

			p._signature = std::make_shared<GraphLanguage::NodeGraphSignature>(patch._signature);

			_interface._patches.emplace_back(std::move(p));
		}

		_interface._descriptorSet = inst._descriptorSet;

		for (const auto&d:inst._depVals)
			if (d)
				::Assets::RegisterAssetDependency(_depVal, d);
		for (const auto&d:inst._depFileStates) {
			assert(!d._filename.empty());
			if (std::find(_dependencies.begin(), _dependencies.end(), d) == _dependencies.end())
				_dependencies.push_back(d);
		}

		_interface._filteringRules = inst._selectorRelevance;
		for (const auto& rawShader:inst._rawShaderFileIncludes) {
			auto filteringRules = ::Assets::MakeAsset<ShaderSourceParser::SelectorFilteringRules>(rawShader);
			filteringRules->StallWhilePending();
			_interface._filteringRules.MergeIn(*filteringRules->Actualize());
		}
		::Assets::RegisterAssetDependency(_depVal, _interface._filteringRules.GetDependencyValidation());

		size_t size = 0;
		for (const auto&i:inst._sourceFragments)
			size += i.size();
		_savedInstantiation.reserve(size);
		for (const auto&i:inst._sourceFragments)
			_savedInstantiation.insert(_savedInstantiation.end(), i.begin(), i.end());
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection() 
	{
		_depVal = std::make_shared<::Assets::DependencyValidation>();
	}

	CompiledShaderPatchCollection::~CompiledShaderPatchCollection() {}

	static std::string Merge(const std::vector<std::string>& v)
	{
		size_t size=0;
		for (const auto&q:v) size += q.size();
		std::string result;
		result.reserve(size);
		for (const auto&q:v) result.insert(result.end(), q.begin(), q.end());
		return result;
	}

	std::string CompiledShaderPatchCollection::InstantiateShader(const ParameterBox& selectors) const
	{
		if (_src.GetPatches().empty()) {
			// If we'ved used  the constructor that takes a ShaderSourceParser::InstantiatedShader,
			// we can't re-instantiate here. So our only choice is to just return the saved
			// instantiation here. However, this means the selectors won't take effect, somewhat awkwardly
			return _savedInstantiation;
		}

		std::vector<ShaderSourceParser::InstantiationRequest> finalInstRequests;
		finalInstRequests.reserve(_src.GetPatches().size());
		for (const auto&i:_src.GetPatches()) finalInstRequests.push_back(i.second);

		ShaderSourceParser::GenerateFunctionOptions generateOptions;
		if (selectors.GetCount() != 0) {
			generateOptions._filterWithSelectors = true;
			generateOptions._selectors = selectors;
		}

		auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(finalInstRequests), generateOptions, GetDefaultShaderLanguage());
		return Merge(inst._sourceFragments);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const uint64_t CompileProcess_InstantiateShaderGraph = ConstHash64<'Inst', 'shdr'>::Value;
	const uint64 CompiledShaderByteCode_InstantiateShaderGraph::CompileProcessType = CompileProcess_InstantiateShaderGraph;

	auto AssembleShader(
		const CompiledShaderPatchCollection& patchCollection,
		IteratorRange<const uint64_t*> redirectedPatchFunctions,
		StringSection<> definesTable) -> ISourceCodePreprocessor::SourceCodeWithRemapping

	{
		// We can assemble the final shader in 3 fragments:
		//  1) the source code in CompiledShaderPatchCollection
		//  2) redirection functions (which redirect from the template function names to the concrete instantiations we want to tie in)
		//  3) include the entry point function itself

		std::stringstream output;

		// Extremely awkwardly; we must go from the "definesTable" format back into a ParameterBox
		// The defines table itself was probably built from a ParameterBox. But we can't pass complex
		// types through the asset compiler interface, so we always end up having to pass them in some
		// kind of string form
		ParameterBox paramBoxSelectors;
		auto p = definesTable.begin();
        while (p != definesTable.end()) {
            while (p != definesTable.end() && std::isspace(*p)) ++p;

            auto definition = std::find(p, definesTable.end(), '=');
            auto defineEnd = std::find(p, definesTable.end(), ';');

            auto endOfName = std::min(defineEnd, definition);
            while ((endOfName-1) > p && std::isspace(*(endOfName-1))) ++endOfName;

            if (definition < defineEnd) {
                auto e = definition+1;
                while (e < defineEnd && std::isspace(*e)) ++e;
				paramBoxSelectors.SetParameter(MakeStringSection(p, endOfName).Cast<utf8>(), MakeStringSection(e, defineEnd));
            } else {
				paramBoxSelectors.SetParameter(MakeStringSection(p, endOfName).Cast<utf8>(), {}, ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Void});
            }

            p = (defineEnd == definesTable.end()) ? defineEnd : (defineEnd+1);
        }
		output << patchCollection.InstantiateShader(paramBoxSelectors);

		for (auto fn:redirectedPatchFunctions) {
			auto i = std::find_if(
				patchCollection.GetInterface().GetPatches().begin(), patchCollection.GetInterface().GetPatches().end(),
				[fn](const CompiledShaderPatchCollection::Interface::Patch& p) { return p._implementsHash == fn; });
			assert(i!=patchCollection.GetInterface().GetPatches().end());
			if (i == patchCollection.GetInterface().GetPatches().end()) {
				Log(Warning) << "Could not find matching patch function for hash (" << fn << ")" << std::endl;
				continue;
			}

			// GenerateScaffoldFunction just creates a function with the name of the template
			// that calls the specific implementation requested.
			// This is important, because the entry point shader code will call the function
			// using that template function name. The raw input source code won't have any implementation
			// for that -- just the function signature.
			// So we provide the implementation here, in the form of a scaffold function
			if (!i->_scaffoldInFunction.empty())
				output << i->_scaffoldInFunction;
		}

		ISourceCodePreprocessor::SourceCodeWithRemapping result;
		result._processedSource = output.str();
		for (const auto& dep:patchCollection._dependencies) { assert(!dep._filename.empty()); }
		result._dependencies.insert(
			result._dependencies.end(),
			patchCollection._dependencies.begin(), patchCollection._dependencies.end());

		// We could fill in the _lineMarkers member with some line marker information
		// from the original shader graph compile; but that might be overkill
		return result;
	}

	static auto AssembleDirectFromFile(StringSection<> filename) -> ISourceCodePreprocessor::SourceCodeWithRemapping
	{
		assert(!XlEqString(filename, "-0"));
		assert(!filename.IsEmpty());

		// Fall back to loading the file directly (without any real preprocessing)
		ISourceCodePreprocessor::SourceCodeWithRemapping result;
		result._dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(filename));

		size_t sizeResult = 0;
		auto blob = ::Assets::TryLoadFileAsMemoryBlock_TolerateSharingErrors(filename, &sizeResult);
		result._processedSource = std::string((char*)blob.get(), (char*)PtrAdd(blob.get(), sizeResult));
		result._lineMarkers.push_back(ILowLevelCompiler::SourceLineMarker{filename.AsString(), 0, 0});
		return result;
	}

	static auto InstantiateShaderGraph_CompileFromFile(
		ShaderService::IShaderSource& internalShaderSource,
		const ILowLevelCompiler::ResId& resId, 
		StringSection<> definesTable,
		const CompiledShaderPatchCollection& patchCollection,
		IteratorRange<const uint64_t*> redirectedPatchFunctions) -> ShaderService::IShaderSource::ShaderByteCodeBlob
	{
		if (patchCollection.GetInterface().GetPatches().empty())
			return internalShaderSource.CompileFromFile(resId, definesTable);

		auto assembledShader = AssembleShader(patchCollection, redirectedPatchFunctions, definesTable);

		// For simplicity, we'll just append the entry point file using an #include directive
		// This will ensure we go through the normal mechanisms to find and load this file.
		// Note that this relies on the underlying shader compiler supporting #includes, however
		//   -- in cases  (like GLSL) that don't have #include support, we would need another
		//	changed preprocessor to handle the include expansions.
		{
			std::stringstream str ;
			str << "#include \"" << resId._filename << "\"" << std::endl;
			assembledShader._processedSource += str.str();
		}

		auto result = internalShaderSource.CompileFromMemory(
			MakeStringSection(assembledShader._processedSource),
			resId._entryPoint, resId._shaderModel,
			definesTable);

		result._deps.insert(result._deps.end(), assembledShader._dependencies.begin(), assembledShader._dependencies.end());
		return result;
	}

	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;
	class ShaderGraphCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		virtual std::vector<TargetDesc> GetTargets() const override
		{
			return {
				TargetDesc { CompileProcess_InstantiateShaderGraph, "main" }
			};
		}
		
		virtual std::vector<SerializedArtifact> SerializeTarget(unsigned idx) override
		{
			std::vector<SerializedArtifact> result;
			if (_byteCode._payload)
				result.push_back({
					CompileProcess_InstantiateShaderGraph, 0, "main",
					_byteCode._payload});
			if (_byteCode._errors)
				result.push_back({
					ChunkType_Log, 0, "log",
					_byteCode._errors});
			return result;
		}

		virtual std::vector<::Assets::DependentFileState> GetDependencies() const override
		{
			return _byteCode._deps;
		}

		ShaderGraphCompileOperation(
			ShaderService::IShaderSource& shaderSource,
			const ILowLevelCompiler::ResId& resId,
			StringSection<> definesTable,
			const CompiledShaderPatchCollection& patchCollection,
			IteratorRange<const uint64_t*> redirectedPatchFunctions)
		: _byteCode { 
			InstantiateShaderGraph_CompileFromFile(shaderSource, resId, definesTable, patchCollection, redirectedPatchFunctions) 
		}
		{
		}
		
		~ShaderGraphCompileOperation()
		{
		}

		ShaderService::IShaderSource::ShaderByteCodeBlob _byteCode;
	};

	::Assets::IntermediateCompilers::CompilerRegistration RegisterInstantiateShaderGraphCompiler(
		const std::shared_ptr<ShaderService::IShaderSource>& shaderSource,
		::Assets::IntermediateCompilers& intermediateCompilers)
	{
		auto result = intermediateCompilers.RegisterCompiler(
			"shader-graph-compiler",
			ConsoleRig::GetLibVersionDesc(),
			nullptr,
			[shaderSource](const ::Assets::InitializerPack& initializers) {
				return std::make_shared<ShaderGraphCompileOperation>(
					*shaderSource,
					ShaderService::MakeResId(initializers.GetInitializer<std::string>(0)),
					initializers.GetInitializer<std::string>(1),
					*initializers.GetInitializer<std::shared_ptr<CompiledShaderPatchCollection>>(2),
					MakeIteratorRange(initializers.GetInitializer<std::vector<uint64_t>>(3))
				);
			});

		uint64_t outputAssetTypes[] = { CompileProcess_InstantiateShaderGraph };
		intermediateCompilers.AssociateRequest(
			result._registrationId,
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

}}

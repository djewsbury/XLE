// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompiledShaderPatchCollection.h"
#include "TechniqueUtils.h"
#include "PipelineLayoutDelegate.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/IntermediateCompilers.h"
#include "../MinimalShaderSource.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../ShaderParser/ShaderPatcher.h"
#include "../../ShaderParser/NodeGraphProvider.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../ShaderParser/DescriptorSetInstantiation.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/ICompileOperation.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/CompletionThreadPool.h"

namespace RenderCore { namespace Techniques
{
	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const RenderCore::Assets::ShaderPatchCollection& src,
		const RenderCore::Assets::PredefinedDescriptorSetLayout* customDescSet,
		const DescriptorSetLayoutAndBinding& materialDescSetLayout)
	: _src(src)
	, _matDescSetLayout(materialDescSetLayout.GetLayout())
	, _matDescSetSlot(materialDescSetLayout.GetSlotIndex())
	{
		_guid = src.GetHash();
		if (customDescSet)
			_guid = customDescSet->CalculateHash(_guid);
		_depVal = ::Assets::GetDepValSys().Make();		// _depVal must be unique, because we call RegistryDependency on it in BuildFromInstantiatedShader
		if (materialDescSetLayout.GetDependencyValidation())
			_depVal.RegisterDependency(materialDescSetLayout.GetDependencyValidation());
		if (customDescSet && customDescSet->GetDependencyValidation())
			_depVal.RegisterDependency(customDescSet->GetDependencyValidation());

		_interface._descriptorSet = materialDescSetLayout.GetLayout();
		_interface._materialDescriptorSetSlotIndex = materialDescSetLayout.GetSlotIndex();
		_interface._preconfiguration = src.GetPreconfigurationFileName().AsString();
		for (unsigned c=0; c<dimof(_interface._overrideShaders); ++c)
			_interface._overrideShaders[c] = src.GetOverrideShader(ShaderStage(c));

		if (customDescSet)
			_interface._descriptorSet = ShaderSourceParser::LinkToFixedLayout(*customDescSet, *_interface._descriptorSet);	

		// With the given shader patch collection, build the source code and the 
		// patching functions associated
		// TRY {
			if (!src.GetPatches().empty()) {
				for (const auto&i:src.GetPatches()) {
					ShaderSourceParser::InstantiationRequest finalInstRequest[] = { i.second };
					ShaderSourceParser::GenerateFunctionOptions generateOptions;
					generateOptions._shaderLanguage = GetDefaultShaderLanguage();
					generateOptions._pipelineLayoutMaterialDescriptorSet = materialDescSetLayout.GetLayout().get();
					generateOptions._materialDescriptorSetIndex = materialDescSetLayout.GetSlotIndex();
					auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(finalInstRequest), generateOptions);
					BuildFromInstantiatedShader(inst);
				}
			}
		// } CATCH(const ::Assets::Exceptions::ConstructionError& e) {
		// 	Throw(::Assets::Exceptions::ConstructionError(e, patchCollectionDepVal));
		// } CATCH(const std::exception& e) {
		// 	Throw(::Assets::Exceptions::ConstructionError(e, patchCollectionDepVal));
		// } CATCH_END
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const ShaderSourceParser::InstantiatedShader& inst,
		const DescriptorSetLayoutAndBinding& materialDescSetLayout)
	: _matDescSetLayout(materialDescSetLayout.GetLayout())
	, _matDescSetSlot(materialDescSetLayout.GetSlotIndex())
	{
		_depVal = ::Assets::GetDepValSys().Make();
		if (materialDescSetLayout.GetDependencyValidation())
			_depVal.RegisterDependency(materialDescSetLayout.GetDependencyValidation());
		_guid = 0;
		BuildFromInstantiatedShader(inst);
		_interface._descriptorSet = materialDescSetLayout.GetLayout();
		_interface._materialDescriptorSetSlotIndex = materialDescSetLayout.GetSlotIndex();
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const DescriptorSetLayoutAndBinding& materialDescSetLayout)
	{
		_depVal = materialDescSetLayout.GetDependencyValidation();
		_guid = 0;
		_interface._descriptorSet = materialDescSetLayout.GetLayout();
		_interface._materialDescriptorSetSlotIndex = materialDescSetLayout.GetSlotIndex();
	}

	static std::string Merge(const std::set<std::string>& preprocessorPrefix)
	{
		size_t size=0;
		for (const auto&q:preprocessorPrefix) size += q.size() + 1;
		std::string result;
		result.reserve(size);
		for (const auto&q:preprocessorPrefix) {
			result.insert(result.end(), q.begin(), q.end());
			result.push_back('\n');
		}
		return result;
	}

	static std::string Merge(const std::vector<std::string>& v)
	{
		size_t size=0;
		for (const auto&q:v) size += q.size() + 1;
		std::string result;
		result.reserve(size);
		for (const auto&q:v) {
			result.insert(result.end(), q.begin(), q.end());
			result.push_back('\n');
		}
		return result;
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

			p._originalEntryPointSignature = std::make_shared<GraphLanguage::NodeGraphSignature>(patch._signature);
			p._originalEntryPointName = patch._name;

			p._scaffoldSignature = std::make_shared<GraphLanguage::NodeGraphSignature>(patch._implementsSignature);
			p._scaffoldEntryPointName = patch._implementsName;

			p._filteringRulesId = (unsigned)_interface._filteringRules.size();

			_interface._patches.emplace_back(std::move(p));
		}

		if (inst._descriptorSet)
			_interface._descriptorSet = inst._descriptorSet;

		for (const auto&d:inst._depVals) {
			assert(d);
			_depVal.RegisterDependency(d);
		}
		for (const auto&d:inst._depFileStates) {
			assert(!d._filename.empty());
			if (std::find(_dependencies.begin(), _dependencies.end(), d) == _dependencies.end())
				_dependencies.push_back(d);
		}

		ShaderSourceParser::SelectorFilteringRules filteringRules = inst._selectorRelevance;
		std::vector<::Assets::PtrToMarkerPtr<ShaderSourceParser::SelectorFilteringRules>> rawIncludeFiltering;
		rawIncludeFiltering.reserve(inst._rawShaderFileIncludes.size());
		for (const auto& rawShader:inst._rawShaderFileIncludes) {
			assert(!rawShader.empty());
			rawIncludeFiltering.emplace_back(::Assets::GetAssetMarkerPtr<ShaderSourceParser::SelectorFilteringRules>(rawShader));
		}

		for (const auto& rawShader:rawIncludeFiltering) {
			rawShader->StallWhilePending();
			filteringRules.MergeIn(*rawShader->Actualize());
		}

		_interface._filteringRules.push_back(filteringRules);
		if (filteringRules.GetDependencyValidation())
			_depVal.RegisterDependency(filteringRules.GetDependencyValidation());
		_savedInstantiation = Merge(inst._sourceFragments);
		_savedInstantiationPrefix = Merge(inst._instantiationPrefix);
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection() 
	{
	}

	CompiledShaderPatchCollection::~CompiledShaderPatchCollection() {}

	const ShaderSourceParser::SelectorFilteringRules& CompiledShaderPatchCollection::Interface::GetSelectorFilteringRules(unsigned filteringRulesId) const
	{
		assert(filteringRulesId < _filteringRules.size());
		return _filteringRules[filteringRulesId];
	}

	std::pair<std::string, std::string> CompiledShaderPatchCollection::InstantiateShader(
		const ParameterBox& selectors,
		IteratorRange<const uint64_t*> patchExpansions) const
	{
		if (_src.GetPatches().empty()) {
			// If we've used  the constructor that takes a ShaderSourceParser::InstantiatedShader,
			// we can't re-instantiate here. So our only choice is to just return the saved
			// instantiation here. However, this means the selectors won't take effect, somewhat awkwardly
			return {_savedInstantiationPrefix, _savedInstantiation};
		}

		// find the particular patches that were requested and instantiate them
		std::vector<ShaderSourceParser::InstantiationRequest> finalInstRequests;
		std::vector<unsigned> srcPatchesToInclude;
		srcPatchesToInclude.reserve(patchExpansions.size());
		for (auto expansion:patchExpansions) {
			auto i = std::find_if(_interface._patches.begin(), _interface._patches.end(), [expansion](const auto& c) { return c._implementsHash == expansion; });
			// assert(i != _interface._patches.end());
			if (i == _interface._patches.end()) continue;

			auto srcPatchIdx = i->_filteringRulesId;		// this is actually the idx from the source patches array
			if (std::find(srcPatchesToInclude.begin(), srcPatchesToInclude.end(), srcPatchIdx) == srcPatchesToInclude.end())
				srcPatchesToInclude.push_back(srcPatchIdx);
		}
		finalInstRequests.reserve(srcPatchesToInclude.size());
		for (auto i:srcPatchesToInclude) {
			assert(i < _src.GetPatches().size());
			finalInstRequests.push_back(_src.GetPatches()[i].second);
		}

		ShaderSourceParser::GenerateFunctionOptions generateOptions;
		if (selectors.GetCount() != 0) {
			generateOptions._filterWithSelectors = true;
			generateOptions._selectors = selectors;
		}

		generateOptions._shaderLanguage = GetDefaultShaderLanguage();
		generateOptions._pipelineLayoutMaterialDescriptorSet = _matDescSetLayout.get();
		generateOptions._materialDescriptorSetIndex = _matDescSetSlot;
		auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(finalInstRequests), generateOptions);

		// Also add in the generated scaffold functions for each of the expanded patches
		{
			std::stringstream scaffoldFns;
			for (auto expansion:patchExpansions) {
				auto i = std::find_if(_interface._patches.begin(), _interface._patches.end(), [expansion](const auto& c) { return c._implementsHash == expansion; });
				// assert(i!=_interface._patches.end());
				if (i == _interface._patches.end()) continue;

				// GenerateScaffoldFunction just creates a function with the name of the template
				// that calls the specific implementation requested.
				// This is important, because the entry point shader code will call the function
				// using that template function name. The raw input source code won't have any implementation
				// for that -- just the function signature.
				// So we provide the implementation here, in the form of a scaffold function
				if (!i->_scaffoldInFunction.empty())
					scaffoldFns << i->_scaffoldInFunction;
			}
			inst._sourceFragments.emplace_back(scaffoldFns.str());
		}

		return {Merge(inst._instantiationPrefix), Merge(inst._sourceFragments)};
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static auto AssembleShader(
		const CompiledShaderPatchCollection& patchCollection,
		StringSection<> mainSourceFile,
		IteratorRange<const uint64_t*> patchExpansions,
		IteratorRange<const std::string*> prePatchFragments,
		IteratorRange<const std::string*> postPatchFragments,
		StringSection<> definesTable) -> SourceCodeWithRemapping
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
		auto instantiated = patchCollection.InstantiateShader(paramBoxSelectors, patchExpansions);

		// For simplicity, we'll just pre-append the entry point file using an #include directive
		// This will ensure we go through the normal mechanisms to find and load this file.
		// Note that this relies on the underlying shader compiler supporting #includes, however
		//   -- in cases  (like GLSL) that don't have #include support, we would need another
		//	changed preprocessor to handle the include expansions.
		//
		// Pre-appending might be better here, because when writing the entry point function itself,
		// it can be confusing if there is other code injected before the start of the file. Since
		// the entry points should have signatures for the patch functions anyway, it should work
		// fine
		output << instantiated.first;
		for (const auto& s:prePatchFragments)
			output << s << std::endl;
		if (!mainSourceFile.IsEmpty())
			output << "#include \"" << mainSourceFile << "\"" << std::endl;
		output << instantiated.second;
		for (const auto& s:postPatchFragments)
			output << s << std::endl;

		SourceCodeWithRemapping result;
		result._processedSource = output.str();
		for (const auto& dep:patchCollection._dependencies) { assert(!dep._filename.empty()); }
		result._dependencies.insert(
			result._dependencies.end(),
			patchCollection._dependencies.begin(), patchCollection._dependencies.end());

		// We could fill in the _lineMarkers member with some line marker information
		// from the original shader graph compile; but that might be overkill
		return result;
	}

	static auto AssembleShader(
		StringSection<> mainSourceFile,
		IteratorRange<const std::string*> prePatchFragments,
		IteratorRange<const std::string*> postPatchFragments) -> SourceCodeWithRemapping
	{
		std::stringstream output;
		for (const auto& s:prePatchFragments)
			output << s << std::endl;
		if (!mainSourceFile.IsEmpty())
			output << "#include \"" << mainSourceFile << "\"" << std::endl;
		for (const auto& s:postPatchFragments)
			output << s << std::endl;

		SourceCodeWithRemapping result;
		result._processedSource = output.str();
		return result;
	}

	static auto AssembleDirectFromFile(StringSection<> filename) -> SourceCodeWithRemapping
	{
		assert(!XlEqString(filename, "-0"));
		assert(!filename.IsEmpty());

		// Fall back to loading the file directly (without any real preprocessing)
		SourceCodeWithRemapping result;
		result._dependencies.push_back(::Assets::GetDepValSys().GetDependentFileState(filename));

		size_t sizeResult = 0;
		auto blob = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(filename, &sizeResult);
		result._processedSource = std::string((char*)blob.get(), (char*)PtrAdd(blob.get(), sizeResult));
		result._lineMarkers.push_back(ILowLevelCompiler::SourceLineMarker{filename.AsString(), 0, 0});
		return result;
	}

	static auto InstantiateShaderGraph_CompileFromFile(
		IShaderSource& internalShaderSource,
		const ShaderCompilePatchResource& res,
		StringSection<> definesTable) -> IShaderSource::ShaderByteCodeBlob
	{
		if ((!res._patchCollection || res._patchCollection->GetInterface().GetPatches().empty()) && res._prePatchesFragments.empty() && res._postPatchesFragments.empty()) {
			assert(!res._entrypoint._filename.empty());
			return internalShaderSource.CompileFromFile(res._entrypoint, definesTable);
		}

		SourceCodeWithRemapping assembledShader;
		if (res._patchCollection) {
			assembledShader = AssembleShader(*res._patchCollection, res._entrypoint._filename, res._patchCollectionExpansions, res._prePatchesFragments, res._postPatchesFragments, definesTable);
		} else {
			assembledShader = AssembleShader(res._entrypoint._filename, res._prePatchesFragments, res._postPatchesFragments);
		}
		auto result = internalShaderSource.CompileFromMemory(
			MakeStringSection(assembledShader._processedSource),
			res._entrypoint._entryPoint, res._entrypoint._shaderModel,
			definesTable);

		result._deps.insert(result._deps.end(), assembledShader._dependencies.begin(), assembledShader._dependencies.end());
		return result;
	}

	uint64_t ShaderCompilePatchResource::CalculateHash(uint64_t seed) const
	{
		seed = _entrypoint.CalculateHash(seed);
		if (!_patchCollectionExpansions.empty())
			seed = Hash64(AsPointer(_patchCollectionExpansions.begin()), AsPointer(_patchCollectionExpansions.end()), seed);
		if (_patchCollection)
			seed = HashCombine(_patchCollection->GetGUID(), seed);
		seed ^= (uint64_t)_postPatchesFragments.size();
		for (const auto& f:_postPatchesFragments)
			seed = Hash64(f, seed);
		seed ^= (uint64_t)_prePatchesFragments.size();
		for (const auto& f:_prePatchesFragments)
			seed = Hash64(f, seed);
		return seed;
	}

	static const auto ChunkType_Log = ConstHash64Legacy<'Log'>::Value;
	class ShaderGraphCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		virtual std::vector<TargetDesc> GetTargets() const override
		{
			return {
				TargetDesc { GetCompileProcessType((CompiledShaderByteCode_InstantiateShaderGraph*)nullptr), "main" }
			};
		}
		
		virtual ::Assets::SerializedTarget SerializeTarget(unsigned idx) override
		{
			std::vector<::Assets::SerializedArtifact> result;
			if (_byteCode._payload)
				result.push_back({
					GetCompileProcessType((CompiledShaderByteCode_InstantiateShaderGraph*)nullptr), 0, "main",
					_byteCode._payload});
			if (_byteCode._errors)
				result.push_back({
					ChunkType_Log, 0, "log",
					_byteCode._errors});
			return { std::move(result), _depVal };
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override
		{
			return _depVal;
		}

		ShaderGraphCompileOperation(
			IShaderSource& shaderSource,
			const ShaderCompilePatchResource& res,
			StringSection<> definesTable)
		: _byteCode { 
			InstantiateShaderGraph_CompileFromFile(shaderSource, res, definesTable) 
		}
		{
			_depVal = ::Assets::GetDepValSys().Make(_byteCode._deps);
		}
		
		~ShaderGraphCompileOperation()
		{
		}

		IShaderSource::ShaderByteCodeBlob _byteCode;
		::Assets::DependencyValidation _depVal;
	};

	::Assets::CompilerRegistration RegisterInstantiateShaderGraphCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"shader-graph-compiler",
			"shader-graph-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[shaderSource](const ::Assets::InitializerPack& initializers) {
				const auto& res = initializers.GetInitializer<ShaderCompilePatchResource>(0);
				return std::make_shared<ShaderGraphCompileOperation>(*shaderSource, res, initializers.GetInitializer<std::string>(1));
			},
			[shaderSource](::Assets::ArtifactTargetCode targetCode, const ::Assets::InitializerPack& initializers) {
				const auto& res = initializers.GetInitializer<ShaderCompilePatchResource>(0);
				auto definesTable = initializers.GetInitializer<std::string>(1);

				assert(targetCode == GetCompileProcessType((CompiledShaderByteCode_InstantiateShaderGraph*)nullptr));
				auto entryId = Hash64(definesTable, res.CalculateHash(DefaultSeed64));
				auto splitFN = MakeFileNameSplitter(res._entrypoint._filename);

				StringMeld<MaxPath> archiveName;
				StringMeld<MaxPath> descriptiveName;
				bool compressedFN = true;
				if (compressedFN) {
					// shader model & extension already considered in entry id; we just need to look at the directory and filename here
					archiveName << splitFN.File() << "-" << std::hex << HashFilenameAndPath(splitFN.StemAndPath());
					descriptiveName << res._entrypoint._filename << ":" << res._entrypoint._entryPoint << "[" << definesTable << "]" << res._entrypoint._shaderModel;
				} else {
					archiveName << res._entrypoint._filename;
					descriptiveName << res._entrypoint._entryPoint << "[" << definesTable << "]" << res._entrypoint._shaderModel;
				}

				return ::Assets::IIntermediateCompilers::SplitArchiveName { archiveName.AsString(), entryId, descriptiveName.AsString() };
			}};

		uint64_t outputAssetTypes[] = { GetCompileProcessType((CompiledShaderByteCode_InstantiateShaderGraph*)nullptr) };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

}}

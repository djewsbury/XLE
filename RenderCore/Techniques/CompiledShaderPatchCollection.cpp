// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompiledShaderPatchCollection.h"
#include "TechniqueUtils.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Assets/IntermediatesStore.h"
#include "../MinimalShaderSource.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ShaderParser/ShaderPatcher.h"
#include "../../ShaderParser/NodeGraphProvider.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../ShaderParser/DescriptorSetInstantiation.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Streams/PathUtils.h"

namespace RenderCore { namespace Techniques
{
	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const RenderCore::Assets::ShaderPatchCollection& src,
		const DescriptorSetLayoutAndBinding& materialDescSetLayout)
	: _src(src)
	, _materialDescSetLayout(materialDescSetLayout)
	{
		_depVal = ::Assets::GetDepValSys().Make();
		_guid = src.GetHash();

		_interface._descriptorSet = materialDescSetLayout.GetLayout();
		_interface._materialDescriptorSetSlotIndex = materialDescSetLayout.GetSlotIndex();

		if (!src.GetDescriptorSetFileName().empty()) {
			auto layoutFileFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::PredefinedPipelineLayoutFile>(src.GetDescriptorSetFileName());
			layoutFileFuture->StallWhilePending();
			auto actualLayoutFile =layoutFileFuture->Actualize();
			auto i = actualLayoutFile->_descriptorSets.find("Material");
			if (i == actualLayoutFile->_descriptorSets.end())
				Throw(std::runtime_error("Expecting to find a descriptor set layout called 'Material' in pipeline layout file (" + src.GetDescriptorSetFileName() + "), but it was not found"));

			// Once we've finally got the descriptor set, we need to link it against the pipeline layout version to make sure it
			// will agree (and potentially rearrange some members to fit)
			_interface._descriptorSet = ShaderSourceParser::LinkToFixedLayout(*i->second, *_interface._descriptorSet);
			_depVal.RegisterDependency(actualLayoutFile->GetDependencyValidation());
		}

		// With the given shader patch collection, build the source code and the 
		// patching functions associated
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
	}

	CompiledShaderPatchCollection::CompiledShaderPatchCollection(
		const ShaderSourceParser::InstantiatedShader& inst,
		const DescriptorSetLayoutAndBinding& materialDescSetLayout)
	: _materialDescSetLayout(materialDescSetLayout)
	{
		_depVal = ::Assets::GetDepValSys().Make();
		_guid = 0;
		BuildFromInstantiatedShader(inst);
		_interface._descriptorSet = materialDescSetLayout.GetLayout();
		_interface._materialDescriptorSetSlotIndex = materialDescSetLayout.GetSlotIndex();
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
			#if defined(_DEBUG)
				p._entryPointName = patch._name;
			#endif
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
		for (const auto& rawShader:inst._rawShaderFileIncludes) {
			auto rawIncludeFilteringRules = ::Assets::MakeAssetPtr<ShaderSourceParser::SelectorFilteringRules>(rawShader);
			rawIncludeFilteringRules->StallWhilePending();
			filteringRules.MergeIn(*rawIncludeFilteringRules->Actualize());
		}
		_interface._filteringRules.push_back(filteringRules);
		if (filteringRules.GetDependencyValidation())
			_depVal.RegisterDependency(filteringRules.GetDependencyValidation());

		size_t size = 0;
		for (const auto&i:inst._sourceFragments)
			size += i.size();
		_savedInstantiation.reserve(size);
		for (const auto&i:inst._sourceFragments)
			_savedInstantiation.insert(_savedInstantiation.end(), i.begin(), i.end());
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

	static std::string Merge(const std::vector<std::string>& v)
	{
		size_t size=0;
		for (const auto&q:v) size += q.size();
		std::string result;
		result.reserve(size);
		for (const auto&q:v) result.insert(result.end(), q.begin(), q.end());
		return result;
	}

	std::string CompiledShaderPatchCollection::InstantiateShader(const ParameterBox& selectors, IteratorRange<const uint64_t*> patchExpansions) const
	{
		if (_src.GetPatches().empty()) {
			// If we'ved used  the constructor that takes a ShaderSourceParser::InstantiatedShader,
			// we can't re-instantiate here. So our only choice is to just return the saved
			// instantiation here. However, this means the selectors won't take effect, somewhat awkwardly
			return _savedInstantiation;
		}

		// find the particular patches that were requested and instantiate them
		std::vector<ShaderSourceParser::InstantiationRequest> finalInstRequests;
		std::vector<unsigned> srcPatchesToInclude;
		srcPatchesToInclude.reserve(patchExpansions.size());
		for (auto expansion:patchExpansions) {
			auto i = std::find_if(_interface._patches.begin(), _interface._patches.end(), [expansion](const auto& c) { return c._implementsHash == expansion; });
			assert(i != _interface._patches.end());
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
		generateOptions._pipelineLayoutMaterialDescriptorSet = _materialDescSetLayout.GetLayout().get();
		generateOptions._materialDescriptorSetIndex = _materialDescSetLayout.GetSlotIndex();
		auto inst = ShaderSourceParser::InstantiateShader(MakeIteratorRange(finalInstRequests), generateOptions);
		return Merge(inst._sourceFragments);
	}

	DescriptorSetLayoutAndBinding::DescriptorSetLayoutAndBinding(
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& layout,
		unsigned slotIdx)
	: _layout(layout), _slotIdx(slotIdx)
	{
		if (layout) {
			_hash = HashCombine(layout->CalculateHash(), slotIdx);
		} else {
			_hash = 0;
		}
	}

	DescriptorSetLayoutAndBinding::DescriptorSetLayoutAndBinding()
	{
		_hash = 0;
		_slotIdx = ~0u;
	}

	DescriptorSetLayoutAndBinding::~DescriptorSetLayoutAndBinding()
	{}

	DescriptorSetLayoutAndBinding FindLayout(const RenderCore::Assets::PredefinedPipelineLayoutFile& file, const std::string& pipelineLayoutName, const std::string& descriptorSetName)
	{
		auto pipeline = file._pipelineLayouts.find(pipelineLayoutName);
		if (pipeline == file._pipelineLayouts.end())
			return {};

		auto i = std::find_if(pipeline->second->_descriptorSets.begin(), pipeline->second->_descriptorSets.end(),
			[descriptorSetName](const auto& c) {
				return c._name == descriptorSetName;
			});
		if (i == pipeline->second->_descriptorSets.end())
			return {};
		
		return DescriptorSetLayoutAndBinding { i->_descSet, (unsigned)std::distance(pipeline->second->_descriptorSets.begin(), i) };
	}

	DescriptorSetLayoutAndBinding FindLayout(const RenderCore::Assets::PredefinedPipelineLayout& pipeline, const std::string& descriptorSetName)
	{
		auto i = std::find_if(pipeline._descriptorSets.begin(), pipeline._descriptorSets.end(),
			[descriptorSetName](const auto& c) {
				return c._name == descriptorSetName;
			});
		if (i == pipeline._descriptorSets.end())
			return {};
		
		return DescriptorSetLayoutAndBinding { i->_descSet, (unsigned)std::distance(pipeline._descriptorSets.begin(), i) };
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const uint64_t CompileProcess_InstantiateShaderGraph = ConstHash64<'Inst', 'shdr'>::Value;
	const uint64 CompiledShaderByteCode_InstantiateShaderGraph::CompileProcessType = CompileProcess_InstantiateShaderGraph;

	auto AssembleShader(
		const CompiledShaderPatchCollection& patchCollection,
		IteratorRange<const uint64_t*> patchExpansions,
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
		output << patchCollection.InstantiateShader(paramBoxSelectors, patchExpansions);

		for (auto fn:patchExpansions) {
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

	static auto AssembleDirectFromFile(StringSection<> filename) -> SourceCodeWithRemapping
	{
		assert(!XlEqString(filename, "-0"));
		assert(!filename.IsEmpty());

		// Fall back to loading the file directly (without any real preprocessing)
		SourceCodeWithRemapping result;
		result._dependencies.push_back(::Assets::IntermediatesStore::GetDependentFileState(filename));

		size_t sizeResult = 0;
		auto blob = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock_TolerateSharingErrors(filename, &sizeResult);
		result._processedSource = std::string((char*)blob.get(), (char*)PtrAdd(blob.get(), sizeResult));
		result._lineMarkers.push_back(ILowLevelCompiler::SourceLineMarker{filename.AsString(), 0, 0});
		return result;
	}

	static auto InstantiateShaderGraph_CompileFromFile(
		IShaderSource& internalShaderSource,
		const ILowLevelCompiler::ResId& resId, 
		StringSection<> definesTable,
		const CompiledShaderPatchCollection& patchCollection,
		IteratorRange<const uint64_t*> redirectedPatchFunctions) -> IShaderSource::ShaderByteCodeBlob
	{
		if (patchCollection.GetInterface().GetPatches().empty())
			return internalShaderSource.CompileFromFile(resId, definesTable);

		auto assembledShader = AssembleShader(patchCollection, redirectedPatchFunctions, definesTable);

		// For simplicity, we'll just pre-append the entry point file using an #include directive
		// This will ensure we go through the normal mechanisms to find and load this file.
		// Note that this relies on the underlying shader compiler supporting #includes, however
		//   -- in cases  (like GLSL) that don't have #include support, we would need another
		//	changed preprocessor to handle the include expansions.
		//
		// Preappending might be better here, because when writing the entry point function itself,
		// it can be confusing if there is other code injected before the start of the file. Since
		// the entry points should have signatures for the patch functions anyway, it should work
		// fine
		{
			std::stringstream str ;
			str << "#include \"" << resId._filename << "\"" << std::endl;
			assembledShader._processedSource = str.str() + assembledShader._processedSource;
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
			IShaderSource& shaderSource,
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

		IShaderSource::ShaderByteCodeBlob _byteCode;
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
				return std::make_shared<ShaderGraphCompileOperation>(
					*shaderSource,
					shaderSource->MakeResId(initializers.GetInitializer<std::string>(0)),
					initializers.GetInitializer<std::string>(1),
					*initializers.GetInitializer<std::shared_ptr<CompiledShaderPatchCollection>>(2),
					MakeIteratorRange(initializers.GetInitializer<std::vector<uint64_t>>(3))
				);
			},
			[shaderSource](::Assets::ArtifactTargetCode targetCode, const ::Assets::InitializerPack& initializers) {
				auto res = shaderSource->MakeResId(initializers.GetInitializer<std::string>(0));
				auto definesTable = initializers.GetInitializer<std::string>(1);
				auto& patchCollection = *initializers.GetInitializer<std::shared_ptr<CompiledShaderPatchCollection>>(2);
				const auto& patchFunctions = initializers.GetInitializer<std::vector<uint64_t>>(3);

				assert(targetCode == CompileProcess_InstantiateShaderGraph);
				auto splitFN = MakeFileNameSplitter(res._filename);
				auto entryId = HashCombine(HashCombine(HashCombine(Hash64(res._entryPoint), Hash64(definesTable)), Hash64(res._shaderModel)), Hash64(splitFN.Extension()));
				entryId = HashCombine(patchCollection.GetGUID(), entryId);
				for (const auto&p:patchFunctions)
					entryId = HashCombine(p, entryId);

				StringMeld<MaxPath> archiveName;
				StringMeld<MaxPath> descriptiveName;
				bool compressedFN = true;
				if (compressedFN) {
					// shader model & extension already considered in entry id; we just need to look at the directory and filename here
					archiveName << splitFN.File() << "-" << std::hex << HashFilenameAndPath(splitFN.DriveAndPath());
					descriptiveName << res._filename << ":" << res._entryPoint << "[" << definesTable << "]" << res._shaderModel;
				} else {
					archiveName << res._filename;
					descriptiveName << res._entryPoint << "[" << definesTable << "]" << res._shaderModel;
				}

				return ::Assets::IIntermediateCompilers::SplitArchiveName { archiveName.AsString(), entryId, descriptiveName.AsString() };
			}};

		uint64_t outputAssetTypes[] = { CompileProcess_InstantiateShaderGraph };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes));
		return result;
	}

}}

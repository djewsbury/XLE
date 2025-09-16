// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ShaderService.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/IteratorUtils.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace RenderCore { class ICompiledPipelineLayout; }
namespace RenderCore { namespace Assets { class ShaderPatchCollection; class PredefinedCBLayout; class PredefinedDescriptorSetLayout; class PredefinedPipelineLayoutFile; class PredefinedPipelineLayout; }}
namespace Utility { class ParameterBox; }
namespace ShaderSourceParser { class InstantiationRequest; class GenerateFunctionOptions; class NodeGraphSignature; }

namespace RenderCore { namespace Techniques
{
	class DescriptorSetLayoutAndBinding;
	
	/// <summary>Compiled and optimized version of RenderCore::Assets::ShaderPatchCollection</summary>
	/// A RenderCore::Assets::ShaderPatchCollection contains references to shader patches used by a material,
	/// however in that form it's not directly usable. We must expand the shader graphs and calculate the inputs
	/// and outputs before we can use it directly.
	/// 
	/// That's too expensive to do during the frame; so we do that during initialization phases and generate
	/// this object, the ShaderPatchInstantiationUtil
	class ShaderPatchInstantiationUtil
	{
	public:

		/// <summary>Interface properties for this patch collection</summary>
		/// The interface to the patch collection determines how it interacts with techniques that
		/// need to use it. Some of these properties are used for optimization (such as the list of
		/// selectors, which is used for filtering valid selectors). Others are used to determine
		/// how the patches should be bound to a technique file.
		class Interface
		{
		public:
			struct Patch 
			{
				uint64_t		_implementsHash = 0;
				std::string 	_originalEntryPointName, _scaffoldEntryPointName;

				std::shared_ptr<GraphLanguage::NodeGraphSignature> _originalEntryPointSignature;
				std::shared_ptr<GraphLanguage::NodeGraphSignature> _scaffoldSignature;

				unsigned 		_filteringRulesId;

				// Scaffold function to use for patching in this particular implementation
				// The scaffold function always has the name of the function it implements
				std::string		_scaffoldInFunction;
			};
			IteratorRange<const Patch*> GetPatches() const { return MakeIteratorRange(_patches); }

			const RenderCore::Assets::PredefinedDescriptorSetLayout& GetMaterialDescriptorSet() const { return *_descriptorSet; }
			std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> GetMaterialDescriptorSetPtr() const { return _descriptorSet; }

			const ShaderSourceParser::SelectorFilteringRules& GetSelectorFilteringRules(unsigned filteringRulesId) const;
			const std::string& GetPreconfigurationFileName() const { return _preconfiguration; }

			StringSection<> GetOverrideShader(ShaderStage stage) const { return (unsigned(stage) < dimof(_overrideShaders)) ? MakeStringSection(_overrideShaders[unsigned(stage)]) : StringSection<>{}; }

			bool HasPatchType(uint64_t implementing) const;

		private:
			std::vector<Patch> _patches;
			std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _descriptorSet;
			unsigned _materialDescriptorSetSlotIndex;
			std::vector<ShaderSourceParser::SelectorFilteringRules> _filteringRules;
			std::string _preconfiguration;
			std::string _overrideShaders[3];

			friend class ShaderPatchInstantiationUtil;
		};

		const Interface& GetInterface() const { return _interface; }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;
		std::vector<::Assets::DependentFileState> _dependencies;

		std::pair<std::string, std::string> InstantiateShader(const ParameterBox& selectors, IteratorRange<const uint64_t*> patchExpansions) const;

		uint64_t GetGUID() const { return _guid; }

		ShaderPatchInstantiationUtil(
			const RenderCore::Assets::ShaderPatchCollection& src,
			const RenderCore::Assets::PredefinedDescriptorSetLayout* customDescSet,
			const DescriptorSetLayoutAndBinding& materialDescSetLayout);
		ShaderPatchInstantiationUtil(
			const ShaderSourceParser::InstantiatedShader& instantiatedShader,
			const DescriptorSetLayoutAndBinding& materialDescSetLayout);
		ShaderPatchInstantiationUtil(
			const DescriptorSetLayoutAndBinding& materialDescSetLayout);
		ShaderPatchInstantiationUtil();
		~ShaderPatchInstantiationUtil();
	private:
		uint64_t _guid = 0;
		Interface _interface;
		RenderCore::Assets::ShaderPatchCollection _src;
		std::string _savedInstantiation;
		std::string _savedInstantiationPrefix;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _matDescSetLayout;
		unsigned _matDescSetSlot = ~0u;

		void BuildFromInstantiatedShader(const ShaderSourceParser::InstantiatedShader& inst);
	};

	inline bool ShaderPatchInstantiationUtil::Interface::HasPatchType(uint64_t implementing) const
	{
		auto iterator = std::find_if(
			_patches.begin(), _patches.end(),
			[implementing](const Patch& patch) { return patch._implementsHash == implementing; });
		return iterator != _patches.end();
	}

	struct ShaderCompilePatchResource
	{
		std::shared_ptr<ShaderPatchInstantiationUtil> _patchCollection;
		std::vector<uint64_t> _patchCollectionExpansions;
		std::vector<std::string> _prePatchesFragments, _postPatchesFragments;
		ShaderCompileResourceName _entrypoint;		// _filename can be empty here, which means the entrypoint is within either _prePatchesFragments, _postPatchesFragments or the patch expansions

		uint64_t CalculateHash(uint64_t seed) const;
	};

	class CompiledShaderByteCode_InstantiateShaderGraph : public CompiledShaderByteCode
	{
	public:
		using CompiledShaderByteCode::CompiledShaderByteCode;
	};

	constexpr uint64_t GetCompileProcessType(CompiledShaderByteCode_InstantiateShaderGraph*) { return ConstHash64Legacy<'Inst', 'shdr'>::Value; }

	::Assets::CompilerRegistration RegisterInstantiateShaderGraphCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers);

}}

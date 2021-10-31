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
	class DescriptorSetLayoutAndBinding
	{
	public:
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& GetLayout() const { return _layout; }
		unsigned GetSlotIndex() const { return _slotIdx; }

		uint64_t GetHash() const { return _hash; }
		::Assets::DependencyValidation GetDependencyValidation() const { return _layout ? _layout->GetDependencyValidation() : ::Assets::DependencyValidation{}; }

		DescriptorSetLayoutAndBinding(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& layout,
			unsigned slotIdx);
		DescriptorSetLayoutAndBinding();
		~DescriptorSetLayoutAndBinding();

	private:
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _layout;
		unsigned _slotIdx;
		uint64_t _hash;
	};

	DescriptorSetLayoutAndBinding FindLayout(const RenderCore::Assets::PredefinedPipelineLayoutFile&, const std::string& pipelineLayoutName, const std::string& descSetName);
	DescriptorSetLayoutAndBinding FindLayout(const RenderCore::Assets::PredefinedPipelineLayout& file, const std::string& descriptorSetName);

	/// <summary>Compiled and optimized version of RenderCore::Assets::ShaderPatchCollection</summary>
	/// A RenderCore::Assets::ShaderPatchCollection contains references to shader patches used by a material,
	/// however in that form it's not directly usable. We must expand the shader graphs and calculate the inputs
	/// and outputs before we can use it directly.
	/// 
	/// That's too expensive to do during the frame; so we do that during initialization phases and generate
	/// this object, the CompiledShaderPatchCollection
	class CompiledShaderPatchCollection
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
				uint64_t		_implementsHash;
				std::string		_scaffoldInFunction;		// scaffold function to use for patching in this particular implementation.
				std::shared_ptr<GraphLanguage::NodeGraphSignature> _signature;
				#if defined(_DEBUG)
					std::string _entryPointName;
				#endif
				unsigned 		_filteringRulesId;
			};
			IteratorRange<const Patch*> GetPatches() const { return MakeIteratorRange(_patches); }
			const RenderCore::Assets::PredefinedDescriptorSetLayout& GetMaterialDescriptorSet() const { return *_descriptorSet; }
			const ShaderSourceParser::SelectorFilteringRules& GetSelectorFilteringRules(unsigned filteringRulesId) const;

			bool HasPatchType(uint64_t implementing) const;

		private:
			std::vector<Patch> _patches;
			std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _descriptorSet;
			unsigned _materialDescriptorSetSlotIndex;
			std::vector<ShaderSourceParser::SelectorFilteringRules> _filteringRules;

			friend class CompiledShaderPatchCollection;
		};

		const Interface& GetInterface() const { return _interface; }

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		::Assets::DependencyValidation _depVal;
		std::vector<::Assets::DependentFileState> _dependencies;

		std::string InstantiateShader(const ParameterBox& selectors, IteratorRange<const uint64_t*> patchExpansions) const;

		uint64_t GetGUID() const { return _guid; }

		CompiledShaderPatchCollection(
			const RenderCore::Assets::ShaderPatchCollection& src,
			const DescriptorSetLayoutAndBinding& materialDescSetLayout);
		CompiledShaderPatchCollection(
			const ShaderSourceParser::InstantiatedShader& instantiatedShader,
			const DescriptorSetLayoutAndBinding& materialDescSetLayout);
		CompiledShaderPatchCollection();
		~CompiledShaderPatchCollection();
	private:
		uint64_t _guid = 0;
		Interface _interface;
		RenderCore::Assets::ShaderPatchCollection _src;
		std::string _savedInstantiation;
		DescriptorSetLayoutAndBinding _materialDescSetLayout;

		void BuildFromInstantiatedShader(const ShaderSourceParser::InstantiatedShader& inst);
	};

	inline bool CompiledShaderPatchCollection::Interface::HasPatchType(uint64_t implementing) const
	{
		auto iterator = std::find_if(
			_patches.begin(), _patches.end(),
			[implementing](const Patch& patch) { return patch._implementsHash == implementing; });
		return iterator != _patches.end();
	}

	class CompiledShaderByteCode_InstantiateShaderGraph : public CompiledShaderByteCode
	{
	public:
		static const uint64 CompileProcessType;

		using CompiledShaderByteCode::CompiledShaderByteCode;
	};

	::Assets::CompilerRegistration RegisterInstantiateShaderGraphCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers);

}}

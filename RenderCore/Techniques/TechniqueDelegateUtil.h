// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CommonBindings.h"     // for TechniqueIndex::Max
#include "../Assets/PredefinedCBLayout.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include <string>

namespace GraphLanguage { class NodeGraphSignature; }
namespace RenderCore::Assets { class PredefinedPipelineLayout; }

namespace RenderCore { namespace Techniques
{
	struct GraphicsPipelineDesc;
	class ShaderPatchInstantiationUtil;

	struct FlexibleTechniqueHelper
	{
	public:
		struct Entry
		{
			ShaderSourceParser::ManualSelectorFiltering _selectorFiltering;
			RenderCore::Assets::ShaderPatchCollection _patches;
			RenderCore::Assets::TechniqueDelegateConfig _delegateConfig;
			std::vector<std::tuple<std::string, GraphLanguage::NodeGraphSignature, uint64_t>> _patchDelegateInput;
			std::string _additionalPrePatchesFragment;
			std::shared_ptr<ShaderSourceParser::SelectorFilteringRules> _additionalSelectorFiltering;
			std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayout> _pipelineLayout;
			::Assets::DependencyValidation _depVal;

			void Configure(
				GraphicsPipelineDesc& nascentDesc,
				std::shared_ptr<ShaderPatchInstantiationUtil> shaderPatches,
				IteratorRange<const uint64_t*> iaAttributes);
		};

		std::vector<std::pair<uint64_t, Entry>> _entries;
		::Assets::DependencyValidation _depVal;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		FlexibleTechniqueHelper() = default;

		static void ConstructToPromise(
			std::promise<FlexibleTechniqueHelper>&& promise,
			std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
			StringSection<> src);
	};

		//////////////////////////////////////////////////////////////////
			//      T E C H N I Q U E                               //
		//////////////////////////////////////////////////////////////////

			//      "technique" is a way to select a correct shader
			//      in a data-driven way. The code provides a technique
			//      index and a set of parameters in ParameterBoxes
			//          -- that is transformed into a concrete shader

	class TechniqueEntry
	{
	public:
		bool IsValid() const { return !_vertexShaderName.empty(); }
		void MergeIn(const TechniqueEntry& source);

		ShaderSourceParser::ManualSelectorFiltering		_selectorFiltering;
		std::string			_vertexShaderName;
		std::string			_pixelShaderName;
		std::string			_geometryShaderName;
		std::string			_preconfigurationFileName;
		std::string			_pipelineLayoutName;
		uint64_t			_shaderNamesHash = 0;		// hash of the shader names, but not _baseSelectors

		void GenerateHash();
	};

	class TechniqueSetFile
	{
	public:
		std::vector<std::pair<uint64_t, TechniqueEntry>> _settings;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		const TechniqueEntry* FindEntry(uint64_t hashName) const;

		TechniqueSetFile(
			Formatters::TextInputFormatter<utf8>& formatter, 
			const ::Assets::DirectorySearchRules& searchRules, 
			const ::Assets::DependencyValidation& depVal);
		~TechniqueSetFile();
	private:
		::Assets::DependencyValidation _depVal;
	};

	XLE_DEPRECATED_ATTRIBUTE class Technique
	{
	public:
		auto GetDependencyValidation() const -> const ::Assets::DependencyValidation& { return _validationCallback; }
		const RenderCore::Assets::PredefinedCBLayout& TechniqueCBLayout() const { return _cbLayout; }
		TechniqueEntry& GetEntry(unsigned idx);
		const TechniqueEntry& GetEntry(unsigned idx) const;

		Technique(StringSection<::Assets::ResChar> resourceName);
		~Technique();
	private:
		TechniqueEntry			_entries[size_t(TechniqueIndex::Max)];

		::Assets::DependencyValidation		_validationCallback;
		RenderCore::Assets::PredefinedCBLayout		_cbLayout;

		void ParseConfigFile(
			Formatters::TextInputFormatter<utf8>& formatter, 
			StringSection<::Assets::ResChar> containingFileName,
			const ::Assets::DirectorySearchRules& searchRules,
			std::vector<::Assets::DependencyValidation>& inheritedAssets);
	};

	struct GraphicsPipelineDesc; class ShaderPatchInstantiationUtil;
	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry);

	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry,
		const std::shared_ptr<ShaderPatchInstantiationUtil>& shaderPatches,
		std::vector<uint64_t>&& vsPatchExpansions,
		std::vector<uint64_t>&& psPatchExpansions,
		std::vector<uint64_t>&& gsPatchExpansions = {});

}}


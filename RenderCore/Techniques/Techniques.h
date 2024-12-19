// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CommonBindings.h"     // for TechniqueIndex::Max
#include "../Assets/PredefinedCBLayout.h"
#include "../DeviceInitialization.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../Utility/ParameterBox.h"
#include <string>
#include <vector>

namespace RenderCore { class UniformsStreamInterface; class IThreadContext; }

namespace RenderCore { namespace Techniques
{
	struct SelectorStages { enum Enum { Geometry, GlobalEnvironment, Runtime, Material, Max }; };

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

		//////////////////////////////////////////////////////////////////
			//      C O N T E X T                                   //
		//////////////////////////////////////////////////////////////////
	
	class IUniformDelegateManager;
	class IAttachmentPool;
	class IFrameBufferPool;
	class CommonResourceBox;
	class IDrawablesPool;
	class PipelineCollection;
	class IPipelineAcceleratorPool;
	class IDeformAcceleratorPool;
	class SemiConstantDescriptorSet;
	class SystemUniformsDelegate;

	class TechniqueContext
	{
	public:
		ParameterBox _globalEnvironmentState;

		std::shared_ptr<IAttachmentPool> _attachmentPool;
		std::shared_ptr<IFrameBufferPool> _frameBufferPool;
		std::shared_ptr<CommonResourceBox> _commonResources;
		std::shared_ptr<IDrawablesPool> _drawablesPool;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IDeformAcceleratorPool> _deformAccelerators;
		std::shared_ptr<PipelineCollection> _graphicsPipelinePool;

		std::shared_ptr<SemiConstantDescriptorSet> _graphicsSequencerDS;
		std::shared_ptr<SemiConstantDescriptorSet> _computeSequencerDS;
		std::shared_ptr<SystemUniformsDelegate> _systemUniformsDelegate;

		std::vector<Format> _systemAttachmentFormats;
	};

	UnderlyingAPI GetTargetAPI();

	std::shared_ptr<IThreadContext> GetThreadContext();
	std::weak_ptr<IThreadContext> SetThreadContext(std::weak_ptr<IThreadContext>);

	struct GraphicsPipelineDesc; class CompiledShaderPatchCollection;
	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry);

	void PrepareShadersFromTechniqueEntry(
		GraphicsPipelineDesc& nascentDesc,
		const TechniqueEntry& entry,
		const std::shared_ptr<CompiledShaderPatchCollection>& shaderPatches,
		std::vector<uint64_t>&& vsPatchExpansions,
		std::vector<uint64_t>&& psPatchExpansions,
		std::vector<uint64_t>&& gsPatchExpansions = {});

}}


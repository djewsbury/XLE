// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "CommonBindings.h"     // for TechniqueIndex::Max
#include "../Assets/PredefinedCBLayout.h"
#include "../Init.h"
#include "../../ShaderParser/ShaderAnalysis.h"
#include "../../Utility/ParameterBox.h"
#include <string>
#include <vector>
#include <unordered_map>

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
        ::Assets::rstring   _vertexShaderName;
        ::Assets::rstring   _pixelShaderName;
        ::Assets::rstring   _geometryShaderName;
        ::Assets::rstring   _preconfigurationFileName;
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
            Utility::InputStreamFormatter<utf8>& formatter, 
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
            InputStreamFormatter<utf8>& formatter, 
			StringSection<::Assets::ResChar> containingFileName,
            const ::Assets::DirectorySearchRules& searchRules,
            std::vector<::Assets::DependencyValidation>& inheritedAssets);
	};

		//////////////////////////////////////////////////////////////////
            //      C O N T E X T                                   //
        //////////////////////////////////////////////////////////////////
    
    class IUniformDelegateManager;
    class AttachmentPool;
    class FrameBufferPool;
    class CommonResourceBox;
    class IDrawablesPool;
    class PipelineCollection;

    class TechniqueContext
    {
    public:
        ParameterBox _globalEnvironmentState;

        std::shared_ptr<IUniformDelegateManager> _uniformDelegateManager;
        std::shared_ptr<AttachmentPool> _attachmentPool;
		std::shared_ptr<FrameBufferPool> _frameBufferPool;
        std::shared_ptr<CommonResourceBox> _commonResources;
        std::shared_ptr<IDrawablesPool> _drawablesPool;
        std::shared_ptr<PipelineCollection> _graphicsPipelinePool;
        std::vector<Format> _systemAttachmentFormats;
    };

	UnderlyingAPI GetTargetAPI();

	std::shared_ptr<IThreadContext> GetThreadContext();
	void SetThreadContext(const std::shared_ptr<IThreadContext>&);

}}


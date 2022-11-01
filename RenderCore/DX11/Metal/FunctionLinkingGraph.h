// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../../Assets/AssetsCore.h"
#include "../../../Assets/AssetUtils.h"
#include "../../../OSServices/WinAPI/WinAPIWrapper.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/IntrusivePtr.h"
#include "IncludeDX11.h"
#include <D3D11Shader.h>
#include <memory>

namespace RenderCore { namespace Metal_DX11
{
	class FunctionLinkingModule;
	class FLGFormatter;

	class FunctionLinkingGraph
    {
    public:
        ID3D11FunctionLinkingGraph* GetUnderlying() { return _graph.get(); }

        bool TryLink(
			::Assets::Blob& payload,
			::Assets::Blob& errors,
            std::vector<::Assets::DependentFileState>& dependencies,
			StringSection<> identifier,
            const char shaderModel[]);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _dependencyValidation; }

        using Section = StringSection<char>;
        FunctionLinkingGraph(Section script, Section shaderProfile, Section defines, const ::Assets::DirectorySearchRules& searchRules);
        ~FunctionLinkingGraph();
    private:
        void ParseAssignmentExpression(FLGFormatter& formatter, Section variableName, const ::Assets::DirectorySearchRules& searchRules);
        void ParsePassValue(Section src, Section dst, StreamLocation loc);

        using NodePtr = intrusive_ptr<ID3D11LinkingNode>;
        using AliasTarget = std::pair<NodePtr, int>;
        AliasTarget ResolveParameter(Section src, StreamLocation loc);
        NodePtr ParseCallExpression(Section fnName, Section paramsShortHand, StreamLocation loc);
        FunctionLinkingModule ParseModuleExpression(Section params, const ::Assets::DirectorySearchRules& searchRules, StreamLocation loc);

        intrusive_ptr<ID3D11FunctionLinkingGraph> _graph;
        std::vector<std::pair<Section, FunctionLinkingModule>> _modules;
        std::vector<std::pair<Section, NodePtr>> _nodes;

        std::vector<std::pair<std::string, AliasTarget>> _aliases;

        std::vector<::Assets::DependentFileState> _depFiles;
		::Assets::DependencyValidation _dependencyValidation;
        std::vector<std::pair<Section, Section>> _referencedFunctions;

        std::string _shaderProfile, _defines;
    };
}}


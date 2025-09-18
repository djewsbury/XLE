// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ShaderService.h"
#include "../Assets/IntermediateCompilers.h"
#include "../Utility/IteratorUtils.h"
#include <memory>

namespace Assets { class DirectorySearchRules; }

namespace RenderCore
{
	struct SourceCodeWithRemapping
    {
        std::string _processedSource;
		unsigned _processedSourceLineCount = 0;
        std::vector<ILowLevelCompiler::SourceLineMarker> _lineMarkers;
        ::Assets::DependencyValidation _depVal;

        const ::Assets::DependencyValidation& GetDependencyValidation() { return _depVal; }
    };

	class ISourceCodePreprocessor
    {
    public:
        virtual SourceCodeWithRemapping RunPreprocessor(
            StringSection<> inputSource, 
            StringSection<> definesTable,
            const ::Assets::DirectorySearchRules& searchRules) = 0;
		virtual ~ISourceCodePreprocessor();
    };

	std::shared_ptr<IShaderSource> CreateMinimalShaderSource(
		std::shared_ptr<ILowLevelCompiler> compiler,
		std::shared_ptr<ISourceCodePreprocessor> preprocessor = nullptr);

	::Assets::CompilerRegistration RegisterShaderCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers,
		ShaderCompileResourceName::CompilationFlags::BitField universalCompilationFlags);
}




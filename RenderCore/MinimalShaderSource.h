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
	class SourceCodeWithRemapping
    {
    public:
        std::string _processedSource;
		unsigned _processedSourceLineCount = 0;
        std::vector<ILowLevelCompiler::SourceLineMarker> _lineMarkers;
        std::vector<::Assets::DependentFileState> _dependencies;
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

	class MinimalShaderSource : public IShaderSource
	{
	public:
		ShaderByteCodeBlob CompileFromFile(
			const ILowLevelCompiler::ResId& resId, 
			StringSection<> definesTable) const override;
			
		ShaderByteCodeBlob CompileFromMemory(
			StringSection<> shaderInMemory, StringSection<> entryPoint, 
			StringSection<> shaderModel, StringSection<> definesTable) const override;

		ILowLevelCompiler::ResId MakeResId(
            StringSection<> initializer) const override;

		std::string GenerateMetrics(
        	IteratorRange<const void*> byteCodeBlob) const override;

		MinimalShaderSource(
			const std::shared_ptr<ILowLevelCompiler>& compiler,
			const std::shared_ptr<ISourceCodePreprocessor>& preprocessor = nullptr);
		~MinimalShaderSource();

	protected:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;

		ShaderByteCodeBlob Compile(
			StringSection<> shaderInMemory,
			const ILowLevelCompiler::ResId& resId,
			StringSection<::Assets::ResChar> definesTable) const;
	};

	::Assets::CompilerRegistration RegisterShaderCompiler(
		const std::shared_ptr<IShaderSource>& shaderSource,
		::Assets::IIntermediateCompilers& intermediateCompilers);
}




set(Src 
	AntlrHelper.cpp
	AutomaticSelectorFiltering.cpp
	DescriptorSetInstantiation.cpp
	GraphSyntaxInstantiate.cpp
	GraphSyntaxParse.cpp
	NodeGraph.cpp
	NodeGraphProvider.cpp
	NodeGraphSignature.cpp
	ParameterSignature.cpp
	ShaderAnalysis.cpp
	ShaderInstantiation.cpp
	ShaderPatcher.cpp
	ShaderSignatureParser.cpp
	StructureForPreview.cpp)
	
set(Headers
	AntlrHelper.h
	AutomaticSelectorFiltering.h
	DescriptorSetInstantiation.h
	Exceptions.h
	GraphSyntax.h
	NodeGraph.h
	NodeGraphProvider.h
	NodeGraphSignature.h
	ParameterSignature.h
	ShaderAnalysis.h
	ShaderInstantiation.h
	ShaderPatcher_Internal.h
	ShaderPatcher.h
	ShaderSignatureParser.h)

set(GeneratedGrammarFiles
	Grammar/GraphSyntaxEval.c Grammar/GraphSyntaxEval.h
 	Grammar/GraphSyntaxLexer.c Grammar/GraphSyntaxLexer.h
	Grammar/GraphSyntaxParser.c Grammar/GraphSyntaxParser.h
	Grammar/ShaderLexer.c Grammar/ShaderLexer.h
	Grammar/ShaderParser.c Grammar/ShaderParser.h
	Grammar/ShaderTreeWalk.c Grammar/ShaderTreeWalk.h)

set(GrammarDir ${CMAKE_CURRENT_LIST_DIR}/Grammar)

add_custom_command(
	OUTPUT ${GeneratedGrammarFiles}
	COMMAND java -jar "${FOREIGN_DIR}/Antlr-3.4/antlr-3.4-complete.jar" -lib ${CMAKE_CURRENT_BINARY_DIR}/Grammar/ -o ${CMAKE_CURRENT_BINARY_DIR}/Grammar/ ${GrammarDir}/Shader.g ${GrammarDir}/GraphSyntax.g ${GrammarDir}/GraphSyntaxEval.g ${GrammarDir}/ShaderTreeWalk.g
	DEPENDS Grammar/Shader.g Grammar/GraphSyntax.g Grammar/GraphSyntaxEval.g Grammar/ShaderTreeWalk.g
	BYPRODUCTS Grammar/GraphSyntax.tokens Grammar/GraphSyntaxEval.tokens Grammar/ShaderTreeWalk.tokens)

add_library(ShaderParser STATIC ${Src} ${Headers} ${GeneratedGrammarFiles})
xle_configure_library(ShaderParser)

target_include_directories(ShaderParser PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ShaderParser PUBLIC RenderCoreAssets)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(ShaderParser PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=c11>)
	target_compile_options(ShaderParser PRIVATE -Wno-parentheses-equality -Wno-incompatible-pointer-types-discards-qualifiers)
endif()

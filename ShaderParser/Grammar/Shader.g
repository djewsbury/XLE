//
// ANTLR grammar for HLSL files
//
// (C) 2009 Christian Schladetsch
// (C) 2009 Blue Lion Software
//
// Permission to use for any purpose is given, as long as this copyright
// information is included in any derived works.

//
//		Modified very heavily by DavidJ!
//			-- this is a very "permissive" grammar for HLSL
//				.. which means that many illegal things will work fine. For our
//				purposes, that is best.
//
//				The grammar is not perfect. I don't think Microsoft publishes
//				and exact grammar for HLSL (but we can get close with GLSL or cg).
//				And there are some things just hacked in... Really, we don't need
//				a perfect parse. We just need to be able to extract the limited 
//				things we're interested in (like function signatures and structure
//				layout)
//

grammar Shader;

options
{
	language = C;
	output = AST;
	backtrack = true; 
	memoize = true;
}

tokens
{
	STRUCT;
	CBUFFER;
	FUNCTION;
	CLASS;
	
	LOCAL_VAR;
	SEMANTIC;
	
	SUBSCRIPT;
	UNIFORM;
	VARIABLE;
	VARIABLE_NAME;
	TYPE_NAME;
	FUNCTION_CALL;
	ARG_LIST;
	FORMAL_ARG;
	BLOCK;
	ASSIGNMENT_EXPRESSION;
	
	IF;
	IF_ELSE;
	CAST;
	
	DIRECTION_OUT;
	DIRECTION_IN_OUT;

	TOPLEVEL;
}

@header
{
	#pragma warning(disable:4244)
	void CustomDisplayRecognitionError(void * recognizer, void * tokenNames);
}

@members
{
	#include "../../Core/Prefix.h"

	#if (COMPILER_ACTIVE == COMPILER_TYPE_GCC) || (COMPILER_ACTIVE == COMPILER_TYPE_CLANG)
		#if !(__has_feature(c_thread_local) || __has_extension(c_thread_local))
			#error The _Thread_local feature is not enabled in the compiler for C code. This is a C11 feature. You may need to ensure the -std=c11 compiler command line flag is set
		#endif
	#endif

	typedef void ExceptionHandler(void*, const ANTLR3_EXCEPTION*, const ANTLR3_UINT8**);
	typedef struct { ExceptionHandler* _callback; void* _userData; } ExceptionHandlerAndUserData;
	thread_local ExceptionHandler* g_ShaderParserExceptionHandler = NULL;
	thread_local void* g_ShaderParserExceptionHandlerUserData = NULL;

	ExceptionHandlerAndUserData SetShaderParserExceptionHandler(ExceptionHandlerAndUserData handler)
	{
		ExceptionHandlerAndUserData prev;
		prev._callback = g_ShaderParserExceptionHandler;
		prev._userData = g_ShaderParserExceptionHandlerUserData;
		g_ShaderParserExceptionHandler = handler._callback;
		g_ShaderParserExceptionHandlerUserData = handler._userData;
		return prev;
	}

	void CustomDisplayRecognitionError(void * recognizer, void * tokenNames)
	{
		ANTLR3_BASE_RECOGNIZER* r = (ANTLR3_BASE_RECOGNIZER*)recognizer;
		ANTLR3_UINT8 ** t = (ANTLR3_UINT8 **)tokenNames;
		ANTLR3_COMMON_TOKEN* token = (ANTLR3_COMMON_TOKEN*)r->state->exception->token;

		if (g_ShaderParserExceptionHandler) {
			(*g_ShaderParserExceptionHandler)(
				g_ShaderParserExceptionHandlerUserData, 
				r->state->exception,
				t);
		}
	}

	unsigned LooksLikePreprocessorMacro(pANTLR3_COMMON_TOKEN token)
	{
			// Expecting all characters to be upper case ASCII chars
			// or underscores
		pANTLR3_STRING text = token->getText(token);
		pANTLR3_UINT8 chrs = text->chars;
		if (!text->len) return 0;

		if ((chrs[0] < 'A' || chrs[0] > 'Z') && chrs[0] != '_')
			return 0;

		for (unsigned c=1; c<text->len; ++c)
			if (!(chrs[c] >= 'A' && chrs[c] <= 'Z') && chrs[c] != '_' && !(chrs[c] >= '0' && chrs[c] <= '9'))
				return 0;
		return 1;
	}
}

@parser::apifuncs 
{
	RECOGNIZER->displayRecognitionError = (void (*)(struct ANTLR3_BASE_RECOGNIZER_struct *, pANTLR3_UINT8 *))&CustomDisplayRecognitionError;
}

storage_class : 'extern' | 'nointerpolation' | 'precise' | 'shared' | 'groupshared' | 'static' | 'uniform' | 'volatile';
type_modifier : 'const' | 'row_major' | 'column_major';

entrypoint
	:	toplevels+=toplevel*
	;
	
toplevel
	:	global
	|	structure
	|	cbuffer
	|	function
	|	function_signature
	|	';'!			// empty statement
	;

global
	:	uniform
	;
	
variablename_single : id=ident sub+=subscript* sem=semantic? registerAssignment? ('=' e=expression)? -> ^(VARIABLE_NAME $id $sub* $sem?);
variablename_list	: variablename_single (',' variablename_single)* -> variablename_single+;

uniform
	:	(storage_class|type_modifier)* ty=type_name names=variablename_list ';' 
		-> ^(UNIFORM $ty $names)
	;

variable
	:	(storage_class|type_modifier)* ty=type_name names=variablename_list ';' 
		-> ^(VARIABLE $ty $names)
	;
	
subscript
	:	'[' (e=expression)? ']' -> SUBSCRIPT
	;

registerValue : ident;

registerAssignment 
	: ':' 'register' '(' registerValue ')'
	| isolated_macro
	;
	
structure
	:	'struct' ident '{' fields+=structure_field* '}' variablename_list? ';' 
		-> ^(STRUCT ident $fields*)
	;

interface_class
	:	('interface'|'class') ident '{' fields+=class_field* '}' ';'
		-> ^(CLASS ident $fields*)
	;

cbuffer
	:	('cbuffer'|'tbuffer') ident registerAssignment? '{' fields+=structure_field* '}'
		-> ^(CBUFFER ident $fields*)
	;
	
structure_field
	:	variable
	|	isolated_macro
	;

class_field
	:	variable
	|	cbuffer
	|	function_signature
	;
	
functionAttributeType 
	: 'numthreads'			// compute shaders
	| 'maxvertexcount'		// geometry shaders
	| 'earlydepthstencil'	// pixel shaders
	| 'domain' | 'partitioning' | 'outputtopology' | 'patchconstantfunc' | 'outputcontrolpoints' | 'maxtessfactor' // hull & domain shaders
	;
	
identifierOrLiteral : literal|StringLiteral|ident;
identifierOrLiteralList : identifierOrLiteral (','! identifierOrLiteral)*;
functionAttributes : ('[' functionAttributeType ('(' identifierOrLiteralList ')')? ']')*;

function
	:	'export'? functionAttributes ret=type_name name=ident '(' args=formal_arglist ')' semantic? block
		-> ^(FUNCTION $name $ret $args semantic? block)
	;

function_signature
	:	'export'? functionAttributes ret=type_name name=ident '(' args=formal_arglist ')' semantic? ';'
		-> ^(FUNCTION $name $ret $args semantic?)
	;

// Note --	
//		isolated_macro macro represents some macro expression in the code that
//		will be expanded by the preprocessor. Since we don't support the preprocessor
//		when parsing, we should assume it does nothing, and just ignore it. This kind
//		of macro is often used for optional function parameters and optional structure
//		members. Unfortunately they will be lost! The only solution is to support 
//		the preprocessor... But that is impractical because of the behaviour of #include...
//
//		We're going to use common preprocessor formatting conventions to try to limit
//		matches with this rule. We can use a semantic predicate test the identifier
isolated_macro
	:	{ LooksLikePreprocessorMacro(LT(1)) }? Identifier!
	;
	
semantic
	:	':' n=Identifier -> ^(SEMANTIC $n)
	;

/*
sampler_type_name : 'sampler' | 'sampler1D' | 'sampler2D' | 'sampler3D' | 'samplerCUBE' | 'sampler_state' | 'SamplerState' | 'SamplerComparisonState' ;

texture_type_name	: 'texture' 
					| 'Texture1D'	| 'Texture1DArray'
					| 'Texture2D'	| 'Texture2DArray'	| 'Texture2DMS'			| 'Texture2DMSArray'
					| 'Texture3D'	| 'TextureCube'		| 'TextureCubeArray'
					| 'RWTexture1D' | 'RWTexture1DArray'
					| 'RWTexture2D' | 'RWTexture2DArray'
					| 'RWTexture3D' | 'RWTexture3DArray'

					| 'Texture2D_MaybeMS'		// (for convenience, include this XLE custom #define)
					;

structuredBufferTypeName : 'StructuredBuffer' | 'RWStructuredBuffer' | 'AppendStructuredBuffer';

streamOutputObject : 'PointStream' | 'LineStream' | 'TriangleStream'; 

type_name
	:	sampler_type_name										-> ^(TYPE_NAME sampler_type_name)
	|	texture_type_name										-> ^(TYPE_NAME texture_type_name)
	|	texture_type_name ('<' identifierOrLiteralList '>')		-> ^(TYPE_NAME texture_type_name identifierOrLiteralList)
	|	structuredBufferTypeName '<' identifierOrLiteralList '>'	-> ^(TYPE_NAME structuredBufferTypeName identifierOrLiteralList)
	|	streamOutputObject '<' identifierOrLiteralList '>'			-> ^(TYPE_NAME streamOutputObject identifierOrLiteralList)
	|	'InputPatch' '<' identifierOrLiteralList '>'				-> ^(TYPE_NAME 'InputPatch' identifierOrLiteralList)
	|	'OutputPatch' '<' identifierOrLiteralList '>'				-> ^(TYPE_NAME 'OutputPatch' identifierOrLiteralList)
	|	ident													-> ^(TYPE_NAME ident)
	;
*/

type_name
	:	ident										-> ^(TYPE_NAME ident)
	|	ident ('<' identifierOrLiteralList '>')		-> ^(TYPE_NAME ident identifierOrLiteralList)
	;
	
ident
	:	id=Identifier
	;	
	
block
	:	'{' statements+=statement* '}' -> ^(BLOCK)		// Don't output statements (ie, with: $statements*) because we don't actually parse that in the tree walker
	;
	
formal_arglist
	:	formal_arg ((',' formal_arg)|isolated_macro)* -> formal_arg+
	|
	;
	
formal_arg
	:	(dir=direction | storage_class | type_modifier)* geometryPrimitiveType? ty=type_name id=ident sub+=subscript* sem=semantic? ('=' expression)?
		-> ^(FORMAL_ARG $id $ty $sub* $sem? $dir?)
	;
	
direction
	:	'in' | 'out' -> DIRECTION_OUT | 'inout' -> DIRECTION_IN_OUT
	;
	
// geometryPrimitiveType is only required for geometry shaders. So there may be
// parsing confusion if there are any structs with a conflicting name (eg: point)
geometryPrimitiveType
	:	'point' | 'line' | 'triangle' | 'lineadj' | 'triangleadj'
	;
	
literal : DecimalLiteral | FloatingPointLiteral | HexLiteral;

//---------------------------------------------------------------------------------
//					 S t a t e m e n t 
//---------------------------------------------------------------------------------

statement
	:	block
	|	if_block
	|	for_loop
	|	while_loop
	|	do_while_loop
	|	'return'^ expression? ';'!
	|	'break' ';'!
	|	'continue' ';'!
	|	';'!
	|	statementExpression ';'!
	|	isolated_macro
	// also -- switch
	;

loop_attribute
	: '[' 'unroll' ('(' ident ')')? ']'
	| '[' 'loop' ']' 
	| ;

cond_attribute
	: '[' 'branch' ']'
	| '[' 'flatten' ']' 
	| ;

for_loop
	:	loop_attribute 'for' '(' start=statementExpression? ';' cond=expression? ';' next=expression? ')' body=statement
		-> ^('for' $cond $next $body $start )
	;

do_while_loop
	:	loop_attribute 'do' body=statement 'while' '(' cond=expression ')' ';'
		-> ^('do' $cond $body )
	;

while_loop
	:	loop_attribute 'while' '(' cond=expression ')' body=statement
		-> ^('while' $cond $body )
	;
	
if_block
	:	cond_attribute 'if' parExpression statement 'else' statement	-> ^(IF_ELSE statement statement parExpression)
	|	cond_attribute 'if' parExpression statement						-> ^(IF statement parExpression)
	;

statementExpression
	:	expression
	|	(storage_class|type_modifier)* ty=ident names=variablename_list	-> ^(LOCAL_VAR $ty $names)
	;
	
//------------------------------------------------------------------------------
//					 E x p r e s s i o n
//------------------------------------------------------------------------------

expressionList
	:	expression (','! expression)*
	;
	
////////////////////////////////////////////////////////////////////////////////
// See the C operator precedence rules:
//		http://en.cppreference.com/w/c/language/operator_precedence
// The grouping and ordering of rules here reflects those changes. I'm assuming
// that HLSL rules are equivalent to C (given that it may not be explictly 
// documented), but I think it is a reasonable assumption.
////////////////////////////////////////////////////////////////////////////////
	
assignmentOp : '=' | '+=' | '-=' | '*=' | '/=' | '&=' | '|=' | '^=' | '%=' | '>>=' | '<<=';
relationalOp : ('<=' | '>=' | '<' | '>');
shiftOp : ('<<' | '>>');
additionOp : ('+' | '-');
multiplicationOp : ('*' | '/' | '%');
unaryOp : '+' | '-' | '~' | '!';
prefixOp : '++' | '--';
postfixOp : '++' | '--';

////////////////////////////////////////////////////////////////////////////////

expression					: conditionalExpression (assignmentOp^ expression)?;
conditionalExpression		: conditionalOrExpression ( '?' expression ':' expression )?;
conditionalOrExpression		: conditionalAndExpression ( '||'^ conditionalAndExpression )*;
conditionalAndExpression	: inclusiveOrExpression ( '&&'^ inclusiveOrExpression )*;
inclusiveOrExpression		: exclusiveOrExpression ( '|'^ exclusiveOrExpression )*;
exclusiveOrExpression		: andExpression ( '^'^ andExpression )*;
andExpression				: equalityExpression ( '&'^ equalityExpression )*;
equalityExpression			: relationalExpression ( ('=='^ | '!='^) relationalExpression )*;
relationalExpression		: shiftExpression ( relationalOp^ shiftExpression )*;
shiftExpression				: additiveExpression ( shiftOp^ additiveExpression )*;
additiveExpression			: multiplicativeExpression ( additionOp^ multiplicativeExpression )*;
multiplicativeExpression	: castExpression ( multiplicationOp^ castExpression )*;

castExpression
	:	'(' type_name ')' unaryExpression
	|	unaryExpression
	;

unaryExpression
	:	postfixExpression
	|	prefixOp^ unaryExpression
	|	unaryOp^ castExpression
	;

postfixExpression
	:	primary
		(
			arguments				-> ^(FUNCTION_CALL arguments $postfixExpression)
			|	'[' expression ']'	-> ^('[' $postfixExpression expression)
			|	'.' ident			-> ^('.' $postfixExpression ident)
			|	postfixOp
		)+
	|	primary
	;

primary
	:	literal
	|	ident
	|	parExpression
	|	curly_brace_initialiser
	;

parExpression
	:	'(' expression ')' -> expression
	;

arguments
	:	'(' expressionList? ')' -> ^(ARG_LIST expressionList)
	;
	
curly_brace_initialiser
	: '{' expression (',' expression)* ','? '}'
	| '{' '}'
	;

//------------------------------------------------------------------------------
//						L E X E R 
//------------------------------------------------------------------------------

HexLiteral : '0' ('x'|'X') HexDigit+ IntegerTypeSuffix? ;

DecimalLiteral : ('0' | '1'..'9' '0'..'9'*) IntegerTypeSuffix? ;

OctalLiteral : '0' ('0'..'7')+ IntegerTypeSuffix? ;

fragment HexDigit : ('0'..'9'|'a'..'f'|'A'..'F') ;

fragment IntegerTypeSuffix : ('l'|'L'|'u'|'U'|'ul'|'UL') ;

FloatingPointLiteral
	:	('0'..'9')+ '.' ('0'..'9')* Exponent? FloatTypeSuffix?
	|	'.' ('0'..'9')+ Exponent? FloatTypeSuffix?
	|	('0'..'9')+ Exponent? FloatTypeSuffix?
	;

fragment Exponent : ('e'|'E') ('+'|'-')? ('0'..'9')+ ;

fragment FloatTypeSuffix : ('f'|'F'|'d'|'D') ;

CharacterLiteral
	:	'\'' ( EscapeSequence | ~('\''|'\\') ) '\''
	;

StringLiteral
	:  '"' ( EscapeSequence | ~('\\'|'"') )* '"'
	;

fragment
EscapeSequence
	:	'\\' ('b'|'t'|'n'|'f'|'r'|'\"'|'\''|'\\')
	|	UnicodeEscape
	|	OctalEscape
	;

fragment OctalEscape
	:	'\\' ('0'..'3') ('0'..'7') ('0'..'7')
	|	'\\' ('0'..'7') ('0'..'7')
	|	'\\' ('0'..'7')
	;

fragment UnicodeEscape
	:	'\\' 'u' HexDigit HexDigit HexDigit HexDigit
	;
	
Identifier 
	:	Letter (Letter|JavaIDDigit)*
	;

QuotedIdentifier 
	:	'`' Identifier
	;

/**I found this char range in JavaCC's grammar, but Letter and Digit overlap.
	Still works, but...
 */
fragment Letter
	:  '\u0024' |
		'\u0041'..'\u005a' |
		'\u005f' |
		'\u0061'..'\u007a' |
		'\u00c0'..'\u00d6' |
		'\u00d8'..'\u00f6' |
		'\u00f8'..'\u00ff' |
		'\u0100'..'\u1fff' |
		'\u3040'..'\u318f' |
		'\u3300'..'\u337f' |
		'\u3400'..'\u3d2d' |
		'\u4e00'..'\u9fff' |
		'\uf900'..'\ufaff'
	;

fragment JavaIDDigit
	:  '\u0030'..'\u0039' |
		'\u0660'..'\u0669' |
		'\u06f0'..'\u06f9' |
		'\u0966'..'\u096f' |
		'\u09e6'..'\u09ef' |
		'\u0a66'..'\u0a6f' |
		'\u0ae6'..'\u0aef' |
		'\u0b66'..'\u0b6f' |
		'\u0be7'..'\u0bef' |
		'\u0c66'..'\u0c6f' |
		'\u0ce6'..'\u0cef' |
		'\u0d66'..'\u0d6f' |
		'\u0e50'..'\u0e59' |
		'\u0ed0'..'\u0ed9' |
		'\u1040'..'\u1049'
	;

WS  :  (' '|'\r'|'\t'|'\u000C'|'\n') {$channel=HIDDEN;}
	;

COMMENT
	:	'/*' ( options {greedy=false;} : . )* '*/' { $channel=HIDDEN; }
	;

LINE_COMMENT
	:	'//' ~('\n'|'\r')* '\r'? '\n' { $channel=HIDDEN; }
	;

//	Hide any line that begins with "#" (just ignore preprocessor stuff)... 
//	We also need to suport the '\' line extension... so just eat everything
//	until we hit a newline that isn't preceeded by a '\'. Of course, this will
//	mean any preprocessor directive can get extended to the next line (not just
//	#define)
PRE_PROCESSOR_LINE
	:	'#' (options {greedy=false;}: . )* ~('\\'|'\r') '\r'? '\n' { $channel=HIDDEN; }
	;

COMPILER_ATTRIBUTE
	:	'[[' ( options {greedy=false;} : . )* ']]' { $channel=HIDDEN; }
	;

// EOF

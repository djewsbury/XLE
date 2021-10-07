// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Utility/Streams/PreprocessorInterpreter.h"
#include "../../Utility/Streams/ConditionalPreprocessingTokenizer.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/ParameterBox.h"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"

namespace Utility
{
	std::ostream& SerializationOperator(std::ostream& str, const Utility::PreprocessorAnalysis& analysis)
	{
		str << "-------- Relevance Rules --------" << std::endl;
		for (const auto&r:analysis._relevanceTable)
			str << "\t" << analysis._tokenDictionary.AsString({r.first}) << " = " << analysis._tokenDictionary.AsString(r.second) << std::endl;

		str << "-------- Substitutions --------" << std::endl;
		auto& subst = analysis._sideEffects;
		for (const auto&i:subst._substitutions) {
			str << "\t" << i._symbol << " is ";
			if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::Undefine) {
				str << "undefined";
			} else {
				if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::Define) str << "defined to ";
				else if (i._type == Utility::Internal::PreprocessorSubstitutions::Type::DefaultDefine) str << "default defined to ";
				else str << "<<unknown operation>> ";
				str << subst._dictionary.AsString(i._substitution);
			}
			if (i._condition.empty() || (i._condition.size() == 1 && i._condition[0] == 1)) {
				// unconditional
			} else 
				str << ", if " << subst._dictionary.AsString(i._condition);
			str << std::endl;
		}

		return str;
	}
}

using namespace Catch::literals;
namespace UnitTests
{
	TEST_CASE( "Utilities-ExpressionRelevance", "[utility]" )
	{
		const char* inputExpression0 = "(SEL0 || SEL1) && SEL2";
		auto expression0Relevance = GeneratePreprocessorAnalysisFromString(inputExpression0);
		std::cout << "Expression0 result: " << std::endl << expression0Relevance;

		const char* inputExpression0a = "(SEL0 || defined(SEL1) || SEL2<5) && (SEL3 || defined(SEL4) || SEL5>=7)";
		auto expression0aRelevance = GeneratePreprocessorAnalysisFromString(inputExpression0a);
		std::cout << "Expression0a result: " << std::endl << expression0aRelevance;

		const char* inputExpression1 = "(SEL0 || SEL1) && SEL2 && !SEL3 && (SEL4==2 || SEL5 < SEL6) || defined(SEL7)";
		auto expression1Relevance = GeneratePreprocessorAnalysisFromString(inputExpression1);
		std::cout << "Expression1 result: " << std::endl << expression1Relevance;
	}

	static const char* s_testFile = R"--(
#if !defined(MAIN_GEOMETRY_H)
#define MAIN_GEOMETRY_H

#define SHADOW_CASCADE_MODE_ARBITRARY 1
#define SHADOW_CASCADE_MODE_ORTHOGONAL 2

#if !defined(VSINPUT_EXTRA)
	#define VSINPUT_EXTRA
#endif

#if !defined(VSOUTPUT_EXTRA)
	#define VSOUTPUT_EXTRA
#endif

#if !defined(VSSHADOWOUTPUT_EXTRA)
	#define VSSHADOWOUTPUT_EXTRA
#endif

struct VSIN //////////////////////////////////////////////////////
{
	#if !defined(GEO_NO_POSITION)
		float3 position : POSITION0;
	#endif

	#if GEO_HAS_COLOR
		float4 color : COLOR0;
	#endif

	#if GEO_HAS_TEXCOORD
		float2 texCoord : TEXCOORD;
	#endif

	#if GEO_HAS_TEXTANGENT
		float4 tangent : TEXTANGENT;
	#endif

	#if GEO_HAS_TEXBITANGENT
		float3 bitangent : TEXBITANGENT;
	#endif

	#if GEO_HAS_NORMAL
		float3 normal : NORMAL;
	#endif

	#if GEO_HAS_BONEWEIGHTS
		uint4 boneIndices : BONEINDICES;
		float4 boneWeights : BONEWEIGHTS;
	#endif

	#if GEO_HAS_PARTICLE_INPUTS
		float4 texCoordScale : TEXCOORDSCALE;
		float4 screenRot : PARTICLEROTATION;
		float4 blendTexCoord : TEXCOORD1;
		#define VSOUT_HAS_BLEND_TEXCOORD 1
	#endif

	#if GEO_HAS_VERTEX_ID
		uint vertexId : SV_VertexID;
	#endif
	
	#if GEO_HAS_INSTANCE_ID
		uint instanceId : SV_InstanceID;
	#endif

	#if GEO_HAS_PER_VERTEX_AO
		float ambientOcclusion : PER_VERTEX_AO;
	#endif

	VSINPUT_EXTRA
}; //////////////////////////////////////////////////////////////////

#if (SPAWNED_INSTANCE==1)
	#define GEO_HAS_INSTANCE_ID 1
	#if !defined(VSOUT_HAS_SHADOW_PROJECTION_COUNT)        // DavidJ -- HACK -- disabling this for shadow shaders
		#define PER_INSTANCE_MLO 1
	#endif
	#if (PER_INSTANCE_MLO==1)
		#define VSOUT_HAS_PER_VERTEX_MLO 1
	#endif
#endif

#if GEO_HAS_COLOR
		// vertex is used only in the vertex shader when
		// "MAT_VCOLOR_IS_ANIM_PARAM" is set. So, in this case,
		// don't output to further pipeline stages.
	#if MAT_VCOLOR_IS_ANIM_PARAM!=1 || VIS_ANIM_PARAM!=0
		#if !defined(VSOUT_HAS_COLOR_LINEAR)
			#if MAT_MODULATE_VERTEX_ALPHA
				#define VSOUT_HAS_COLOR_LINEAR 1
			#else
				#define VSOUT_HAS_COLOR_LINEAR 2
			#endif
		#endif
	#endif
#endif

#if GEO_HAS_TEXCOORD
	#if !defined(VSOUT_HAS_TEXCOORD)
		#define VSOUT_HAS_TEXCOORD 1
	#endif
#endif

#if GEO_HAS_TEXTANGENT
	#if RES_HAS_NormalsTexture
		#if defined(TANGENT_PROCESS_IN_PS) && TANGENT_PROCESS_IN_PS==1
			#if !defined(VSOUT_HAS_LOCAL_TANGENT_FRAME)
				#define VSOUT_HAS_LOCAL_TANGENT_FRAME 1
			#endif
		#else
			#if !defined(VSOUT_HAS_TANGENT_FRAME)
				#define VSOUT_HAS_TANGENT_FRAME 1
			#endif
		#endif
	#endif
#endif

#if GEO_HAS_NORMAL
	#if !defined(VSOUT_HAS_NORMAL)
		#define VSOUT_HAS_NORMAL 1
	#endif
#endif

#if GEO_HAS_PARTICLE_INPUTS
	#define VSOUT_HAS_BLEND_TEXCOORD 1
#endif

#if GEO_HAS_PER_VERTEX_AO
	#if !defined(VSOUT_HAS_PER_VERTEX_AO)
		#define VSOUT_HAS_PER_VERTEX_AO 1
	#endif
#endif

#if (MAT_DO_PARTICLE_LIGHTING==1) && GEO_HAS_TEXCOORD && RES_HAS_NormalsTexture
	#undef VSOUT_HAS_TANGENT_FRAME
	#define VSOUT_HAS_TANGENT_FRAME 1

	#if RES_HAS_CUSTOM_MAP
		#undef VSOUT_HAS_WORLD_VIEW_VECTOR
		#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
	#endif
#endif

#if GEO_HAS_COLOR ///////////////////////////////////////////////
	float4 VSIN_GetColor0(VSIN input) { return input.color; }
#else
	float4 VSIN_GetColor0(VSIN input) { return 1.0.xxxx; }
#endif //////////////////////////////////////////////////////////////

#if GEO_HAS_TEXCOORD /////////////////////////////////////////////
	float2 VSIN_GetTexCoord0(VSIN input) { return input.texCoord; }
#else
	float2 VSIN_GetTexCoord0(VSIN input) { return 0.0.xx; }
#endif //////////////////////////////////////////////////////////////

#if (GEO_HAS_NORMAL || GEO_HAS_TEXTANGENT) && (AUTO_COTANGENT==1)
	#undef VSOUT_HAS_TANGENT_FRAME
	#undef VSOUT_HAS_LOCAL_TANGENT_FRAME

		// Can do this in either local or world space -- set VSOUT_HAS_LOCAL_NORMAL & VSOUT_HAS_LOCAL_VIEW_VECTOR for normal space
	#define VSOUT_HAS_NORMAL 1
	#define VSOUT_HAS_WORLD_VIEW_VECTOR 1
#endif

#if MAT_REFLECTIVENESS
	#define VSOUT_HAS_WORLD_VIEW_VECTOR 1       // (need world view vector for the fresnel calculation)
#endif

#if MAT_BLEND_FOG
	#define VSOUT_HAS_FOG_COLOR 1
#endif

struct VSOUT /////////////////////////////////////////////////////
{
	float4 position : SV_Position;
	#if VSOUT_HAS_COLOR_LINEAR>=2
		float3 color : COLOR0;
	#elif VSOUT_HAS_COLOR_LINEAR
		float4 color : COLOR0;
	#endif

	#if VSOUT_HAS_TEXCOORD
		float2 texCoord : TEXCOORD0;
	#endif

	#if VSOUT_HAS_TANGENT_FRAME
		float3 tangent : TEXTANGENT;
		float3 bitangent : TEXBITANGENT;
	#endif

	#if VSOUT_HAS_LOCAL_TANGENT_FRAME
		float4 localTangent : LOCALTANGENT;
		float3 localBitangent : LOCALBITANGENT;
	#endif

	#if VSOUT_HAS_NORMAL
		float3 normal : NORMAL;
	#endif

	#if VSOUT_HAS_LOCAL_NORMAL
		float3 localNormal : LOCALNORMAL;
	#endif

	#if VSOUT_HAS_LOCAL_VIEW_VECTOR
		float3 localViewVector : LOCALVIEWVECTOR;
	#endif

	#if VSOUT_HAS_WORLD_VIEW_VECTOR
		float3 worldViewVector : WORLDVIEWVECTOR;
	#endif

	#if VSOUT_HAS_PRIMITIVE_ID
		nointerpolation uint primitiveId : SV_PrimitiveID;
	#endif

	#if VSOUT_HAS_RENDER_TARGET_INDEX
		nointerpolation uint renderTargetIndex : SV_RenderTargetArrayIndex;
	#endif

	#if VSOUT_HAS_WORLD_POSITION
		float3 worldPosition : WORLDPOSITION;
	#endif

	#if VSOUT_HAS_BLEND_TEXCOORD
		float3 blendTexCoord : TEXCOORD1;
	#endif

	#if VSOUT_HAS_FOG_COLOR
		float4 fogColor : FOGCOLOR;
	#endif

	#if VSOUT_HAS_PER_VERTEX_AO
		float ambientOcclusion : AMBIENTOCCLUSION;
	#endif

	#if VSOUT_HAS_PER_VERTEX_MLO
		float mainLightOcclusion : MAINLIGHTOCCLUSION;
	#endif

	#if VSOUT_HAS_INSTANCE_ID
		uint instanceId : SV_InstanceID;
	#endif

	VSOUTPUT_EXTRA
}; //////////////////////////////////////////////////////////////////

#endif
)--";

	TEST_CASE( "Utilities-FileRelevance", "[utility]" )
	{
		auto preprocAnalysis = GeneratePreprocessorAnalysisFromString(s_testFile);

		// We only care about AUTO_COTANGENT is GEO_HAS_NORMAL or GEO_HAS_TEXTANGENT
		auto autoCotangentRelevance = preprocAnalysis._relevanceTable[
			preprocAnalysis._tokenDictionary.GetToken(Utility::Internal::TokenDictionary::TokenType::Variable, "AUTO_COTANGENT")];

		auto expr = preprocAnalysis._tokenDictionary.AsString(autoCotangentRelevance);
		INFO(expr);

		ParameterBox env;
		const ParameterBox* envBoxes[] = { &env };
		REQUIRE(!preprocAnalysis._tokenDictionary.EvaluateExpression(autoCotangentRelevance, envBoxes));
		
		env.SetParameter("GEO_HAS_NORMAL", 1);
		REQUIRE(preprocAnalysis._tokenDictionary.EvaluateExpression(autoCotangentRelevance, envBoxes));

		env.SetParameter("GEO_HAS_NORMAL", 2);
		env.SetParameter("GEO_HAS_TEXTANGENT", 1);
		REQUIRE(preprocAnalysis._tokenDictionary.EvaluateExpression(autoCotangentRelevance, envBoxes));

		env.SetParameter("GEO_HAS_TEXTANGENT", "nothing");
		REQUIRE(!preprocAnalysis._tokenDictionary.EvaluateExpression(autoCotangentRelevance, envBoxes));
	}

	static std::string SimplifyExpression(StringSection<> input)
	{
		Utility::Internal::TokenDictionary dictionary;
		auto tokenExpr = Utility::Internal::AsExpressionTokenList(dictionary, input);
		dictionary.Simplify(tokenExpr);
		auto res = dictionary.AsString(tokenExpr);

		// Validate that if we parse in what we've written out, we'll get the same result again
		// (this is mostly to check order of operations rules)
		{
			auto reconversion = Utility::Internal::AsExpressionTokenList(dictionary, res);
			REQUIRE(tokenExpr == reconversion);
		}

		return res;
	}

	TEST_CASE( "Utilities-ExpressionSimplification", "[utility]" )
	{
		REQUIRE(SimplifyExpression("(A + B) * C") == "(A + B) * C");
		REQUIRE(SimplifyExpression("(A * B) + C") == "C + A * B");
		REQUIRE(SimplifyExpression("C * (A + B)") == "C * (A + B)");
		REQUIRE(SimplifyExpression("!A && C") == "C && !A");
		REQUIRE(SimplifyExpression("!(A && C)") == "!(A && C)");
		REQUIRE(SimplifyExpression("!A == C") == "C == !A");

		// We can simplify down many expressions just by identifying similar parts
		REQUIRE(SimplifyExpression("((A < B) || (B > A)) && ((B > A) || (A < B))") == "A < B");
		REQUIRE(SimplifyExpression("((A < B) || (C >= D)) && ((D <= C) || (B > A))") == "A < B || C >= D");
		REQUIRE(SimplifyExpression("!(A == B) || !(C < D) || !(E != (A&B))") == "E == (A & B) || (A != B || C >= D)");
	}
	
	TEST_CASE( "Utilities-ConditionalPreprocessingTest", "[utility]" )
	{
		const char* input = R"--(
			Token0 Token1
			#if SELECTOR_0 || SELECTOR_1
				#if SELECTOR_2
					Token2
				#endif
				Token3
			#endif
		)--";

		ConditionalProcessingTokenizer tokenizer(input);

		REQUIRE(std::string("Token0") == tokenizer.GetNextToken()._value.AsString());
		REQUIRE(std::string("") == tokenizer._preprocessorContext.GetCurrentConditionString());

		REQUIRE(std::string("Token1") == tokenizer.GetNextToken()._value.AsString());
		REQUIRE(std::string("") == tokenizer._preprocessorContext.GetCurrentConditionString());

		REQUIRE(std::string("Token2") == tokenizer.GetNextToken()._value.AsString());
		REQUIRE(std::string("(SELECTOR_2) && (SELECTOR_0 || SELECTOR_1)") == tokenizer._preprocessorContext.GetCurrentConditionString());

		REQUIRE(std::string("Token3") == tokenizer.GetNextToken()._value.AsString());
		REQUIRE(std::string("(SELECTOR_0 || SELECTOR_1)") == tokenizer._preprocessorContext.GetCurrentConditionString());

		REQUIRE(tokenizer.PeekNextToken()._value.IsEmpty());
	}
}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderLangUtil.h"
#include "UniformsStream.h"
#include "../Utility/FastParseValue.h"
#include <algorithm>

namespace RenderCore
{
	static std::pair<StringSection<char>, ImpliedTyping::TypeCat> s_baseTypes[] = 
	{
		{ "float", ImpliedTyping::TypeCat::Float },
		{ "uint", ImpliedTyping::TypeCat::UInt32 },
		{ "dword", ImpliedTyping::TypeCat::UInt32 },
		{ "int", ImpliedTyping::TypeCat::Int32 },
		{ "byte", ImpliedTyping::TypeCat::UInt8 },
		{ "bool", ImpliedTyping::TypeCat::Bool },
		{ "vec", ImpliedTyping::TypeCat::Float },          // GLSL-style naming
		{ "ivec", ImpliedTyping::TypeCat::Int32 },
		{ "uvec", ImpliedTyping::TypeCat::UInt32 }
		// "half", "double" not supported
	};

	ImpliedTyping::TypeDesc ShaderLangTypeNameAsTypeDesc(StringSection<char> hlslTypeName)
	{
			// Note that HLSL type names are not case sensitive!
			//  see "Keywords" in the HLSL docs
		using namespace ImpliedTyping;
		for (unsigned c=0; c<dimof(s_baseTypes); ++c) {
			auto len = s_baseTypes[c].first.Length();
			if (hlslTypeName.Length() >= len
				&& !XlComparePrefixI(s_baseTypes[c].first.begin(), hlslTypeName.begin(), len)) {

				auto matrixMarker = hlslTypeName.begin() + len;
				while (matrixMarker != hlslTypeName.end() && *matrixMarker != 'x') ++matrixMarker;
				if (matrixMarker != hlslTypeName.end()) {
					unsigned count0 = 1, count1 = 1;
					auto* endCount0 = FastParseValue(MakeStringSection(&hlslTypeName[len], matrixMarker), count0);
					auto* endCount1 = FastParseValue(MakeStringSection(matrixMarker+1, hlslTypeName.end()), count1);
					assert(endCount0 == matrixMarker && endCount1 == hlslTypeName.end());

					TypeDesc result;
					result._arrayCount = (uint16)std::max(1u, count0 * count1);
					result._type = s_baseTypes[c].second;
					result._typeHint = TypeHint::Matrix;
					return result;
				} else {
					unsigned count = 1;
					auto* endCountOpt = FastParseValue(MakeStringSection(hlslTypeName.begin() + len, hlslTypeName.end()), count);
					assert(endCountOpt == hlslTypeName.end());
					if (count == 0 || count > 4) count = 1;
					TypeDesc result;
					result._arrayCount = (uint16)count;
					result._type = s_baseTypes[c].second;
					result._typeHint = (count > 1) ? TypeHint::Vector : TypeHint::None;
					return result;
				}
			}
		}

		if (XlEqString(hlslTypeName, "mat4")) {
			return TypeDesc { ImpliedTyping::TypeCat::Float, 16, ImpliedTyping::TypeHint::Matrix };
		} else if (XlEqString(hlslTypeName, "mat3")) {
			return TypeDesc { ImpliedTyping::TypeCat::Float, 9, ImpliedTyping::TypeHint::Matrix };
		}

		return TypeDesc{TypeCat::Void, 0};
	}

	std::string AsShaderLangTypeName(const ImpliedTyping::TypeDesc& type, ShaderLanguage language)
	{
		// only HLSL style names supported currently
		assert(language == ShaderLanguage::HLSL);

		const char* baseName = nullptr;
		using namespace ImpliedTyping;
		switch (type._type) {
		case TypeCat::Bool: baseName = "bool"; break;
		case TypeCat::Int8: baseName = "int"; break;
		case TypeCat::UInt8: baseName = "byte"; break;
		case TypeCat::Int16: baseName = "int"; break;
		case TypeCat::UInt16: baseName = "uint"; break;
		case TypeCat::Int32: baseName = "int"; break;
		case TypeCat::UInt32: baseName = "uint"; break;
		case TypeCat::Int64: baseName = "uint"; break;
		case TypeCat::UInt64: baseName = "uint"; break;
		case TypeCat::Float: baseName = "float"; break;
		case TypeCat::Double: baseName = "float"; break;
		default:
		case TypeCat::Void: return "<<unknown type>>";
		}

		if (type._typeHint == TypeHint::Matrix) {
			if (type._arrayCount == 16)
				return baseName + std::string("4x4");
			if (type._arrayCount == 12)
				return baseName + std::string("3x4");
			if (type._arrayCount == 9)
				return baseName + std::string("3x3");
		}

		if (type._arrayCount <= 1)
			return baseName;
		return baseName + std::to_string(type._arrayCount);
	}

	DescriptorType ShaderLangTypeNameAsDescriptorType(StringSection<> type)
	{
		// HLSL keywords are not case sensitive. We could assume that
		// a type name that does not begin with one of the scalar type
		// prefixes is a global resource (like a texture, etc). This
		// should provide some flexibility with new DX12 types, and perhaps
		// allow for manipulating custom "interface" types...?
		//
		// However, that isn't going to work well with struct types (which
		// can be contained with cbuffers and be passed to functions like
		// scalars)
		//
		// const char* scalarTypePrefixes[] =
		// {
		//     "bool", "int", "uint", "half", "float", "double"
		// };
		//
		// Note that we only check the prefix -- to catch variations on
		// texture and RWTexture (and <> arguments like Texture2D<float4>)

		// HLSL names:
		// cbuffer, tbuffer,
		// StructuredBuffer, Buffer, ByteAddressBuffer, AppendStructuredBuffer,
		// RWBuffer, RWByteAddressBuffer, RWStructuredBuffer,
		//
		// RWTexture, RWTexture1D, RWTexture1DArray, RWTexture2D, RWTexture2DArray, RWTexture3D
		// texture, Texture1D, Texture1DArray, Texture2D, Texture2DArray, Texture2DMS, Texture2DMSArray, Texture3D, TextureCube, TextureCubeArray
		//
		// sampler, SamplerState, SamplerComparisonState
		//
		// special .fx file types:
		// BlendState, DepthStencilState, DepthStencilView, RasterizerState, RenderTargetView,
		// note -- some really special function signature-only: InputPatch, OutputPatch, LineStream, TriangleStream, PointStream

		if (XlBeginsWithI(type, "texture"))
			return DescriptorType::SampledTexture;

		if (XlBeginsWithI(type, "rwtexture"))
			return DescriptorType::UnorderedAccessTexture;

		if (XlEqStringI(type, "cbuffer"))
			return DescriptorType::UniformBuffer;

		if (XlEqStringI(type, "tbuffer") || XlEqStringI(type, "Buffer"))
			return DescriptorType::UniformTexelBuffer;

		if (XlEqStringI(type, "RWBuffer"))
			return DescriptorType::UnorderedAccessTexelBuffer;

		const char* bufferTypes[] {
			"StructuredBuffer", "ByteAddressBuffer", "AppendStructuredBuffer",
			"RWByteAddressBuffer", "RWStructuredBuffer"
		};

		for (unsigned c=0; c<dimof(bufferTypes); ++c)
			if (XlBeginsWithI(type, MakeStringSection(bufferTypes[c])))
				return DescriptorType::UnorderedAccessBuffer;
		
		if (XlBeginsWithI(type, "sampler"))
			return DescriptorType::Sampler;

		return DescriptorType::Empty;
	}
}



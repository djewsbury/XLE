// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

namespace UnitTests
{
	#define HLSLPrefix R"(
            int fakeMod(int lhs, int rhs)
            {
                // only valid for positive values
                float A = float(lhs) / float(rhs);
                return int((A - floor(A)) * float(rhs));
            }
        )"

    static const char vsText_clipInput[] = 
		HLSLPrefix
        R"(
            void main(float4 position : position, float4 color : color, out float4 a_color : COLOR0, out float4 a_position : SV_Position)
            {
                a_position = position;
                a_color = color;
            }
        )";

    static const char vsText_clipInputTransform[] = 
        HLSLPrefix
		R"(
            cbuffer Transform
            {
                float4x4 inputToClip;
            }

            void main(float4 position : position, float4 color : color, out float4 a_color : COLOR0, out float4 a_position : SV_Position)
            {
                a_position = transpose(inputToClip) * position;
                a_color = color;
            }
        )";

    static const char vsText[] = 
        HLSLPrefix
		R"(
            void main(int2 position : position, float4 color : color, out float4 a_color : COLOR0, out float4 a_position : SV_Position)
            {
                a_position.x = (position.x / 1024.0) * 2.0 - 1.0;
                a_position.y = (position.y / 1024.0) * 2.0 - 1.0;
                a_position.zw = float2(0.0, 1.0);
                a_color = color;
            }
        )";

    static const char vsText_Instanced[] =
        HLSLPrefix
		R"(
            void main(int2 position : position, float4 color : color, int2 instanceOffset : instanceOffset, out float4 a_color : COLOR0, out float4 a_position : SV_Position)
            {
                a_position.x = ((position.x + instanceOffset.x) / 1024.0) * 2.0 - 1.0;
                a_position.y = ((position.y + instanceOffset.y) / 1024.0) * 2.0 - 1.0;
                a_position.zw = float2(0.0, 1.0);
                a_color = color;
            }
        )";

    static const char vsText_FullViewport[] =
        HLSLPrefix
		R"(
            void main(uint in_vertexID : SV_VertexID, out float2 a_texCoord : TEXCOORD, out float4 a_position : SV_Position)
            {
                a_texCoord = float2(
                    (fakeMod(in_vertexID, 2) == 1)     ? 0.0f :  1.0f,
                    (fakeMod(in_vertexID/2, 2) == 1) ? 0.0f :  1.0f);
                a_position = float4(
                    a_texCoord.x *  2.0f - 1.0f,
                    a_texCoord.y * -2.0f + 1.0f,		// (note -- there's a flip here relative OGLES & Apple Metal)
                    0.0, 1.0
                );
                #if GFXAPI_TARGET == GFXAPI_VULKAN
                    a_texCoord.y = 1.0f - a_texCoord.y;     // todo; more consistency around this flip
                #endif
            }
        )";

    static const char vsText_FullViewport2[] =
        HLSLPrefix
		R"(
            void main(int vertexID : vertexID, out float2 a_texCoord : TEXCOORD, out float4 a_position : SV_Position)
            {
                int in_vertexID = int(vertexID);
                a_texCoord = float2(
                    (fakeMod(in_vertexID, 2) == 1)     ? 0.0f :  1.0f,
                    (fakeMod(in_vertexID/2, 2) == 1) ? 0.0f :  1.0f);
                a_position = float4(
                    a_texCoord.x *  2.0f - 1.0f,
                    a_texCoord.y *  -2.0f + 1.0f,		// (note -- there's a flip here relative OGLES & Apple Metal)
                    0.0, 1.0
                );
                #if GFXAPI_TARGET == GFXAPI_VULKAN
                    a_texCoord.y = 1.0f - a_texCoord.y;     // todo; more consistency around this flip
                #endif
            }
        )";

    static const char psText[] = 
        HLSLPrefix
		R"(
            float4 main(float4 a_color : COLOR0) : SV_Target0
            {
                return a_color;
            }
        )";

    static const char psText_Uniforms[] =
        HLSLPrefix
		R"(
            cbuffer Values : register(b3, space0)
            {
                float A, B, C;
                float4 vA;
            }

            float4 main() : SV_Target0
            {
                return float4(A, B, vA.x, vA.y);
            }
        )";

    static const char psText_TextureBinding[] = 
        HLSLPrefix
		R"(
            Texture2D Texture : register(t0, space0);
            SamplerState Texture_sampler : register(s5, space0);
            float4 main(float2 a_texCoord : TEXCOORD) : SV_Target0
            {
                return Texture.Sample(Texture_sampler, a_texCoord);
            }
        )";

    static const char psText_LegacyBindings[] = 
        HLSLPrefix
		R"(
            Texture2D<uint> Texture0 : register(t5);
            Texture2D<uint> Texture1 : register(t6);
            cbuffer TestBuffer : register(b9)
            {
                float3 InputA;
			    float InputB;
            };
            float4 main(float2 a_texCoord : TEXCOORD) : SV_Target0
            {
                uint4 t0 = Texture0.Load(int3(3,3,0));
                uint4 t1 = Texture1.Load(int3(4,4,0));
                bool success = 
                    t0 == uint4(7,3,5,9)
                    && t1 == uint4(10, 45, 99, 23)
                    && InputA == float3(1,0,1)
                    && InputB == 8
                    ;

                if (success) {
                    return float4(0, 1, 0, 1);
                } else {
                    return float4(1, 0, 0, 1);
                }
            }
        )";
}

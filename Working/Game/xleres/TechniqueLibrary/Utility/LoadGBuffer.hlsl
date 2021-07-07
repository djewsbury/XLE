// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(LOAD_GBUFFER_H)
#define LOAD_GBUFFER_H

#include "../Framework/gbuffer.hlsl"
#include "../Math/TextureAlgorithm.hlsl"

#if !defined(VULKAN)
    #define GBUFFER_SHADER_RESOURCE 1
#endif

#if defined(GBUFFER_SHADER_RESOURCE) /////////////////////////////////////////

    Texture2D_MaybeMS<float4>		GBuffer_Diffuse		BIND_SEQ_T6;
    Texture2D_MaybeMS<float4>		GBuffer_Normals		BIND_SEQ_T7;
    #if HAS_PROPERTIES_BUFFER==1
        Texture2D_MaybeMS<float4>	GBuffer_Parameters	BIND_SEQ_T8;
    #endif

    GBufferValues LoadGBuffer(float2 position, SystemInputs sys)
    {
        int2 pixelCoord = position.xy;

        #if SHADER_NODE_EDITOR==1
                //      In the shader node editor, the gbuffer textures might not be
                //      the same dimensions as the output viewport. So we need to resample.
                //      Just do basic point resampling (otherwise normal filtering, etc,
                //      could get really complex)
            uint2 textureDims;
            GBuffer_Diffuse.GetDimensions(textureDims.x, textureDims.y);
            pixelCoord = textureDims * pixelCoord / NodeEditor_GetOutputDimensions();
        #endif

        GBufferEncoded encoded;
        encoded.diffuseBuffer = LoadFloat4(GBuffer_Diffuse, pixelCoord, GetSampleIndex(sys));
        encoded.normalBuffer = LoadFloat4(GBuffer_Normals, pixelCoord, GetSampleIndex(sys));
        #if HAS_PROPERTIES_BUFFER==1
            encoded.propertiesBuffer = LoadFloat4(GBuffer_Parameters, pixelCoord, GetSampleIndex(sys));
        #endif
        return Decode(encoded);
    }

#else //////////////////////////////////////////////////////////////////////////////////

    [[vk::input_attachment_index(0)]] SubpassInput<float4> GBuffer_Diffuse BIND_SEQ_T6;
    [[vk::input_attachment_index(1)]] SubpassInput<float4> GBuffer_Normals BIND_SEQ_T7;
    #if HAS_PROPERTIES_BUFFER==1
        [[vk::input_attachment_index(2)]] SubpassInput<float4> GBuffer_Parameters BIND_SEQ_T8;
    #endif

    GBufferValues LoadGBuffer(float2 position, SystemInputs sys)
    {
        GBufferEncoded encoded;
        encoded.diffuseBuffer = GBuffer_Diffuse.SubpassLoad();
        encoded.normalBuffer = GBuffer_Normals.SubpassLoad();
        #if HAS_PROPERTIES_BUFFER==1
            encoded.propertiesBuffer = GBuffer_Parameters.SubpassLoad();
        #endif
        return Decode(encoded);
    }

#endif //////////////////////////////////////////////////////////////////////////////////

#endif

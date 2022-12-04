// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if !defined(BINDING_H)
#define BINDING_H

#pragma selector_filtering(push_disable)

//
// Simplified descriptor set arrangement:
//      "sequencer" -- descriptors that change very infrequently, mostly constant across the entire frame
//      "material" -- descriptors initialized at load time (typically related to object material properties and fixed over many frames)
//      "numeric" -- per-draw call or descriptors that don't fit into other categories
//

#define BIND_SEQ_B0 : register(b0, space1)
#define BIND_SEQ_B1 : register(b1, space1)
#define BIND_SEQ_B2 : register(b2, space1)
#define BIND_SEQ_B3 : register(b3, space1)
#define BIND_SEQ_B4 : register(b4, space1)
#define BIND_SEQ_B5 : register(b5, space1)

#define BIND_SEQ_S0 : register(s11, space1)
#define BIND_SEQ_S1 : register(s12, space1)
#define BIND_SEQ_S2 : register(s13, space1)
#define BIND_SEQ_S3 : register(s14, space1)

#define BIND_SEQ_T6 : register(t6, space1)
#define BIND_SEQ_T7 : register(t7, space1)
#define BIND_SEQ_T8 : register(t8, space1)
#define BIND_SEQ_T9 : register(t9, space1)
#define BIND_SEQ_T10 : register(t10, space1)

///////////////////////////////////////////////////////////////////////////////////////////////////

#define BIND_MAT_B0 : register(b0, space2)

#define BIND_MAT_T1 : register(t1, space2)
#define BIND_MAT_T2 : register(t2, space2)
#define BIND_MAT_T3 : register(t3, space2)
#define BIND_MAT_T4 : register(t4, space2)
#define BIND_MAT_T5 : register(t5, space2)
#define BIND_MAT_T6 : register(t6, space2)
#define BIND_MAT_T7 : register(t7, space2)
#define BIND_MAT_T8 : register(t8, space2)
#define BIND_MAT_T9 : register(t9, space2)
#define BIND_MAT_T10 : register(t10, space2)
#define BIND_MAT_T11 : register(t11, space2)
#define BIND_MAT_T12 : register(t12, space2)
#define BIND_MAT_T13 : register(t13, space2)

#define BIND_MAT_S1 : register(s1, space2)
#define BIND_MAT_S2 : register(s2, space2)
#define BIND_MAT_S3 : register(s3, space2)
#define BIND_MAT_S4 : register(s4, space2)
#define BIND_MAT_S5 : register(s5, space2)
#define BIND_MAT_S6 : register(s6, space2)
#define BIND_MAT_S7 : register(s7, space2)
#define BIND_MAT_S8 : register(s8, space2)
#define BIND_MAT_S9 : register(s9, space2)
#define BIND_MAT_S10 : register(s10, space2)
#define BIND_MAT_S11 : register(s11, space2)
#define BIND_MAT_S12 : register(s12, space2)
#define BIND_MAT_S13 : register(s13, space2)

#define BIND_MAT_U1 : register(u1, space2)
#define BIND_MAT_U2 : register(u2, space2)
#define BIND_MAT_U3 : register(u3, space2)
#define BIND_MAT_U4 : register(u4, space2)
#define BIND_MAT_U5 : register(u5, space2)
#define BIND_MAT_U6 : register(u6, space2)
#define BIND_MAT_U7 : register(u7, space2)
#define BIND_MAT_U8 : register(u8, space2)
#define BIND_MAT_U9 : register(u9, space2)
#define BIND_MAT_U10 : register(u10, space2)
#define BIND_MAT_U11 : register(u11, space2)
#define BIND_MAT_U12 : register(u12, space2)
#define BIND_MAT_U13 : register(u13, space2)

///////////////////////////////////////////////////////////////////////////////////////////////////

#define BIND_NUMERIC_T0 : register(t0, space0)
#define BIND_NUMERIC_T1 : register(t1, space0)
#define BIND_NUMERIC_T2 : register(t2, space0)

#define BIND_NUMERIC_B3 : register(b3, space0)
#define BIND_NUMERIC_B4 : register(b4, space0)

///////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(LIGHT_RESOLVE_SHADER)
    // Light operator for deferred
    #define BIND_SHADOW_B0 : register(b0, space2)
    #define BIND_SHADOW_B1 : register(b1, space2)
    #define BIND_SHADOW_B2 : register(b2, space2)

    #define BIND_SHADOW_T3 : register(t3, space2)
    #define BIND_SHADOW_T4 : register(t4, space2)
    #define BIND_SHADOW_T5 : register(t5, space2)

    #define BIND_SHARED_LIGHTING_B0 : register(b0, space3)
    #define BIND_SHARED_LIGHTING_T1 : register(t1, space3)

    #define BIND_SHARED_LIGHTING_S2 : register(s2, space3)
    #define BIND_SHARED_LIGHTING_S3 : register(s3, space3)
#else
    // Forward plus style lighting
    #define BIND_SHADOW_B0 : register(b0, space4)
    #define BIND_SHADOW_B1 : register(b1, space4)
    #define BIND_SHADOW_B2 : register(b2, space4)

    #define BIND_SHADOW_T3 : register(t3, space4)
    #define BIND_SHADOW_T4 : register(t4, space4)
    #define BIND_SHADOW_T5 : register(t5, space4)

    // Here, BIND_SHARED_LIGHTING_* map onto the pipeline in forward.pipeline
    #define BIND_SHARED_LIGHTING_T1 : register(t8, space3)
    #define BIND_SHARED_LIGHTING_S2 : register(s9, space3)
    #define BIND_SHARED_LIGHTING_S3 : register(s10, space3)
#endif

#pragma selector_filtering(pop)

// We need to use this selector outside of the "pragma selector_filtering" to ensure that
// that it's considered relevant
#if defined(LIGHT_RESOLVE_SHADER)
#endif

#endif

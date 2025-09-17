// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueUtils.h"
#include "DrawableDelegates.h"
#include "CommonBindings.h"
#include "../Types.h"
#include "../Metal/Metal.h"       // for GFXAPI_TARGET define used below
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
    Float3 NegativeLightDirection    = Normalize(Float3(0.f, 1.0f, 1.f));

    Float4x4 Projection(const CameraDesc& sceneCamera, float viewportAspect)
    {
        if (sceneCamera._projection == CameraDesc::Projection::Orthogonal) {
            return OrthogonalProjection(
                sceneCamera._left, sceneCamera._top, 
                sceneCamera._right, sceneCamera._bottom, 
                sceneCamera._nearClip, sceneCamera._farClip,
                GeometricCoordinateSpace::RightHanded, 
                GetDefaultClipSpaceType());
        } else {
            return PerspectiveProjection(
                sceneCamera._verticalFieldOfView, viewportAspect,
                sceneCamera._nearClip, sceneCamera._farClip, 
                GeometricCoordinateSpace::RightHanded, 
                GetDefaultClipSpaceType());
        }
    }

    ClipSpaceType GetDefaultClipSpaceType()
    {
            // (todo -- this condition could be a runtime test)
        #if (GFXAPI_TARGET == GFXAPI_DX11) || (GFXAPI_TARGET == GFXAPI_DX9) || (GFXAPI_TARGET == GFXAPI_APPLEMETAL)
            return ClipSpaceType::Positive_ReverseZ;
        #elif (GFXAPI_TARGET == GFXAPI_VULKAN)
            return ClipSpaceType::PositiveRightHanded_ReverseZ;
        #else
            return ClipSpaceType::StraddlingZero;
        #endif
    }

	ShaderLanguage GetDefaultShaderLanguage()
	{
		#if (GFXAPI_TARGET == GFXAPI_DX11) || (GFXAPI_TARGET == GFXAPI_DX9)
            return ShaderLanguage::HLSL;
        #elif (GFXAPI_TARGET == GFXAPI_VULKAN)
            return ShaderLanguage::HLSL;		// by default, we use HLSL for Vulkan; but of course GLSL could work as well
		#elif (GFXAPI_TARGET == GFXAPI_OPENGLES)
			return ShaderLanguage::GLSL;
		#elif (GFXAPI_TARGET == GFXAPI_APPLEMETAL)
            return ShaderLanguage::MetalShaderLanguage;
        #else
            #error No GFX API selected
        #endif
	}

    // this must correspond to GetDefaultClipSpaceType()
    // ReverseZ are 1.f -> 0.f, otherwise
    // 0.f -> 1.f
    #if (GFXAPI_TARGET == GFXAPI_DX11) || (GFXAPI_TARGET == GFXAPI_DX9) || (GFXAPI_TARGET == GFXAPI_APPLEMETAL) || (GFXAPI_TARGET == GFXAPI_VULKAN)
        float g_NDCDepthAtNearClip = 1.f;
        float g_NDCDepthAtFarClip = 0.f;
    #else
        float g_NDCDepthAtNearClip = 0.f;
        float g_NDCDepthAtFarClip = 1.f;
    #endif

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, const CameraDesc& sceneCamera, 
        const std::pair<Float2, Float2>& viewport)
    {
            // calculate proper worldToProjection for this cameraDesc and viewport
            //      -- then get the frustum corners. We can use these to find the
            //          correct direction from the view position under the given 
            //          mouse position
        Float3 frustumCorners[8];
        const float viewportAspect = (viewport.second[0] - viewport.first[0]) / float(viewport.second[1] - viewport.first[1]);
        auto projectionMatrix = Projection(sceneCamera, viewportAspect);

        auto worldToProjection = Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), projectionMatrix);
        CalculateAbsFrustumCorners(frustumCorners, worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());

        return XLEMath::BuildRayUnderCursor(mousePosition, frustumCorners, viewport);
    }

    std::pair<Float3, Float3> BuildRayUnderCursor(
        Int2 mousePosition, const ProjectionDesc& projDesc,
        const std::pair<Float2, Float2>& viewport)
    {
        Float3 frustumCorners[8];
        CalculateAbsFrustumCorners(frustumCorners, projDesc._worldToProjection, RenderCore::Techniques::GetDefaultClipSpaceType());
        return XLEMath::BuildRayUnderCursor(mousePosition, frustumCorners, viewport);
    }

    ProjectionDesc::ProjectionDesc()
    {
		if ((size_t(this) % 16) != 0) {
            Throw(std::runtime_error("Expecting aligned type"));
        }

        _worldToProjection = Identity<Float4x4>();
        _cameraToProjection = Identity<Float4x4>();
        _cameraToWorld = Identity<Float4x4>();
        _verticalFov = 0.f;
        _aspectRatio = 0.f;
        _nearClip = 0.f;
        _farClip = 0.f;
    }

	#pragma push_macro("new")
	#undef new

    void* ProjectionDesc::operator new(size_t size)
	{
		return XlMemAlign(size, 16);
	}

	#pragma pop_macro("new")

	void ProjectionDesc::operator delete(void* ptr)
	{
		XlMemAlignFree(ptr);
	}

    GlobalTransformConstants BuildGlobalTransformConstants(const ProjectionDesc& projDesc)
    {
        GlobalTransformConstants globalTransform;
        globalTransform._worldToClip = projDesc._worldToProjection;
        globalTransform._viewToWorld = projDesc._cameraToWorld;
        globalTransform._worldSpaceView = ExtractTranslation(projDesc._cameraToWorld);
        globalTransform._minimalProjection = ExtractMinimalProjection(projDesc._cameraToProjection);
        if (projDesc._nearClip == 0.f && projDesc._farClip == 0.f) {  // bitwise compare intended
            globalTransform._farClip = 0;   // degenerate case, near and far clip haven't been configured
        } else if (IsOrthogonalProjection(projDesc._cameraToProjection)) {
            globalTransform._farClip = CalculateNearAndFarPlane_Ortho(globalTransform._minimalProjection, GetDefaultClipSpaceType()).second;
            assert(globalTransform._farClip > 0.f);
            globalTransform._farClip = -globalTransform._farClip;       // we use negative far clip as a flag for orthogonal projection
        } else {
            globalTransform._farClip = CalculateNearAndFarPlane(globalTransform._minimalProjection, GetDefaultClipSpaceType()).second;
            assert(globalTransform._farClip > 0.f);
        }
        globalTransform._prevWorldToClip = globalTransform._worldToClip;

            //  We can calculate the projection corners either from the camera to world,
            //  transform or from the final world-to-clip transform. Let's try to pick 
            //  the method that gives the most accurate results.
            //
            //  Using the world to clip matrix should be the most reliable, because it 
            //  will most likely agree with the shader results. The shaders only use 
            //  cameraToWorld occasionally, but WorldToClip is an important part of the
            //  pipeline.

        enum FrustumCornersMode { FromWorldToClip, FromCameraToWorld };
        const FrustumCornersMode cornersMode = FromWorldToClip;

        if (constant_expression<cornersMode == FromWorldToClip>::result()) {

            Float3 absFrustumCorners[8];
            CalculateAbsFrustumCorners(absFrustumCorners, globalTransform._worldToClip, RenderCore::Techniques::GetDefaultClipSpaceType());
            for (unsigned c=0; c<4; ++c) {
                globalTransform._frustumCorners[c] = 
                    Expand(Float3(absFrustumCorners[4+c] - globalTransform._worldSpaceView), 1.f);
            }

        } else if (constant_expression<cornersMode == FromCameraToWorld>::result()) {

                //
                //      "transform._frustumCorners" should be the world offsets of the corners of the frustum
                //      from the camera position.
                //
                //      Camera coords:
                //          Forward:    -Z
                //          Up:         +Y
                //          Right:      +X
                //
            const float top = projDesc._nearClip * XlTan(.5f * projDesc._verticalFov);
            const float right = top * projDesc._aspectRatio;
            Float3 preTransformCorners[] = {
                Float3(-right,  top, -projDesc._nearClip),
                Float3(-right, -top, -projDesc._nearClip),
                Float3( right,  top, -projDesc._nearClip),
                Float3( right, -top, -projDesc._nearClip) 
            };
            float scale = projDesc._farClip / projDesc._nearClip;
            for (unsigned c=0; c<4; ++c) {
                globalTransform._frustumCorners[c] = 
                    Expand(Float3(TransformDirectionVector(projDesc._cameraToWorld, preTransformCorners[c]) * scale), 1.f);
            }
        }

        return globalTransform;
    }

    GlobalTransformConstants BuildGlobalTransformConstants(const ProjectionDesc& projDesc, const ProjectionDesc& prevProjDesc)
    {
        auto result = BuildGlobalTransformConstants(projDesc);
        result._prevWorldToClip = prevProjDesc._worldToProjection;
        // Expecting "jitter" placed on the projection for TAA to be replicated to the prev proj desc
        assert(projDesc._cameraToProjection(0, 2) == prevProjDesc._cameraToProjection(0, 2));
        assert(projDesc._cameraToProjection(1, 2) == prevProjDesc._cameraToProjection(1, 2));
        return result;
    }

    ViewportConstants BuildViewportConstants(const ViewportDesc& viewport)
    {
        return ViewportConstants { 
            Float2{1.f/float(viewport._width), 1.f/float(viewport._height)}, 
            Float2{viewport._x, viewport._y},
            Float2{viewport._width, viewport._height},
            Float2{viewport._x+.5f*viewport._width, viewport._y+.5f*viewport._height},
            Float2{.5f*viewport._width, .5f*viewport._height},
            {0, 0} };
    }

    SharedPkt MakeLocalTransformPacket(const Float4x4& localToWorld, const CameraDesc& camera)
    {
        return MakeLocalTransformPacket(localToWorld, ExtractTranslation(camera._cameraToWorld));
    }

    LocalTransformConstants MakeLocalTransform(const Float4x4& localToWorld, const Float3& worldSpaceCameraPosition, uint32_t viewMask)
    {
        LocalTransformConstants localTransform;
        CopyTransform(localTransform._localToWorld, localToWorld);
        // note; disabled because many local-to-world transforms have scales, and shaders aren't reading this very frequently, anyway  
        // localTransform._localSpaceView = TransformPointByOrthonormalInverse(localToWorld, worldSpaceCameraPosition);
        localTransform._localSpaceView = Float3{0,0,0};
        localTransform._viewMask = viewMask;
        return localTransform;
    }

    SharedPkt MakeLocalTransformPacket(const Float4x4& localToWorld, const Float3& worldSpaceCameraPosition)
    {
        return MakeSharedPkt(MakeLocalTransform(localToWorld, worldSpaceCameraPosition));
    }

    bool HasHandinessFlip(const ProjectionDesc& projDesc)
    {
        float det = Determinant(Truncate3x3(projDesc._worldToProjection));
        return det > 0.0f;
    }

	IteratorRange<const ConstantBufferElementDesc*> IUniformBufferDelegate::GetLayout() { return {}; }

    void IShaderResourceDelegate::WriteResourceViews(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) { assert(0); }
    void IShaderResourceDelegate::WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst) { assert(0); }
    void IShaderResourceDelegate::WriteImmediateData(ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) { assert(0); }
    size_t IShaderResourceDelegate::GetImmediateDataSize(ParsingContext& context, const void* objectContext, unsigned idx) { return 0; }

    void IShaderResourceDelegate::BindResourceView(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements)
    {
        _interface.BindResourceView(slot, hashName, cbElements);
    }
    void IShaderResourceDelegate::BindImmediateData(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements)
    {
        _interface.BindImmediateData(slot, hashName, cbElements);
    }
    void IShaderResourceDelegate::BindSampler(unsigned slot, uint64_t hashName)
    {
        _interface.BindSampler(slot, hashName);
    }

	IUniformBufferDelegate::~IUniformBufferDelegate() {}
	IShaderResourceDelegate::~IShaderResourceDelegate() {}
    IUniformDelegateManager::~IUniformDelegateManager() {}

	ProjectionDesc BuildProjectionDesc(const CameraDesc& sceneCamera, float viewportAspect)
    {
        auto cameraToProjection = Techniques::Projection(sceneCamera, viewportAspect);

        RenderCore::Techniques::ProjectionDesc projDesc;
        projDesc._verticalFov = sceneCamera._verticalFieldOfView;
        projDesc._aspectRatio = viewportAspect;
        projDesc._nearClip = sceneCamera._nearClip;
        projDesc._farClip = sceneCamera._farClip;
        projDesc._worldToProjection = Combine(InvertOrthonormalTransform(sceneCamera._cameraToWorld), cameraToProjection);
        projDesc._cameraToProjection = cameraToProjection;
        projDesc._cameraToWorld = sceneCamera._cameraToWorld;
        return projDesc;
    }

    ProjectionDesc BuildOrthogonalProjectionDesc(
        const Float4x4& cameraToWorld,
        float l, float t, float r, float b,
        float nearClip, float farClip)
    {
        auto cameraToProjection = OrthogonalProjection(l, t, r, b, nearClip, farClip, Techniques::GetDefaultClipSpaceType());

        RenderCore::Techniques::ProjectionDesc projDesc;
        projDesc._verticalFov = 0.f;
        projDesc._aspectRatio = 1.f;
        projDesc._nearClip = nearClip;
        projDesc._farClip = farClip;
        projDesc._worldToProjection = Combine(InvertOrthonormalTransform(cameraToWorld), cameraToProjection);
        projDesc._cameraToProjection = cameraToProjection;
        projDesc._cameraToWorld = cameraToWorld;
        return projDesc;
    }
    
    GeometricCoordinateSpace GetGeometricCoordinateSpaceForCubemaps()
    {
        return GeometricCoordinateSpace::LeftHanded;
    }

    ProjectionDesc BuildCubemapProjectionDesc(unsigned cubeFace, Float3 centerLocation, float nearClip, float farClip, ClipSpaceType clipSpaceType)
    {
        // Slightly awkward here -- because we usually want to query the final cubemaps in world space
        // we need to follow the GFX API's cubemap specifications very closely. For Vulkan, that requires
        // setting our geometric coordinate space to left handed, rather than our typical right handed
        // This will correspondingly flip face winding
        // See Vulkan spec "16.5.4. Cube Map Face Selection" for Vulkan's rules for querying a cubemap texture
        // with a 3d vector input
        auto m = CubemapViewAndProjection(
            cubeFace, centerLocation, nearClip, farClip,
            GetGeometricCoordinateSpaceForCubemaps(),
            clipSpaceType);
        Techniques::ProjectionDesc projDesc;
        projDesc._verticalFov = gPI/2.0f;
        projDesc._aspectRatio = 1.f;
        projDesc._nearClip = nearClip;
        projDesc._farClip = farClip;
        projDesc._worldToProjection = Combine(m.first, m.second);
        projDesc._cameraToProjection = m.second;
        projDesc._cameraToWorld = InvertOrthonormalTransform(m.first);
        return projDesc;
    }

    

}}


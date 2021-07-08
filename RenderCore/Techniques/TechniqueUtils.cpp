// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TechniqueUtils.h"
#include "DrawableDelegates.h"
#include "CommonBindings.h"
#include "../../RenderCore/Types.h"
#include "../../RenderCore/Metal/Metal.h"       // for GFXAPI_TARGET define used below
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
            return ClipSpaceType::Positive;
        #elif (GFXAPI_TARGET == GFXAPI_VULKAN)
            return ClipSpaceType::PositiveRightHanded;
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

        Float3 cameraPosition = ExtractTranslation(sceneCamera._cameraToWorld);
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
        if (IsOrthogonalProjection(projDesc._cameraToProjection)) {
            globalTransform._farClip = CalculateNearAndFarPlane_Ortho(globalTransform._minimalProjection, GetDefaultClipSpaceType()).second;
            globalTransform._farClip = -globalTransform._farClip;       // we use negative far clip as a flag for orthogonal projection
        } else {
            globalTransform._farClip = CalculateNearAndFarPlane(globalTransform._minimalProjection, GetDefaultClipSpaceType()).second;
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
        return result;
    }

    ViewportConstants BuildViewportConstants(const ViewportDesc& viewport)
    {
        return ViewportConstants { 
            Float2{1.f/float(viewport._width), 1.f/float(viewport._height)}, 
            Float2{viewport._x, viewport._y},
            Float2{viewport._width, viewport._height},
            0, 0 };
    }

    SharedPkt MakeLocalTransformPacket(const Float4x4& localToWorld, const CameraDesc& camera)
    {
        return MakeLocalTransformPacket(localToWorld, ExtractTranslation(camera._cameraToWorld));
    }

    LocalTransformConstants MakeLocalTransform(const Float4x4& localToWorld, const Float3& worldSpaceCameraPosition)
    {
        LocalTransformConstants localTransform;
        CopyTransform(localTransform._localToWorld, localToWorld);
        auto worldToLocal = InvertOrthonormalTransform(localToWorld);
        localTransform._localSpaceView = TransformPoint(worldToLocal, worldSpaceCameraPosition);
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

	IUniformBufferDelegate::~IUniformBufferDelegate() {}
	IShaderResourceDelegate::~IShaderResourceDelegate() {}
	IMaterialDelegate::~IMaterialDelegate() {}

	Metal::ShaderProgram* ITechniqueDelegate_Old::GetShader(
		ParsingContext& context,
		const ParameterBox* shaderSelectors[],
		const DrawableMaterial& material)
	{
		Throw(std::runtime_error("This variation of GetShader() not implemented for this technique delegate"));
	}

	RenderCore::Metal::ShaderProgram* ITechniqueDelegate_Old::GetShader(
		ParsingContext& context,
		const ParameterBox* shaderSelectors[],
		const DrawableMaterial& material,
		unsigned techniqueIndex)
	{
		return GetShader(context, shaderSelectors, material);
	}

	ITechniqueDelegate_Old::~ITechniqueDelegate_Old() {}

	void SetGeoSelectors(ParameterBox& geoParameters, IteratorRange<const InputElementDesc*> ia)
	{
		if (HasElement(ia, "TEXCOORD"))     { geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXCOORD", 1); }
        if (HasElement(ia, "COLOR"))        { geoParameters.SetParameter((const utf8*)"GEO_HAS_COLOR", 1); }
        if (HasElement(ia, "NORMAL"))		{ geoParameters.SetParameter((const utf8*)"GEO_HAS_NORMAL", 1); }
        if (HasElement(ia, "TEXTANGENT"))      { geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXTANGENT", 1); }
        if (HasElement(ia, "TEXBITANGENT"))    { geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXBITANGENT", 1); }
        if (HasElement(ia, "BONEINDICES") && HasElement(ia, "BONEWEIGHTS"))
            { geoParameters.SetParameter((const utf8*)"GEO_HAS_BONEWEIGHTS", 1); }
        if (HasElement(ia, "PER_VERTEX_AO"))
            { geoParameters.SetParameter((const utf8*)"GEO_HAS_PER_VERTEX_AO", 1); }
	}

	void SetGeoSelectors(ParameterBox& geoParameters, IteratorRange<const MiniInputElementDesc*> ia)
	{
		if (HasElement(ia, CommonSemantics::TEXCOORD))			{ geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXCOORD", 1); }
        if (HasElement(ia, CommonSemantics::COLOR))				{ geoParameters.SetParameter((const utf8*)"GEO_HAS_COLOR", 1); }
        if (HasElement(ia, CommonSemantics::NORMAL))				{ geoParameters.SetParameter((const utf8*)"GEO_HAS_NORMAL", 1); }
        if (HasElement(ia, CommonSemantics::TEXTANGENT))			{ geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXTANGENT", 1); }
        if (HasElement(ia, CommonSemantics::TEXBITANGENT))		{ geoParameters.SetParameter((const utf8*)"GEO_HAS_TEXBITANGENT", 1); }
        if (HasElement(ia, CommonSemantics::BONEINDICES) && HasElement(ia, CommonSemantics::BONEWEIGHTS))
            { geoParameters.SetParameter((const utf8*)"GEO_HAS_BONEWEIGHTS", 1); }
        if (HasElement(ia, CommonSemantics::PER_VERTEX_AO))
            { geoParameters.SetParameter((const utf8*)"GEO_HAS_PER_VERTEX_AO", 1); }
	}

	ProjectionDesc BuildProjectionDesc(const CameraDesc& sceneCamera, UInt2 viewportDims)
    {
        const float aspectRatio = viewportDims[0] / float(viewportDims[1]);
        auto cameraToProjection = Techniques::Projection(sceneCamera, aspectRatio);

        RenderCore::Techniques::ProjectionDesc projDesc;
        projDesc._verticalFov = sceneCamera._verticalFieldOfView;
        projDesc._aspectRatio = aspectRatio;
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

}}


// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Matrix.h"
#include "Quaternion.h"

#if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML
    #pragma warning(push)
    #pragma warning(disable:4512)       // assignment operator could not be generated
    #include <cml/mathlib/matrix/rotation.h>
    #include <cml/mathlib/quaternion/rotation.h>
    #pragma warning(pop)
#endif

namespace XLEMath
{
        //
        //      Primitive transformation types
        //

    class UniformScale      { public: float _scale;     explicit UniformScale(float scale) : _scale(scale) {} };
    class ArbitraryScale    { public: Float3 _scale;    explicit ArbitraryScale(const Float3& scale) : _scale(scale) {} };
    class RotationX         { public: float _angle;     explicit RotationX(float angle) : _angle(angle) {} };
    class RotationY         { public: float _angle;     explicit RotationY(float angle) : _angle(angle) {} };
    class RotationZ         { public: float _angle;     explicit RotationZ(float angle) : _angle(angle) {} };

    class ArbitraryRotation
    {
    public:
        Float3 _axis;
        float _angle;

            // IsRotation... returns 1, 0 or -1 
            //      (-1 means a rotation around the negative axis direction)
        signed IsRotationX() const;
        signed IsRotationY() const;
        signed IsRotationZ() const;

		ArbitraryRotation() : _axis(Float3(1.0f, 0.f, 0.f)), _angle(0.f) {}
        ArbitraryRotation(Float3 axis, float angle) : _axis(axis), _angle(angle) {}
        ArbitraryRotation(const Float3x3& rotationMatrix);
    };

    class LookAt
    {
    public:
		Float3 _origin = Float3(0.f, 0.f, 0.f);
        Float3 _focusPosition = Float3(10.f, 0.f, 0.f);
        Float3 _upDirection = Float3(0.f, 0.f, 1.f);
    };

    class Skew
    {
    public:
        float _angle = 0.f;
        Float3 _axisA = Float3(1.f, 0.f, 0.f);
        Float3 _axisB = Float3(0.f, 0.f, 1.f);
    };

    #if MATHLIBRARY_ACTIVE == MATHLIBRARY_CML
		inline Float3x3   MakeRotationMatrix(Float3 axis, Float3::value_type angle)
        {
			Float3x3 result;
            cml::matrix_rotation_axis_angle(result, axis, angle);
            return result;
        }

		inline Quaternion   MakeRotationQuaternion(Float3 axis, Float3::value_type angle)
		{
			Quaternion result;
			cml::quaternion_rotation_axis_angle(result, axis, angle);
			return result;
		}
    #endif

    template<typename RotationType>
        class ScaleRotationTranslation
    {
    public:
            //
            //      This is a full 3D transformation.
            //      It can represent any affline transformation,
            //      and includes a non-uniform scale parameter.
            //
            //      In effect, we apply in this order:
			//			1) scale 
			//			2) rotation
			//			3) translation
            //
        RotationType    _rotation;
        Float3          _scale;
        Float3          _translation;

        explicit ScaleRotationTranslation(const Float4x4& copyFrom);
        ScaleRotationTranslation(const Float4x4& copyFrom, bool& goodDecomposition);
        explicit ScaleRotationTranslation(const Float3x4& copyFrom);
        ScaleRotationTranslation(const Float3x4& copyFrom, bool& goodDecomposition);
        ScaleRotationTranslation(Float3 scale, const RotationType& rotation, Float3 translation)
            : _rotation(rotation), _scale(scale), _translation(translation) {}
    };

    using ScaleRotationTranslationQ = ScaleRotationTranslation<Quaternion>;
    using ScaleRotationTranslationM = ScaleRotationTranslation<Float3x3>;

    class ScaleTranslation
    {
    public:
        Float3  _scale = Float3(1.f, 1.f, 1.0f);
        Float3  _translation = Float3(0.f, 0.f, 0.f);
    };

    class UniformScaleYRotTranslation
    {
    public:
        float _scale = 1.0f;
        float _yRotation = 0.f;
        Float3 _translation = Float3(0.f, 0.f, 0.f);
    };
        
        //
        //      "Combine" and "Combine_IntoLHS/Combine_IntoRHS" patterns.
        //
        //      For many transformation types, there are overloads for Combine, Combine_IntoLHS, Combine_IntoRHS.
        //      This will combine two transforms together, in such a way that the first
        //      parameter is always the first transformation applied.
        //
        //      That is to say, the following 2 cases are defined to be the same:
        //          ptResult = Transform(Combine(A, B), ptOriginal)
        //
        //      Or
        //          ptMidway = Transform(A, ptOriginal)
        //          ptResult = Transform(B, ptMidway)
        //
        //      Combine_IntoLHS/Combine_IntoRHS provides basic optimisation when the output result is mostly
        //      the same as one of the input (perhaps only a few elements have changed)
        //

    inline Float4x4     Combine(const Float4x4& firstTransform, const Float4x4& secondTransform)
    {
        return secondTransform * firstTransform;
    }
    
    void            Combine_IntoRHS(const Float3& translate, Float4x4& transform);
    void            Combine_IntoRHS(const UniformScale& scale, Float4x4& transform);
    void            Combine_IntoRHS(const ArbitraryScale& scale, Float4x4& transform);
    void            Combine_IntoRHS(RotationX rotation, Float4x4& transform);
    void            Combine_IntoRHS(RotationY rotation, Float4x4& transform);
    void            Combine_IntoRHS(RotationZ rotation, Float4x4& transform);
	void            Combine_IntoRHS(ArbitraryRotation rotation, Float4x4& transform);
	void            Combine_IntoRHS(Quaternion rotation, Float4x4& transform);

    void            Combine_IntoLHS(Float4x4& transform, const Float3& translate);
    void            Combine_IntoLHS(Float4x4& transform, const UniformScale& scale);
    void            Combine_IntoLHS(Float4x4& transform, const ArbitraryScale& scale);
    void            Combine_IntoLHS(Float4x4& transform, RotationX rotation);
    void            Combine_IntoLHS(Float4x4& transform, RotationY rotation);
    void            Combine_IntoLHS(Float4x4& transform, RotationZ rotation);
	void            Combine_IntoLHS(Float4x4& transform, ArbitraryRotation rotation);
    void            Combine_IntoLHS(Float4x4& transform, Quaternion rotation);

    Float4x4        Combine(const Float3x3& rotation, const Float4x4& transform);
	Float4x4        Combine(const Float4x4& transform, const Float3x3& rotation);

        //
        //      Basic transformations
        //
    Float3          TransformPoint(const Float3x4& transform, Float3 pt);
    Float3          TransformPoint(const Float4x4& transform, Float3 pt);
    Float3          TransformDirectionVector(const Float3x3& transform, Float3 pt);
    Float3          TransformDirectionVector(const Float3x4& transform, Float3 pt);
    Float3          TransformDirectionVector(const Float4x4& transform, Float3 pt);
    Float3          TransformPointByOrthonormalInverse(const Float3x4& transform, Float3 pt);
    Float3          TransformPointByOrthonormalInverse(const Float4x4& transform, Float3 pt);
    Float4          TransformPlane(const Float4x4& transform, Float4 plane);

        //
        //      Orthonormal matrices have special properties. Use the following, instead
        //      of more general operations.
        //
    Float4x4        InvertOrthonormalTransform(const Float4x4& input);
    Float3x4        InvertOrthonormalTransform(const Float3x4& input);
    Float2x3        InvertOrthonormalTransform(const Float2x3& input);
    bool            IsOrthonormal(const Float3x3& input, float tolerance = 0.01f);

        //
        //      Extract the basis vectors from transformations.
        //      Use these, instead of accessing the rows & columns of the matrix directly.
        //      This will insulate your code from matrix configuration options.
        //
        //
        //      Normal object-to-world:
        //          Forward:    +Y
        //          Up:         +Z
        //          Right:      +X
        //
        //      Camera:
        //          Forward:    -Z
        //          Up:         +Y
        //          Right:      +X
        //
    inline Float3   ExtractTranslation(const Float4x4& matrix)      { return  Float3(matrix(0,3), matrix(1,3), matrix(2,3)); }
    inline Float3   ExtractRight(const Float4x4& matrix)            { return  Float3(matrix(0,0), matrix(1,0), matrix(2,0)); }
    inline Float3   ExtractForward(const Float4x4& matrix)          { return  Float3(matrix(0,1), matrix(1,1), matrix(2,1)); }
    inline Float3   ExtractUp(const Float4x4& matrix)               { return  Float3(matrix(0,2), matrix(1,2), matrix(2,2)); }

    inline Float3   ExtractTranslation(const Float3x4& matrix)      { return  Float3(matrix(0,3), matrix(1,3), matrix(2,3)); }
    inline Float3   ExtractRight(const Float3x4& matrix)            { return  Float3(matrix(0,0), matrix(1,0), matrix(2,0)); }
    inline Float3   ExtractForward(const Float3x4& matrix)          { return  Float3(matrix(0,1), matrix(1,1), matrix(2,1)); }
    inline Float3   ExtractUp(const Float3x4& matrix)               { return  Float3(matrix(0,2), matrix(1,2), matrix(2,2)); }

    inline Float3   ExtractRight(const Float3x3& matrix)            { return  Float3(matrix(0,0), matrix(1,0), matrix(2,0)); }
    inline Float3   ExtractForward(const Float3x3& matrix)          { return  Float3(matrix(0,1), matrix(1,1), matrix(2,1)); }
    inline Float3   ExtractUp(const Float3x3& matrix)               { return  Float3(matrix(0,2), matrix(1,2), matrix(2,2)); }

    inline Float3   ExtractRight_Cam(const Float4x4& matrix)        { return  Float3(matrix(0,0), matrix(1,0), matrix(2,0)); }
    inline Float3   ExtractForward_Cam(const Float4x4& matrix)      { return -Float3(matrix(0,2), matrix(1,2), matrix(2,2)); }
    inline Float3   ExtractUp_Cam(const Float4x4& matrix)           { return  Float3(matrix(0,1), matrix(1,1), matrix(2,1)); }

    inline Float3   ExtractRight_Cam(const Float3x4& matrix)        { return  Float3(matrix(0,0), matrix(1,0), matrix(2,0)); }
    inline Float3   ExtractForward_Cam(const Float3x4& matrix)      { return -Float3(matrix(0,2), matrix(1,2), matrix(2,2)); }
    inline Float3   ExtractUp_Cam(const Float3x4& matrix)           { return  Float3(matrix(0,1), matrix(1,1), matrix(2,1)); }

    float ExtractUniformScaleFast(const Float3x4& matrix);

    inline void SetTranslation(Float4x4& matrix, const Float3& position)	{ matrix(0,3) = position[0]; matrix(1,3) = position[1]; matrix(2,3) = position[2]; }
	inline void SetRight(Float4x4& matrix, const Float3& right)				{ matrix(0,0) = right[0]; matrix(1,0) = right[1]; matrix(2,0) = right[2]; }
	inline void SetForward(Float4x4& matrix, const Float3& forward)			{ matrix(0,1) = forward[0]; matrix(1,1) = forward[1]; matrix(2,1) = forward[2]; }
	inline void SetUp(Float4x4& matrix, const Float3& up)					{ matrix(0,2) = up[0]; matrix(1,2) = up[1]; matrix(2,2) = up[2]; }

    ScaleRotationTranslationQ    SphericalInterpolate(const ScaleRotationTranslationQ& lhs, const ScaleRotationTranslationQ& rhs, float alpha);

    Float4x4    AsFloat4x4(const ScaleTranslation& input);
    T1(RotationType) Float4x4    AsFloat4x4(const RotationType& input);
    Float4x4    AsFloat4x4(const UniformScale& input);
    Float4x4    AsFloat4x4(const RotationX& input);
    Float4x4    AsFloat4x4(const RotationY& input);
    Float4x4    AsFloat4x4(const RotationZ& input);
    Float4x4    AsFloat4x4(const ArbitraryRotation& input);
    Float4x4    AsFloat4x4(const ArbitraryScale& input);
    Float4x4    AsFloat4x4(const UniformScaleYRotTranslation& input);
    Float4x4    AsFloat4x4(const ScaleRotationTranslationQ& input);
    Float4x4    AsFloat4x4(const ScaleRotationTranslationM& input);

    Float4x4    AsFloat4x4(const Float3& translation);
    Float3x4    AsFloat3x4(const Float3& translation);
	Float4x4    AsFloat4x4(const Float3x3& rotationMatrix); 
	Float4x4    AsFloat4x4(const Quaternion& input);
	Float4x4    AsFloat4x4(const Float3x4& orthonormalTransform);

    Float3x3    AsFloat3x3(const Quaternion& input);
    Float3x4    AsFloat3x4(const Float4x4& orthonormalTransform);

    Float4x4    AsFloat4x4(const Float2x3& input);

    inline Float4x4     Combine(const Float3x4& firstTransform, const Float4x4& secondTransform)
    {
            // placeholder for better implementation!
        return secondTransform * AsFloat4x4(firstTransform);
    }
    
    inline Float4x4     Combine(const Float4x4& firstTransform, const Float3x4& secondTransform)
    {
            // placeholder for better implementation!
        return AsFloat4x4(secondTransform) * firstTransform;
    }

    Float3x4    Combine(const Float3x4& firstTransform, const Float3x4& secondTransform);

    Float4x4    MakeCameraToWorld(const Float3& forward, const Float3& up, const Float3& position);
    Float4x4    MakeCameraToWorld(const Float3& forward, const Float3& up, const Float3& right, const Float3& position);

	Float4x4    MakeObjectToWorld(const Float3& forward, const Float3& up, const Float3& position);
	Float4x4    MakeObjectToWorld(const Float3& forward, const Float3& up, const Float3& right, const Float3& position);

    inline void CopyTransform(Float3x4& destination, const Float4x4& localToWorld)
    {
        destination(0, 0)    = localToWorld(0, 0);
        destination(0, 1)    = localToWorld(0, 1);
        destination(0, 2)    = localToWorld(0, 2);
        destination(0, 3)    = localToWorld(0, 3);
        destination(1, 0)    = localToWorld(1, 0);
        destination(1, 1)    = localToWorld(1, 1);
        destination(1, 2)    = localToWorld(1, 2);
        destination(1, 3)    = localToWorld(1, 3);
        destination(2, 0)    = localToWorld(2, 0);
        destination(2, 1)    = localToWorld(2, 1);
        destination(2, 2)    = localToWorld(2, 2);
        destination(2, 3)    = localToWorld(2, 3);
    }
}

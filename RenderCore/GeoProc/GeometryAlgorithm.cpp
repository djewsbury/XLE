// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GeometryAlgorithm.h"
#include "MeshDatabase.h"
#include "../Assets/ModelMachine.h"     // for VertexElement
#include "../Format.h"
#include "../Types.h"
#include "../../Math/Geometry.h"
#include "../../Math/Transformations.h"
#include "../../OSServices/Log.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Core/Exceptions.h"
#include <cstdlib>

namespace RenderCore { namespace Assets { namespace GeoProc
{
    Float3 CorrectAxisDirection(const Float3& input, const Float3& p0, const Float3& p1, const Float3& p2, float t0, float t1, float t2)
    {
        float A0 = Dot(p0 - p1, input);
        float A1 = Dot(p1 - p2, input);
        float A2 = Dot(p2 - p0, input);
        float a0 = t0 - t1;
        float a1 = t1 - t2;
        float a2 = t2 - t0;

        float w0 = XlAbs(A0 * a0);
        float w1 = XlAbs(A1 * a1);
        float w2 = XlAbs(A2 * a2);
        if (w0 > w1) {
            if (w0 > w2) return ((A0 > 0.f) == (a0 > 0.f)) ? input : -input;
            return ((A2 > 0.f) == (a2 > 0.f)) ? input : -input;
        } else {
            if (w1 > w2) return ((A1 > 0.f) == (a1 > 0.f)) ? input : -input;
            return ((A2 > 0.f) == (a2 > 0.f)) ? input : -input;
        }
    }

    static Float3 QuantizeUnitVector(Float3 input, float quantizeValue)
    {
        Float3 r;
        r[0] = std::copysign(std::round(std::abs(input[0] * quantizeValue)) / quantizeValue, input[0]);
        r[1] = std::copysign(std::round(std::abs(input[1] * quantizeValue)) / quantizeValue, input[1]);
        r[2] = std::copysign(std::round(std::abs(input[2] * quantizeValue)) / quantizeValue, input[2]);
        return r;
    }
    
    static Float4 QuantizeUnitVector(Float4 input, float quantizeValue)
    {
        Float4 r;
        r[0] = std::copysign(std::round(std::abs(input[0] * quantizeValue)) / quantizeValue, input[0]);
        r[1] = std::copysign(std::round(std::abs(input[1] * quantizeValue)) / quantizeValue, input[1]);
        r[2] = std::copysign(std::round(std::abs(input[2] * quantizeValue)) / quantizeValue, input[2]);
        r[3] = std::copysign(std::round(std::abs(input[3] * quantizeValue)) / quantizeValue, input[3]);
        return r;
    }

    void GenerateNormalsAndTangents( 
        MeshDatabase& mesh, 
        unsigned normalMapTextureCoordinateSemanticIndex,
		float equivalenceThreshold,
        const void* rawIb, size_t indexCount, Format ibFormat)
	{
            // testing -- remove existing tangents & normals
        // mesh.RemoveStream(mesh.FindElement("NORMAL"));
        // mesh.RemoveStream(mesh.FindElement("TEXTANGENT"));
        // mesh.RemoveStream(mesh.FindElement("TEXBITANGENT"));

        auto tcElement = mesh.FindElement("TEXCOORD", normalMapTextureCoordinateSemanticIndex);

        bool hasNormals = !!(mesh.HasElement("NORMAL") & 0x1);
        bool hasTangents = !!(mesh.HasElement("TEXTANGENT") & 0x1);
        bool hasBitangents = !!(mesh.HasElement("TEXBITANGENT") & 0x1);
        if ((hasNormals && hasTangents) || (hasTangents && hasBitangents)) return;
        if (hasNormals && tcElement == ~0u) return;

        auto posElement = mesh.FindElement("POSITION");

		struct Triangle
		{
			Float3 normal;
			Float3 tangent;
			Float3 bitangent;
		};
		
            //
            //      Note that when building normals and tangents, there are some
            //      cases were we might want to split a vertex into two...
            //      This can happen if we want to create a sharp edge in the model,
            //      or a seam in the texturing.
            //      However, this method never splits vertices... We only modify the
            //      input vertices. This can create stretching or warping in some 
            //      models -- that can only be fixed by changing the input data.
            //
            //      Also note that this is an unweighted method... Which means that
            //      each vertex is influenced by all triangles it is part of evenly.
            //      Some other methods will weight the influence of triangles such
            //      that larger or more important triangles have a larger influence.
            //
        std::vector<Float3> normals(mesh.GetUnifiedVertexCount(), Zero<Float3>());
		std::vector<Float4> tangents(mesh.GetUnifiedVertexCount(), Zero<Float4>());
		std::vector<Float3> bitangents(mesh.GetUnifiedVertexCount(), Zero<Float3>());

        unsigned indexStride = 2;
        unsigned indexMask = 0xffff;
        if (ibFormat == Format::R32_UINT) { indexStride = 4; indexMask = 0xffffffff; }
        auto* ib = (const uint32*)rawIb;

		if (ibFormat == Format::Unknown) {
			indexCount = mesh.GetUnifiedVertexCount();
			ib = nullptr;
		}

		auto triangleCount = indexCount / 3;   // assuming index buffer is triangle-list format
		for (size_t c=0; c<triangleCount; c++) {
			unsigned v0, v1, v2;
			if (ibFormat != Format::Unknown) {
				v0 = (*ib) & indexMask; ib = PtrAdd(ib, indexStride);
				v1 = (*ib) & indexMask; ib = PtrAdd(ib, indexStride);
				v2 = (*ib) & indexMask; ib = PtrAdd(ib, indexStride);
			} else {
				v0 = unsigned(c*3+0);
				v1 = unsigned(c*3+1);
				v2 = unsigned(c*3+2);
			}

			if (expect_evaluation(v0 != v1 && v1 != v2 && v0 != v2, true)) {
				auto p0 = mesh.GetUnifiedElement<Float3>(v0, posElement);
				auto p1 = mesh.GetUnifiedElement<Float3>(v1, posElement);
				auto p2 = mesh.GetUnifiedElement<Float3>(v2, posElement);

				Float4 plane;
				if (expect_evaluation(PlaneFit_Checked(&plane, p0, p1, p2), true)) {
					Triangle tri;
					tri.normal = Truncate(plane);

					if (tcElement != ~0u) {
							/*	There is one natural tangent and one natural bitangent for each triangle, on the v=0 and u=0 axes 
								in 3 space. We'll calculate them for this triangle here and then use a composite of triangle tangents
								for the vertex tangents below.

								Here's a good reference:
								http://www.terathon.com/code/tangent.html
								from "Mathematics for 3D Game Programming and Computer Graphics, 2nd ed."

								These equations just solve for v=0 and u=0 on the triangle surface.
							*/
						const auto UV0 = mesh.GetUnifiedElement<Float2>(v0, tcElement);
						const auto UV1 = mesh.GetUnifiedElement<Float2>(v1, tcElement);
						const auto UV2 = mesh.GetUnifiedElement<Float2>(v2, tcElement);
						auto Q1 = p1 - p0;
						auto Q2 = p2 - p0;
						auto st1 = UV1 - UV0;
						auto st2 = UV2 - UV0;
						float rr = (st1[0] * st2[1] + st2[0] * st1[1]);
						if (Equivalent(rr, 0.f, 1e-10f)) { 
                            tri.tangent = tri.bitangent = Zero<Float3>();
                        } else {
							float r = 1.f / rr;
							Float3 sAxis( (st2[1] * Q1 - st1[1] * Q2) * r );
							Float3 tAxis( (st1[0] * Q2 - st2[0] * Q1) * r );

								// We may need to flip the direction of the s or t axis
								// check the texture coordinates to find the correct direction
								// for these axes...
							sAxis = CorrectAxisDirection(sAxis, p0, p1, p2, UV0[0], UV1[0], UV2[0]);
							tAxis = CorrectAxisDirection(tAxis, p0, p1, p2, UV0[1], UV1[1], UV2[1]);

							auto sMagSq = MagnitudeSquared(sAxis);
							auto tMagSq = MagnitudeSquared(tAxis);
						
							float recipSMag, recipTMag;
							if (XlRSqrt_Checked(&recipSMag, sMagSq) && XlRSqrt_Checked(&recipTMag, tMagSq)) {
								tri.tangent = sAxis * recipSMag;
								tri.bitangent = tAxis * recipTMag;
							} else {
								tri.tangent = tri.bitangent = Zero<Float3>();
							}

							tri.tangent = sAxis;
							tri.bitangent = tAxis;
						}

						assert( tri.tangent[0] == tri.tangent[0] );
					} else {
						tri.tangent = Zero<Float3>();
						tri.bitangent = Zero<Float3>();
					}

						// We add the influence of this triangle to all vertices
						// each vertex should get an even balance of influences from
						// all triangles it is part of.
					assert(std::isfinite(tri.normal[0]) && !std::isnan(tri.normal[0]) && tri.normal[0] == tri.normal[0]);
					assert(std::isfinite(tri.normal[1]) && !std::isnan(tri.normal[1]) && tri.normal[1] == tri.normal[1]);
					assert(std::isfinite(tri.normal[2]) && !std::isnan(tri.normal[2]) && tri.normal[2] == tri.normal[2]);
					normals[v0] += tri.normal; normals[v1] += tri.normal; normals[v2] += tri.normal;
					tangents[v0] += Expand(tri.tangent, 0.f); tangents[v1] += Expand(tri.tangent, 0.f); tangents[v2] += Expand(tri.tangent, 0.f);
					bitangents[v0] += tri.bitangent; bitangents[v1] += tri.bitangent; bitangents[v2] += tri.bitangent;
				} else {
						/* this triangle is so small we can't derive any useful information from it */
					Log(Warning) << "GenerateNormalsAndTangents: Near-degenerate triangle found on vertices (" << v0 << ", " << v1 << ", " << v2 << ")" << std::endl;
				}
			} else {
				Log(Warning) << "GenerateNormalsAndTangents: Degenerate triangle found on vertices (" << v0 << ", " << v1 << ", " << v2 << ")" << std::endl;
			}
		}

            //  Create new streams for the normal & tangent, and write the results to the mesh database
            //  If we already have tangents or normals, don't write the new ones

        if (!hasNormals) {
            for (size_t c=0; c<mesh.GetUnifiedVertexCount(); c++)
                Normalize_Checked(&normals[c], normals[c]);		// (note -- it's possible for the normal to to Zero<Float3>() if this vertex wasn't used by the index buffer)
        
			auto normalsData = CreateRawDataSource(AsPointer(normals.cbegin()), AsPointer(normals.cend()), Format::R32G32B32_FLOAT);
			if (equivalenceThreshold != 0.0f) {
				std::vector<unsigned> unifiedMapping;
                Float3* begin = (Float3*)normalsData->GetData().begin(), *end = (Float3*)normalsData->GetData().end();
                float quantValue = 1.f/equivalenceThreshold;
                for (auto& f:MakeIteratorRange(begin, end))
                    f = QuantizeUnitVector(f, quantValue);
				normalsData = RenderCore::Assets::GeoProc::RemoveBitwiseIdenticals(unifiedMapping, *normalsData);
				mesh.AddStream(normalsData, std::move(unifiedMapping), "NORMAL", 0);
			} else {
				mesh.AddStream(normalsData, std::vector<unsigned>(), "NORMAL", 0);
			}
        }

		// if there are no texture coordinates, we can only generate normals, not tangents
        // also, we should only generate tangents if we're missing both tangent and bitangents
        // (ie, normal + bitangent + handiness flag is still a valid tangent frame)
        if (tcElement != ~0u && !hasTangents && !hasBitangents) {

            unsigned normalsElement = mesh.FindElement("NORMAL");
            bool atLeastOneGoodTangent = false;

                //  normals and tangents will have fallen out of orthogonality by the blending above.
			    //  we can re-orthogonalize using the Gram-Schmidt process -- we won't modify the normal, we'd rather lift the tangent and bitangent
			    //  off the triangle surface that distort the normal direction too much.
                //  Note that we don't need to touch the bitangent here... We're not going to write the bitangent
                //  to the output, so it doesn't matter right now. All we need to do is calculate the "handiness"
                //  value and write it to the "w" part of the tangent vector.
            for (size_t c=0; c<mesh.GetUnifiedVertexCount(); c++) {
                auto t3 = Truncate(tangents[c]);
                auto handinessValue = 0.f;

                    // if we already had normals in the mesh, we should prefex
                    // those normals (over the ones we generated here)
                auto n = normals[c];
                if (hasNormals) n = mesh.GetUnifiedElement<Float3>(c, normalsElement);

                if (Normalize_Checked(&t3, t3)) {
                    handinessValue = Dot(Cross(bitangents[c], t3), n) < 0.f ? -1.f : 1.f;
                    atLeastOneGoodTangent = true;
                } else
                    t3 = Zero<Float3>();
                
                tangents[c] = Expand(t3, handinessValue);
            }

            if (atLeastOneGoodTangent) {
                auto tangentsData = CreateRawDataSource(AsPointer(tangents.begin()), AsPointer(tangents.cend()), Format::R32G32B32A32_FLOAT);
                if (equivalenceThreshold != 0.0f) {
                    Float4* begin = (Float4*)tangentsData->GetData().begin(), *end = (Float4*)tangentsData->GetData().end();
                    float quantValue = 1.f/equivalenceThreshold;
                    for (auto& f:MakeIteratorRange(begin, end))
                        f = QuantizeUnitVector(f, quantValue);
                    std::vector<unsigned> unifiedMapping;
                    tangentsData = RenderCore::Assets::GeoProc::RemoveBitwiseIdenticals(unifiedMapping, *tangentsData);
                    mesh.AddStream(tangentsData, std::move(unifiedMapping), "TEXTANGENT", 0);
                } else {
                    mesh.AddStream(tangentsData, std::vector<unsigned>(), "TEXTANGENT", 0);
                }
            }

        }

        // if (!hasBitangents) {
        // 
        //     unsigned normalsElement = mesh.FindElement("NORMAL");
        // 
        //     for (size_t c=0; c<mesh._unifiedVertexCount; c++) {
        //         auto t3 = bitangents[c];
        // 
        //             // if we already had normals in the mesh, we should prefex
        //             // those normals (over the ones we generated here)
        //         auto n = normals[c];
        //         if (hasNormals) n = mesh.GetUnifiedElement<Float3>(c, normalsElement);
        // 
        //         if (Normalize_Checked(&t3, Float3(t3 - n * Dot(n, t3)))) {
        //         } else {
        //             t3 = Zero<Float3>();
        //         }
        // 
        //         bitangents[c] = t3;
        //     }
        // 
        //     mesh.AddStream(
        //         AsPointer(bitangents.begin()), AsPointer(bitangents.cend()), Format::R32G32B32_FLOAT,
        //         Use16BitFloats ? Format::R16G16B16A16_FLOAT : Format::R32G32B32_FLOAT,
        //         "TEXBITANGENT", 0);
        // 
        // }
	}

    template<typename Type>
        Format AsNativeFormat();

    template<> Format AsNativeFormat<float>() 
        { return Format::R32_FLOAT; }
    template<> Format AsNativeFormat<Float2>() 
        { return Format::R32G32_FLOAT; }
    template<> Format AsNativeFormat<Float3>() 
        { return Format::R32G32B32_FLOAT; }
    template<> Format AsNativeFormat<Float4>() 
        { return Format::R32G32B32A32_FLOAT; }

    // #define CHECK_FOR_NAN
    #if defined(CHECK_FOR_NAN)
        static void Check(float i) { assert(!std::isnan(i)); }
        static void Check(Float2 i) { assert(!std::isnan(i[0]) && !std::isnan(i[1])); }
        static void Check(Float3 i) { assert(!std::isnan(i[0]) && !std::isnan(i[1]) && !std::isnan(i[2])); }
        static void Check(Float4 i) { assert(!std::isnan(i[0]) && !std::isnan(i[1]) && !std::isnan(i[2]) && !std::isnan(i[3])); }
    #endif

    template<typename PivotType, typename TransformFn>
        std::shared_ptr<IVertexSourceData> TransformStream(
            const IVertexSourceData& src,
            const TransformFn& transform)
        {
            std::vector<uint8> tempBuffer;
            tempBuffer.resize(src.GetCount() * sizeof(PivotType));
            auto* dst = (PivotType*)AsPointer(tempBuffer.begin());
            for (unsigned c=0; c<src.GetCount(); ++c)
                dst[c] = GetVertex<PivotType>(src, c);

            #if defined(CHECK_FOR_NAN)
                for (unsigned c=0; c<src.GetCount(); ++c) Check(dst[c]);
            #endif

            transform(dst, &dst[src.GetCount()]);

            #if defined(CHECK_FOR_NAN)
                for (unsigned c=0; c<src.GetCount(); ++c) Check(dst[c]);
            #endif

                // Let's make sure the new stream data is in the same format
                // as the old one.
            auto finalStride = BitsPerPixel(src.GetFormat())/8;
            auto finalFormat = src.GetFormat();
            auto pivotFormat = AsNativeFormat<PivotType>();
            if (finalFormat != pivotFormat) {
                assert(finalStride <= sizeof(PivotType));  // we can do this in-place so long as the destination stride isn't bigger
                CopyVertexData(
                    AsPointer(tempBuffer.begin()), finalFormat, finalStride, tempBuffer.size(),
                    AsPointer(tempBuffer.begin()), pivotFormat, sizeof(PivotType), tempBuffer.size(),
                    unsigned(src.GetCount()));
            }

            return CreateRawDataSource(
                std::move(tempBuffer), src.GetCount(), finalStride, finalFormat);
        }

    template<typename PivotType, typename TransformFn>
        void TransformStream(
            RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
            unsigned streamIndex,
            const TransformFn& transform)
    {
        const auto& stream = mesh.GetStreams()[streamIndex];
        auto newStream = TransformStream<PivotType>(*stream.GetSourceData(), transform);

            // swap the old stream with the new one.
        auto semanticName = stream.GetSemanticName();
        auto semanticIndex = stream.GetSemanticIndex();
        auto vertexMap = stream.GetVertexMap();
        mesh.RemoveStream(streamIndex);
        mesh.InsertStream(
            streamIndex, std::move(newStream),
            std::move(vertexMap), semanticName.c_str(), semanticIndex);
    }

    void Transform(
        RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
        const Float4x4& transform)
    {
        // For each stream in the mesh, we need to decide how to transform it.
        // We have 3 typical options:
        //      TransformPoint -- 
        //          This uses the full 4x4 transform
        //          (ie, this should be applied to POSITION)
        //      TransformUnitVector -- 
        //          This uses only the rotational element of the transform,
        //          with the scale and translation removed.
        //          (ie, this should be applied to NORMAL, TEXTANGENT, etc)
        //      None --
        //          No transform at all
        //          (ie, this should be applied to TEXCOORD)
        //
        // In the case of TransformUnitVector, we're going to assume a
        // well behaved 4x4 transform -- with no skew or wierd non-orthogonal
        // behaviour. Actually, we can get an arbitrarily complex 4x4 transform
        // from Collada... But let's just assume it's simple.

        for (unsigned s=0; s<mesh.GetStreams().size(); s++) {
            const auto& stream = mesh.GetStreams()[s];

            auto semanticName = stream.GetSemanticName();
            enum Type { Point, UnitVector, None } type = None;

                // todo -- semantic names are hard coded here. But we
                // could make this data-driven by using a configuration
                // file to select the transform type.
            if (semanticName == "POSITION") {
                type = Point;
            } else if (
                    semanticName == "NORMAL" 
                ||  semanticName == "TEXTANGENT"
                ||  semanticName == "TEXBITANGENT"
                ||  semanticName == "TEXBINORMAL"
                ||  semanticName == "TANGENT"
                ||  semanticName == "BITANGENT"
                ||  semanticName == "BINORMAL") {
                type = UnitVector;
            }
            if (type == None) continue;

            const auto& src = *stream.GetSourceData();
            auto componentCount = GetComponentCount(GetComponents(src.GetFormat()));
            if (type == Point) {
                    // We can support both 3d and 2d pretty easily here. Collada generalizes to 2d well,
                    // so we might as well support it (though maybe the 3d case is by far the most common
                    // case)
                if (componentCount==3) {
                    TransformStream<Float3>(
                        mesh, s, 
                        [&transform](Float3* begin, Float3* end)
                            { for (auto i=begin; i!=end; ++i) *i = TransformPoint(transform, *i); });
                } else if (componentCount==4) {
                    TransformStream<Float4>(
                        mesh, s, 
                        [&transform](Float4* begin, Float4* end)
                            { for (auto i=begin; i!=end; ++i) *i = transform * (*i); });
                } else if (componentCount==2) {
                    TransformStream<Float2>(
                        mesh, s, 
                        [&transform](Float2* begin, Float2* end)
                            { for (auto i=begin; i!=end; ++i) *i = Truncate(TransformPoint(transform, Expand(*i, 0.f))); });
                } else if (componentCount==1) {
                    TransformStream<float>(
                        mesh, s, 
                        [&transform](float* begin, float* end)
                            { for (auto i=begin; i!=end; ++i) *i = TransformPoint(transform, Float3(*i, 0.f, 0.f))[0]; });
                }
            } else if (type == UnitVector) {
                // We can do this in two ways... We can create a version of the matrix that
                // has the scale removed. This would be fine for uniform scale. 
                // But in the nonuniform scale, the normal should get deformed.
                // Alternatively, we can transform with the scale part there, and just renormalize
                // afterwards.
                auto truncated = Truncate3x3(transform);
                if (componentCount==3) {
                    TransformStream<Float3>(
                        mesh, s, 
                        [&truncated](Float3* begin, Float3* end)
                            { for (auto i=begin; i!=end; ++i) *i = Normalize(truncated * (*i)); });
                } else if (componentCount==4) {
                    // note that the "tangent" stream can be 4D if the "handiness" flag is already attached.
                    Throw(::Exceptions::BasicLabel("Attempting to apply 3D transform to 4D vector. Perhaps the final component is the tangent handiness flag?"));
                } else if (componentCount==2) {
                    TransformStream<Float2>(
                        mesh, s, 
                        [&truncated](Float2* begin, Float2* end)
                            { for (auto i=begin; i!=end; ++i) *i = Truncate(Normalize(truncated * Expand(*i, 0.f))); });
                }
            }
        }
    }

    void RemoveRedundantBitangents(MeshDatabase& mesh)
    {
            // remove bitangents for every stream that has both normals and tangents
        auto normAndTan = mesh.HasElement("NORMAL") & mesh.HasElement("TEXTANGENT");
        if (normAndTan!=0) {
            for (unsigned b=0; b<(32-xl_ctz4(normAndTan)); ++b) {
                auto bitan = mesh.FindElement("TEXBITANGENT", b);
                if (bitan != ~0u)
                    mesh.RemoveStream(bitan);
            }
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void CopyVertexElements(     
        IteratorRange<void*> destinationBuffer,            size_t destinationVertexStride,
        IteratorRange<const void*> sourceBuffer,           size_t sourceVertexStride,
        IteratorRange<const Assets::VertexElement*> destinationLayout,
        IteratorRange<const Assets::VertexElement*> sourceLayout,
        IteratorRange<const uint32_t*> reordering)
    {
        uint32      elementReordering[32];
        signed      maxSourceLayout = -1;
        for (auto source=sourceLayout.begin(); source!=sourceLayout.end(); ++source) {
                //      look for the same element in the destination layout (or put ~uint32(0x0) if it's not there)
            elementReordering[source-sourceLayout.begin()] = ~uint32(0x0);
            for (auto destination=destinationLayout.begin(); destination!=destinationLayout.end(); ++destination) {
                if (    !XlCompareString(destination->_semanticName, source->_semanticName)
                    &&  destination->_semanticIndex  == source->_semanticIndex
                    &&  destination->_nativeFormat   == source->_nativeFormat) {

                    elementReordering[source-sourceLayout.begin()] = uint32(destination-destinationLayout.begin());
                    maxSourceLayout = std::max(maxSourceLayout, signed(source-sourceLayout.begin()));
                    break;
                }
            }
        }

        if (maxSourceLayout<0) return;

        size_t vertexCount = reordering.size(); (void)vertexCount;

        #if defined(_DEBUG)
                    //  fill in some dummy values
            std::fill((uint8_t*)destinationBuffer.begin(), (uint8_t*)destinationBuffer.end(), (uint8_t)0xaf);
        #endif

            ////////////////     copy each vertex (slowly) piece by piece       ////////////////
        for (auto reorderingIterator = reordering.begin(); reorderingIterator!=reordering.end(); ++reorderingIterator) {
            size_t sourceIndex               = reorderingIterator-reordering.begin(), destinationIndex = *reorderingIterator;
            void* destinationVertexStart     = PtrAdd(destinationBuffer.begin(), destinationIndex*destinationVertexStride);
            const void* sourceVertexStart    = PtrAdd(sourceBuffer.begin(), sourceIndex*sourceVertexStride);
            for (unsigned c=0; c<=(unsigned)maxSourceLayout; ++c) {
                if (elementReordering[c] != ~uint32(0x0)) {
                    const auto& destinationElement = destinationLayout[elementReordering[c]]; assert(&destinationElement < destinationLayout.end());
                    const auto& sourceElement = sourceLayout[c]; assert(&sourceElement < sourceLayout.end());
                    size_t elementSize = BitsPerPixel(destinationElement._nativeFormat)/8;
                    assert(elementSize == BitsPerPixel(sourceElement._nativeFormat)/8);
                    assert(destinationElement._alignedByteOffset + elementSize <= destinationVertexStride);
                    assert(sourceElement._alignedByteOffset + elementSize <= sourceVertexStride);
                    assert(PtrAdd(destinationVertexStart, destinationElement._alignedByteOffset+elementSize) <= PtrAdd(destinationVertexStart, vertexCount*destinationVertexStride));
                    assert(PtrAdd(sourceVertexStart, sourceElement._alignedByteOffset+elementSize) <= PtrAdd(sourceVertexStart, vertexCount*sourceVertexStride));
					assert(PtrAdd(destinationVertexStart, destinationElement._alignedByteOffset+elementSize) <= destinationBuffer.end());
                    assert(PtrAdd(sourceVertexStart, sourceElement._alignedByteOffset+elementSize) <= sourceBuffer.end());

                    XlCopyMemory(
                        PtrAdd(destinationVertexStart, destinationElement._alignedByteOffset),
                        PtrAdd(sourceVertexStart, sourceElement._alignedByteOffset),
                        elementSize);
                }
            }
        }
    }

    unsigned CalculateVertexSize(IteratorRange<const Assets::VertexElement*> layout)
    {
        unsigned result = 0;
        for (auto l=layout.begin(); l!=layout.end(); ++l)
            result += BitsPerPixel(l->_nativeFormat);
        return result/8;
    }

    unsigned CalculateVertexSize(IteratorRange<const InputElementDesc*> layout)
    {
        unsigned result = 0;
        for (auto l=layout.begin(); l!=layout.end(); ++l)
            result += BitsPerPixel(l->_nativeFormat);
        return result/8;
    }

	struct WorkingEdge
	{
		uint64_t _id;
		unsigned _tri0, _tri1;
		unsigned _tri0EdgeIdx;
		unsigned _tri1EdgeIdx;

		friend bool operator<(const WorkingEdge& lhs, const WorkingEdge& rhs) { return lhs._id < rhs._id; }

		WorkingEdge(unsigned v0, unsigned v1, unsigned edgeIdx, unsigned tri0)
		{
			auto vSmall = std::min(v0, v1);
			auto vBig = std::max(v0, v1);
			_id = uint64_t(vSmall) | (uint64_t(vBig) << 32ull);
			_tri0 = tri0;
			_tri1 = ~0u;
			_tri0EdgeIdx = edgeIdx;
			_tri1EdgeIdx = ~0u;
		}
	};

	void TriListToTriListWithAdjacency(
		IteratorRange<unsigned*> outputTriListWithAdjacency,
		IteratorRange<const unsigned*> inputTriListIndexBuffer)
	{
		// 1. Find all of the edges in the input buffer, and generate an edge list buffer
		// 		If we find any edge that are used in more than 2 triangles, we must throw an exception
		// 2. while doing this, resolve the adjacency by finding the "third vertex" that completes the tri along with the edge
		// 3. write out an index buffer with the adjacent vertex indices in interleaved order
		// when there is no adjacency for an edge, we duplicate one of the vertex indices from the edge

		const unsigned inputTriCount = inputTriListIndexBuffer.size() / 3;
		const unsigned estimateEdgeCount = inputTriCount * 3 / 2;		// assuming each edge is used twice
		
		std::vector<WorkingEdge> edges;
		edges.reserve(estimateEdgeCount + estimateEdgeCount/2);			// estimateEdgeCount is best case, so add some extra

		std::vector<unsigned> adjacentVertices;
		adjacentVertices.reserve(inputTriListIndexBuffer.size());

		assert(inputTriListIndexBuffer.size()%3 == 0);
		const unsigned thirdVerticesIdx[] { 2, 0, 1 };
		for (auto trii=inputTriListIndexBuffer.begin(); trii!=inputTriListIndexBuffer.end(); trii+=3) {
			auto v0 = *(trii+0), v1 = *(trii+1), v2 = *(trii+2);
			if (v0 == v1 || v1 == v2 || v0 == v2) {
				// degenerate -- no point in finding adjacencies or make this an adjacency of anything else, since it's just a line
				adjacentVertices.push_back(~0u);
				adjacentVertices.push_back(~0u);
				adjacentVertices.push_back(~0u);
				continue;
			}

			auto triIdx = (unsigned)std::distance(inputTriListIndexBuffer.begin(), trii) / 3u;
			WorkingEdge es[] { WorkingEdge{v0, v1, 0, triIdx}, WorkingEdge{v1, v2, 1, triIdx}, WorkingEdge{v2, v0, 2, triIdx} };
			
			for (unsigned c=0; c<3; ++c) {
				auto i = std::lower_bound(edges.begin(), edges.end(), es[c]);
				if (i != edges.end() && i->_id == es[c]._id) {
					if (i->_tri1 != ~0u) {
						// what do we do with edges that are used by more than 2 triangles? We can try to figure out which of the triangles are most likely to
						// contribute to a silhouette...? Or we can just disable adjacency information for this edge entirely
						Log(Warning) << "Some edges used more than 2 times when building adjacency information in TriListToTriListWithAdjacency" << std::endl;
						adjacentVertices.push_back(~0u);
						adjacentVertices[i->_tri0*3+i->_tri0EdgeIdx] = ~0u;		// disable previously calculated adjacency
						adjacentVertices[i->_tri1*3+i->_tri1EdgeIdx] = ~0u;
						continue;
					}
					i->_tri1 = triIdx;
					i->_tri1EdgeIdx = c;

					// third vertex of tri0 becomes our adjacent vertex
					auto* adjacentTri = &inputTriListIndexBuffer[i->_tri0*3];
					auto tri0ThirdVertex = *(adjacentTri+thirdVerticesIdx[i->_tri0EdgeIdx]);
					adjacentVertices.push_back(tri0ThirdVertex);

					// third vertex of ours becomes the adjacency for the other triangle
					assert(adjacentVertices[i->_tri0*3+i->_tri0EdgeIdx] == ~0u);
					adjacentVertices[i->_tri0*3+i->_tri0EdgeIdx] = trii[thirdVerticesIdx[c]];
				} else {
					edges.insert(i, es[c]);
					adjacentVertices.push_back(~0u);	// no adjacency, but may get one later
				}
			}
		}

		// Edges now contains a list of all edges in the mesh, with the indices of the triangles that include that edge
		// adjacentVertices contains the list of adjacent vertices in edge order
		// just need to interleave them both

		assert(outputTriListWithAdjacency.size() == inputTriListIndexBuffer.size()*2);	// 6 indices per triangle, rather than 3s
		assert(adjacentVertices.size() == inputTriListIndexBuffer.size());

		auto i = inputTriListIndexBuffer.begin();
		auto i2 = adjacentVertices.begin();
		auto o = outputTriListWithAdjacency.begin();
		for (; i!=inputTriListIndexBuffer.end(); ++i, ++i2) {
			*o++ = *i; 
			*o++ = (*i2 != ~0u) ? *i2 : *i; 		// when no adjacency, just duplicate the preceeding vertex
		}
	}

	std::vector<uint8_t> BuildAdjacencyIndexBuffer(
		RenderCore::Assets::GeoProc::MeshDatabase& mesh, 
		const void* rawIb, size_t indexCount, Format ibFormat,
		Topology topology)
	{
		// Generate an adjacency index buffer for the given input mesh.
		// We need to find unique vertex positions; rather than relying on the unified vertices in the mesh database
		
		auto posElement = mesh.FindElement("POSITION");
		if (posElement == ~0u)
			return {};		// can't be generated because there are no positions

		// if the position streams have a vertex map, we can assume this is a mapping from unified vertex index
		// to unique position index. It's best to reuse this, if this mapping already exists -- because it might have
		// be specifically authored in a content tool
        // Either way, we'll combine bitwise identical positions, because that should not have any negative effects
		auto& stream = mesh.GetStreams()[posElement];
        auto mappingToUniquePositions = MapToBitwiseIdenticals(*stream.GetSourceData(), stream.GetVertexMap(), true);

		std::vector<unsigned> remappedIndexBuffer;
		remappedIndexBuffer.reserve(indexCount);
		if (ibFormat == Format::R32_UINT) {
			for (const auto i:MakeIteratorRange((const unsigned*)rawIb, (const unsigned*)rawIb+indexCount))
				remappedIndexBuffer.push_back(mappingToUniquePositions[i]);
		} else if (ibFormat == Format::R16_UINT) {
			for (const auto i:MakeIteratorRange((const uint16_t*)rawIb, (const uint16_t*)rawIb+indexCount))
				remappedIndexBuffer.push_back(mappingToUniquePositions[i]);
		} else if (ibFormat == Format::R8_UINT) {
			for (const auto i:MakeIteratorRange((const uint8_t*)rawIb, (const uint8_t*)rawIb+indexCount))
				remappedIndexBuffer.push_back(mappingToUniquePositions[i]);
		} else
			Throw(std::runtime_error("Unsupported index format in BuildAdjacencyIndexBuffer"));

		// Now we have an buffer with indices of unique positions, we can build a topological buffer
		std::vector<uint8_t> adjacencyIndexBuffer;
		adjacencyIndexBuffer.resize(remappedIndexBuffer.size()*2*sizeof(unsigned));
		TriListToTriListWithAdjacency(
			MakeIteratorRange((unsigned*)AsPointer(adjacencyIndexBuffer.begin()), (unsigned*)AsPointer(adjacencyIndexBuffer.end())),
			MakeIteratorRange(remappedIndexBuffer));

		// The new index buffer still has indicies to unique positions. We need to convert this to the unified vertex indices, so that
		// it's useful in shaders. There will be multiple options for each unique vertex position; probably with different normals and so
		// on... Let's just assume we're only interested in the vertex position and choose randomly

		std::vector<unsigned> demapBuffer;
		demapBuffer.resize(indexCount, ~0u);		// overestimate
		for (unsigned c=0; c<mappingToUniquePositions.size(); ++c) {
			auto m = mappingToUniquePositions[c];
			if (demapBuffer[m] == ~0u) demapBuffer[m] = c;
		}

		for (auto& i:MakeIteratorRange((unsigned*)adjacencyIndexBuffer.data(), (unsigned*)AsPointer(adjacencyIndexBuffer.end()))) {
			assert(demapBuffer[i] != ~0u);
			i = demapBuffer[i];
		}

		// go back to the original index format; there's no reason to make it wider
		if (ibFormat == Format::R32_UINT) {
			return adjacencyIndexBuffer;
		} else if (ibFormat == Format::R16_UINT) {
			std::vector<uint8_t> convertedResult;
			convertedResult.resize(adjacencyIndexBuffer.size()/2);
			std::copy(
				(const unsigned*)adjacencyIndexBuffer.data(), (const unsigned*)AsPointer(adjacencyIndexBuffer.end()),
				(uint16_t*)convertedResult.data());
			return convertedResult;
		} else if (ibFormat == Format::R8_UINT) {
			std::vector<uint8_t> convertedResult;
			convertedResult.resize(adjacencyIndexBuffer.size()/4);
			std::copy(
				(const unsigned*)adjacencyIndexBuffer.data(), (const unsigned*)AsPointer(adjacencyIndexBuffer.end()),
				(uint8_t*)convertedResult.data());
			return convertedResult;
		} else {
			assert(0);
			return {};
		}
	}

}}}


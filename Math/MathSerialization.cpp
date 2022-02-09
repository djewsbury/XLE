// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MathSerialization.h"
#include "Transformations.h"

namespace XLEMath
{
    std::ostream& CompactTransformDescription(std::ostream& str, const Float4x4& transform)
    {
        bool goodDecomposition = false;
        ScaleRotationTranslationM decomposed{transform, goodDecomposition};

        auto originalPrecision = str.precision(4);

        if (goodDecomposition) {
            bool first = true;
            if (!Equivalent(decomposed._scale, Float3(1,1,1), 1e-3f)) {
                if (Equivalent(decomposed._scale[0], decomposed._scale[1], 1e-3f) && Equivalent(decomposed._scale[0], decomposed._scale[2], 1e-3f)) {
                    str << "uniform scale " << decomposed._scale[0];
                } else {
                    bool scaleX = !Equivalent(decomposed._scale[0], 1.0f, 1e-3f);
                    bool scaleY = !Equivalent(decomposed._scale[1], 1.0f, 1e-3f);
                    bool scaleZ = !Equivalent(decomposed._scale[2], 1.0f, 1e-3f);
                    if (scaleX && !scaleY && !scaleZ) str << "scale X " << decomposed._scale[0];
                    else if (scaleY && !scaleX && !scaleZ) str << "scale Y " << decomposed._scale[1];
                    else if (scaleZ && !scaleX && !scaleY) str << "scale Z " << decomposed._scale[2];
                    else str << "scale {" << decomposed._scale[0] << ", " << decomposed._scale[1] << ", " << decomposed._scale[2] << "}";
                }
                first = false;
            }

            if (!Equivalent(decomposed._rotation, Identity<Float3x3>(), 1e-3f)) {
                const cml::EulerOrder eulerOrder = cml::euler_order_yxz;
                Float3 ypr = cml::matrix_to_euler<Float3x3, Float3x3::value_type>(decomposed._rotation, eulerOrder);

                const char* labels[] = { "rotate Y", "rotate X", "rotate Z" };
                for (unsigned c=0; c<3; ++c) {
                    if (Equivalent(ypr[c], 0.f, 1e-3f)) continue;
                    if (!first) str << ", then ";
                    str << labels[c] << " " << Rad2Deg(ypr[c]) << "deg";
                    first = false;
                }
            }

            if (!Equivalent(decomposed._translation, Zero<Float3>(), 1e-3f)) {
                if (!first) str << ", then ";
                str << "translate {" << decomposed._translation[0] << ", " << decomposed._translation[1] << ", " << decomposed._translation[2] << "}";
                first = false;
            }
        } else {
            str << "skewed matrix ";
            for (unsigned i=0; i<4; ++i) {
                if (i == 0) str << "{";
                else str << ", {";
                for (unsigned j=0; j<4; ++j) {
                    if (j != 0) str << ", ";
                    str << transform(i, j);
                }
                str << "}";
            }
        }

        str.precision(originalPrecision);

        return str;
    }
}

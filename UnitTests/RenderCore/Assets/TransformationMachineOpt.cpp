// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../../RenderCore/Assets/TransformationCommands.h"
#include "../../../Math/Geometry.h"
#include "../../../OSServices/Log.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include <vector>
#include <random>
#include <iostream>
#include <sstream>

using namespace Catch::literals;
namespace UnitTests
{
    static Float3 RandomUnitVector(std::mt19937& rng)
    {
        return SphericalToCartesian(Float3(
            Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng)),
            Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng)),
            1.f));
    }

    static float RandomSign(std::mt19937& rng) { return ((std::uniform_real_distribution<>(-1.f, 1.f)(rng) < 0.f) ? -1.f : 1.f); }
    static float RandomScaleValue(std::mt19937& rng)
    {
        auto a = RandomSign(rng) * (float)std::uniform_real_distribution<>(1.f, 5.f)(rng);
        return (std::uniform_real_distribution<>(-1.f, 1.f)(rng) < 0.f) ? a : 1.f/a;
    }

    static Float3 RandomScaleVector(std::mt19937& rng)
    {
        return Float3(RandomScaleValue(rng), RandomScaleValue(rng), RandomScaleValue(rng));
    }

    static Float3 RandomTranslationVector(std::mt19937& rng)
    {
        return Float3(
            (float)std::uniform_real_distribution<>(-1000.f, 1000.f)(rng),
            (float)std::uniform_real_distribution<>(-1000.f, 1000.f)(rng),
            (float)std::uniform_real_distribution<>(-1000.f, 1000.f)(rng));
    }

    static Float4x4 RandomComplexTransform(std::mt19937& rng)
    {
        auto rotationAxis = RandomUnitVector(rng);
        auto rotationAngle = Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng));
        auto scale = RandomScaleVector(rng);
        auto translation = RandomTranslationVector(rng);
        return AsFloat4x4(
            ScaleRotationTranslationM(
                scale, MakeRotationMatrix(rotationAxis, rotationAngle), translation));
    }

    static void InsertRandomTransforms(std::vector<uint32>& machine, std::mt19937& rng, unsigned transformCount, bool writeComments)
    {
        using namespace RenderCore::Assets;
        // {
        //     for (unsigned c=0; c<3; ++c) {
        //         ScaleRotationTranslationM temp(
        //             RandomScaleVector(rng), Identity<Float3x3>(), RandomTranslationVector(rng));
        //         auto transform = AsFloat4x4(temp);
        // 
        //         if (writeComments) {
        //             machine.push_back((uint32)TransformStackCommand::Comment);
        //             std::string comment = "Transform";
        //             comment.resize(64, 0);
        //             machine.insert(machine.end(), (uint32*)AsPointer(comment.begin()), (uint32*)AsPointer(comment.begin()+64));
        //         }
        // 
        //         machine.push_back((uint32)TransformStackCommand::TransformFloat4x4_Static);
        //         machine.insert(machine.end(), (uint32*)(&transform), (uint32*)(&transform + 1));
        //     }
        //     return;
        // }

        for (auto t=0u; t<transformCount; ++t) {
            auto type = std::uniform_int_distribution<>(0, 6)(rng);
            Float4x4 transform;
            std::string comment;
            if (type == 0) {
                transform = RandomComplexTransform(rng);
                comment = "Complex";
            } else if (type == 1) {
                transform = AsFloat4x4(RandomTranslationVector(rng));
                comment = "Translation";
            } else if (type == 2) {
                auto rotationAxis = RandomUnitVector(rng);
                auto rotationAngle = Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng));
                transform = AsFloat4x4(MakeRotationMatrix(rotationAxis, rotationAngle));
                comment = "Axis/Angle";
            } else if (type == 3) {
                auto rotationAngle = Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng));
                transform = AsFloat4x4(RotationX(rotationAngle));
                comment = "RotationX";
            } else if (type == 4) {
                transform = AsFloat4x4(UniformScale(RandomScaleValue(rng)));
                comment = "UniformScale";
            } else if (type == 5) {
                transform = AsFloat4x4(ArbitraryScale(RandomScaleVector(rng)));
                comment = "AbitraryScale";
            } else {
                ScaleRotationTranslationM temp(
                    RandomScaleVector(rng), Identity<Float3x3>(), RandomTranslationVector(rng));
                transform = AsFloat4x4(temp);
                comment = "ScaleAndTranslation";
            }

            if (writeComments) {
                machine.push_back((uint32)TransformStackCommand::Comment);
                comment.resize(64, 0);
                machine.insert(machine.end(), (uint32*)AsPointer(comment.begin()), (uint32*)AsPointer(comment.begin()+64));
            }

            machine.push_back((uint32)TransformStackCommand::TransformFloat4x4_Static);
            machine.insert(machine.end(), (uint32*)(&transform), (uint32*)(&transform + 1));
        }
    }

    static void LogTransMachines(std::ostream& stream, IteratorRange<const uint32*> orig, IteratorRange<const uint32*> opt, unsigned index)
    {
        using namespace RenderCore::Assets;
        stream << "============== Machine (" << index << ") ==============" << std::endl;
        TraceTransformationMachine(stream, orig, nullptr, nullptr);
        stream << " O P T I M I Z E S   T O : " << std::endl;
        TraceTransformationMachine(stream, opt, nullptr, nullptr);
    }

    class Optimizer : public RenderCore::Assets::ITransformationMachineOptimizer
    {
    public:
        bool CanMergeIntoOutputMatrix(unsigned outputMatrixIndex) const { return false; }
        void MergeIntoOutputMatrix(unsigned outputMatrixIndex, const Float4x4& transform) {}
        ~Optimizer() {}
    };

    static bool RelativeEquivalent(const Float4x4& lhs, const Float4x4& rhs, float threshold)
    {
        for (unsigned j=0;j<4;++j)
            for (unsigned i=0;i<4;++i) {
                auto diff = XlAbs(lhs(i, j) - rhs(i, j));
                if (diff > threshold * std::max(XlAbs(lhs(i, j)), XlAbs(rhs(i, j))))
                    return false;
            }
        return true;
    }

    TEST_CASE( "TransformationMachineOpt-TransformationSimplification", "[rendercore_assets]" )
    {
        using namespace RenderCore::Assets;

        std::mt19937 rng(std::random_device().operator()());
        const auto testCount = 1000u;

        {
            auto initialMat = AsFloat4x4(ScaleRotationTranslationM(
                RandomScaleVector(rng), 
                MakeRotationMatrix(RandomUnitVector(rng), Deg2Rad((float)std::uniform_real_distribution<>(-180.f, 180.f)(rng))),
                RandomTranslationVector(rng)));

            ScaleRotationTranslationM temp(
                RandomScaleVector(rng), Identity<Float3x3>(), RandomTranslationVector(rng));
            auto tMat = Combine(AsFloat4x4(temp), initialMat);

            auto cMat = initialMat;
            Combine_IntoRHS(temp._translation, cMat);
            Combine_IntoRHS(ArbitraryScale(temp._scale), cMat);
            // cMat = Combine(AsFloat4x4(ArbitraryScale(temp._scale)), cMat);
            const float tolerance = 1e-3f;
            REQUIRE(Equivalent(cMat, tMat, tolerance));
        }

            // Do this twice... once with comments; once without.
            // Without comments the machine will be collapsed down to
            // a single matrix. With comments, each individual transform
            // will be simplified down to it's most basic form.
        for (auto i=0u; i<2; ++i) {
            for (auto c=0u; c<testCount; ++c) {
                    // We will build a transformation machine, and then optimize it
                    // Then we will verify that the output is the same each time.
                std::vector<uint32> machine;
                InsertRandomTransforms(machine, rng, 40, i==0);

                    // write out a single output matrix:
                machine.push_back((uint32)TransformStackCommand::WriteOutputMatrix);
                machine.push_back(0);

                Optimizer opt;
                auto optimized = OptimizeTransformationMachine(MakeIteratorRange(machine), opt);
                // std::stringstream str;
                // LogTransMachines(str, MakeIteratorRange(machine), MakeIteratorRange(optimized), c);
                // INFO(str.str());

                Float4x4 resultUnOpt, resultOpt;
                GenerateOutputTransforms(MakeIteratorRange(&resultUnOpt, &resultUnOpt+1), nullptr, MakeIteratorRange(machine));
                GenerateOutputTransforms(MakeIteratorRange(&resultOpt, &resultOpt+1), nullptr, MakeIteratorRange(optimized));

                const float tolerance = 3e-2f;
                REQUIRE(RelativeEquivalent(resultUnOpt, resultOpt, tolerance));
            }
        }
    }
}

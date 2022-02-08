// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TransformationCommands.h"
#include "../../OSServices/Log.h"
#include "../../Math/MathSerialization.h"
#include <sstream>

#pragma warning(disable:4127)
#pragma warning(disable:4505)       // unreferenced function removed

namespace RenderCore { namespace Assets
{
    static bool IsStaticCommand(TransformCommand cmd)
    {
        return  cmd == TransformCommand::TransformFloat4x4_Static
            ||  cmd == TransformCommand::Translate_Static
            ||  cmd == TransformCommand::RotateX_Static
            ||  cmd == TransformCommand::RotateY_Static
            ||  cmd == TransformCommand::RotateZ_Static
            ||  cmd == TransformCommand::RotateAxisAngle_Static
            ||  cmd == TransformCommand::RotateQuaternion_Static
            ||  cmd == TransformCommand::UniformScale_Static
            ||  cmd == TransformCommand::ArbitraryScale_Static;
    }
    template<typename Iterator>
        Iterator NextTransformationCommand_(Iterator cmd)
    {
        switch ((TransformCommand)*cmd) {
        case TransformCommand::PushLocalToWorld:           return cmd+1+(0);
        case TransformCommand::PopLocalToWorld:            return cmd+1+(1);
        case TransformCommand::TransformFloat4x4_Static:   return cmd+1+(16);
        case TransformCommand::Translate_Static:           return cmd+1+(3);
        case TransformCommand::RotateX_Static:             return cmd+1+(1);
        case TransformCommand::RotateY_Static:             return cmd+1+(1);
        case TransformCommand::RotateZ_Static:             return cmd+1+(1);
        case TransformCommand::RotateAxisAngle_Static:              return cmd+1+(4);
		case TransformCommand::RotateQuaternion_Static:    return cmd+1+(4);
        case TransformCommand::UniformScale_Static:        return cmd+1+(1);
        case TransformCommand::ArbitraryScale_Static:      return cmd+1+(3);

        case TransformCommand::TransformFloat4x4_Parameter:
        case TransformCommand::Translate_Parameter:
        case TransformCommand::RotateX_Parameter:
        case TransformCommand::RotateY_Parameter:
        case TransformCommand::RotateZ_Parameter:
        case TransformCommand::RotateAxisAngle_Parameter:
		case TransformCommand::RotateQuaternion_Parameter:
        case TransformCommand::UniformScale_Parameter:
        case TransformCommand::ArbitraryScale_Parameter:
            return cmd+1+(1);

        case TransformCommand::WriteOutputMatrix:
            return cmd+1+(1);

        case TransformCommand::TransformFloat4x4AndWrite_Static: return cmd+1+(1+16);
        case TransformCommand::TransformFloat4x4AndWrite_Parameter: return cmd+1+(1+1);

        case TransformCommand::Comment: return cmd+1+(64/4);

        case TransformCommand::BindingPoint_0:
            return cmd+1+(2);

        case TransformCommand::BindingPoint_1:
            cmd += 1+(2);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            return cmd;

        case TransformCommand::BindingPoint_2:
            cmd += 1+(2);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            return cmd;

        case TransformCommand::BindingPoint_3:
            cmd += 1+(2);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            assert(IsStaticCommand((TransformCommand)*cmd));
            cmd = NextTransformationCommand_(cmd);
            return cmd;

        default: 
            assert(0);
            return cmd+1;
        }
    }

    const uint32_t* NextTransformationCommand(const uint32_t* cmd) { return NextTransformationCommand_(cmd); }

    T1(Iterator) static Iterator SkipUntilPop(Iterator i, Iterator end, signed& finalIdentLevel)
    {
        finalIdentLevel = 1;
        for (; i!=end;) {
            if (*i == (uint32_t)TransformCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                finalIdentLevel -= signed(popCount);
                if (finalIdentLevel <= 0)
                    return i;
            } else if (*i == (uint32_t)TransformCommand::PushLocalToWorld)
                ++finalIdentLevel;
            i = NextTransformationCommand_(i);
        }
        return end;
    }

    static bool IsTransformCommand(TransformCommand cmd)
    {
        return 
                (cmd >= TransformCommand::TransformFloat4x4_Static && cmd <= TransformCommand::ArbitraryScale_Static)
            ||  (cmd >= TransformCommand::TransformFloat4x4_Parameter && cmd <= TransformCommand::ArbitraryScale_Parameter);
    }

	static bool IsOutputCommand(TransformCommand cmd)
    {
        return (cmd >= TransformCommand::WriteOutputMatrix && cmd <= TransformCommand::TransformFloat4x4AndWrite_Parameter);
    }

    T1(Iterator) 
        static Iterator IsRedundantPush(Iterator i, Iterator end, bool& isRedundant)
    {
        // Scan forward... if the transform isn't modified at this level, or if
        // the matrix isn't used after the pop, then the push/pop is redundant
        assert(*i == (uint32_t)TransformCommand::PushLocalToWorld);
        ++i;

        bool foundTransformCmd = false;
        for (;i<end;) {
            auto cmd = TransformCommand(*i);
            if (IsTransformCommand(cmd)) {
                foundTransformCmd = true;
            } else if (cmd == TransformCommand::PushLocalToWorld) {
                ++i;
                signed finalIdentLevel = 0;
                i = SkipUntilPop(i, end, finalIdentLevel);
                if (finalIdentLevel < 0) {
                    isRedundant = (finalIdentLevel < -1) || !foundTransformCmd || (i+2) == end;
                    return i;
                }   
            } else if (cmd == TransformCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                isRedundant = (popCount > 1) || !foundTransformCmd || (i+2) == end;
                return i;
            }

            i = NextTransformationCommand_(i);
        }
        isRedundant = true;
        return i;
    }

    static void RemoveRedundantPushes(std::vector<uint32_t>& cmdStream)
    {
            // First, we just want to convert series of pop operations into
            // a single pop.
        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            if (*i == (uint32_t)TransformCommand::PopLocalToWorld) {
                if (    (cmdStream.end() - i) >= 4
                    &&  *(i+2) == (uint32_t)TransformCommand::PopLocalToWorld) {
                        // combine these 2 pops into a single pop command
                    auto newPopCount = *(i+1) + *(i+3);
                    i = cmdStream.erase(i, i+2);
                    *(i+1) = newPopCount;
                } else {
                    i += 2;
                }
            } else {
                i = NextTransformationCommand_(i);
            }
        }

            // Now look for push operations that are redundant
        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            if (*i == (uint32_t)TransformCommand::PushLocalToWorld) {
                bool isRedundant = false;
                auto pop = IsRedundantPush(i, cmdStream.end(), isRedundant);
                if (isRedundant) {
                    if (pop < cmdStream.end()) {
                        auto& popCount = *(pop+1);
                        if (popCount > 1) {
                            popCount--;
                        } else {
                            cmdStream.erase(pop, pop+2);
                        }
                    }
                    i = cmdStream.erase(i);
                    continue;
                }
            } 

            i = NextTransformationCommand_(i);
        }
    }

	T1(Iterator) 
        static bool HasFollowingOutputCommand(Iterator i, Iterator end)
    {
		signed pushDepth = 0;
		for (;i<end && pushDepth >= 0;) {
            auto cmd = TransformCommand(*i);
            if (IsOutputCommand(cmd)) {
                return true;
            } else if (cmd == TransformCommand::PushLocalToWorld) {
                ++pushDepth;
            } else if (cmd == TransformCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                pushDepth -= popCount;
            }

            i = NextTransformationCommand_(i);
        }
		return false;
	}

	static void RemoveRedundantTransformationCommands(std::vector<uint32_t>& cmdStream)
	{
			// For each transformation command we come across, scan forward to
			// see if it's used as part of an WriteOutputMatrix operation
			// If we don't encounter a WriteOutputMatrix, the transformation command
			// can not have an affect on the output, so is therefore redundant
		std::stringstream str;
		str << " ---------- before RemoveRedundantTransformationCommands ------- " << std::endl;
		TraceTransformationMachine(
			str, 
			MakeIteratorRange(cmdStream),
			[](unsigned) { return std::string{}; },
			[](unsigned) { return std::string{}; });

		for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
			auto nexti = NextTransformationCommand_(i);
			auto cmd = TransformCommand(*i);
            if (IsTransformCommand(cmd)) {
				if (!HasFollowingOutputCommand(nexti, cmdStream.end())) {
					i = cmdStream.erase(i, nexti);
					continue;
				}
			}

			i = nexti;
		}

		str << " ---------- after RemoveRedundantTransformationCommands ------- " << std::endl;
		TraceTransformationMachine(
			str, 
			MakeIteratorRange(cmdStream),
			[](unsigned) { return std::string{}; },
			[](unsigned) { return std::string{}; });

		auto debug = str.str();
		(void)debug;
	}

    enum MergeType { StaticTransform, OutputMatrix, Push, Pop, Blocker };
    static MergeType AsMergeType(TransformCommand cmd)
    {
        switch (cmd) {
        case TransformCommand::TransformFloat4x4_Static:
        case TransformCommand::Translate_Static:
        case TransformCommand::RotateX_Static:
        case TransformCommand::RotateY_Static:
        case TransformCommand::RotateZ_Static:
        case TransformCommand::RotateAxisAngle_Static:
		case TransformCommand::RotateQuaternion_Static:
        case TransformCommand::UniformScale_Static:
        case TransformCommand::ArbitraryScale_Static:  return MergeType::StaticTransform;

        case TransformCommand::PushLocalToWorld:       return MergeType::Push;
        case TransformCommand::PopLocalToWorld:        return MergeType::Pop;
        case TransformCommand::WriteOutputMatrix:      return MergeType::OutputMatrix;

        default: return MergeType::Blocker;
        }
    }

    static const uint32_t* FindDownstreamInfluences(
        const uint32_t* i, IteratorRange<const uint32_t*> range, 
        std::vector<size_t>& result, signed& finalIdentLevel)
    {
            // Search forward and find the commands that are going to directly 
            // effected by the transform before i
        for (;i<range.end();) {
            auto type = AsMergeType(TransformCommand(*i));
            if (type == MergeType::StaticTransform || type == MergeType::Blocker) {
                // Hitting a static transform blocks any further searches
                // We can just skip until we pop out of this block
                result.push_back(i-range.begin());
                i = SkipUntilPop(i, range.end(), finalIdentLevel);
                return NextTransformationCommand_(i);
            } else if (type == MergeType::OutputMatrix) {
                result.push_back(i-range.begin());
                i = NextTransformationCommand_(i);
            } else if (type == MergeType::Pop) {
                auto popCount = *(i+1);
                finalIdentLevel = popCount-1;
                return NextTransformationCommand_(i);
            } else if (type == MergeType::Push) {
                // Hitting a push operation means we have to branch.
                // Here, we must find all of the influences in the
                // pushed branch, and then continue on from the next
                // pop
                i = FindDownstreamInfluences(i+1, range, result, finalIdentLevel);
                if (finalIdentLevel < 0) {
                    ++finalIdentLevel;
                    return i;
                }
            }
        }
        finalIdentLevel = 1;
        return i;
    }

    static bool ShouldDoMerge(
        IteratorRange<size_t*> influences, 
        IteratorRange<const uint32_t*> cmdStream,
        ITransformationMachineOptimizer& optimizer)
    {
        signed commandAdjustment = -1;
        for (auto c:influences) {
            switch (AsMergeType(TransformCommand(cmdStream[c]))) {
            case MergeType::StaticTransform:
                    // This other transform might be merged away, also -- if it can be merged further.
                    // so let's consider it another dropped command
                --commandAdjustment;    
                break;
            case MergeType::Blocker:
                ++commandAdjustment;
                break;
            case MergeType::OutputMatrix:
                if (!optimizer.CanMergeIntoOutputMatrix(cmdStream[c+1]))
                    ++commandAdjustment;
                break;

            default:
                assert(0); // push & pop shouldn't be registered as influences
                break;
            }
        }
        return commandAdjustment < 0;
    }

    static bool ShouldDoSimpleMerge(TransformCommand lhs, TransformCommand rhs)
    {
        if (    lhs == TransformCommand::TransformFloat4x4_Static
            ||  rhs == TransformCommand::TransformFloat4x4_Static)
            return true;

        switch (lhs) {
        case TransformCommand::Translate_Static:
                // only merge into another translate
            return rhs == TransformCommand::Translate_Static;

        case TransformCommand::RotateX_Static:
        case TransformCommand::RotateY_Static:
        case TransformCommand::RotateZ_Static:
        case TransformCommand::RotateAxisAngle_Static:
		case TransformCommand::RotateQuaternion_Static:
                // only merge into another rotate
            return (rhs == TransformCommand::RotateX_Static)
                || (rhs == TransformCommand::RotateY_Static)
                || (rhs == TransformCommand::RotateZ_Static)
                || (rhs == TransformCommand::RotateAxisAngle_Static)
				|| (rhs == TransformCommand::RotateQuaternion_Static)
                ;

        case TransformCommand::UniformScale_Static:
        case TransformCommand::ArbitraryScale_Static:
                // only merge into another scale
            return (rhs == TransformCommand::UniformScale_Static)
                || (rhs == TransformCommand::ArbitraryScale_Static)
                ;

        default:
            break;
        }

        return false;
    }

    static Float4x4 PromoteToFloat4x4(const uint32_t* cmd)
    {
        switch (TransformCommand(*cmd)) {
        case TransformCommand::TransformFloat4x4_Static:
            return *(const Float4x4*)(cmd+1);

        case TransformCommand::Translate_Static:
            return AsFloat4x4(*(const Float3*)(cmd+1));

        case TransformCommand::RotateX_Static:
            return AsFloat4x4(*(const RotationX*)(cmd+1));

        case TransformCommand::RotateY_Static:
            return AsFloat4x4(*(const RotationY*)(cmd+1));

        case TransformCommand::RotateZ_Static:
            return AsFloat4x4(*(const RotationZ*)(cmd+1));

        case TransformCommand::RotateAxisAngle_Static:
            return AsFloat4x4(*(const ArbitraryRotation*)(cmd+1));

		case TransformCommand::RotateQuaternion_Static:
            return AsFloat4x4(*(const Quaternion*)(cmd+1));

        case TransformCommand::UniformScale_Static:
            return AsFloat4x4(*(const UniformScale*)(cmd+1));

        case TransformCommand::ArbitraryScale_Static:
            return AsFloat4x4(*(const ArbitraryScale*)(cmd+1));

        default:
            assert(0);
            return Identity<Float4x4>();
        }
    }

    static void DoTransformMerge(
        std::vector<uint32_t>& cmdStream, 
        std::vector<uint32_t>::iterator dst,
        std::vector<uint32_t>::iterator mergingCmd)
    {
        // If the transforms are of exactly the same type (and not RotateAxisAngle_Static)
        // then we can merge into a final transform that is the same type.
        // Otherwise we should merge to Float4x4. In some cases, the final Final4x4
        // can be converted into a simplier transform... We will go back through
        // and optimize those cases later.
        auto typeDst = TransformCommand(*dst);
        auto typeMerging = TransformCommand(*mergingCmd);
        if (typeDst == TransformCommand::Translate_Static
            && typeMerging == TransformCommand::Translate_Static) {

            auto& dstTrans = *(Float3*)AsPointer(dst+1);
            auto& mergeTrans = *(Float3*)AsPointer(mergingCmd+1);
            dstTrans += mergeTrans;
        } else if ((typeDst == TransformCommand::RotateX_Static
            && typeMerging == TransformCommand::RotateX_Static)
            || (typeDst == TransformCommand::RotateY_Static
            && typeMerging == TransformCommand::RotateY_Static)
            || (typeDst == TransformCommand::RotateZ_Static
            && typeMerging == TransformCommand::RotateZ_Static)) {

            auto& dstTrans = *(float*)AsPointer(dst+1);
            auto& mergeTrans = *(float*)AsPointer(mergingCmd+1);
            dstTrans += mergeTrans;
        } else if (typeDst == TransformCommand::UniformScale_Static
            && typeMerging == TransformCommand::UniformScale_Static) {

            auto& dstTrans = *(float*)AsPointer(dst+1);
            auto& mergeTrans = *(float*)AsPointer(mergingCmd+1);
            dstTrans *= mergeTrans;
        } else if (typeDst == TransformCommand::ArbitraryScale_Static
            && typeMerging == TransformCommand::ArbitraryScale_Static) {

            auto& dstTrans = *(Float3*)AsPointer(dst+1);
            auto& mergeTrans = *(Float3*)AsPointer(mergingCmd+1);
            dstTrans[0] *= mergeTrans[0];
            dstTrans[1] *= mergeTrans[1];
            dstTrans[2] *= mergeTrans[2];
        } else if (typeDst == TransformCommand::TransformFloat4x4_Static
            && typeMerging == TransformCommand::TransformFloat4x4_Static) {

            auto& dstTrans = *(Float4x4*)AsPointer(dst+1);
            auto& mergeTrans = *(Float4x4*)AsPointer(mergingCmd+1);
            dstTrans = Combine(dstTrans, mergeTrans);
        } else {
            // Otherwise we need to promote both transforms into Float4x4, and we will push
            // a new Float4x4 transform into the space in "dst"
            auto dstTransform = PromoteToFloat4x4(AsPointer(dst));
            auto mergeTransform = PromoteToFloat4x4(AsPointer(mergingCmd));
            auto t = cmdStream.erase(dst+1, NextTransformationCommand_(dst));
            assert(t==dst+1);
            *dst = (uint32_t)TransformCommand::TransformFloat4x4_Static;
            auto finalTransform = Combine(dstTransform, mergeTransform);
            cmdStream.insert(dst+1, (uint32_t*)&finalTransform, (uint32_t*)(&finalTransform + 1));
        }
    }

    static void MergeSequentialTransforms(std::vector<uint32_t>& cmdStream, ITransformationMachineOptimizer& optimizer)
    {
        // Where we have multiple static transforms in a row, we can choose
        // to merge them together.
        // We can also merge static transforms into output matrices (where
        // this is marked as ok).
        // How this works depends on what comes immediately after the static
        // transform operation:
        //      (1) another static transform -- candidate for simple merge
        //      (2) parameter transform -- blocks merging
        //      (3) WriteOutputMatrix -- possibly merge into this output matrix
        //      (4) PushLocalToWorld -- creates a branching structure whereby
        //          the static transform is going to affect multiple future
        //          operations.
        // 
        // Consider the following command structure:
        // Here, the first transform can safely merged into 3 following transforms.
        // Since they are all transforms of the same type, there is a clear benefit
        // to doing this.
        // 
        // TransformFloat4x4_Static (diag:1, 1, 1, 1)
        // PushLocalToWorld
        //      TransformFloat4x4_Static (diag:1, 1, 1, 1)
        //      WriteOutputMatrix [1] (forge_wood)
        //      PopLocalToWorld (1)
        // PushLocalToWorld
        //      TransformFloat4x4_Static (diag:1, 1, 1, 1)
        //      WriteOutputMatrix [2] (forge_woll_brick)
        //      PopLocalToWorld (1)
        // PushLocalToWorld
        //      TransformFloat4x4_Static (diag:1, 1, 1, 1)
        //      WriteOutputMatrix [3] (forge_roof_wood)
        //      PopLocalToWorld (1)
        //
        // But, sometimes when a merge is possible, it might not be desirable.
        // Consider:
        // Translate_Static ... 
        // RotateX_Static ...
        // PushLocalToWorld
        //      TransformFloat4x4_Static (diag:1, 1, 1, 1)
        //      WriteOutputMatrix [1] (forge_wood)
        //      PopLocalToWorld (1)
        // PushLocalToWorld
        //      RotateZ_Static ...
        //      WriteOutputMatrix [2] (forge_woll_brick)
        //      PopLocalToWorld (1)
        // PushLocalToWorld
        //      UniformScale_Static ....
        //      WriteOutputMatrix [3] (forge_roof_wood)
        //      PopLocalToWorld (1)
        //
        // Here we have a more complex situation, because the transforms are all
        // different types. In some cases, merging may be preferable... In others,
        // the unmerged case might be best. There is no easy way to calculate the 
        // best combination of merges for cases like this. We could build up a list
        // of potential merges, and then analyse them all to find the best... Or we
        // could just upgrade them all into 4x4 matrices and merge them all.

        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            auto cmdType = AsMergeType(TransformCommand(*i));
            auto next = NextTransformationCommand_(i);
            if (cmdType == MergeType::StaticTransform) {
                    // Search forward & find influences
                std::vector<size_t> influences; signed finalIdentLevel = 0;
                FindDownstreamInfluences(
                    AsPointer(next), MakeIteratorRange(cmdStream),
                    influences, finalIdentLevel);

                    // We need to decide whether to merge or not.
                    // If we merge, we must do something for each
                    // downstream influence -- (either a merge, or push in
                    // a new command).
                    // We will do 2 calculations to decide whether or not
                    // to merge
                    //  1)  In the case where we have 1 static transform influence,
                    //      and that transform isn't going to be merged... We will merge
                    //      the two transforms for only certain combinations of transform
                    //      types
                    //  2)  In other cases, we will merge only if it reduces the overall
                    //      command count.
                if (influences.empty()) {
                    // no influences means this transform is redundant... just remove it
                    i = cmdStream.erase(i, next);
                    continue;
                } 
                
                bool isSpecialCase = false;
                if (influences.size() == 1 && AsMergeType(TransformCommand(cmdStream[influences[0]])) == MergeType::StaticTransform) {
                        // we have a single static transform influence. Let's check the influences for
                        // the other transform.
                    std::vector<size_t> secondaryInfluences; 
                    FindDownstreamInfluences(
                        &cmdStream[influences[0]], MakeIteratorRange(cmdStream),
                        secondaryInfluences, finalIdentLevel);
                    isSpecialCase = !ShouldDoMerge(MakeIteratorRange(secondaryInfluences), MakeIteratorRange(cmdStream), optimizer);
                }

                bool doMerge = false;
                if (isSpecialCase) {
                    doMerge = ShouldDoSimpleMerge(TransformCommand(*i), TransformCommand(cmdStream[influences[0]]));
                } else {
                    doMerge = ShouldDoMerge(MakeIteratorRange(influences), MakeIteratorRange(cmdStream), optimizer);
                }

                if (doMerge) {
                    auto iPos = std::distance(cmdStream.begin(), i);
                    auto nextPos = std::distance(cmdStream.begin(), next);

                        // If we've decided to do the merge, we need to walk through all of the influences
                        // and do something for each one (either a merge operation, or an insertion of another
                        // command). Let's walk through the influences in reverse order, to avoid screwing up
                        // our iterators immediately
                    for (auto r=influences.rbegin(); r!=influences.rend(); ++r) {
                        auto i2 = cmdStream.begin() + *r;
                        auto type = AsMergeType(TransformCommand(*i2));
                        if (type == MergeType::StaticTransform) {
                            DoTransformMerge(cmdStream, i2, i);
                            i = cmdStream.begin()+iPos; next = cmdStream.begin()+nextPos;
                        } else if (type == MergeType::Blocker) {
                            // this case always involves pushing a duplicate of the original command
                            // plus, we need a push/pop pair surrounding it
                            auto insertSize = next-i;
                            i2 = cmdStream.insert(i2, i, next);
                            i2 = cmdStream.insert(i2, (uint32_t)TransformCommand::PushLocalToWorld);
                            uint32_t popIntr[] = { (uint32_t)TransformCommand::PopLocalToWorld, 1 };
                            i2 = cmdStream.insert(i2+1+insertSize, &popIntr[0], &popIntr[2]);
                            i = cmdStream.begin()+iPos; next = cmdStream.begin()+nextPos;
                        } else if (type == MergeType::OutputMatrix) {
                            // We must either record this transform to be merged into
                            // this output transform, or we have to insert a push into here
                            auto outputMatrixIndex = *(i2+1);
                            bool canMerge = optimizer.CanMergeIntoOutputMatrix(outputMatrixIndex);
                            if (canMerge) {
                                    // If the same output matrix appears multiple times in our influences
                                    // list, then it will cause problems... We don't want to merge the same
                                    // transform into the same output matrix multiple times. But a single 
                                    // command list should write to each output matrix only once -- so this
                                    // should never happen.
                                for (auto r2=influences.rbegin(); r2<r; ++r2)
                                    if (    AsMergeType(TransformCommand(cmdStream[*r2])) == MergeType::OutputMatrix
                                        &&  cmdStream[*r2+1] == outputMatrixIndex)
                                        Throw(::Exceptions::BasicLabel("Writing to the same output matrix multiple times in transformation machine. Output matrix index: %u", outputMatrixIndex));

                                auto transform = PromoteToFloat4x4(AsPointer(i));
                                optimizer.MergeIntoOutputMatrix(outputMatrixIndex, transform);
                            } else {
                                auto insertSize = next-i; auto outputMatSize = NextTransformationCommand_(i2) - i2;
                                i2 = cmdStream.insert(i2, i, next);
                                i2 = cmdStream.insert(i2, (uint32_t)TransformCommand::PushLocalToWorld);
                                uint32_t popIntr[] = { (uint32_t)TransformCommand::PopLocalToWorld, 1 };
                                i2 = cmdStream.insert(i2+1+insertSize+outputMatSize, popIntr, ArrayEnd(popIntr));
                                i = cmdStream.begin()+iPos; next = cmdStream.begin()+nextPos;
                            }
                        }
                    }

                        // remove the original...
                    i = cmdStream.erase(i, next);
                    continue;
                }
            }

            i = NextTransformationCommand_(i);
        }
    }

    static void OptimizePatterns(std::vector<uint32_t>& cmdStream)
    {
        // Replace certain common patterns in the stream with a "macro" command.
        // This is like macro instructions for intel processors... they are a single
        // command that expands to multiple simplier instructions.
        //
        // Patterns:
        //    * Push, TransformFloat4x4_Static, WriteOutputMatrix, Pop
        //          -> TransformFloat4x4AndWrite_Static
        //    * Push, TransformFloat4x4_Parameter, WriteOutputMatrix, Pop
        //          -> TransformFloat4x4AndWrite_Parameter
        
        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            std::pair<TransformCommand, std::vector<uint32_t>::iterator> nextInstructions[3];
            auto i2 = i;
            for (unsigned c=0; c<dimof(nextInstructions); ++c) {
                if (i2 < cmdStream.end()) {
                    nextInstructions[c] = std::make_pair(TransformCommand(*i2), i2);
                    i2 = NextTransformationCommand_(i2);
                } else {
                    nextInstructions[c] = std::make_pair(TransformCommand(~0u), i2);
                }
            }

            if (    (nextInstructions[0].first == TransformCommand::TransformFloat4x4_Static || nextInstructions[1].first == TransformCommand::TransformFloat4x4_Parameter)
                &&  nextInstructions[1].first == TransformCommand::WriteOutputMatrix
                &&  nextInstructions[2].first == TransformCommand::PopLocalToWorld) {

                    //  Merge 0 & 1 into a single TransformFloat4x4AndWrite_Static
                    //  Note that the push/pop pair should be removed with RemoveRedundantPushes
                auto outputIndex = *(nextInstructions[1].second+1);
                cmdStream.erase(nextInstructions[1].second, nextInstructions[2].second);
                if (nextInstructions[0].first == TransformCommand::TransformFloat4x4_Static)
                    *nextInstructions[0].second = (uint32_t)TransformCommand::TransformFloat4x4AndWrite_Static;
                else 
                    *nextInstructions[0].second = (uint32_t)TransformCommand::TransformFloat4x4AndWrite_Parameter;
                i = cmdStream.insert(nextInstructions[0].second+1, outputIndex) - 1;
                continue;
            }

            i = NextTransformationCommand_(i);
        }
    }

    static bool IsUniformScale(Float3 scale, float threshold)
    {
            // expensive, but balanced way to do this...
        float diff1 = XlAbs(scale[0] - scale[1]);
        if (diff1 > std::max(XlAbs(scale[0]), XlAbs(scale[1])) * threshold)
            return false;
        float diff2 = XlAbs(scale[0] - scale[2]);
        if (diff2 > std::max(XlAbs(scale[0]), XlAbs(scale[2])) * threshold)
            return false;
        float diff3 = XlAbs(scale[1] - scale[2]);
        if (diff3 > std::max(XlAbs(scale[1]), XlAbs(scale[2])) * threshold)
            return false;
        return true;
    }

    static float GetMedianElement(Float3 input)
    {
        Float3 absv(XlAbs(input[0]), XlAbs(input[1]), XlAbs(input[2]));
        if (absv[0] < absv[1]) {
            if (absv[2] < absv[0]) return input[0];
            if (absv[2] < absv[1]) return input[2];
            return input[1];
        } else {
            if (absv[2] > absv[0]) return input[0];
            if (absv[2] > absv[1]) return input[2];
            return input[1];
        }
    }

    static void SimplifyTransformTypes(std::vector<uint32_t>& cmdStream)
    {
        // In some cases we can simplify the transformation type used in a command. 
        // For example, if the command is a Float4x4 transform, but that matrix only 
        // performs a translation, we can simplify this to just a "translate" operation.
        // Of course, we can only do this for static transform types.

        const float scaleThreshold = 1e-4f;
        const float identityThreshold = 1e-4f;

        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            auto type = TransformCommand(*i);
            if (type == TransformCommand::TransformFloat4x4_Static) {

                auto cmdEnd = NextTransformationCommand_(i);

                    // Let's try to decompose our matrix into its component
                    // parts. If we get a very simple result, we should 
                    // replace the transform
                auto transform = *(Float4x4*)AsPointer(i+1);
                bool goodDecomposition = false;
                ScaleRotationTranslationM decomposed(transform, goodDecomposition);
                if (goodDecomposition) {
                    bool hasRotation    = !Equivalent(decomposed._rotation, Identity<Float3x3>(), identityThreshold);
                    bool hasScale       = !Equivalent(decomposed._scale, Float3(1.f, 1.f, 1.f), identityThreshold);
                    bool hasTranslation = !Equivalent(decomposed._translation, Float3(0.f, 0.f, 0.f), identityThreshold);

                    // If we have only a single type of transform, we will decompose.
                    // If we have just scale & translation, we will also decompose.
                    if (hasRotation && !hasScale && !hasTranslation) {
                        // What's the best form for rotation here? We have lots of options:
                        //  Float3x3
                        //  euler angles
                        //  axis, angle
                        //  quaternion
                        //  (in some cases, explicit RotateX, RotateY, RotateZ)
                        //  Collada normally prefers axis, angle -- so we'll use that.
                        ArbitraryRotation rot(decomposed._rotation);
                        if (signed rotX = rot.IsRotationX()) {
                            *i = (uint32_t)TransformCommand::RotateX_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotX) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else if (signed rotY = rot.IsRotationY()) {
                            *i = (uint32_t)TransformCommand::RotateY_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotY) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else if (signed rotZ = rot.IsRotationZ()) {
                            *i = (uint32_t)TransformCommand::RotateZ_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotZ) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else {
                            *i = (uint32_t)TransformCommand::RotateAxisAngle_Static;
                            *(Float3*)AsPointer(i+1) = rot._axis;
                            *(float*)AsPointer(i+4) = Rad2Deg(rot._angle);
                            cmdStream.erase(i+5, cmdEnd);
                        }
                    } else if (hasTranslation && !hasRotation) {
                        // translation (and maybe scale)
                        *i = (uint32_t)TransformCommand::Translate_Static;
                        *(Float3*)AsPointer(i+1) = decomposed._translation;
                        auto transEnd = i+4;
                        if (hasScale) {
                            if (IsUniformScale(decomposed._scale, scaleThreshold)) {
                                *transEnd = (uint32_t)TransformCommand::UniformScale_Static;
                                *(float*)AsPointer(transEnd+1) = GetMedianElement(decomposed._scale);
                                cmdStream.erase(transEnd+2, cmdEnd);
                            } else {
                                *transEnd = (uint32_t)TransformCommand::ArbitraryScale_Static;
                                *(Float3*)AsPointer(transEnd+1) = decomposed._scale;
                                cmdStream.erase(transEnd+4, cmdEnd);
                            }
                        } else
                            cmdStream.erase(transEnd, cmdEnd);
                    } else if (hasScale && !hasRotation) {
                        // just scale
                        auto scaleEnd = i;
                        if (IsUniformScale(decomposed._scale, scaleThreshold)) {
                            *i = (uint32_t)TransformCommand::UniformScale_Static;
                            *(float*)AsPointer(i+1) = GetMedianElement(decomposed._scale);
                            scaleEnd = i+2;
                        } else {
                            *i = (uint32_t)TransformCommand::ArbitraryScale_Static;
                            *(Float3*)AsPointer(i+1) = decomposed._scale;
                            scaleEnd = i+4;
                        }
                        cmdStream.erase(scaleEnd, cmdEnd);
                    }
                }

            } else if (type == TransformCommand::ArbitraryScale_Static) {
                    // if our arbitrary scale factor is actually a uniform scale,
                    // we should definitely change it!
                auto scale = *(Float3*)AsPointer(i+1);
                if (IsUniformScale(scale, scaleThreshold)) {
                    *i = (uint32_t)TransformCommand::UniformScale_Static;
                    cmdStream.erase(i+1, i+3);
                    *(float*)AsPointer(i+1) = GetMedianElement(scale);
                }
            }

            // note -- there's some more things we could do:
            //  * remove identity transforms (eg, scale by 1.f, translate by zero)
            //  * simplify RotateAxisAngle_Static to RotateX_Static, RotateY_Static, RotateZ_Static

            i = NextTransformationCommand_(i);
        }
    }

    std::vector<uint32_t> OptimizeTransformationMachine(
        IteratorRange<const uint32_t*> input,
        ITransformationMachineOptimizer& optimizer)
    {
        // Create an optimzied version of the given transformation machine.
        // We want to parse through the command stream, and optimize out redundancies.
        // Here are the changes we want to make:
        //  (1) Series of static transforms (eg, rotate, then scale, then translate)
        //      should be combined into a single Transform4x4 command
        //  (2) If a "pop" is followed by another pop, it means that one of the "pushes"
        //      is redundant. In cases like this, we can remove the push. 
        //  (3) In some cases, we can merge a static transform with the actual geometry.
        //      These cases should result in removing both the transform command and the
        //      write output matrix command.
        //  (4) Where a push is followed immediately by a pop, we can remove both.
        //  (5) We can convert static transformations into equivalent simplier types
        //      (eg, replace a matrix 4x4 transforms with an equivalent translate transform)
        //  (6) Replace certain patterns with optimized simplier patterns 
        //      (eg, "push, transform, output, pop" can become a single optimised command)
        //
        // Note that the order in which we consider each optimisation will effect the final
        // result (because some optimisation will create new cases for other optimisations to
        // work). To make it easy, let's consider only one optimisation at a time.

        std::vector<uint32_t> result(input.cbegin(), input.cend());
		RemoveRedundantTransformationCommands(result);
        RemoveRedundantPushes(result);
        MergeSequentialTransforms(result, optimizer);
        RemoveRedundantPushes(result);
        SimplifyTransformTypes(result);
        OptimizePatterns(result);
        RemoveRedundantPushes(result);

        return std::move(result);
    }

    ITransformationMachineOptimizer::~ITransformationMachineOptimizer() {}

    inline Float3 AsFloat3(const float input[])     { return Float3(input[0], input[1], input[2]); }

	static const unsigned MaxSkeletonMachineDepth = 64;

    template<typename Type>
        const Type& GetParameter(IteratorRange<const void*> parameterBlock, unsigned offset)
    {
        auto* result = (Type*)PtrAdd(parameterBlock.begin(), offset);
        assert(PtrAdd(result, sizeof(Type)) <= parameterBlock.end());
        return *result;
    }

    template<bool UseDebugIterator>
        void GenerateOutputTransforms_Int(
            IteratorRange<Float4x4*>					result,
            IteratorRange<const void*>                  parameterBlock,
            IteratorRange<const uint32_t*>              commandStream,
            const std::function<void(const Float4x4&, const Float4x4&)>&     debugIterator)
    {
            // The command stream will sometimes not write to 
            // output matrices. This can happen when the first output
            // transforms are just identity. Maybe there is a better
            // way to do this that would prevent having to write to this
            // array first...?
        std::fill(result.begin(), result.end(), Identity<Float4x4>());

            //
            //      Follow the commands in our command list, and output
            //      the resulting transformations.
            //

        Float4x4 workingStack[MaxSkeletonMachineDepth]; // (fairly large space on the stack)
        Float4x4* workingTransform = workingStack;
        *workingTransform = Identity<Float4x4>();

        for (auto i=commandStream.cbegin(); i!=commandStream.cend();) {
            auto commandIndex = *i++;
            switch ((TransformCommand)commandIndex) {
            case TransformCommand::PushLocalToWorld:
                if ((workingTransform+1) >= &workingStack[dimof(workingStack)]) {
                    Throw(::Exceptions::BasicLabel("Exceeded maximum stack depth in GenerateOutputTransforms"));
                }
                    
                if (constant_expression<UseDebugIterator>::result())
                    debugIterator((workingTransform != workingStack) ? *(workingTransform-1) : Identity<Float4x4>(), *workingTransform);

                *(workingTransform+1) = *workingTransform;
                ++workingTransform;
                break;

            case TransformCommand::PopLocalToWorld:
                {
                    auto popCount = *i++;
                    if (workingTransform < workingStack+popCount) {
                        Throw(::Exceptions::BasicLabel("Stack underflow in GenerateOutputTransforms"));
                    }

                    workingTransform -= popCount;
                }
                break;

            case TransformCommand::TransformFloat4x4_Static:
                    //
                    //      Parameter is a static single precision 4x4 matrix
                    //
                {
                    // i = AdvanceTo16ByteAlignment(i);
                    const Float4x4& transformMatrix = *reinterpret_cast<const Float4x4*>(AsPointer(i)); 
                    i += 16;
                    *workingTransform = Combine(transformMatrix, *workingTransform);
                }
                break;

            case TransformCommand::Translate_Static:
                // i = AdvanceTo16ByteAlignment(i);
                Combine_IntoRHS(AsFloat3(reinterpret_cast<const float*>(AsPointer(i))), *workingTransform);
                i += 3;
                break;

            case TransformCommand::RotateX_Static:
                Combine_IntoRHS(RotationX(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformCommand::RotateY_Static:
                Combine_IntoRHS(RotationY(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformCommand::RotateZ_Static:
                Combine_IntoRHS(RotationZ(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformCommand::RotateAxisAngle_Static:
                // i = AdvanceTo16ByteAlignment(i);
                Combine_IntoRHS(ArbitraryRotation(AsFloat3(reinterpret_cast<const float*>(AsPointer(i))), Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i+3)))), *workingTransform);
                i += 4;
                break;

			case TransformCommand::RotateQuaternion_Static:
				Combine_IntoRHS(*reinterpret_cast<const Quaternion*>(AsPointer(i)), *workingTransform);
				i += 4;
				break;

            case TransformCommand::UniformScale_Static:
                Combine_IntoRHS(UniformScale(*reinterpret_cast<const float*>(AsPointer(i))), *workingTransform);
                i++;
                break;

            case TransformCommand::ArbitraryScale_Static:
                Combine_IntoRHS(ArbitraryScale(AsFloat3(reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i+=3;
                break;

            case TransformCommand::TransformFloat4x4_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    *workingTransform = Combine(GetParameter<Float4x4>(parameterBlock, parameterOffset), *workingTransform);
                }
                break;

            case TransformCommand::Translate_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    #if 0
                        // Hack -- flag for object space translation
                        if (float4s[parameterIndex][3] >= 256.f) {
                            auto objectSpaceTranslation = Truncate(float4s[parameterIndex]);
                            objectSpaceTranslation = TransformPoint(
                                AsFloat4x4(Float3x3{-1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f}), 
                                objectSpaceTranslation);
                            auto localSpaceTranslation = TransformPointByOrthonormalInverse(*workingTransform, objectSpaceTranslation);
                            Combine_IntoRHS(localSpaceTranslation, *workingTransform);
                        } else {
                    #endif
                    Combine_IntoRHS(GetParameter<Float3>(parameterBlock, parameterOffset), *workingTransform);
                }
                break;

            case TransformCommand::RotateX_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(RotationX(Deg2Rad(GetParameter<float>(parameterBlock, parameterOffset))), *workingTransform);
                }
                break;

            case TransformCommand::RotateY_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(RotationY(Deg2Rad(GetParameter<float>(parameterBlock, parameterOffset))), *workingTransform);
                }
                break;

            case TransformCommand::RotateZ_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(RotationZ(Deg2Rad(GetParameter<float>(parameterBlock, parameterOffset))), *workingTransform);
                }
                break;

            case TransformCommand::RotateAxisAngle_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Float4 param = GetParameter<Float4>(parameterBlock, parameterOffset);
                    Combine_IntoRHS(ArbitraryRotation(Truncate(param), Deg2Rad(param[3])), *workingTransform);
                }
                break;

			case TransformCommand::RotateQuaternion_Parameter:
				{
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(GetParameter<Quaternion>(parameterBlock, parameterOffset), *workingTransform);
                }
                break;

            case TransformCommand::UniformScale_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(UniformScale(GetParameter<float>(parameterBlock, parameterOffset)), *workingTransform);
                }
                break;

            case TransformCommand::ArbitraryScale_Parameter:
                {
                    uint32_t parameterOffset = *i++;
                    Combine_IntoRHS(ArbitraryScale(GetParameter<Float3>(parameterBlock, parameterOffset)), *workingTransform);
                }
                break;

            case TransformCommand::WriteOutputMatrix:
                    //
                    //      Dump the current working transform to the output array
                    //
                {
                    uint32_t outputIndex = *i++;
                    if (outputIndex < result.size()) {
                        result[outputIndex] = *workingTransform;
                        if (constant_expression<UseDebugIterator>::result())
                            debugIterator((workingTransform != workingStack) ? *(workingTransform-1) : Identity<Float4x4>(), *workingTransform);
                    } else
                        Log(Warning) << "Warning -- bad output matrix index (" << outputIndex << ")" << std::endl;
                }
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Static:
                {
                    uint32_t outputIndex = *i++;
                    const Float4x4& transformMatrix = *reinterpret_cast<const Float4x4*>(AsPointer(i)); 
                    i += 16;
                    if (outputIndex < result.size()) {
                        result[outputIndex] = Combine(transformMatrix, *workingTransform);
                        if (constant_expression<UseDebugIterator>::result())
                            debugIterator(*workingTransform, result[outputIndex]);
                    } else
                        Log(Warning) << "Warning -- bad output matrix index in TransformFloat4x4AndWrite_Static (" << outputIndex << ")" << std::endl;
                }
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Parameter:
                {
                    uint32_t outputIndex = *i++;
                    uint32_t parameterOffset = *i++;
                    if (outputIndex < result.size()) {
                        result[outputIndex] = Combine(GetParameter<Float4x4>(parameterBlock, parameterOffset), *workingTransform);
                        if (constant_expression<UseDebugIterator>::result())
                            debugIterator(*workingTransform, result[outputIndex]);
                    } else
                        Log(Warning) << "Warning -- bad output matrix index in TransformFloat4x4AndWrite_Parameter (" << outputIndex << ")" << std::endl;
                }
                break;

            case TransformCommand::BindingPoint_0:
            case TransformCommand::BindingPoint_1:
            case TransformCommand::BindingPoint_2:
            case TransformCommand::BindingPoint_3:
                // skip over the binding point and treat the static defaults as just normal statics
                i += 2;
                break;

            case TransformCommand::Comment:
                i+=64/4;
                break;
            }
        }
    }

    void GenerateOutputTransforms(
        IteratorRange<Float4x4*>                    result,
        IteratorRange<const void*>                  parameterBlock,
        IteratorRange<const uint32_t*>              commandStream)
    {
        GenerateOutputTransforms_Int<false>(
            result, parameterBlock, commandStream, 
            std::function<void(const Float4x4&, const Float4x4&)>());
    }

	void CalculateParentPointers(
		IteratorRange<uint32_t*>					result,
		IteratorRange<const uint32_t*>				commandStream)
	{
		uint32_t workingStack[MaxSkeletonMachineDepth];
        uint32_t* workingTransform = workingStack;
        *workingTransform = ~0u;
		for (auto&i:result) i = ~0u;

		for (auto i=commandStream.cbegin(); i!=commandStream.cend();) {
            switch ((TransformCommand)*i) {
            case TransformCommand::PushLocalToWorld:
                ++i;
                if ((workingTransform+1) >= &workingStack[dimof(workingStack)])
                    Throw(::Exceptions::BasicLabel("Exceeded maximum stack depth in CalculateParentPointers"));

                *(workingTransform+1) = *workingTransform;
                ++workingTransform;
                break;

            case TransformCommand::PopLocalToWorld:
                {
                    ++i;
                    auto popCount = *i++;
                    if (workingTransform < workingStack+popCount)
                        Throw(::Exceptions::BasicLabel("Stack underflow in CalculateParentPointers"));
                    workingTransform -= popCount;
                }
                break;

            case TransformCommand::WriteOutputMatrix:
                {
                    ++i;
                    uint32_t outputIndex = *i++;
                    if (outputIndex < result.size()) {
						assert(result[outputIndex] == ~0u);		// if a given output marker is written to twice, we can end up here. It doesn't make much sense to do this, because only the last value written will be used (this applies both to this function and GenerateOutputTransforms)
                        result[outputIndex] = *workingTransform;
                    } else
                        Log(Warning) << "Warning -- bad output matrix index (" << outputIndex << ")" << std::endl;

					// We can't always distinquish siblings from children. If there are two siblings with identical
					// transforms, we can end up mistaking it for a parent-child relationship here.
					*workingTransform = outputIndex;
                }
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Static:
                {
                    ++i;
                    uint32_t outputIndex = *i++;
                    i += 16;
                    if (outputIndex < result.size()) {
                        result[outputIndex] = *workingTransform;
						*workingTransform = outputIndex;
                    } else
                        Log(Warning) << "Warning -- bad output matrix index in TransformFloat4x4AndWrite_Static (" << outputIndex << ")" << std::endl;
					// The transformation we wrote doesn't affect the working transform. So we won't consider
					// the marker we wrote as the new parent on the stack
                }
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Parameter:
                {
                    ++i;
                    uint32_t outputIndex = *i++;
                    i++;
                    if (outputIndex < result.size()) {
                        result[outputIndex] = *workingTransform;
						*workingTransform = outputIndex;
                    } else
                        Log(Warning) << "Warning -- bad output matrix index in TransformFloat4x4AndWrite_Parameter (" << outputIndex << ")" << std::endl;
					// The transformation we wrote doesn't affect the working transform. So we won't consider
					// the marker we wrote as the new parent on the stack
                }
                break;

            default:
                i = NextTransformationCommand(i);
                break;
            }
        }
	}

        ///////////////////////////////////////////////////////

	std::vector<uint32_t> RemapOutputMatrices(
		IteratorRange<const uint32_t*> input,
		IteratorRange<const unsigned*> outputMatrixMapping)
	{
		std::vector<uint32_t> result;
		result.reserve(input.size());

            // First, we just want to convert series of pop operations into
            // a single pop.
        for (auto i=input.begin(); i!=input.end();) {
			auto nexti = NextTransformationCommand_(i);
            if (IsOutputCommand((TransformCommand)*i)) {
				auto oldOutputMatrix = *(i+1);
				unsigned newOutputMatrix = ~0u;
				if (oldOutputMatrix < outputMatrixMapping.size())
					newOutputMatrix = outputMatrixMapping[oldOutputMatrix];

				if (newOutputMatrix != ~0u) {
					// Write the command to the output, but with a modified output matrix
					// in the second slot
					result.push_back(*i);
					result.push_back(newOutputMatrix);
					result.insert(result.end(), i+2, nexti);
				}
                
            } else {
				result.insert(result.end(), i, nexti);
            }
			i = nexti;
        }

		return result;
    }

        ///////////////////////////////////////////////////////

    static void MakeIndentBuffer(char buffer[], unsigned bufferSize, signed identLevel)
    {
        std::fill(buffer, &buffer[std::min(std::max(0,identLevel*2), signed(bufferSize-1))], ' ');
        buffer[std::min(std::max(0,identLevel*2), signed(bufferSize-1))] = '\0';
    }

    static inline const uint32_t* TraceStaticTransformCommand(std::ostream&  stream, TransformCommand commandIndex, const uint32_t* i)
    {
        switch (commandIndex) {
        case TransformCommand::TransformFloat4x4_Static:
            {
                auto trans = *reinterpret_cast<const Float4x4*>(AsPointer(i));
                stream << "TransformFloat4x4_Static (";
                CompactTransformDescription(stream, trans);
                stream << ")"; 
                i += 16;
            }
            break;

        case TransformCommand::Translate_Static:
            {
                auto trans = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                stream << "Translate_Static (" << trans[0] << ", " << trans[1] << ", " << trans[2] << ")";
                i += 3;
            }
            break;

        case TransformCommand::RotateX_Static:
            stream << "RotateX_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")";
            i++;
            break;

        case TransformCommand::RotateY_Static:
            stream << "RotateY_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")";
            i++;
            break;

        case TransformCommand::RotateZ_Static:
            stream << "RotateZ_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")";
            i++;
            break;

        case TransformCommand::RotateAxisAngle_Static:
            {
                auto a = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                float r = *reinterpret_cast<const float*>(AsPointer(i+3));
                stream << "RotateAxisAngle_Static (" << a[0] << ", " << a[1] << ", " << a[2] << ")(" << r << ")";
                i += 4;
            }
            break;

        case TransformCommand::RotateQuaternion_Static:
            {
                auto& q = *reinterpret_cast<const Quaternion*>(AsPointer(i));
                stream << "RotateQuaternion_Static (" << q[0] << ", " << q[1] << ", " << q[2] << ", " << q[3] << ")";
                i += 4;
            }
            break;

        case TransformCommand::UniformScale_Static:
            stream << "UniformScale_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")";
            i++;
            break;

        case TransformCommand::ArbitraryScale_Static:
            {
                auto trans = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                stream << "ArbitraryScale_Static (" << trans[0] << ", " << trans[1] << ", " << trans[2] << ")";
            }
            i+=3;
            break;
        default:
            assert(0);
            break;
        }
        return i;
    }
    
    void TraceTransformationMachine(
        std::ostream&   stream,
        IteratorRange<const uint32_t*>    commandStream,
        std::function<std::string(unsigned)> outputMatrixToName,
        std::function<std::string(unsigned)> parameterToName)
    {
        stream << "Transformation machine size: (" << (commandStream.size()) * sizeof(uint32_t) << ") bytes" << std::endl;

        char indentBuffer[32], doubleIndentBuffer[32];
        signed indentLevel = 1;
        MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);
        MakeIndentBuffer(doubleIndentBuffer, dimof(doubleIndentBuffer), indentLevel+1);

        for (auto i=commandStream.begin(); i!=commandStream.end();) {
            auto commandIndex = *i++;
            switch ((TransformCommand)commandIndex) {
            case TransformCommand::PushLocalToWorld:
                stream << indentBuffer << "PushLocalToWorld" << std::endl;
                ++indentLevel;
                MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);
                break;

            case TransformCommand::PopLocalToWorld:
                {
                    auto popCount = *i++;
                    stream << indentBuffer << "PopLocalToWorld (" << popCount << ")" << std::endl;
                    indentLevel -= popCount;
                    MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);
                }
                break;

            case TransformCommand::TransformFloat4x4_Static:
            case TransformCommand::Translate_Static:
            case TransformCommand::RotateX_Static:
            case TransformCommand::RotateY_Static:
            case TransformCommand::RotateZ_Static:
            case TransformCommand::RotateAxisAngle_Static:
            case TransformCommand::RotateQuaternion_Static:
            case TransformCommand::UniformScale_Static:
            case TransformCommand::ArbitraryScale_Static:
                stream << indentBuffer;
                i = TraceStaticTransformCommand(stream, (TransformCommand)commandIndex, i);
                stream << std::endl;
                break;

            case TransformCommand::TransformFloat4x4_Parameter:
                stream << indentBuffer << "TransformFloat4x4_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::Translate_Parameter:
                stream << indentBuffer << "Translate_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::RotateX_Parameter:
                stream << indentBuffer << "RotateX_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                stream << std::endl;
                i++;
                break;

            case TransformCommand::RotateY_Parameter:
                stream << indentBuffer << "RotateY_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::RotateZ_Parameter:
                stream << indentBuffer << "RotateZ_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::RotateAxisAngle_Parameter:
                stream << indentBuffer << "RotateAxisAngle_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

			case TransformCommand::RotateQuaternion_Parameter:
                stream << indentBuffer << "RotateQuaternion_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::UniformScale_Parameter:
                stream << indentBuffer << "UniformScale_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::ArbitraryScale_Parameter:
                stream << indentBuffer << "ArbitraryScale_Parameter at offset (0x" << std::hex << *i << std::dec << ")" << std::endl;
                i++;
                break;

            case TransformCommand::BindingPoint_0:
            case TransformCommand::BindingPoint_1:
            case TransformCommand::BindingPoint_2:
            case TransformCommand::BindingPoint_3:
                stream << indentBuffer << "Binding point for parameter [" << *i << "]";
                if (parameterToName) 
                    stream << " (" << parameterToName(*i) << ")" << std::endl;
                i++;

                // handle defaults
                {
                    unsigned defaultCount = 0;
                    if (commandIndex == (uint32_t)TransformCommand::BindingPoint_0) {
                        stream << " with no defaults" << std::endl;
                    } else if (commandIndex == (uint32_t)TransformCommand::BindingPoint_1) {
                        stream << " with 1 defaults" << std::endl;
                        defaultCount = 1;
                    } else if (commandIndex == (uint32_t)TransformCommand::BindingPoint_2) {
                        stream << " with 2 defaults" << std::endl;
                        defaultCount = 2;
                    } else if (commandIndex == (uint32_t)TransformCommand::BindingPoint_3) {
                        stream << " with 3 defaults" << std::endl;
                        defaultCount = 3;
                    }
                    while (defaultCount--) {
                        auto cmd = *(TransformCommand*)i;
                        ++i;
                        stream << doubleIndentBuffer << "Default: ";
                        i = TraceStaticTransformCommand(stream, cmd, i);
                        stream << std::endl;
                    }
                }
                break;

            case TransformCommand::WriteOutputMatrix:
                stream << indentBuffer << "WriteOutputMatrix [" << *i << "]";
                if (outputMatrixToName)
                    stream << " (" << outputMatrixToName(*i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Static:
                {
                    stream << indentBuffer << "TransformFloat4x4AndWrite_Static [" << *i << "]";
                    if (outputMatrixToName)
                        stream << " (" << outputMatrixToName(*i) << ")";
                    auto trans = *reinterpret_cast<const Float4x4*>(AsPointer(i+1));
                    stream << indentBuffer << " (";
                    CompactTransformDescription(stream, trans);
                    stream << ")" << std::endl;
                    i+=1+16;
                }
                break;

            case TransformCommand::TransformFloat4x4AndWrite_Parameter:
                stream << indentBuffer << "TransformFloat4x4AndWrite_Parameter [" << *i << "]";
                if (outputMatrixToName)
                    stream << " (" << outputMatrixToName(*i) << ")";
                stream << indentBuffer << " at offset (0x" << std::hex << *(i+1) << std::dec << ")" << std::endl;
                i+=2;
                break;

            case TransformCommand::Comment:
                {
                    std::string str((const char*)AsPointer(i), (const char*)AsPointer(i+64/4));
                    str = str.substr(0, str.find_first_of('\0'));
                    stream << indentBuffer << "Comment: " << str << std::endl;
                    i += 64/4;
                }
                break;

            default:
                stream << "Error: " << i << std::endl;
                break;
            }

            assert(i <= commandStream.end());  // make sure we haven't jumped past the end marker
        }
    }

}}


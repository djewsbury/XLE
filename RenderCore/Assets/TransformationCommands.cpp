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
    static unsigned CommandSize(TransformStackCommand cmd)
    {
        switch (cmd) {
        case TransformStackCommand::PushLocalToWorld:           return 0;
        case TransformStackCommand::PopLocalToWorld:            return 1;
        case TransformStackCommand::TransformFloat4x4_Static:   return 16;
        case TransformStackCommand::Translate_Static:           return 3;
        case TransformStackCommand::RotateX_Static:             return 1;
        case TransformStackCommand::RotateY_Static:             return 1;
        case TransformStackCommand::RotateZ_Static:             return 1;
        case TransformStackCommand::Rotate_Static:              return 4;
		case TransformStackCommand::RotateQuaternion_Static:    return 4;
        case TransformStackCommand::UniformScale_Static:        return 1;
        case TransformStackCommand::ArbitraryScale_Static:      return 3;

        case TransformStackCommand::TransformFloat4x4_Parameter:
        case TransformStackCommand::Translate_Parameter:
        case TransformStackCommand::RotateX_Parameter:
        case TransformStackCommand::RotateY_Parameter:
        case TransformStackCommand::RotateZ_Parameter:
        case TransformStackCommand::Rotate_Parameter:
		case TransformStackCommand::RotateQuaternion_Parameter:
        case TransformStackCommand::UniformScale_Parameter:
        case TransformStackCommand::ArbitraryScale_Parameter:
            return 1;

        case TransformStackCommand::WriteOutputMatrix:
            return 1;

        case TransformStackCommand::TransformFloat4x4AndWrite_Static: return 1+16;
        case TransformStackCommand::TransformFloat4x4AndWrite_Parameter: return 1+1;

        case TransformStackCommand::Comment: return 64/4;

        default: return 0;
        }
    }

    T1(Iterator) static Iterator SkipUntilPop(Iterator i, Iterator end, signed& finalIdentLevel)
    {
        finalIdentLevel = 1;
        for (; i!=end;) {
            if (*i == (uint32_t)TransformStackCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                finalIdentLevel -= signed(popCount);
                if (finalIdentLevel <= 0)
                    return i;
            } else if (*i == (uint32_t)TransformStackCommand::PushLocalToWorld)
                ++finalIdentLevel;
            i += 1 + CommandSize((TransformStackCommand)*i);
        }
        return end;
    }

    static bool IsTransformCommand(TransformStackCommand cmd)
    {
        return 
                (cmd >= TransformStackCommand::TransformFloat4x4_Static && cmd <= TransformStackCommand::ArbitraryScale_Static)
            ||  (cmd >= TransformStackCommand::TransformFloat4x4_Parameter && cmd <= TransformStackCommand::ArbitraryScale_Parameter);
    }

	static bool IsOutputCommand(TransformStackCommand cmd)
    {
        return (cmd >= TransformStackCommand::WriteOutputMatrix && cmd <= TransformStackCommand::TransformFloat4x4AndWrite_Parameter);
    }

    T1(Iterator) 
        static Iterator IsRedundantPush(Iterator i, Iterator end, bool& isRedundant)
    {
        // Scan forward... if the transform isn't modified at this level, or if
        // the matrix isn't used after the pop, then the push/pop is redundant
        assert(*i == (uint32_t)TransformStackCommand::PushLocalToWorld);
        ++i;

        bool foundTransformCmd = false;
        for (;i<end;) {
            auto cmd = TransformStackCommand(*i);
            if (IsTransformCommand(cmd)) {
                foundTransformCmd = true;
            } else if (cmd == TransformStackCommand::PushLocalToWorld) {
                ++i;
                signed finalIdentLevel = 0;
                i = SkipUntilPop(i, end, finalIdentLevel);
                if (finalIdentLevel < 0) {
                    isRedundant = (finalIdentLevel < -1) || !foundTransformCmd || (i+2) == end;
                    return i;
                }   
            } else if (cmd == TransformStackCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                isRedundant = (popCount > 1) || !foundTransformCmd || (i+2) == end;
                return i;
            }

            i += 1 + CommandSize((TransformStackCommand)*i);
        }
        isRedundant = true;
        return i;
    }

    static void RemoveRedundantPushes(std::vector<uint32_t>& cmdStream)
    {
            // First, we just want to convert series of pop operations into
            // a single pop.
        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            if (*i == (uint32_t)TransformStackCommand::PopLocalToWorld) {
                if (    (cmdStream.end() - i) >= 4
                    &&  *(i+2) == (uint32_t)TransformStackCommand::PopLocalToWorld) {
                        // combine these 2 pops into a single pop command
                    auto newPopCount = *(i+1) + *(i+3);
                    i = cmdStream.erase(i, i+2);
                    *(i+1) = newPopCount;
                } else {
                    i += 2;
                }
            } else {
                i += 1 + CommandSize((TransformStackCommand)*i);
            }
        }

            // Now look for push operations that are redundant
        for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
            if (*i == (uint32_t)TransformStackCommand::PushLocalToWorld) {
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

            i += 1 + CommandSize((TransformStackCommand)*i);
        }
    }

	T1(Iterator) 
        static bool HasFollowingOutputCommand(Iterator i, Iterator end)
    {
		signed pushDepth = 0;
		for (;i<end && pushDepth >= 0;) {
            auto cmd = TransformStackCommand(*i);
            if (IsOutputCommand(cmd)) {
                return true;
            } else if (cmd == TransformStackCommand::PushLocalToWorld) {
                ++pushDepth;
            } else if (cmd == TransformStackCommand::PopLocalToWorld) {
                auto popCount = *(i+1);
                pushDepth -= popCount;
            }

            i += 1 + CommandSize((TransformStackCommand)*i);
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
			[](AnimSamplerType, unsigned) { return std::string{}; });

		for (auto i=cmdStream.begin(); i!=cmdStream.end();) {
			auto nexti = i + 1 + CommandSize((TransformStackCommand)*i);
			auto cmd = TransformStackCommand(*i);
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
			[](AnimSamplerType, unsigned) { return std::string{}; });

		auto debug = str.str();
		(void)debug;
	}

    enum MergeType { StaticTransform, OutputMatrix, Push, Pop, Blocker };
    static MergeType AsMergeType(TransformStackCommand cmd)
    {
        switch (cmd) {
        case TransformStackCommand::TransformFloat4x4_Static:
        case TransformStackCommand::Translate_Static:
        case TransformStackCommand::RotateX_Static:
        case TransformStackCommand::RotateY_Static:
        case TransformStackCommand::RotateZ_Static:
        case TransformStackCommand::Rotate_Static:
		case TransformStackCommand::RotateQuaternion_Static:
        case TransformStackCommand::UniformScale_Static:
        case TransformStackCommand::ArbitraryScale_Static:  return MergeType::StaticTransform;

        case TransformStackCommand::PushLocalToWorld:       return MergeType::Push;
        case TransformStackCommand::PopLocalToWorld:        return MergeType::Pop;
        case TransformStackCommand::WriteOutputMatrix:      return MergeType::OutputMatrix;

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
            auto type = AsMergeType(TransformStackCommand(*i));
            if (type == MergeType::StaticTransform || type == MergeType::Blocker) {
                // Hitting a static transform blocks any further searches
                // We can just skip until we pop out of this block
                result.push_back(i-range.begin());
                i = SkipUntilPop(i, range.end(), finalIdentLevel);
                return i+1+CommandSize((TransformStackCommand)*i);
            } else if (type == MergeType::OutputMatrix) {
                result.push_back(i-range.begin());
                i += 1 + CommandSize((TransformStackCommand)*i);
            } else if (type == MergeType::Pop) {
                auto popCount = *(i+1);
                finalIdentLevel = popCount-1;
                return i+1+CommandSize((TransformStackCommand)*i);
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
            switch (AsMergeType(TransformStackCommand(cmdStream[c]))) {
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

    static bool ShouldDoSimpleMerge(TransformStackCommand lhs, TransformStackCommand rhs)
    {
        if (    lhs == TransformStackCommand::TransformFloat4x4_Static
            ||  rhs == TransformStackCommand::TransformFloat4x4_Static)
            return true;

        switch (lhs) {
        case TransformStackCommand::Translate_Static:
                // only merge into another translate
            return rhs == TransformStackCommand::Translate_Static;

        case TransformStackCommand::RotateX_Static:
        case TransformStackCommand::RotateY_Static:
        case TransformStackCommand::RotateZ_Static:
        case TransformStackCommand::Rotate_Static:
		case TransformStackCommand::RotateQuaternion_Static:
                // only merge into another rotate
            return (rhs == TransformStackCommand::RotateX_Static)
                || (rhs == TransformStackCommand::RotateY_Static)
                || (rhs == TransformStackCommand::RotateZ_Static)
                || (rhs == TransformStackCommand::Rotate_Static)
				|| (rhs == TransformStackCommand::RotateQuaternion_Static)
                ;

        case TransformStackCommand::UniformScale_Static:
        case TransformStackCommand::ArbitraryScale_Static:
                // only merge into another scale
            return (rhs == TransformStackCommand::UniformScale_Static)
                || (rhs == TransformStackCommand::ArbitraryScale_Static)
                ;

        default:
            break;
        }

        return false;
    }

    static Float4x4 PromoteToFloat4x4(const uint32_t* cmd)
    {
        switch (TransformStackCommand(*cmd)) {
        case TransformStackCommand::TransformFloat4x4_Static:
            return *(const Float4x4*)(cmd+1);

        case TransformStackCommand::Translate_Static:
            return AsFloat4x4(*(const Float3*)(cmd+1));

        case TransformStackCommand::RotateX_Static:
            return AsFloat4x4(*(const RotationX*)(cmd+1));

        case TransformStackCommand::RotateY_Static:
            return AsFloat4x4(*(const RotationY*)(cmd+1));

        case TransformStackCommand::RotateZ_Static:
            return AsFloat4x4(*(const RotationZ*)(cmd+1));

        case TransformStackCommand::Rotate_Static:
            return AsFloat4x4(*(const ArbitraryRotation*)(cmd+1));

		case TransformStackCommand::RotateQuaternion_Static:
            return AsFloat4x4(*(const Quaternion*)(cmd+1));

        case TransformStackCommand::UniformScale_Static:
            return AsFloat4x4(*(const UniformScale*)(cmd+1));

        case TransformStackCommand::ArbitraryScale_Static:
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
        // If the transforms are of exactly the same type (and not Rotate_Static)
        // then we can merge into a final transform that is the same type.
        // Otherwise we should merge to Float4x4. In some cases, the final Final4x4
        // can be converted into a simplier transform... We will go back through
        // and optimize those cases later.
        auto typeDst = TransformStackCommand(*dst);
        auto typeMerging = TransformStackCommand(*mergingCmd);
        if (typeDst == TransformStackCommand::Translate_Static
            && typeMerging == TransformStackCommand::Translate_Static) {

            auto& dstTrans = *(Float3*)AsPointer(dst+1);
            auto& mergeTrans = *(Float3*)AsPointer(mergingCmd+1);
            dstTrans += mergeTrans;
        } else if ((typeDst == TransformStackCommand::RotateX_Static
            && typeMerging == TransformStackCommand::RotateX_Static)
            || (typeDst == TransformStackCommand::RotateY_Static
            && typeMerging == TransformStackCommand::RotateY_Static)
            || (typeDst == TransformStackCommand::RotateZ_Static
            && typeMerging == TransformStackCommand::RotateZ_Static)) {

            auto& dstTrans = *(float*)AsPointer(dst+1);
            auto& mergeTrans = *(float*)AsPointer(mergingCmd+1);
            dstTrans += mergeTrans;
        } else if (typeDst == TransformStackCommand::UniformScale_Static
            && typeMerging == TransformStackCommand::UniformScale_Static) {

            auto& dstTrans = *(float*)AsPointer(dst+1);
            auto& mergeTrans = *(float*)AsPointer(mergingCmd+1);
            dstTrans *= mergeTrans;
        } else if (typeDst == TransformStackCommand::ArbitraryScale_Static
            && typeMerging == TransformStackCommand::ArbitraryScale_Static) {

            auto& dstTrans = *(Float3*)AsPointer(dst+1);
            auto& mergeTrans = *(Float3*)AsPointer(mergingCmd+1);
            dstTrans[0] *= mergeTrans[0];
            dstTrans[1] *= mergeTrans[1];
            dstTrans[2] *= mergeTrans[2];
        } else if (typeDst == TransformStackCommand::TransformFloat4x4_Static
            && typeMerging == TransformStackCommand::TransformFloat4x4_Static) {

            auto& dstTrans = *(Float4x4*)AsPointer(dst+1);
            auto& mergeTrans = *(Float4x4*)AsPointer(mergingCmd+1);
            dstTrans = Combine(dstTrans, mergeTrans);
        } else {
            // Otherwise we need to promote both transforms into Float4x4, and we will push
            // a new Float4x4 transform into the space in "dst"
            auto dstTransform = PromoteToFloat4x4(AsPointer(dst));
            auto mergeTransform = PromoteToFloat4x4(AsPointer(mergingCmd));
            auto t = cmdStream.erase(dst+1, dst+1+CommandSize(TransformStackCommand(*dst)));
            assert(t==dst+1);
            *dst = (uint32_t)TransformStackCommand::TransformFloat4x4_Static;
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
            auto cmdType = AsMergeType(TransformStackCommand(*i));
            auto next = i + 1 + CommandSize(TransformStackCommand(*i));
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
                if (influences.size() == 1 && AsMergeType(TransformStackCommand(cmdStream[influences[0]])) == MergeType::StaticTransform) {
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
                    doMerge = ShouldDoSimpleMerge(TransformStackCommand(*i), TransformStackCommand(cmdStream[influences[0]]));
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
                        auto type = AsMergeType(TransformStackCommand(*i2));
                        if (type == MergeType::StaticTransform) {
                            DoTransformMerge(cmdStream, i2, i);
                            i = cmdStream.begin()+iPos; next = cmdStream.begin()+nextPos;
                        } else if (type == MergeType::Blocker) {
                            // this case always involves pushing a duplicate of the original command
                            // plus, we need a push/pop pair surrounding it
                            auto insertSize = next-i;
                            i2 = cmdStream.insert(i2, i, next);
                            i2 = cmdStream.insert(i2, (uint32_t)TransformStackCommand::PushLocalToWorld);
                            uint32_t popIntr[] = { (uint32_t)TransformStackCommand::PopLocalToWorld, 1 };
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
                                    if (    AsMergeType(TransformStackCommand(cmdStream[*r2])) == MergeType::OutputMatrix
                                        &&  cmdStream[*r2+1] == outputMatrixIndex)
                                        Throw(::Exceptions::BasicLabel("Writing to the same output matrix multiple times in transformation machine. Output matrix index: %u", outputMatrixIndex));

                                auto transform = PromoteToFloat4x4(AsPointer(i));
                                optimizer.MergeIntoOutputMatrix(outputMatrixIndex, transform);
                            } else {
                                auto insertSize = next-i; auto outputMatSize = 1+CommandSize(TransformStackCommand(*i2));
                                i2 = cmdStream.insert(i2, i, next);
                                i2 = cmdStream.insert(i2, (uint32_t)TransformStackCommand::PushLocalToWorld);
                                uint32_t popIntr[] = { (uint32_t)TransformStackCommand::PopLocalToWorld, 1 };
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

            i += 1 + CommandSize(TransformStackCommand(*i));
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
            std::pair<TransformStackCommand, std::vector<uint32_t>::iterator> nextInstructions[3];
            auto i2 = i;
            for (unsigned c=0; c<dimof(nextInstructions); ++c) {
                if (i2 < cmdStream.end()) {
                    nextInstructions[c] = std::make_pair(TransformStackCommand(*i2), i2);
                    i2 += 1 + CommandSize(TransformStackCommand(*i2));
                } else {
                    nextInstructions[c] = std::make_pair(TransformStackCommand(~0u), i2);
                }
            }

            if (    (nextInstructions[0].first == TransformStackCommand::TransformFloat4x4_Static || nextInstructions[1].first == TransformStackCommand::TransformFloat4x4_Parameter)
                &&  nextInstructions[1].first == TransformStackCommand::WriteOutputMatrix
                &&  nextInstructions[2].first == TransformStackCommand::PopLocalToWorld) {

                    //  Merge 0 & 1 into a single TransformFloat4x4AndWrite_Static
                    //  Note that the push/pop pair should be removed with RemoveRedundantPushes
                auto outputIndex = *(nextInstructions[1].second+1);
                cmdStream.erase(nextInstructions[1].second, nextInstructions[2].second);
                if (nextInstructions[0].first == TransformStackCommand::TransformFloat4x4_Static)
                    *nextInstructions[0].second = (uint32_t)TransformStackCommand::TransformFloat4x4AndWrite_Static;
                else 
                    *nextInstructions[0].second = (uint32_t)TransformStackCommand::TransformFloat4x4AndWrite_Parameter;
                i = cmdStream.insert(nextInstructions[0].second+1, outputIndex) - 1;
                continue;
            }

            i += 1 + CommandSize(TransformStackCommand(*i));
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
            auto type = TransformStackCommand(*i);
            if (type == TransformStackCommand::TransformFloat4x4_Static) {

                auto cmdEnd = i+1+CommandSize(type);

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
                            *i = (uint32_t)TransformStackCommand::RotateX_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotX) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else if (signed rotY = rot.IsRotationY()) {
                            *i = (uint32_t)TransformStackCommand::RotateY_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotY) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else if (signed rotZ = rot.IsRotationZ()) {
                            *i = (uint32_t)TransformStackCommand::RotateZ_Static;
                            *(float*)AsPointer(i+1) = Rad2Deg(float(rotZ) * rot._angle);
                            cmdStream.erase(i+2, cmdEnd);
                        } else {
                            *i = (uint32_t)TransformStackCommand::Rotate_Static;
                            *(Float3*)AsPointer(i+1) = rot._axis;
                            *(float*)AsPointer(i+4) = Rad2Deg(rot._angle);
                            cmdStream.erase(i+5, cmdEnd);
                        }
                    } else if (hasTranslation && !hasRotation) {
                        // translation (and maybe scale)
                        *i = (uint32_t)TransformStackCommand::Translate_Static;
                        *(Float3*)AsPointer(i+1) = decomposed._translation;
                        auto transEnd = i+4;
                        if (hasScale) {
                            if (IsUniformScale(decomposed._scale, scaleThreshold)) {
                                *transEnd = (uint32_t)TransformStackCommand::UniformScale_Static;
                                *(float*)AsPointer(transEnd+1) = GetMedianElement(decomposed._scale);
                                cmdStream.erase(transEnd+2, cmdEnd);
                            } else {
                                *transEnd = (uint32_t)TransformStackCommand::ArbitraryScale_Static;
                                *(Float3*)AsPointer(transEnd+1) = decomposed._scale;
                                cmdStream.erase(transEnd+4, cmdEnd);
                            }
                        } else
                            cmdStream.erase(transEnd, cmdEnd);
                    } else if (hasScale && !hasRotation) {
                        // just scale
                        auto scaleEnd = i;
                        if (IsUniformScale(decomposed._scale, scaleThreshold)) {
                            *i = (uint32_t)TransformStackCommand::UniformScale_Static;
                            *(float*)AsPointer(i+1) = GetMedianElement(decomposed._scale);
                            scaleEnd = i+2;
                        } else {
                            *i = (uint32_t)TransformStackCommand::ArbitraryScale_Static;
                            *(Float3*)AsPointer(i+1) = decomposed._scale;
                            scaleEnd = i+4;
                        }
                        cmdStream.erase(scaleEnd, cmdEnd);
                    }
                }

            } else if (type == TransformStackCommand::ArbitraryScale_Static) {
                    // if our arbitrary scale factor is actually a uniform scale,
                    // we should definitely change it!
                auto scale = *(Float3*)AsPointer(i+1);
                if (IsUniformScale(scale, scaleThreshold)) {
                    *i = (uint32_t)TransformStackCommand::UniformScale_Static;
                    cmdStream.erase(i+1, i+3);
                    *(float*)AsPointer(i+1) = GetMedianElement(scale);
                }
            }

            // note -- there's some more things we could do:
            //  * remove identity transforms (eg, scale by 1.f, translate by zero)
            //  * simplify Rotate_Static to RotateX_Static, RotateY_Static, RotateZ_Static

            i += 1 + CommandSize(TransformStackCommand(*i));
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

    template<bool UseDebugIterator>
        void GenerateOutputTransforms_Int(
            IteratorRange<Float4x4*>					result,
            const TransformationParameterSet*           parameterSet,
            IteratorRange<const uint32_t*>                commandStream,
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

        IteratorRange<const float*> float1s;
        IteratorRange<const Float3*> float3s;
        IteratorRange<const Float4*> float4s;
        IteratorRange<const Float4x4*> float4x4s;
        if (parameterSet) {
            float1s         = parameterSet->GetFloat1Parameters();
            float3s         = parameterSet->GetFloat3Parameters();
            float4s         = parameterSet->GetFloat4Parameters();
            float4x4s       = parameterSet->GetFloat4x4Parameters();
        }

        for (auto i=commandStream.cbegin(); i!=commandStream.cend();) {
            auto commandIndex = *i++;
            switch ((TransformStackCommand)commandIndex) {
            case TransformStackCommand::PushLocalToWorld:
                if ((workingTransform+1) >= &workingStack[dimof(workingStack)]) {
                    Throw(::Exceptions::BasicLabel("Exceeded maximum stack depth in GenerateOutputTransforms"));
                }
                    
                if (constant_expression<UseDebugIterator>::result())
                    debugIterator((workingTransform != workingStack) ? *(workingTransform-1) : Identity<Float4x4>(), *workingTransform);

                *(workingTransform+1) = *workingTransform;
                ++workingTransform;
                break;

            case TransformStackCommand::PopLocalToWorld:
                {
                    auto popCount = *i++;
                    if (workingTransform < workingStack+popCount) {
                        Throw(::Exceptions::BasicLabel("Stack underflow in GenerateOutputTransforms"));
                    }

                    workingTransform -= popCount;
                }
                break;

            case TransformStackCommand::TransformFloat4x4_Static:
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

            case TransformStackCommand::Translate_Static:
                // i = AdvanceTo16ByteAlignment(i);
                Combine_IntoRHS(AsFloat3(reinterpret_cast<const float*>(AsPointer(i))), *workingTransform);
                i += 3;
                break;

            case TransformStackCommand::RotateX_Static:
                Combine_IntoRHS(RotationX(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformStackCommand::RotateY_Static:
                Combine_IntoRHS(RotationY(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformStackCommand::RotateZ_Static:
                Combine_IntoRHS(RotationZ(Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i++;
                break;

            case TransformStackCommand::Rotate_Static:
                // i = AdvanceTo16ByteAlignment(i);
                Combine_IntoRHS(ArbitraryRotation(AsFloat3(reinterpret_cast<const float*>(AsPointer(i))), Deg2Rad(*reinterpret_cast<const float*>(AsPointer(i+3)))), *workingTransform);
                i += 4;
                break;

			case TransformStackCommand::RotateQuaternion_Static:
				Combine_IntoRHS(*reinterpret_cast<const Quaternion*>(AsPointer(i)), *workingTransform);
				i += 4;
				break;

            case TransformStackCommand::UniformScale_Static:
                Combine_IntoRHS(UniformScale(*reinterpret_cast<const float*>(AsPointer(i))), *workingTransform);
                i++;
                break;

            case TransformStackCommand::ArbitraryScale_Static:
                Combine_IntoRHS(ArbitraryScale(AsFloat3(reinterpret_cast<const float*>(AsPointer(i)))), *workingTransform);
                i+=3;
                break;

            case TransformStackCommand::TransformFloat4x4_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float4x4s.size()) {
                        *workingTransform = Combine(float4x4s[parameterIndex], *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for TransformFloat4x4_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::Translate_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float3s.size()) {
                        Combine_IntoRHS(float3s[parameterIndex], *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for Translate_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::RotateX_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float1s.size()) {
                        Combine_IntoRHS(RotationX(Deg2Rad(float1s[parameterIndex])), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for RotateX_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::RotateY_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float1s.size()) {
                        Combine_IntoRHS(RotationY(Deg2Rad(float1s[parameterIndex])), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for RotateY_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::RotateZ_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float1s.size()) {
                        Combine_IntoRHS(RotationZ(Deg2Rad(float1s[parameterIndex])), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for RotateZ_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::Rotate_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float4s.size()) {
						Combine_IntoRHS(ArbitraryRotation(Truncate(float4s[parameterIndex]), Deg2Rad(float4s[parameterIndex][3])), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for Rotate_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

			case TransformStackCommand::RotateQuaternion_Parameter:
				{
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float4s.size()) {
						const Float4& p = float4s[parameterIndex];
                        Combine_IntoRHS(Quaternion(p[0], p[1], p[2], p[3]), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for RotateQuaternion_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::UniformScale_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float1s.size()) {
                        Combine_IntoRHS(UniformScale(float1s[parameterIndex]), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for UniformScale_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::ArbitraryScale_Parameter:
                {
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float3s.size()) {
                        Combine_IntoRHS(ArbitraryScale(float3s[parameterIndex]), *workingTransform);
                    } else {
                        Log(Warning) << "Warning -- bad parameter index for ArbitraryScale_Parameter command (" << parameterIndex << ")" << std::endl;
                    }
                }
                break;

            case TransformStackCommand::WriteOutputMatrix:
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

            case TransformStackCommand::TransformFloat4x4AndWrite_Static:
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

            case TransformStackCommand::TransformFloat4x4AndWrite_Parameter:
                {
                    uint32_t outputIndex = *i++;
                    uint32_t parameterIndex = *i++;
                    if (parameterIndex < float4x4s.size()) {
                        if (outputIndex < result.size()) {
                            result[outputIndex] = Combine(float4x4s[parameterIndex], *workingTransform);
                            if (constant_expression<UseDebugIterator>::result())
                                debugIterator(*workingTransform, result[outputIndex]);
                        } else
                            Log(Warning) << "Warning -- bad output matrix index in TransformFloat4x4AndWrite_Parameter (" << outputIndex << ")" << std::endl;
                    } else
                        Log(Warning) << "Warning -- bad parameter index for TransformFloat4x4AndWrite_Parameter command (" << parameterIndex << ")" << std::endl;
                }
                break;

            case TransformStackCommand::Comment:
                i+=64/4;
                break;
            }
        }
    }

    void GenerateOutputTransforms(
        IteratorRange<Float4x4*>					result,
        const TransformationParameterSet*           parameterSet,
        IteratorRange<const uint32_t*>                commandStream)
    {
        GenerateOutputTransforms_Int<false>(
            result, parameterSet, commandStream, 
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
            auto commandIndex = *i++;
            switch ((TransformStackCommand)commandIndex) {
            case TransformStackCommand::PushLocalToWorld:
                if ((workingTransform+1) >= &workingStack[dimof(workingStack)])
                    Throw(::Exceptions::BasicLabel("Exceeded maximum stack depth in CalculateParentPointers"));

                *(workingTransform+1) = *workingTransform;
                ++workingTransform;
                break;

            case TransformStackCommand::PopLocalToWorld:
                {
                    auto popCount = *i++;
                    if (workingTransform < workingStack+popCount)
                        Throw(::Exceptions::BasicLabel("Stack underflow in CalculateParentPointers"));
                    workingTransform -= popCount;
                }
                break;

            case TransformStackCommand::TransformFloat4x4_Static:
                i += 16;
                break;

            case TransformStackCommand::Translate_Static:
                i += 3;
                break;

            case TransformStackCommand::RotateX_Static:
            case TransformStackCommand::RotateY_Static:
            case TransformStackCommand::RotateZ_Static:
                i++;
                break;

            case TransformStackCommand::Rotate_Static:
                i += 4;
                break;

			case TransformStackCommand::RotateQuaternion_Static:
				i += 4;
				break;

            case TransformStackCommand::UniformScale_Static:
                i++;
                break;

            case TransformStackCommand::ArbitraryScale_Static:
                i+=3;
                break;

            case TransformStackCommand::TransformFloat4x4_Parameter:
            case TransformStackCommand::Translate_Parameter:
            case TransformStackCommand::RotateX_Parameter:
            case TransformStackCommand::RotateY_Parameter:
            case TransformStackCommand::RotateZ_Parameter:
            case TransformStackCommand::Rotate_Parameter:
			case TransformStackCommand::RotateQuaternion_Parameter:
            case TransformStackCommand::UniformScale_Parameter:
            case TransformStackCommand::ArbitraryScale_Parameter:
				++i;
				break;

            case TransformStackCommand::WriteOutputMatrix:
                {
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

            case TransformStackCommand::TransformFloat4x4AndWrite_Static:
                {
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

            case TransformStackCommand::TransformFloat4x4AndWrite_Parameter:
                {
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

            case TransformStackCommand::Comment:
                i+=64/4;
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
			auto nexti = i + 1 + CommandSize((TransformStackCommand)*i);
            if (IsOutputCommand((TransformStackCommand)*i)) {
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
    
    void TraceTransformationMachine(
        std::ostream&   stream,
        IteratorRange<const uint32_t*>    commandStream,
        std::function<std::string(unsigned)> outputMatrixToName,
        std::function<std::string(AnimSamplerType, unsigned)> parameterToName)
    {
        stream << "Transformation machine size: (" << (commandStream.size()) * sizeof(uint32_t) << ") bytes" << std::endl;

        char indentBuffer[32];
        signed indentLevel = 1;
        MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);

        for (auto i=commandStream.begin(); i!=commandStream.end();) {
            auto commandIndex = *i++;
            switch ((TransformStackCommand)commandIndex) {
            case TransformStackCommand::PushLocalToWorld:
                stream << indentBuffer << "PushLocalToWorld" << std::endl;
                ++indentLevel;
                MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);
                break;

            case TransformStackCommand::PopLocalToWorld:
                {
                    auto popCount = *i++;
                    stream << indentBuffer << "PopLocalToWorld (" << popCount << ")" << std::endl;
                    indentLevel -= popCount;
                    MakeIndentBuffer(indentBuffer, dimof(indentBuffer), indentLevel);
                }
                break;

            case TransformStackCommand::TransformFloat4x4_Static:
                {
                    auto trans = *reinterpret_cast<const Float4x4*>(AsPointer(i));
                    stream << indentBuffer << "TransformFloat4x4_Static (";
                    CompactTransformDescription(stream, trans);
                    stream << ")" << std::endl; 
                    i += 16;
                }
                break;

            case TransformStackCommand::Translate_Static:
                {
                    auto trans = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                    stream << indentBuffer << "Translate_Static (" << trans[0] << ", " << trans[1] << ", " << trans[2] << ")" << std::endl;
                    i += 3;
                }
                break;

            case TransformStackCommand::RotateX_Static:
                stream << indentBuffer << "RotateX_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")" << std::endl;
                i++;
                break;

            case TransformStackCommand::RotateY_Static:
                stream << indentBuffer << "RotateY_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")" << std::endl;
                i++;
                break;

            case TransformStackCommand::RotateZ_Static:
                stream << indentBuffer << "RotateZ_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")" << std::endl;
                i++;
                break;

            case TransformStackCommand::Rotate_Static:
                {
                    auto a = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                    float r = *reinterpret_cast<const float*>(AsPointer(i+3));
                    stream << indentBuffer << "Rotate_Static (" << a[0] << ", " << a[1] << ", " << a[2] << ")(" << r << ")" << std::endl;
                    i += 4;
                }
                break;

			case TransformStackCommand::RotateQuaternion_Static:
                {
                    auto& q = *reinterpret_cast<const Quaternion*>(AsPointer(i));
                    stream << indentBuffer << "RotateQuaternion_Static (" << q[0] << ", " << q[1] << ", " << q[2] << ", " << q[3] << ")" << std::endl;
                    i += 4;
                }
                break;

            case TransformStackCommand::UniformScale_Static:
                stream << indentBuffer << "UniformScale_Static (" << *reinterpret_cast<const float*>(AsPointer(i)) << ")" << std::endl;
                i++;
                break;

            case TransformStackCommand::ArbitraryScale_Static:
                {
                    auto trans = AsFloat3(reinterpret_cast<const float*>(AsPointer(i)));
                    stream << indentBuffer << "ArbitraryScale_Static (" << trans[0] << ", " << trans[1] << ", " << trans[2] << ")" << std::endl;
                }
                i+=3;
                break;

            case TransformStackCommand::TransformFloat4x4_Parameter:
                stream << indentBuffer << "TransformFloat4x4_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float4x4, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::Translate_Parameter:
                stream << indentBuffer << "Translate_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float3, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::RotateX_Parameter:
                stream << indentBuffer << "RotateX_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float1, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::RotateY_Parameter:
                stream << indentBuffer << "RotateY_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float1, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::RotateZ_Parameter:
                stream << indentBuffer << "RotateZ_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float1, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::Rotate_Parameter:
                stream << indentBuffer << "Rotate_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float4, *i) << ")";
                stream << std::endl;
                i++;
                break;

			case TransformStackCommand::RotateQuaternion_Parameter:
                stream << indentBuffer << "RotateQuaternion_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Quaternion, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::UniformScale_Parameter:
                stream << indentBuffer << "UniformScale_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float1, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::ArbitraryScale_Parameter:
                stream << indentBuffer << "ArbitraryScale_Parameter [" << *i << "]";
                if (parameterToName)
                    stream << " (" << parameterToName(AnimSamplerType::Float3, *i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::WriteOutputMatrix:
                stream << indentBuffer << "WriteOutputMatrix [" << *i << "]";
                if (outputMatrixToName)
                    stream << " (" << outputMatrixToName(*i) << ")";
                stream << std::endl;
                i++;
                break;

            case TransformStackCommand::TransformFloat4x4AndWrite_Static:
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

            case TransformStackCommand::TransformFloat4x4AndWrite_Parameter:
                stream << indentBuffer << "TransformFloat4x4AndWrite_Parameter [" << *i << "]";
                if (outputMatrixToName)
                    stream << " (" << outputMatrixToName(*i) << ")";
                stream << indentBuffer << " param: (" 
                    << parameterToName(AnimSamplerType::Float4x4, *(i+1)) << ")" << std::endl;
                i+=2;
                break;

            case TransformStackCommand::Comment:
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

	const char* AsString(AnimSamplerType value)
	{
		switch (value) {
		case AnimSamplerType::Float1: return "Float1";
		case AnimSamplerType::Float3: return "Float3";
		case AnimSamplerType::Float4: return "Float4";
		case AnimSamplerType::Quaternion: return "Quaternion";
		case AnimSamplerType::Float4x4: return "Float4x4";
		}
		return "<<unknown>>";
	}

}}


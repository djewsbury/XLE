// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LightWeightBuildDrawables.h"
#include "TechniqueUtils.h"
#include "ParsingContext.h"
#include "Drawables.h"
#include "DrawableConstructor.h"
#include "CommonBindings.h"
#include "SimpleModelRenderer.h"		// for ModelConstructionSkeletonBinding
#include "../Assets/ModelMachine.h"
#include "../UniformsStream.h"
#include "../../Math/Transformations.h"
#include "../../Utility/ArithmeticUtils.h"

namespace RenderCore { namespace Techniques
{
	#if _DEBUG
		static Float3x4    Combine_NoDebugOverhead(const Float3x4& firstTransform, const Float3x4& secondTransform)
		{
			// Debug overhead is kind of crazy for this operation. Let's just cut some of it out, just for day-to-day
			// convenience
			const float* lhs = (const float*)&secondTransform;
			const float* rhs = (const float*)&firstTransform;

			Float3x4 resultm;
			float* result = (float*)&resultm;
			result[0*4+0] = lhs[0*4+0] * rhs[0*4+0] + lhs[0*4+1] * rhs[1*4+0] + lhs[0*4+2] * rhs[2*4+0];
			result[0*4+1] = lhs[0*4+0] * rhs[0*4+1] + lhs[0*4+1] * rhs[1*4+1] + lhs[0*4+2] * rhs[2*4+1];
			result[0*4+2] = lhs[0*4+0] * rhs[0*4+2] + lhs[0*4+1] * rhs[1*4+2] + lhs[0*4+2] * rhs[2*4+2];
			result[0*4+3] = lhs[0*4+0] * rhs[0*4+3] + lhs[0*4+1] * rhs[1*4+3] + lhs[0*4+2] * rhs[2*4+3] + lhs[0*4+3];
			
			result[1*4+0] = lhs[1*4+0] * rhs[0*4+0] + lhs[1*4+1] * rhs[1*4+0] + lhs[1*4+2] * rhs[2*4+0];
			result[1*4+1] = lhs[1*4+0] * rhs[0*4+1] + lhs[1*4+1] * rhs[1*4+1] + lhs[1*4+2] * rhs[2*4+1];
			result[1*4+2] = lhs[1*4+0] * rhs[0*4+2] + lhs[1*4+1] * rhs[1*4+2] + lhs[1*4+2] * rhs[2*4+2];
			result[1*4+3] = lhs[1*4+0] * rhs[0*4+3] + lhs[1*4+1] * rhs[1*4+3] + lhs[1*4+2] * rhs[2*4+3] + lhs[1*4+3];
			
			result[2*4+0] = lhs[2*4+0] * rhs[0*4+0] + lhs[2*4+1] * rhs[1*4+0] + lhs[2*4+2] * rhs[2*4+0];
			result[2*4+1] = lhs[2*4+0] * rhs[0*4+1] + lhs[2*4+1] * rhs[1*4+1] + lhs[2*4+2] * rhs[2*4+1];
			result[2*4+2] = lhs[2*4+0] * rhs[0*4+2] + lhs[2*4+1] * rhs[1*4+2] + lhs[2*4+2] * rhs[2*4+2];
			result[2*4+3] = lhs[2*4+0] * rhs[0*4+3] + lhs[2*4+1] * rhs[1*4+3] + lhs[2*4+2] * rhs[2*4+3] + lhs[2*4+3];
			return resultm;
		}
	#else
		#define Combine_NoDebugOverhead Combine
	#endif
	
	namespace Internal
	{
		struct InstancedFixedSkeleton_Drawable : public RenderCore::Techniques::Drawable
		{
			unsigned _firstIndex, _indexCount;
			const Float3x4* _objectToWorlds;
			unsigned _objectToWorldCount;
		};

		static void DrawFn_InstancedFixedSkeleton(
			RenderCore::Techniques::ParsingContext& parserContext,
			const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext,
			const InstancedFixedSkeleton_Drawable& drawable)
		{
			assert(drawable._objectToWorldCount != 0);
			assert(drawFnContext.GetBoundLooseImmediateDatas());
			LocalTransformConstants localTransform;
			localTransform._localSpaceView = {0,0,0};
			localTransform._viewMask = 1u;
			RenderCore::UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(localTransform) };

			for (unsigned c=0; c<drawable._objectToWorldCount; ++c) {
				localTransform._localToWorld = drawable._objectToWorlds[c];
				drawFnContext.ApplyLooseUniforms(RenderCore::UniformsStream{{}, immDatas});
				drawFnContext.DrawIndexed(drawable._indexCount, drawable._firstIndex);
			}
		}

		static UniformsStreamInterface MakeLocalTransformUSI()
		{
			UniformsStreamInterface result;
			result.BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
			return result;
		}
		static UniformsStreamInterface s_localTransformUSI = MakeLocalTransformUSI();
	}

	void LightWeightBuildDrawables::InstancedFixedSkeleton(
		RenderCore::Techniques::DrawableConstructor& constructor,
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
		IteratorRange<const Float3x4*> objectToWorlds)
	{
		using namespace RenderCore;
		assert(!constructor._cmdStreams.empty());
		auto& cmdStream = constructor._cmdStreams.front();		// first is always the default
		Internal::InstancedFixedSkeleton_Drawable* drawables[dimof(cmdStream._drawCallCounts)];
		RenderCore::Techniques::DrawablesPacket* pktForAllocations = nullptr;
		for (unsigned c=0; c<dimof(cmdStream._drawCallCounts); ++c) {
			if (cmdStream._drawCallCounts[c] && pkts[c]) {
				drawables[c] = pkts[c]->_drawables.Allocate<Internal::InstancedFixedSkeleton_Drawable>(cmdStream._drawCallCounts[c]);
				pktForAllocations = pkts[c];
			} else {
				drawables[c] = nullptr;
			}
		}
		if (!pktForAllocations) return;		// no overlap between our output pkts and what's in 'pkts'

		const unsigned deformInstanceIdx = ~0u;

		const Float4x4* geoSpaceToNodeSpace = nullptr;
		unsigned transformMarker = ~0u;
		std::pair<unsigned, unsigned> baseTransformsRange { 0, 0 };
		for (auto cmd:cmdStream.GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				assert(transformMarker+baseTransformsRange.first < baseTransformsRange.second);
				assert(transformMarker+baseTransformsRange.first < constructor._baseTransforms.size());
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				baseTransformsRange = constructor._elementBaseTransformRanges[cmd.As<unsigned>()];
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto* transformsPkt = (Float3x4*)pktForAllocations->AllocateStorage(DrawablesPacket::Storage::CPU, sizeof(Float3x4)*objectToWorlds.size())._data.begin();
					assert(transformMarker != ~0u);		// SetTransformMarker must come first
					if (geoSpaceToNodeSpace) {
						for (unsigned c=0; c<objectToWorlds.size(); ++c)
							transformsPkt[c] = Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorlds[c]));
					} else
						for (unsigned c=0; c<objectToWorlds.size(); ++c)
							transformsPkt[c] = Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorlds[c]);

					for (const auto& dc:MakeIteratorRange(cmdStream._drawCalls.begin()+drawCallsRef._start, cmdStream._drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = constructor._drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = constructor._pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = constructor._descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&Internal::DrawFn_InstancedFixedSkeleton;
						drawable._looseUniformsInterface = &Internal::s_localTransformUSI;
						assert(dc._firstVertex == 0);
						drawable._firstIndex = dc._firstIndex;
						drawable._indexCount = dc._indexCount;
						drawable._objectToWorldCount = objectToWorlds.size();
						drawable._objectToWorlds = transformsPkt;
						drawable._deformInstanceIdx = deformInstanceIdx;
					}
				}
				break;
			}
		}
	}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct InstancedFixedSkeletonViewMask_Drawable : public InstancedFixedSkeleton_Drawable
		{
			uint32_t* _viewMasks;
		};

		static unsigned CountBitsSet(uint32_t viewMask)
		{
			unsigned v=0;
			while (viewMask) {
				auto lz = xl_ctz8(viewMask);
				v++;
				viewMask ^= 1ull<<lz;
			}
			return v;
		}

		static void DrawFn_InstancedFixedSkeletonViewMask(
			RenderCore::Techniques::ParsingContext& parserContext,
			const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext,
			const InstancedFixedSkeletonViewMask_Drawable& drawable)
		{
			assert(drawable._objectToWorldCount != 0);
			assert(drawFnContext.GetBoundLooseImmediateDatas());
			LocalTransformConstants localTransform;
			localTransform._localSpaceView = {0,0,0};
			RenderCore::UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(localTransform) };

			for (unsigned c=0; c<drawable._objectToWorldCount; ++c) {
				auto viewCount = CountBitsSet(drawable._viewMasks[c]);
				assert(viewCount);
				localTransform._localToWorld = drawable._objectToWorlds[c];
				localTransform._viewMask = drawable._viewMasks[c];
				drawFnContext.ApplyLooseUniforms(RenderCore::UniformsStream{{}, immDatas});
				drawFnContext.DrawIndexedInstances(drawable._indexCount, viewCount, drawable._firstIndex);
			}
		}
	}

	void LightWeightBuildDrawables::InstancedFixedSkeleton(
		RenderCore::Techniques::DrawableConstructor& constructor,
		IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
		IteratorRange<const Float3x4*> objectToWorlds,
		IteratorRange<const unsigned*> viewMasks)
	{
		using namespace RenderCore;
		assert(viewMasks.size() == objectToWorlds.size());
		assert(!constructor._cmdStreams.empty());
		auto& cmdStream = constructor._cmdStreams.front();		// first is always the default
		Internal::InstancedFixedSkeletonViewMask_Drawable* drawables[dimof(cmdStream._drawCallCounts)];
		RenderCore::Techniques::DrawablesPacket* pktForAllocations = nullptr;
		for (unsigned c=0; c<dimof(cmdStream._drawCallCounts); ++c) {
			if (cmdStream._drawCallCounts[c] && pkts[c]) {
				drawables[c] = pkts[c]->_drawables.Allocate<Internal::InstancedFixedSkeletonViewMask_Drawable>(cmdStream._drawCallCounts[c]);
				pktForAllocations = pkts[c];
			} else {
				drawables[c] = nullptr;
			}
		}
		if (!pktForAllocations) return;		// no overlap between our output pkts and what's in 'pkts'

		const unsigned deformInstanceIdx = ~0u;

		const Float4x4* geoSpaceToNodeSpace = nullptr;
		unsigned transformMarker = ~0u;
		std::pair<unsigned, unsigned> baseTransformsRange { 0, 0 };
		for (auto cmd:cmdStream.GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				assert(transformMarker+baseTransformsRange.first < baseTransformsRange.second);
				assert(transformMarker+baseTransformsRange.first < constructor._baseTransforms.size());
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				baseTransformsRange = constructor._elementBaseTransformRanges[cmd.As<unsigned>()];
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto* extraData = pktForAllocations->AllocateStorage(DrawablesPacket::Storage::CPU, (sizeof(Float3x4)+sizeof(uint32_t))*objectToWorlds.size())._data.begin();
					auto* transformsPkt = (Float3x4*)extraData;
					assert(transformMarker != ~0u);		// SetTransformMarker must come first
					if (geoSpaceToNodeSpace) {
						for (unsigned c=0; c<objectToWorlds.size(); ++c)
							transformsPkt[c] = Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorlds[c]));
					} else
						for (unsigned c=0; c<objectToWorlds.size(); ++c)
							transformsPkt[c] = Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorlds[c]);

					auto* viewMasksPkt = (uint32_t*)PtrAdd(extraData, sizeof(Float3x4)*objectToWorlds.size());
					for (unsigned c=0; c<viewMasks.size(); ++c) viewMasksPkt[c] = viewMasks[c];

					for (const auto& dc:MakeIteratorRange(cmdStream._drawCalls.begin()+drawCallsRef._start, cmdStream._drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = constructor._drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = constructor._pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = constructor._descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&Internal::DrawFn_InstancedFixedSkeletonViewMask;
						drawable._looseUniformsInterface = &Internal::s_localTransformUSI;
						assert(dc._firstVertex == 0);
						drawable._firstIndex = dc._firstIndex;
						drawable._indexCount = dc._indexCount;
						drawable._objectToWorldCount = objectToWorlds.size();
						drawable._objectToWorlds = transformsPkt;
						drawable._deformInstanceIdx = deformInstanceIdx;
						drawable._viewMasks = viewMasksPkt;
					}
				}
				break;
			}
		}
	}

	namespace Internal
	{
		struct SingleInstanceViewMask_Drawable : public RenderCore::Techniques::Drawable
		{
			unsigned _firstIndex, _indexCount;
			Float3x4 _localToWorld;
			uint32_t _viewMask;
		};

		static void DrawFn_SingleInstanceViewMask(
			RenderCore::Techniques::ParsingContext& parserContext,
			const RenderCore::Techniques::ExecuteDrawableContext& drawFnContext,
			const SingleInstanceViewMask_Drawable& drawable)
		{
			assert(drawFnContext.GetBoundLooseImmediateDatas());
			LocalTransformConstants localTransform;
			localTransform._localSpaceView = {0,0,0};
			RenderCore::UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(localTransform) };

			auto viewCount = CountBitsSet(drawable._viewMask);
			assert(viewCount);
			localTransform._localToWorld = drawable._localToWorld;
			localTransform._viewMask = drawable._viewMask;
			drawFnContext.ApplyLooseUniforms(RenderCore::UniformsStream{{}, immDatas});
			drawFnContext.DrawIndexedInstances(drawable._indexCount, viewCount, drawable._firstIndex);
		}
	}

	void LightWeightBuildDrawables::SingleInstance(
		DrawableConstructor& constructor,
		IteratorRange<DrawablesPacket** const> pkts,
		const Float3x4& objectToWorld,
		unsigned deformInstanceIdx,
		uint32_t viewMask)
	{
		using namespace RenderCore;
		assert(viewMask);
		assert(!constructor._cmdStreams.empty());
		auto& cmdStream = constructor._cmdStreams.front();		// first is always the default
		Internal::SingleInstanceViewMask_Drawable* drawables[dimof(cmdStream._drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream._drawCallCounts); ++c)
			drawables[c] = (cmdStream._drawCallCounts[c] && pkts[c]) ? pkts[c]->_drawables.Allocate<Internal::SingleInstanceViewMask_Drawable>(cmdStream._drawCallCounts[c]) : nullptr;

		const Float4x4* geoSpaceToNodeSpace = nullptr;
		unsigned transformMarker = ~0u;
		std::pair<unsigned, unsigned> baseTransformsRange { 0, 0 };
		for (auto cmd:cmdStream.GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				assert(transformMarker+baseTransformsRange.first < baseTransformsRange.second);
				assert(transformMarker+baseTransformsRange.first < constructor._baseTransforms.size());
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				baseTransformsRange = constructor._elementBaseTransformRanges[cmd.As<unsigned>()];
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					assert(transformMarker != ~0u);		// SetTransformMarker must come first
					Float3x4 localToWorld;
					if (geoSpaceToNodeSpace) {
						localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorld));
					} else
						localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&constructor._baseTransforms[transformMarker+baseTransformsRange.first], objectToWorld);

					for (const auto& dc:MakeIteratorRange(cmdStream._drawCalls.begin()+drawCallsRef._start, cmdStream._drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = constructor._drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = constructor._pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = constructor._descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&Internal::DrawFn_SingleInstanceViewMask;
						drawable._looseUniformsInterface = &Internal::s_localTransformUSI;
						assert(dc._firstVertex == 0);
						drawable._firstIndex = dc._firstIndex;
						drawable._indexCount = dc._indexCount;
						drawable._localToWorld = localToWorld;
						drawable._deformInstanceIdx = deformInstanceIdx;
						drawable._viewMask = viewMask;
					}
				}
				break;
			}
		}
	}

	void LightWeightBuildDrawables::SingleInstance(
		DrawableConstructor& constructor,
		IteratorRange<DrawablesPacket** const> pkts,
		const Float3x4& objectToWorld,
		const ModelConstructionSkeletonBinding& skeletonBinding,
		IteratorRange<const Float4x4*> animatedSkeletonOutput,
		unsigned deformInstanceIdx,
		uint32_t viewMask)
	{
		using namespace RenderCore;
		assert(viewMask);
		assert(!constructor._cmdStreams.empty());
		auto& cmdStream = constructor._cmdStreams.front();		// first is always the default
		Internal::SingleInstanceViewMask_Drawable* drawables[dimof(cmdStream._drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream._drawCallCounts); ++c)
			drawables[c] = (cmdStream._drawCallCounts[c] && pkts[c]) ? pkts[c]->_drawables.Allocate<Internal::SingleInstanceViewMask_Drawable>(cmdStream._drawCallCounts[c]) : nullptr;

		auto nodeSpaceToWorld = Identity<Float3x4>();
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		for (auto cmd:cmdStream.GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					auto animatedIdx =skeletonBinding.ModelJointToMachineOutput(elementIdx, transformMarker);
					if (animatedIdx < animatedSkeletonOutput.size()) {
						auto& animatedTransform = animatedSkeletonOutput[animatedIdx];
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&animatedTransform, objectToWorld);
					} else {
						auto& baseTransform = skeletonBinding.ModelJointToUnanimatedTransform(elementIdx, transformMarker);
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&baseTransform, objectToWorld);
					}
				}
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					assert(transformMarker != ~0u);		// SetTransformMarker must come first
					auto localTransform = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld;
					for (const auto& dc:MakeIteratorRange(cmdStream._drawCalls.begin()+drawCallsRef._start, cmdStream._drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = constructor._drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = constructor._pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = constructor._descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&Internal::DrawFn_SingleInstanceViewMask;
						drawable._looseUniformsInterface = &Internal::s_localTransformUSI;
						assert(dc._firstVertex == 0);
						drawable._firstIndex = dc._firstIndex;
						drawable._indexCount = dc._indexCount;
						drawable._localToWorld = localTransform;
						drawable._deformInstanceIdx = deformInstanceIdx;
						drawable._viewMask = viewMask;
					}
				}
				break;
			}
		}
	}
}}

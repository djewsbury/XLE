// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelRenderer.h"
#include "DrawableConstructor.h"
#include "Drawables.h"
#include "TechniqueUtils.h"
#include "CommonBindings.h"
#include "PipelineAccelerator.h"
#include "DeformAccelerator.h"
#include "DeformGeometryInfrastructure.h"
#include "DeformerConstruction.h"
#include "SkinDeformer.h"
#include "Services.h"
#include "../Assets/ModelRendererConstruction.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelMachine.h"		// for DrawCallDesc
#include "../Assets/AnimationBindings.h"
#include "../Assets/CompoundObject.h"
#include "../Assets/CompileAndAsyncManager.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/ArithmeticUtils.h"
#include <utility>
#include <map>

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleModelDrawable : public Techniques::Drawable
	{
	public:
		RenderCore::Assets::DrawCallDesc _drawCall;
		LocalTransformConstants _localTransform;
		uint64_t _materialGuid;
		unsigned _drawCallIdx;
	};

	struct DrawCallProperties
	{
		uint64_t _materialGuid;
		unsigned _drawCallIdx;
		unsigned _dummy;
	};

	static void DrawFn_SimpleModelStatic(
		Techniques::ParsingContext& parserContext,
		const Techniques::ExecuteDrawableContext& drawFnContext,
		const SimpleModelDrawable& drawable)
	{
		if (drawFnContext.GetBoundLooseImmediateDatas()) {
			DrawCallProperties drawCallProps{drawable._materialGuid, drawable._drawCallIdx};
			UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(drawable._localTransform), MakeOpaqueIteratorRange(drawCallProps) };
			drawFnContext.ApplyLooseUniforms(UniformsStream{{}, immDatas});
		}

		drawFnContext.DrawIndexed(
			drawable._drawCall._indexCount, drawable._drawCall._firstIndex, drawable._drawCall._firstVertex);
	}

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

	static void DrawFn_SimpleModelStaticMultiView(
		Techniques::ParsingContext& parserContext,
		const Techniques::ExecuteDrawableContext& drawFnContext,
		const SimpleModelDrawable& drawable)
	{
		auto viewCount = CountBitsSet(drawable._localTransform._viewMask);
		if (!viewCount) return;
		assert(viewCount <= 32);

		if (drawFnContext.GetBoundLooseImmediateDatas()) {
			DrawCallProperties drawCallProps{drawable._materialGuid, drawable._drawCallIdx};
			UniformsStream::ImmediateData immDatas[] { MakeOpaqueIteratorRange(drawable._localTransform), MakeOpaqueIteratorRange(drawCallProps) };
			drawFnContext.ApplyLooseUniforms(UniformsStream{{}, immDatas});
		}

		drawFnContext.DrawIndexedInstances(
			drawable._drawCall._indexCount, viewCount, drawable._drawCall._firstIndex, drawable._drawCall._firstVertex);
	}

	class SimpleModelDrawable_Delegate : public SimpleModelDrawable
	{
	public:
		std::shared_ptr<ICustomDrawDelegate> _delegate;
	};

	static void DrawFn_SimpleModelDelegate(
		Techniques::ParsingContext& parserContext,
		const Techniques::ExecuteDrawableContext& drawFnContext,
		const SimpleModelDrawable_Delegate& drawable)
	{
		assert(drawable._delegate);
		drawable._delegate->OnDraw(parserContext, drawFnContext, drawable);
	}

	uint64_t ICustomDrawDelegate::GetMaterialGuid(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._materialGuid; }
	unsigned ICustomDrawDelegate::GetDrawCallIndex(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._drawCallIdx; }
	Float3x4 ICustomDrawDelegate::GetLocalToWorld(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._localTransform._localToWorld; }
	uint32_t ICustomDrawDelegate::GetViewMask(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._localTransform._viewMask; }
	RenderCore::Assets::DrawCallDesc ICustomDrawDelegate::GetDrawCallDesc(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._drawCall; }
	void ICustomDrawDelegate::ExecuteStandardDraw(ParsingContext& parsingContext, const ExecuteDrawableContext& drawFnContext, const Drawable& d)
	{
		auto& simpleModelDrawable = (const SimpleModelDrawable&)d;
		if (simpleModelDrawable._localTransform._viewMask == 1) {
			DrawFn_SimpleModelStatic(parsingContext, drawFnContext, simpleModelDrawable);
		} else {
			DrawFn_SimpleModelStaticMultiView(parsingContext, drawFnContext, simpleModelDrawable);
		}
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static const uint64_t s_topologicalCmdStream = Hash64("adjacency");

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		uint32_t viewMask,
		uint64_t cmdStreamGuid) const
	{
		assert(viewMask != 0);
		if (_deformAccelerator)
			EnableInstanceDeform(*_deformAccelerator, deformInstanceIdx);

		auto* cmdStream = _drawableConstructor->FindCmdStream(cmdStreamGuid);
		assert(cmdStream);
		SimpleModelDrawable* drawables[dimof(cmdStream->_drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream->_drawCallCounts); ++c) {
			if (!cmdStream->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable>(cmdStream->_drawCallCounts[c]) : nullptr;
		}

		auto* drawableFn = (viewMask==1) ? (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic : (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStaticMultiView; 

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:cmdStream->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto& baseTransform = _skeletonBinding.ModelJointToUnanimatedTransform(elementIdx, transformMarker);
					nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&baseTransform, localToWorld3x4);
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				localToWorld3x4 = Combine(_skeletonBinding._elementToObject[elementIdx], AsFloat3x4(localToWorld));
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto localTransform = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld; // todo -- don't have to recalculate this every draw call
					for (const auto& dc:MakeIteratorRange(cmdStream->_drawCalls.begin()+drawCallsRef._start, cmdStream->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = _drawableConstructor->_pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = _drawableConstructor->_descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = drawableFn;
						drawable._drawCall = RenderCore::Assets::DrawCallDesc { dc._firstIndex, dc._indexCount, dc._firstVertex };
						drawable._looseUniformsInterface = _usi.get();
						drawable._materialGuid = materialGuids[materialGuidsIterator++];
						drawable._drawCallIdx = drawCallCounter;
						drawable._localTransform._localToWorld = localTransform;
						drawable._localTransform._localSpaceView = Float3{0,0,0};
						drawable._localTransform._viewMask = viewMask;
						drawable._deformInstanceIdx = deformInstanceIdx;
						++drawCallCounter;
					}
				}
				break;
			}
		}

		// if we need the topological batch, make sure to draw the appropriate cmd stream
		if (pkts[(unsigned)Batch::Topological] && cmdStreamGuid != s_topologicalCmdStream)
			BuildDrawables(pkts, localToWorld, deformInstanceIdx, viewMask, s_topologicalCmdStream);
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		IteratorRange<const Float4x4*> animatedSkeletonOutput,
		unsigned deformInstanceIdx,
		uint32_t viewMask,
		uint64_t cmdStreamGuid) const
	{
		assert(viewMask != 0);
		if (_deformAccelerator)
			EnableInstanceDeform(*_deformAccelerator, deformInstanceIdx);

		auto* cmdStream = _drawableConstructor->FindCmdStream(cmdStreamGuid);
		assert(cmdStream);
		SimpleModelDrawable* drawables[dimof(cmdStream->_drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream->_drawCallCounts); ++c) {
			if (!cmdStream->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable>(cmdStream->_drawCallCounts[c]) : nullptr;
		}

		auto* drawableFn = (viewMask==1) ? (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic : (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStaticMultiView; 

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:cmdStream->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto animatedIdx =_skeletonBinding.ModelJointToMachineOutput(elementIdx, transformMarker);
					if (animatedIdx < animatedSkeletonOutput.size()) {
						auto& animatedTransform = animatedSkeletonOutput[animatedIdx];
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&animatedTransform, localToWorld3x4);
					} else {
						auto& baseTransform = _skeletonBinding.ModelJointToUnanimatedTransform(elementIdx, transformMarker);
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&baseTransform, localToWorld3x4);
					}
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				localToWorld3x4 = Combine(_skeletonBinding._elementToObject[elementIdx], AsFloat3x4(localToWorld));
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto localTransform = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld; // todo -- don't have to recalculate this every draw call
					for (const auto& dc:MakeIteratorRange(cmdStream->_drawCalls.begin()+drawCallsRef._start, cmdStream->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = _drawableConstructor->_pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = _drawableConstructor->_descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = drawableFn;
						drawable._drawCall = RenderCore::Assets::DrawCallDesc { dc._firstIndex, dc._indexCount, dc._firstVertex };
						drawable._looseUniformsInterface = _usi.get();
						drawable._materialGuid = materialGuids[materialGuidsIterator++];
						drawable._drawCallIdx = drawCallCounter;
						drawable._localTransform._localToWorld = localTransform;
						drawable._localTransform._localSpaceView = Float3{0,0,0};
						drawable._localTransform._viewMask = viewMask;
						drawable._deformInstanceIdx = deformInstanceIdx;
						++drawCallCounter;
					}
				}
				break;
			}
		}

		// if we need the topological batch, make sure to draw the appropriate cmd stream
		if (pkts[(unsigned)Batch::Topological] && cmdStreamGuid != s_topologicalCmdStream)
			BuildDrawables(pkts, localToWorld, deformInstanceIdx, viewMask, s_topologicalCmdStream);
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		IteratorRange<const Float4x4*> animatedSkeletonOutput,
		unsigned deformInstanceIdx,
		const std::shared_ptr<ICustomDrawDelegate>& delegate,
		uint32_t viewMask,
		uint64_t cmdStreamGuid) const
	{
		if (!delegate) {
			BuildDrawables(pkts, localToWorld, animatedSkeletonOutput, deformInstanceIdx, viewMask, cmdStreamGuid);
			return;
		}

		assert(viewMask != 0);
		if (_deformAccelerator)
			EnableInstanceDeform(*_deformAccelerator, deformInstanceIdx);

		auto* cmdStream = _drawableConstructor->FindCmdStream(cmdStreamGuid);
		assert(cmdStream);
		SimpleModelDrawable* drawables[dimof(cmdStream->_drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream->_drawCallCounts); ++c) {
			if (!cmdStream->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable_Delegate>(cmdStream->_drawCallCounts[c]) : nullptr;
		}

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:cmdStream->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto animatedIdx =_skeletonBinding.ModelJointToMachineOutput(elementIdx, transformMarker);
					if (animatedIdx < animatedSkeletonOutput.size()) {
						auto& animatedTransform = animatedSkeletonOutput[animatedIdx];
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&animatedTransform, localToWorld3x4);
					} else {
						auto& baseTransform = _skeletonBinding.ModelJointToUnanimatedTransform(elementIdx, transformMarker);
						nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&baseTransform, localToWorld3x4);
					}
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				localToWorld3x4 = Combine(_skeletonBinding._elementToObject[elementIdx], AsFloat3x4(localToWorld));
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto localTransform = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld; // todo -- don't have to recalculate this every draw call
					for (const auto& dc:MakeIteratorRange(cmdStream->_drawCalls.begin()+drawCallsRef._start, cmdStream->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx].get();
						drawable._pipeline = _drawableConstructor->_pipelineAccelerators[dc._pipelineAcceleratorIdx].get();
						drawable._descriptorSet = _drawableConstructor->_descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx].get();
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelDelegate;
						drawable._drawCall = RenderCore::Assets::DrawCallDesc { dc._firstIndex, dc._indexCount, dc._firstVertex };
						drawable._looseUniformsInterface = _usi.get();
						drawable._materialGuid = materialGuids[materialGuidsIterator++];
						drawable._drawCallIdx = drawCallCounter;
						drawable._localTransform._localToWorld = localTransform;
						drawable._localTransform._localSpaceView = Float3{0,0,0};
						drawable._localTransform._viewMask = viewMask;
						drawable._deformInstanceIdx = deformInstanceIdx;
						++drawCallCounter;
					}
				}
				break;
			}
		}

		// if we need the topological batch, make sure to draw the appropriate cmd stream
		if (pkts[(unsigned)Batch::Topological] && cmdStreamGuid != s_topologicalCmdStream)
			BuildDrawables(pkts, localToWorld, animatedSkeletonOutput, deformInstanceIdx, delegate, viewMask, s_topologicalCmdStream);
	}

	void SimpleModelRenderer::BuildGeometryProcables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		uint64_t cmdStreamGuid) const 
	{
		assert(!_drawableConstructor->_cmdStreams.empty());
		auto* cmdStream = _drawableConstructor->FindCmdStream(cmdStreamGuid);
		assert(cmdStream);
		GeometryProcable* drawables[dimof(cmdStream->_drawCallCounts)];
		for (unsigned c=0; c<dimof(cmdStream->_drawCallCounts); ++c) {
			if (!cmdStream->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<GeometryProcable>(cmdStream->_drawCallCounts[c]) : nullptr;
		}

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		for (auto cmd:cmdStream->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto& baseTransform = _skeletonBinding.ModelJointToUnanimatedTransform(elementIdx, transformMarker);
					nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&baseTransform, localToWorld3x4);
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				localToWorld3x4 = Combine(_skeletonBinding._elementToObject[elementIdx], AsFloat3x4(localToWorld));
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();
					auto localToWorld = AsFloat4x4(geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld); // todo -- don't have to recalculate this every draw call
					for (const auto& dc:MakeIteratorRange(cmdStream->_drawCalls.begin()+drawCallsRef._start, cmdStream->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx].get();
						drawable._inputAssembly = _drawableConstructor->_drawableInputAssemblies[dc._iaIdx].get();
						drawable._localToWorld = localToWorld;
						drawable._indexCount = dc._indexCount;
						drawable._startIndexLocation = dc._firstIndex;
						assert(dc._firstVertex == 0);
					}
				}
				break;
			}
		}

		// if we need the topological batch, make sure to draw the appropriate cmd stream
		if (pkts[(unsigned)Batch::Topological] && cmdStreamGuid != s_topologicalCmdStream)
			BuildGeometryProcables(pkts, localToWorld, s_topologicalCmdStream);
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	SimpleModelRenderer::SimpleModelRenderer(
		IDrawablesPool& drawablesPool,
		const Assets::ModelRendererConstruction& construction,
		std::shared_ptr<DrawableConstructor> drawableConstructor,
		std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<DeformAccelerator> deformAccelerator,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	: _drawableConstructor(std::move(drawableConstructor))
	{
		_depVal = _drawableConstructor->GetDependencyValidation();

		using namespace RenderCore::Assets;
		std::shared_ptr<IDeformGeoAttachment> geoDeformerInfrastructure;
		if (deformAccelerator && deformAcceleratorPool) {  // need both or neither
			_deformAccelerator = std::move(deformAccelerator);
			geoDeformerInfrastructure = std::dynamic_pointer_cast<IDeformGeoAttachment>(deformAcceleratorPool->GetDeformGeoAttachment(*_deformAccelerator));
		}

		if (!uniformBufferDelegates.empty()) {
			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
			usi.BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);
			unsigned c=2;
			for (const auto&u:uniformBufferDelegates)
				usi.BindImmediateData(c++, u.first, u.second->GetLayout());
			_usi = drawablesPool.CreateProtectedLifetime(std::move(usi));
		} else {
			// todo -- this can just become static
			UniformsStreamInterface usi;
			usi.BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
			usi.BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);
			_usi = drawablesPool.CreateProtectedLifetime(std::move(usi));
		}
		
		_completionCmdList = _drawableConstructor->_completionCommandList;
		if (geoDeformerInfrastructure)
			_completionCmdList = std::max(_completionCmdList, geoDeformerInfrastructure->GetCompletionCommandList());

		// setup skeleton binding
		auto externalSkeletonScaffold = construction.GetSkeletonScaffold();
		if (externalSkeletonScaffold) {
			// merge in the dep val from the skeleton scaffold
			::Assets::DependencyValidationMarker depVals[] { _depVal, externalSkeletonScaffold->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		_skeletonBinding = ModelConstructionSkeletonBinding{construction};
	}

	SimpleModelRenderer::~SimpleModelRenderer() {}

	static std::future<std::shared_ptr<DrawableConstructor>> ToFuture(DrawableConstructor& construction)
	{
		std::promise<std::shared_ptr<DrawableConstructor>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	static std::future<std::shared_ptr<DeformerConstruction>> ToFuture(DeformerConstruction& construction)
	{
		std::promise<std::shared_ptr<DeformerConstruction>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	std::shared_ptr<DeformerConstruction> CreateDefaultDeformConstruction(
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<Assets::ModelRendererConstruction>& rendererConstruction)
	{
		// The default deform accelerators just contains a skinning deform operation
		auto deformerConstruction = std::make_shared<DeformerConstruction>(deformAcceleratorPool->GetDevice(), rendererConstruction);
		if (auto* skinConfigure = Services::GetInstance().FindDeformConfigure("gpu_skin"))
			skinConfigure->Configure(*deformerConstruction);
		if (deformerConstruction->IsEmpty()) return {};
		return deformerConstruction;
	}

	static std::future<std::shared_ptr<DeformAccelerator>> CreateDeformAccelerator(
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		DeformerConstruction& deformConstruction)
	{
		std::promise<std::shared_ptr<DeformAccelerator>> promise;
		auto result = promise.get_future();

		::Assets::WhenAll(ToFuture(deformConstruction)).ThenConstructToPromise(
			std::move(promise),
			[pool=std::weak_ptr<IDeformAcceleratorPool>(deformAcceleratorPool)](auto deformerConstructionActual) {
				auto l = pool.lock();
				if (!l) Throw(std::runtime_error("DeformAcceleratorPool expired"));

				auto result = l->CreateDeformAccelerator();
				if (auto geoAttachment = deformerConstructionActual->GetGeoAttachment())
					l->Attach(*result, std::move(geoAttachment));
				if (auto uniformsAttachment = deformerConstructionActual->GetUniformsAttachment())
					l->Attach(*result, std::move(uniformsAttachment));
				return result;
			});

		return result;	
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		std::shared_ptr<IDrawablesPool> drawablesPool,
		std::shared_ptr<IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<ResourceConstructionContext> constructionContext,
		std::shared_ptr<Assets::ModelRendererConstruction> construction,
		std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool,
		std::shared_ptr<DeformerConstruction> deformerConstruction,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		std::vector<UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };

		::Assets::WhenAll(RenderCore::Assets::ToFuture(*construction)).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[pipelineAcceleratorPool=std::move(pipelineAcceleratorPool), deformAcceleratorPool=std::move(deformAcceleratorPool), drawablesPool=std::move(drawablesPool),
			deformerConstruction=std::move(deformerConstruction), uniformBufferBindings=std::move(uniformBufferBindings), constructionContext=std::move(constructionContext)](auto&& promise, auto completedConstruction) mutable {

				struct Helper
				{
					std::shared_ptr<DrawableConstructor> _drawableConstructor;
					std::future<std::shared_ptr<DrawableConstructor>> _drawableConstructorFuture;
					std::shared_future<void> _deformAcceleratorInitFuture;
				};
				auto helper = std::make_shared<Helper>();
				std::shared_ptr<DeformAccelerator> deformAccelerator;

				TRY {
					// if we were given a deform accelerator pool, but no deform accelerator, go ahead and create the default accelerator
					if (deformAcceleratorPool) {
						if (!deformerConstruction)
							deformerConstruction = CreateDefaultDeformConstruction(deformAcceleratorPool, completedConstruction);
						if (deformerConstruction) {
							auto deformAcceleratorFuture = CreateDeformAccelerator(deformAcceleratorPool, *deformerConstruction);
							if (deformAcceleratorFuture.valid()) {
								YieldToPool(deformAcceleratorFuture);
								deformAccelerator = deformAcceleratorFuture.get();
							}
						}
					}
					
					if (deformAccelerator) {
						auto* geoInfrastructure = deformAcceleratorPool->GetDeformGeoAttachment(*deformAccelerator).get();
						if (geoInfrastructure)
							helper->_deformAcceleratorInitFuture = geoInfrastructure->GetInitializationFuture();
					}

					helper->_drawableConstructor = std::make_shared<DrawableConstructor>(drawablesPool, std::move(pipelineAcceleratorPool), std::move(constructionContext), *completedConstruction, deformAcceleratorPool, deformAccelerator);
					helper->_drawableConstructorFuture = ToFuture(*helper->_drawableConstructor);
				} CATCH(...) {
					promise.set_exception(std::current_exception());
					return;
				} CATCH_END

				::Assets::PollToPromise(
					std::move(promise),
					[helper](auto timeout) {
						auto timeoutTime = std::chrono::steady_clock::now() + timeout;
						auto status = helper->_drawableConstructorFuture.wait_until(timeoutTime);
						if (status == std::future_status::timeout)
							return ::Assets::PollStatus::Continue;

						// Need to ensure IGeoDeformerInfrastructure::GetCompletionCommandList() is ready, if we have a deform accelerator with a geo attachment
						if (helper->_deformAcceleratorInitFuture.valid()) {
							auto status = helper->_deformAcceleratorInitFuture.wait_until(timeoutTime);
							if (status == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
						}

						return ::Assets::PollStatus::Finish;
					},
					[drawablesPool=std::move(drawablesPool), deformAcceleratorPool=std::move(deformAcceleratorPool), 
					deformAccelerator=std::move(deformAccelerator), completedConstruction=std::move(completedConstruction), 
					helper, uniformBufferBindings=std::move(uniformBufferBindings)]() mutable {
						// call "get" on these futures to propagate exceptions
						helper->_drawableConstructorFuture.get();
						if (helper->_deformAcceleratorInitFuture.valid()) helper->_deformAcceleratorInitFuture.get();

						return std::make_shared<SimpleModelRenderer>(
							*drawablesPool,
							*completedConstruction,
							std::move(helper->_drawableConstructor),
							std::move(deformAcceleratorPool), std::move(deformAccelerator),
							uniformBufferBindings);
					});
			});
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		std::shared_ptr<IDrawablesPool> drawablesPool,
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		StringSection<> modelScaffoldName,
		StringSection<> materialScaffoldName,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		auto construction = std::make_shared<Assets::ModelRendererConstruction>();
		construction->AddElement().SetModelAndMaterialScaffolds(modelScaffoldName, materialScaffoldName);
		return ConstructToPromise(
			std::move(promise),
			std::move(drawablesPool), std::move(pipelineAcceleratorPool), nullptr, std::move(construction),
			nullptr, nullptr,
			uniformBufferDelegates);
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		std::shared_ptr<IDrawablesPool> drawablesPool,
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
		std::shared_ptr<IDeformAcceleratorPool> deformAcceleratorPool,
		StringSection<> modelScaffoldName)
	{
		if (::Assets::Services::GetAsyncMan().GetIntermediateCompilers().HasAssociatedCompiler(Assets::CompoundObjectScaffold::CompileProcessType, modelScaffoldName)) {
			// This is a compound object
			::Assets::WhenAll(::Assets::MakeAsset<Assets::CompoundObjectScaffold>(modelScaffoldName)).ThenConstructToPromise(
				std::move(promise),
				[pipelineAcceleratorPool=std::move(pipelineAcceleratorPool), drawablesPool=std::move(drawablesPool), deformAcceleratorPool=std::move(deformAcceleratorPool)](auto&& promise, const Assets::CompoundObjectScaffold& actualCompoundObject) mutable {
					// have to wait on the model renderer construction before we can call DeserializeDeformerConstruction
					auto rendererConstruction = actualCompoundObject.GetModelRendererConstruction();
					auto rendererConstructionFuture = ToFuture(*rendererConstruction);
					YieldToPool(rendererConstructionFuture);
					rendererConstructionFuture.get();

					auto cfg = actualCompoundObject.OpenConfiguration();
					auto deformerConstruction = DeserializeDeformerConstruction(pipelineAcceleratorPool->GetDevice(), rendererConstruction, cfg);

					ConstructToPromise(
						std::move(promise),
						std::move(drawablesPool), std::move(pipelineAcceleratorPool), nullptr, std::move(rendererConstruction),
						std::move(deformAcceleratorPool), std::move(deformerConstruction));
				});
		} else {
			auto construction = std::make_shared<Assets::ModelRendererConstruction>();
			construction->AddElement().SetModelAndMaterialScaffolds(modelScaffoldName, {});
			ConstructToPromise(
				std::move(promise),
				std::move(drawablesPool), std::move(pipelineAcceleratorPool), nullptr, std::move(construction),
				std::move(deformAcceleratorPool), nullptr);
		}
	}

	ICustomDrawDelegate::~ICustomDrawDelegate() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	ModelConstructionSkeletonBinding::ModelConstructionSkeletonBinding(
		const Assets::ModelRendererConstruction& construction)
	{
		auto externalSkeletonScaffold = construction.GetSkeletonScaffold();
		_elementToObject.reserve(construction.GetElementCount());

		for (auto ele:construction) {
			_elementStarts.push_back(_modelJointIndexToMachineOutput.size());

			_elementToObject.push_back(AsFloat3x4(ele.GetElementToObject().value_or(Identity<Float4x4>())));
			
			auto modelScaffold = ele.GetModelScaffold();
			if (modelScaffold) {
				const auto* primarySkeleton = externalSkeletonScaffold ? &externalSkeletonScaffold->GetSkeletonMachine() : nullptr;
				const auto* secondarySkeleton = modelScaffold->EmbeddedSkeleton();

				auto cmdStreamInput = modelScaffold->FindCommandStreamInputInterface();
				_modelJointIndexToMachineOutput.resize(_modelJointIndexToMachineOutput.size()+cmdStreamInput.size(), ~0u);
				auto elementBindingRange = MakeIteratorRange(_modelJointIndexToMachineOutput.end()-cmdStreamInput.size(), _modelJointIndexToMachineOutput.end());
				_unanimatedTransforms.resize(_unanimatedTransforms.size()+cmdStreamInput.size(), Identity<Float4x4>());
				auto unanimatedTransformsRange = MakeIteratorRange(_unanimatedTransforms.end()-cmdStreamInput.size(), _unanimatedTransforms.end());
				
				// support 2 skeletons -- in this way if there are nodes that are not matched to the external skeleton,
				// we can drop back to the embedded skeleton. Since the embedded skeleton always comes from the model
				// source file itself, it should always have the transforms we need
				if (!primarySkeleton) {
					primarySkeleton = secondarySkeleton;
					secondarySkeleton = nullptr;
				}
					
				if (primarySkeleton && secondarySkeleton) {

					///////
					VLA_UNSAFE_FORCE(Float4x4, primaryOutputs, primarySkeleton->GetOutputMatrixCount());
					VLA_UNSAFE_FORCE(Float4x4, secondaryOutputs, secondarySkeleton->GetOutputMatrixCount());
					primarySkeleton->GenerateOutputTransforms(MakeIteratorRange(primaryOutputs, &primaryOutputs[primarySkeleton->GetOutputMatrixCount()]));
					secondarySkeleton->GenerateOutputTransforms(MakeIteratorRange(secondaryOutputs, &secondaryOutputs[secondarySkeleton->GetOutputMatrixCount()]));

					///////
					auto& primaryInterface = primarySkeleton->GetOutputInterface();
					auto& secondaryInterface = secondarySkeleton->GetOutputInterface();
					for (size_t c=0; c<cmdStreamInput.size(); ++c) {
						uint64_t name = cmdStreamInput[c];
						bool gotMatch = false;
						for (size_t c2=0; c2<primaryInterface._outputMatrixNameCount; ++c2)
							if (primaryInterface._outputMatrixNames[c2] == name) {
								elementBindingRange[c] = unsigned(c2);
								unanimatedTransformsRange[c] = primaryOutputs[c2];
								gotMatch = true;
								break;
							}

						if (!gotMatch)
							for (size_t c2=0; c2<secondaryInterface._outputMatrixNameCount; ++c2)
								if (secondaryInterface._outputMatrixNames[c2] == name) {
									unanimatedTransformsRange[c] = secondaryOutputs[c2];
									gotMatch = true;
									break;
								}

						if (!gotMatch)
							Throw(std::runtime_error("Geocall to world unbound in skeleton binding"));
					}
					
				} else if (primarySkeleton) {
					VLA_UNSAFE_FORCE(Float4x4, primaryOutputs, primarySkeleton->GetOutputMatrixCount());
					primarySkeleton->GenerateOutputTransforms(MakeIteratorRange(primaryOutputs, &primaryOutputs[primarySkeleton->GetOutputMatrixCount()]));
					
					///////
					auto& primaryInterface = primarySkeleton->GetOutputInterface();
					for (size_t c=0; c<cmdStreamInput.size(); ++c) {
						uint64_t name = cmdStreamInput[c];
						for (size_t c2=0; c2<primaryInterface._outputMatrixNameCount; ++c2)
							if (primaryInterface._outputMatrixNames[c2] == name) {
								elementBindingRange[c] = unsigned(c2);
								unanimatedTransformsRange[c] = primaryOutputs[c2];
								break;
							}
						if (elementBindingRange[c] == ~0u)
							Throw(std::runtime_error("Geocall to world unbound in skeleton binding"));
					}
				}
			}
		}
	}

	ModelConstructionSkeletonBinding::ModelConstructionSkeletonBinding() = default;

	void RendererSkeletonInterface::FeedInSkeletonMachineResults(
		unsigned instanceIdx,
		IteratorRange<const Float4x4*> skeletonMachineOutput)
	{
		for (const auto&d:_deformers)
			d._skinDeformer->FeedInSkeletonMachineResults(instanceIdx, skeletonMachineOutput, d._deformerBindings);
	}

	RendererSkeletonInterface::RendererSkeletonInterface(
		const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
		IteratorRange<const std::shared_ptr<IGeoDeformer>*> skinDeformers)
	{
		_deformers.reserve(skinDeformers.size());
		for (auto&d:skinDeformers) {
			auto* skinDeformer = (ISkinDeformer*)d->QueryInterface(typeid(ISkinDeformer).hash_code());
			if (!skinDeformer)
				Throw(std::runtime_error("Incorrect deformer type passed to RendererSkeletonInterface. Expecting SkinDeformer"));
			_deformers.push_back(Deformer{ skinDeformer, skinDeformer->CreateBinding(smOutputInterface), d });
		}
	}

	RendererSkeletonInterface::RendererSkeletonInterface(
		const RenderCore::Assets::SkeletonMachine::OutputInterface& smOutputInterface,
		IDeformGeoAttachment& geoDeformerInfrastructure,
		::Assets::DependencyValidation depVal)
	: _depVal(depVal)
	{
		auto srcDeformers = geoDeformerInfrastructure.GetOperations(typeid(ISkinDeformer).hash_code());
		_deformers.reserve(srcDeformers.size());
		for (auto&d:srcDeformers) {
			auto* skinDeformer = (ISkinDeformer*)d->QueryInterface(typeid(ISkinDeformer).hash_code());
			if (!skinDeformer)
				Throw(std::runtime_error("Incorrect deformer type passed to RendererSkeletonInterface. Expecting SkinDeformer"));
			_deformers.push_back(Deformer{ skinDeformer, skinDeformer->CreateBinding(smOutputInterface), d });
		}
	}

	RendererSkeletonInterface::~RendererSkeletonInterface()
	{}

	void RendererSkeletonInterface::ConstructToPromise(
		std::promise<std::shared_ptr<RendererSkeletonInterface>>&& promise,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator,
		const std::shared_ptr<Assets::ModelRendererConstruction>& construction)
	{
		::Assets::WhenAll(ToFuture(*construction)).ThenConstructToPromise(
			std::move(promise),
			[deformAcceleratorPool, deformAccelerator](auto completedConstruction) mutable {
				// We must have either an external skeleton that applies to the whole model, or a single element with a model
				// scaffold with an embedded skeleton
				const Assets::SkeletonMachine* skeleton = nullptr;
				::Assets::DependencyValidation depVal;
				if (completedConstruction->GetSkeletonScaffold()) {
					skeleton = &completedConstruction->GetSkeletonScaffold()->GetSkeletonMachine();
					depVal = completedConstruction->GetSkeletonScaffold()->GetDependencyValidation();
				}
				if (!skeleton) {
					if (completedConstruction->GetElementCount() != 1 || completedConstruction->GetElement(0)->GetModelScaffold())
						Throw(std::runtime_error("Cannot bind skeleton interface to ModelRendererConstruction, because there are multiple separate skeletons within the one construction"));
					skeleton = completedConstruction->GetElement(0)->GetModelScaffold()->EmbeddedSkeleton();
					depVal = completedConstruction->GetElement(0)->GetModelScaffold()->GetDependencyValidation();
				}
				if (!skeleton)
					Throw(std::runtime_error("Cannot bind skeleton interface to ModelRendererConstruction, because no skeleton with provided either as an embedded skeleton, or as an external skeleton"));
				
				IDeformGeoAttachment* geoDeform = nullptr;
				if (deformAccelerator && deformAcceleratorPool)
					geoDeform = deformAcceleratorPool->GetDeformGeoAttachment(*deformAccelerator).get();
				if (!geoDeform)
					Throw(std::runtime_error("Cannot bind skeleton interface to ModelRendererConstruction, because there is no geo deformer attached to the given deform accelerator"));
				
				// might need to take a dep val from the IGeoDeformerInfrastructure here, as well
				return std::make_shared<RendererSkeletonInterface>(skeleton->GetOutputInterface(), *geoDeform, depVal);
			});
	}

	void SkinningUniformBufferDelegate::FeedInSkeletonMachineResults(
		IteratorRange<const Float4x4*> skeletonMachineOutput)
	{
		for (auto& section:_sections) {
			for (unsigned j=0; j<section._sectionMatrixToMachineOutput.size(); ++j) {
				assert(j < section._cbData.size());
				auto machineOutput = section._sectionMatrixToMachineOutput[j];
				if (machineOutput != ~unsigned(0x0)) {
					auto finalMatrix = Combine(section._bindShapeByInverseBind[j], skeletonMachineOutput[machineOutput]);
					section._cbData[j] = AsFloat3x4(finalMatrix);
				} else {
					section._cbData[j] = Identity<Float3x4>();
				}
			}
		}
	}

	void SkinningUniformBufferDelegate::WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst)
	{
		std::memcpy(dst.begin(), _sections[0]._cbData.data(), std::min(dst.size(), _sections[0]._cbData.size() * sizeof(Float3x4)));
	}

	size_t SkinningUniformBufferDelegate::GetSize()
	{
		return _sections[0]._cbData.size() * sizeof(Float3x4);
	}

	SkinningUniformBufferDelegate::SkinningUniformBufferDelegate(
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& scaffoldActual, 
		const std::shared_ptr<RenderCore::Assets::SkeletonScaffold>& skeletonActual)
	{
		RenderCore::Assets::SkeletonBinding binding {
			skeletonActual->GetSkeletonMachine().GetOutputInterface(),
			scaffoldActual->FindCommandStreamInputInterface() };

		std::vector<Float4x4> defaultTransforms(skeletonActual->GetSkeletonMachine().GetOutputInterface()._outputMatrixNameCount);
		skeletonActual->GetSkeletonMachine().GenerateOutputTransforms(MakeIteratorRange(defaultTransforms));

		for (unsigned geoIdx=0; geoIdx<scaffoldActual->GetGeoCount(); ++geoIdx) {
			for (auto cmd:scaffoldActual->GetGeoMachine(geoIdx)) {
				if (cmd.Cmd() != (uint32_t)Assets::GeoCommand::AttachSkinningData) continue;

				auto& skinningData = cmd.As<Assets::SkinningDataDesc>();

				for (const auto&section:skinningData._preskinningSections) {
					Section finalSection;
					finalSection._sectionMatrixToMachineOutput.reserve(section._jointMatrixCount);
					finalSection._bindShapeByInverseBind = std::vector<Float4x4>(section._bindShapeByInverseBindMatrices.begin(), section._bindShapeByInverseBindMatrices.end());
					finalSection._cbData = std::vector<Float3x4>(section._jointMatrixCount);
					finalSection._geoIdx = geoIdx;
					for (unsigned j=0; j<section._jointMatrixCount; ++j) {
						auto machineOutput = binding.ModelJointToMachineOutput(section._jointMatrices[j]);
						finalSection._sectionMatrixToMachineOutput.push_back(machineOutput);
						if (machineOutput != ~unsigned(0x0)) {
							finalSection._cbData[j] = AsFloat3x4(Combine(finalSection._bindShapeByInverseBind[j], defaultTransforms[machineOutput]));
						} else {
							finalSection._cbData[j] = Identity<Float3x4>();
						}
					}
					_sections.emplace_back(std::move(finalSection));
				}
			}
		}
	}

	SkinningUniformBufferDelegate::~SkinningUniformBufferDelegate()
	{
	}


}}

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
#include "DeformOperationFactory.h"
#include "SkinDeformer.h"
#include "Services.h"
#include "ModelRendererConstruction.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelMachine.h"		// for DrawCallDesc
#include "../Assets/AnimationBindings.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/ArithmeticUtils.h"
#include <utility>
#include <map>

#include "PipelineCollection.h"	// temporary - related to creating default deform accelerator

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
			// constants._viewIndices[v++] = lz;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
	RenderCore::Assets::DrawCallDesc ICustomDrawDelegate::GetDrawCallDesc(const Drawable& d) { return ((SimpleModelDrawable_Delegate&)d)._drawCall; }
	void ICustomDrawDelegate::ExecuteStandardDraw(ParsingContext& parsingContext, const ExecuteDrawableContext& drawFnContext, const Drawable& d)
	{
		DrawFn_SimpleModelStatic(parsingContext, drawFnContext, (const SimpleModelDrawable_Delegate&)d);
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		uint32_t viewMask) const
	{
		assert(viewMask != 0);
		if (_deformAcceleratorPool && _deformAccelerator)
			_deformAcceleratorPool->EnableInstance(*_deformAccelerator, deformInstanceIdx);

		SimpleModelDrawable* drawables[dimof(_drawableConstructor->_drawCallCounts)];
		for (unsigned c=0; c<dimof(_drawableConstructor->_drawCallCounts); ++c) {
			if (!_drawableConstructor->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable>(_drawableConstructor->_drawCallCounts[c]) : nullptr;
		}

		auto* drawableFn = (viewMask==1) ? (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic : (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStaticMultiView; 

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();;
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:_drawableConstructor->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto& ele = _elements[elementIdx];
					auto machineOutput = ele._skeletonBinding.ModelJointToMachineOutput(transformMarker);
					assert(machineOutput < ele._baseTransformCount);
					nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&ele._baseTransforms[machineOutput], localToWorld3x4);
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
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
					for (const auto& dc:MakeIteratorRange(_drawableConstructor->_drawCalls.begin()+drawCallsRef._start, _drawableConstructor->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx];
						drawable._pipeline = _drawableConstructor->_pipelineAccelerators[dc._pipelineAcceleratorIdx];
						drawable._descriptorSet = _drawableConstructor->_descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx];
						drawable._drawFn = drawableFn;
						drawable._drawCall = RenderCore::Assets::DrawCallDesc { dc._firstIndex, dc._indexCount, dc._firstVertex };
						drawable._looseUniformsInterface = _usi;
						drawable._materialGuid = materialGuids[materialGuidsIterator++];
						drawable._drawCallIdx = drawCallCounter;
						drawable._localTransform._localToWorld = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld; // todo -- don't have to recalculate this every draw call
						drawable._localTransform._localSpaceView = Float3{0,0,0};
						drawable._localTransform._viewMask = viewMask;
						drawable._deformInstanceIdx = deformInstanceIdx;
						++drawCallCounter;
					}
				}
				break;
			}
		}
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		const std::shared_ptr<ICustomDrawDelegate>& delegate) const
	{
		if (!delegate) {
			BuildDrawables(pkts, localToWorld, deformInstanceIdx, 1u);
			return;
		}

		if (_deformAcceleratorPool && _deformAccelerator)
			_deformAcceleratorPool->EnableInstance(*_deformAccelerator, deformInstanceIdx);

		SimpleModelDrawable* drawables[dimof(_drawableConstructor->_drawCallCounts)];
		for (unsigned c=0; c<dimof(_drawableConstructor->_drawCallCounts); ++c) {
			if (!_drawableConstructor->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable_Delegate>(_drawableConstructor->_drawCallCounts[c]) : nullptr;
		}

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();;
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:_drawableConstructor->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto& ele = _elements[elementIdx];
					auto machineOutput = ele._skeletonBinding.ModelJointToMachineOutput(transformMarker);
					assert(machineOutput < ele._baseTransformCount);
					nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&ele._baseTransforms[machineOutput], localToWorld3x4);
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
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
					for (const auto& dc:MakeIteratorRange(_drawableConstructor->_drawCalls.begin()+drawCallsRef._start, _drawableConstructor->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx];
						drawable._pipeline = _drawableConstructor->_pipelineAccelerators[dc._pipelineAcceleratorIdx];
						drawable._descriptorSet = _drawableConstructor->_descriptorSetAccelerators[dc._descriptorSetAcceleratorIdx];
						drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelDelegate;
						drawable._drawCall = RenderCore::Assets::DrawCallDesc { dc._firstIndex, dc._indexCount, dc._firstVertex };
						drawable._looseUniformsInterface = _usi;
						drawable._materialGuid = materialGuids[materialGuidsIterator++];
						drawable._drawCallIdx = drawCallCounter;
						drawable._localTransform._localToWorld = geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld; // todo -- don't have to recalculate this every draw call
						drawable._localTransform._localSpaceView = Float3{0,0,0};
						drawable._localTransform._viewMask = ~0u;
						drawable._deformInstanceIdx = deformInstanceIdx;
						++drawCallCounter;
					}
				}
				break;
			}
		}
	}

	void SimpleModelRenderer::BuildGeometryProcables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld) const 
	{
		GeometryProcable* drawables[dimof(_drawableConstructor->_drawCallCounts)];
		for (unsigned c=0; c<dimof(_drawableConstructor->_drawCallCounts); ++c) {
			if (!_drawableConstructor->_drawCallCounts[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<GeometryProcable>(_drawableConstructor->_drawCallCounts[c]) : nullptr;
		}

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		auto nodeSpaceToWorld = Identity<Float3x4>();;
		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		for (auto cmd:_drawableConstructor->GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				{
					assert(elementIdx != ~0u);
					auto& ele = _elements[elementIdx];
					auto machineOutput = ele._skeletonBinding.ModelJointToMachineOutput(transformMarker);
					assert(machineOutput < ele._baseTransformCount);
					nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&ele._baseTransforms[machineOutput], localToWorld3x4);
				}
				break;
			case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
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
					for (const auto& dc:MakeIteratorRange(_drawableConstructor->_drawCalls.begin()+drawCallsRef._start, _drawableConstructor->_drawCalls.begin()+drawCallsRef._end)) {
						if (!drawables[dc._batchFilter]) continue;
						auto& drawable = *drawables[dc._batchFilter]++;
						drawable._geo = _drawableConstructor->_drawableGeos[dc._drawableGeoIdx];
						drawable._inputAssembly = _drawableConstructor->_drawableInputAssemblies[dc._iaIdx];
						drawable._localToWorld = AsFloat4x4(geoSpaceToNodeSpace ? Combine_NoDebugOverhead(*(const Float3x4*)geoSpaceToNodeSpace, nodeSpaceToWorld) : nodeSpaceToWorld); // todo -- don't have to recalculate this every draw call
						drawable._indexCount = dc._indexCount;
						drawable._startIndexLocation = dc._firstIndex;
						assert(dc._firstVertex == 0);
					}
				}
				break;
			}
		}

	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	SimpleModelRenderer::SimpleModelRenderer(
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<ModelRendererConstruction>& construction,
		const std::shared_ptr<DrawableConstructor>& drawableConstructor,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	: _drawableConstructor(drawableConstructor)
	, _depVal(drawableConstructor->GetDependencyValidation())
	{
		using namespace RenderCore::Assets;
		std::shared_ptr<IDeformGeoAttachment> geoDeformerInfrastructure;
		if (deformAccelerator && deformAcceleratorPool) {  // need both or neither
			_deformAccelerator = deformAccelerator;
			_deformAcceleratorPool = deformAcceleratorPool;
			geoDeformerInfrastructure = std::dynamic_pointer_cast<IDeformGeoAttachment>(_deformAcceleratorPool->GetDeformGeoAttachment(*_deformAccelerator));
		}

		_usi = std::make_shared<UniformsStreamInterface>();
		_usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
		_usi->BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);

		unsigned c=2;
		for (const auto&u:uniformBufferDelegates)
			_usi->BindImmediateData(c++, u.first, u.second->GetLayout());

		_usi = pipelineAcceleratorPool->CombineWithLike(std::move(_usi));
		
		_completionCmdList = _drawableConstructor->_completionCommandList;
		if (geoDeformerInfrastructure)
			_completionCmdList = std::max(_completionCmdList, geoDeformerInfrastructure->GetCompletionCommandList());

		// setup skeleton binding
		auto externalSkeletonScaffold = construction->GetSkeletonScaffold();
		if (externalSkeletonScaffold) {
			// merge in the dep val from the skeleton scaffold
			::Assets::DependencyValidationMarker depVals[] { _depVal, externalSkeletonScaffold->GetDependencyValidation() };
			_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
		}

		_elements.reserve(construction->GetElementCount());
		for (auto ele:*construction) {
			auto modelScaffold = ele.GetModelScaffold();
			if (modelScaffold) {
				const SkeletonMachine* primarySkeleton = externalSkeletonScaffold ? &externalSkeletonScaffold->GetSkeletonMachine() : nullptr;
				const SkeletonMachine* secondarySkeleton = modelScaffold->EmbeddedSkeleton();
				
				// support 2 skeletons -- in this way if there are nodes that are not matched to the external skeleton,
				// we can drop back to the embedded skeleton. Since the embedded skeleton always comes from the model
				// source file itself, it should always have the transforms we need
				if (!primarySkeleton) {
					primarySkeleton = secondarySkeleton;
					secondarySkeleton = nullptr;
				}
					
				if (primarySkeleton && secondarySkeleton) {
					Element ele;
					ele._skeletonBinding = SkeletonBinding(
						primarySkeleton->GetOutputInterface(),
						secondarySkeleton->GetOutputInterface(),
						modelScaffold->FindCommandStreamInputInterface());
					ele._baseTransformCount = primarySkeleton->GetOutputMatrixCount() + secondarySkeleton->GetOutputMatrixCount();
					ele._baseTransforms = std::make_unique<Float4x4[]>(ele._baseTransformCount);
					primarySkeleton->GenerateOutputTransforms(MakeIteratorRange(ele._baseTransforms.get(), ele._baseTransforms.get() + primarySkeleton->GetOutputMatrixCount()));
					secondarySkeleton->GenerateOutputTransforms(MakeIteratorRange(ele._baseTransforms.get() + primarySkeleton->GetOutputMatrixCount(), ele._baseTransforms.get() + primarySkeleton->GetOutputMatrixCount() + secondarySkeleton->GetOutputMatrixCount()));
					_elements.emplace_back(std::move(ele));
				} else if (primarySkeleton) {
					Element ele;
					ele._skeletonBinding = SkeletonBinding(
						primarySkeleton->GetOutputInterface(),
						modelScaffold->FindCommandStreamInputInterface());
					ele._baseTransformCount = primarySkeleton->GetOutputMatrixCount();
					ele._baseTransforms = std::make_unique<Float4x4[]>(ele._baseTransformCount);
					primarySkeleton->GenerateOutputTransforms(MakeIteratorRange(ele._baseTransforms.get(), ele._baseTransforms.get() + primarySkeleton->GetOutputMatrixCount()));
					_elements.emplace_back(std::move(ele));
				} else {
					_elements.emplace_back(Element{});
				}
			} else {
				_elements.emplace_back(Element{});
			}
		}

		// Check to make sure we've got a skeleton binding for each referenced geo call to world referenced
		{
			std::optional<unsigned> currentElementIdx;
			auto cmdStream = Assets::MakeScaffoldCmdRange(MakeIteratorRange(_drawableConstructor->_translatedCmdStream));
			for (auto cmd:cmdStream) {
				switch (cmd.Cmd()) {
				case (uint32_t)DrawableConstructor::Command::BeginElement:
					currentElementIdx = cmd.As<unsigned>();
					break;
				case (uint32_t)Assets::ModelCommand::SetTransformMarker:
					if (!currentElementIdx.has_value() || currentElementIdx.value() >= _elements.size())
						Throw(std::runtime_error("Bad or missing element index in DrawableConstructor command stream"));
					auto& element = _elements[*currentElementIdx];
					unsigned machineOutput = ~0u;
					if (cmd.As<unsigned>() < element._skeletonBinding.GetModelJointCount())
						machineOutput = element._skeletonBinding.ModelJointToMachineOutput(cmd.As<unsigned>());
					if (machineOutput >= element._baseTransformCount)
						Throw(std::runtime_error("Geocall to world unbound in skeleton binding"));
					break;
				}
			}
		}
	}

	SimpleModelRenderer::~SimpleModelRenderer() {}

	static std::future<std::shared_ptr<ModelRendererConstruction>> ToFuture(ModelRendererConstruction& construction)
	{
		std::promise<std::shared_ptr<ModelRendererConstruction>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

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

	std::future<std::shared_ptr<DeformAccelerator>> CreateDefaultDeformAccelerator(
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<ModelRendererConstruction>& rendererConstruction)
	{
		// The default deform accelerators just contains a skinning deform operation
		auto deformerConstruction = std::make_shared<DeformerConstruction>();
		SkinDeformerSystem::GetInstance()->ConfigureGPUSkinDeformers(
			*deformerConstruction, *rendererConstruction);
		if (deformerConstruction->IsEmpty()) return {};

		std::promise<std::shared_ptr<DeformAccelerator>> promise;
		auto result = promise.get_future();

		::Assets::WhenAll(ToFuture(*deformerConstruction)).ThenConstructToPromise(
			std::move(promise),
			[pool=std::weak_ptr<IDeformAcceleratorPool>(deformAcceleratorPool), rendererConstruction](auto deformerConstructionActual) {
				auto l = pool.lock();
				if (!l) Throw(std::runtime_error("DeformAcceleratorPool expired"));

				auto geometryInfrastructure = CreateDeformGeoAttachment(
					*l->GetDevice(), *rendererConstruction, *deformerConstructionActual);
				
				auto result = l->CreateDeformAccelerator();
				l->Attach(*result, geometryInfrastructure);
				return result;
			});

		return result;	
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<ModelRendererConstruction>& construction,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		std::shared_ptr<DeformAccelerator> deformAcceleratorInit,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		std::vector<UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };

		::Assets::WhenAll(ToFuture(*construction)).ThenConstructToPromise(
			std::move(promise),
			[pipelineAcceleratorPool, deformAcceleratorPool, deformAccelerator=deformAcceleratorInit, uniformBufferBindings=std::move(uniformBufferBindings)](auto&& promise, auto completedConstruction) mutable {

				std::shared_ptr<DrawableConstructor> drawableConstructor;
				std::shared_future<void> deformAcceleratorInitFuture;

				TRY {
					// if we were given a deform accelerator pool, but no deform accelerator, go ahead and create the default accelerator
					if (deformAcceleratorPool && !deformAccelerator) {
						auto deformAcceleratorFuture = CreateDefaultDeformAccelerator(deformAcceleratorPool, completedConstruction);
						if (deformAcceleratorFuture.valid()) {
							deformAcceleratorFuture.wait();
							deformAccelerator = deformAcceleratorFuture.get();
						}
					}
					
					if (deformAccelerator) {
						auto* geoInfrastructure = deformAcceleratorPool->GetDeformGeoAttachment(*deformAccelerator).get();
						if (geoInfrastructure)
							deformAcceleratorInitFuture = geoInfrastructure->GetInitializationFuture();
					}

					auto& bufferUploads = Services::GetInstance().GetBufferUploads();
					drawableConstructor = std::make_shared<DrawableConstructor>(pipelineAcceleratorPool, bufferUploads, *completedConstruction, deformAcceleratorPool, deformAccelerator);
				} CATCH(...) {
					promise.set_exception(std::current_exception());
					return;
				} CATCH_END

				::Assets::PollToPromise(
					std::move(promise),
					[constructorFuture=ToFuture(*drawableConstructor), deformAcceleratorInitFuture=std::move(deformAcceleratorInitFuture)](auto timeout) {
						auto timeoutTime = std::chrono::steady_clock::now() + timeout;
						auto status = constructorFuture.wait_until(timeoutTime);
						if (status == std::future_status::timeout)
							return ::Assets::PollStatus::Continue;

						// Need to ensure IGeoDeformerInfrastructure::GetCompletionCommandList() is ready, if we have a deform accelerator with a geo attachment
						if (deformAcceleratorInitFuture.valid()) {
							auto status = deformAcceleratorInitFuture.wait_until(timeoutTime);
							if (status == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
						}

						return ::Assets::PollStatus::Finish;
					},
					[pipelineAcceleratorPool, deformAcceleratorPool, deformAccelerator, completedConstruction, drawableConstructor, uniformBufferBindings=std::move(uniformBufferBindings)]() {
						return std::make_shared<SimpleModelRenderer>(
							pipelineAcceleratorPool,
							completedConstruction,
							drawableConstructor,
							deformAcceleratorPool, deformAccelerator,
							uniformBufferBindings);
					});
			});
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName,
		StringSection<> materialScaffoldName,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		auto construction = std::make_shared<ModelRendererConstruction>();
		construction->AddElement().SetModelAndMaterialScaffolds(modelScaffoldName, materialScaffoldName);
		return ConstructToPromise(
			std::move(promise),
			pipelineAcceleratorPool, construction,
			nullptr, nullptr,
			uniformBufferDelegates);
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName)
	{
		ConstructToPromise(std::move(promise), pipelineAcceleratorPool, modelScaffoldName, modelScaffoldName);
	}

	ICustomDrawDelegate::~ICustomDrawDelegate() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
		const std::shared_ptr<ModelRendererConstruction>& construction)
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

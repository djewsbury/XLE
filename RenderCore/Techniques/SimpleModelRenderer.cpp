// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelRenderer.h"
#include "DrawableConstructor.h"
#include "Drawables.h"
#include "TechniqueUtils.h"
#include "ParsingContext.h"
#include "CommonBindings.h"
#include "CommonUtils.h"
#include "PipelineAccelerator.h"
#include "DescriptorSetAccelerator.h"
#include "DeformAccelerator.h"
#include "DeformGeometryInfrastructure.h"
#include "SkinDeformer.h"
#include "DeformParametersInfrastructure.h"
#include "CompiledShaderPatchCollection.h"
#include "DrawableDelegates.h"
#include "Services.h"
#include "../Assets/ScaffoldCmdStream.h"		// change to RendererConstruction
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/AnimationBindings.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Types.h"
#include "../ResourceDesc.h"
#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../BufferView.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/ArithmeticUtils.h"
#include <utility>
#include <map>

#include "DeformOperationFactory.h"
#include "SkinDeformer.h"

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

#if 0
	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<Techniques::DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		uint32_t viewMask) const
	{
		assert(viewMask != 0);
		if (_deformAcceleratorPool && _deformAccelerator)
			_deformAcceleratorPool->EnableInstance(*_deformAccelerator, deformInstanceIdx);

		SimpleModelDrawable* drawables[dimof(_drawablesCount)];
		for (unsigned c=0; c<dimof(_drawablesCount); ++c) {
			if (!_drawablesCount[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable>(_drawablesCount[c]) : nullptr;
		}

		auto* drawableFn = (viewMask==1) ? (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic : (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStaticMultiView; 

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		unsigned drawCallCounter = 0;
		const auto& cmdStream = _modelScaffold->CommandStream();
		const auto& immData = _modelScaffold->ImmutableData();
		auto geoCallIterator = _geoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = immData._geos[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&_baseTransforms[machineOutput], localToWorld3x4);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _geos[geoCall._geoId];
				drawable._pipeline = compiledGeoCall._pipelineAccelerator;
				drawable._descriptorSet = compiledGeoCall._descriptorSetAccelerator;
				drawable._drawFn = drawableFn;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
				drawable._localTransform._localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._localTransform._localSpaceView = Float3{0,0,0};
				drawable._localTransform._viewMask = viewMask;
				drawable._deformInstanceIdx = deformInstanceIdx;
				++drawCallCounter;
			}

			geoCallIterator += geoCall._materialCount;
		}

		geoCallIterator = _boundSkinnedControllerGeoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = immData._boundSkinnedControllers[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&_baseTransforms[machineOutput], localToWorld3x4);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

					// now we have at least once piece of geometry
					// that we want to render... We need to bind the material,
					// index buffer and vertex buffer and topology
					// then we just execute the draw command

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _boundSkinnedControllers[geoCall._geoId];
				drawable._pipeline = compiledGeoCall._pipelineAccelerator;
				drawable._descriptorSet = compiledGeoCall._descriptorSetAccelerator;
				drawable._drawFn = drawableFn;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
				drawable._localTransform._localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._localTransform._localSpaceView = Float3{0,0,0};
				drawable._localTransform._viewMask = viewMask;
				drawable._deformInstanceIdx = deformInstanceIdx;

				++drawCallCounter;
			}

			geoCallIterator += geoCall._materialCount;
		}
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif

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

#if 0
	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<Techniques::DrawablesPacket** const> pkts,
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

		SimpleModelDrawable_Delegate* drawables[dimof(_drawablesCount)];
		for (unsigned c=0; c<dimof(_drawablesCount); ++c) {
			if (!_drawablesCount[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<SimpleModelDrawable_Delegate>(_drawablesCount[c]) : nullptr;
		}

		auto localToWorld3x4 = AsFloat3x4(localToWorld);
		unsigned drawCallCounter = 0;
		const auto& cmdStream = _modelScaffold->CommandStream();
		const auto& immData = _modelScaffold->ImmutableData();
		auto geoCallIterator = _geoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = immData._geos[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&_baseTransforms[machineOutput], localToWorld3x4);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _geos[geoCall._geoId];
				drawable._pipeline = compiledGeoCall._pipelineAccelerator;
				drawable._descriptorSet = compiledGeoCall._descriptorSetAccelerator;
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelDelegate;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
				drawable._delegate = delegate;
				drawable._localTransform._localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._localTransform._localSpaceView = Float3{0,0,0};
				drawable._localTransform._viewMask = ~0u;
				drawable._deformInstanceIdx = deformInstanceIdx;

				++drawCallCounter;
			}

			geoCallIterator += geoCall._materialCount;
		}

		geoCallIterator = _boundSkinnedControllerGeoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = immData._boundSkinnedControllers[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&_baseTransforms[machineOutput], localToWorld3x4);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

					// now we have at least once piece of geometry
					// that we want to render... We need to bind the material,
					// index buffer and vertex buffer and topology
					// then we just execute the draw command

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _boundSkinnedControllers[geoCall._geoId];
				drawable._pipeline = compiledGeoCall._pipelineAccelerator;
				drawable._descriptorSet = compiledGeoCall._descriptorSetAccelerator;
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelDelegate;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
				drawable._delegate = delegate;
				drawable._localTransform._localToWorld = Combine_NoDebugOverhead(*(const Float3x4*)&rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._localTransform._localSpaceView = Float3{0,0,0};
				drawable._localTransform._viewMask = ~0u;
				drawable._deformInstanceIdx = deformInstanceIdx;

				++drawCallCounter;
			}

			geoCallIterator += geoCall._materialCount;
		}
	}

	void SimpleModelRenderer::BuildGeometryProcables(
		IteratorRange<Techniques::DrawablesPacket** const> pkts,
		const Float4x4& localToWorld) const
	{
		GeometryProcable* drawables[dimof(_drawablesCount)];
		for (unsigned c=0; c<dimof(_drawablesCount); ++c) {
			if (!_drawablesCount[c]) {
				drawables[c] = nullptr;
				continue;
			}
			drawables[c] = pkts[c] ? pkts[c]->_drawables.Allocate<GeometryProcable>(_drawablesCount[c]) : nullptr;
		}

		const auto& cmdStream = _modelScaffold->CommandStream();
		const auto& immData = _modelScaffold->ImmutableData();
		auto geoCallIterator = _geoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = immData._geos[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _geos[geoCall._geoId];
				drawable._inputAssembly = _drawableIAs[compiledGeoCall._iaIdx];
				drawable._localToWorld = Combine(rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._indexCount = drawCall._indexCount;
				drawable._startIndexLocation = drawCall._firstIndex;
				assert(drawCall._firstVertex == 0);
			}

			geoCallIterator += geoCall._materialCount;
		}

		geoCallIterator = _boundSkinnedControllerGeoCalls.begin();
		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
			const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = immData._boundSkinnedControllers[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			assert(machineOutput < _baseTransformCount);
			auto nodeSpaceToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

			for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
				const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				if (!drawables[compiledGeoCall._batchFilter]) continue;
				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _boundSkinnedControllers[geoCall._geoId];
				drawable._inputAssembly = _drawableIAs[compiledGeoCall._iaIdx];
				drawable._localToWorld = Combine(rawGeo._geoSpaceToNodeSpace, nodeSpaceToWorld);
				drawable._indexCount = drawCall._indexCount;
				drawable._startIndexLocation = drawCall._firstIndex;
				assert(drawCall._firstVertex == 0);
			}

			geoCallIterator += geoCall._materialCount;
		}
	}
#endif

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		uint32_t viewMask) const {}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		unsigned deformInstanceIdx,
		const std::shared_ptr<ICustomDrawDelegate>& delegate) const {}

	void SimpleModelRenderer::BuildGeometryProcables(
		IteratorRange<DrawablesPacket** const> pkts,
		const Float4x4& localToWorld) const {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	SimpleModelRenderer::SimpleModelRenderer(
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<Assets::RendererConstruction>& construction,
		const std::shared_ptr<DrawableConstructor>& drawableConstructor,
		const std::shared_ptr<Assets::SkeletonScaffold>& skeletonScaffold,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	: _drawableConstructor(drawableConstructor)
	, _depVal(construction->GetDependencyValidation())
	{
		using namespace RenderCore::Assets;
		if (deformAccelerator && deformAcceleratorPool) {  // need both or neither
			_deformAccelerator = deformAccelerator;
			_deformAcceleratorPool = deformAcceleratorPool;
			_geoDeformerInfrastructure = std::dynamic_pointer_cast<IGeoDeformerInfrastructure>(_deformAcceleratorPool->GetDeformAttachment(*_deformAccelerator));
		}

		_elements.reserve(construction->GetElementCount());
		if (!skeletonScaffold) {		
			for (auto ele:*construction) {
				auto modelScaffold = ele.GetModelScaffold();
				if (modelScaffold && modelScaffold->EmbeddedSkeleton()) {
					auto* skeleton = modelScaffold->EmbeddedSkeleton();
					Element ele;
					ele._skeletonBinding = SkeletonBinding(
						skeleton->GetOutputInterface(),
						modelScaffold->FindCommandStreamInputInterface());
					ele._baseTransformCount = skeleton->GetOutputMatrixCount();
					ele._baseTransforms = std::make_unique<Float4x4[]>(ele._baseTransformCount);
					skeleton->GenerateOutputTransforms(MakeIteratorRange(ele._baseTransforms.get(), ele._baseTransforms.get() + ele._baseTransformCount));
					_elements.emplace_back(std::move(ele));
				} else {
					_elements.emplace_back(Element{});
				}
			}
		} else {
			assert(0);		// support binding with external skeleton
		}

		_usi = std::make_shared<UniformsStreamInterface>();
		_usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
		_usi->BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);

		unsigned c=2;
		for (const auto&u:uniformBufferDelegates)
			_usi->BindImmediateData(c++, u.first, u.second->GetLayout());

		_usi = pipelineAcceleratorPool->CombineWithLike(std::move(_usi));

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

#if 0
	std::pair<std::shared_ptr<DeformAccelerator>, std::shared_ptr<IGeoDeformerInfrastructure>> CreateAndBindDeformAccelerator(
		IDeformAcceleratorPool& pool,
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		auto geoDeformAttachment = CreateDeformGeometryInfrastructure(
			*pool.GetDevice(), 
			initializer, modelScaffold, modelScaffoldName);
		if (!geoDeformAttachment)
			return {nullptr, nullptr};

		auto deformAccelerator = pool.CreateDeformAccelerator();
		pool.Attach(*deformAccelerator, geoDeformAttachment);
		return {deformAccelerator, geoDeformAttachment};
		return {nullptr, nullptr};
	}
#endif

	static std::future<std::shared_ptr<Assets::RendererConstruction>> ToFuture(Assets::RendererConstruction& construction)
	{
		std::promise<std::shared_ptr<Assets::RendererConstruction>> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	static std::future<DrawableConstructor::FulFilledPromise> ToFuture(DrawableConstructor& construction)
	{
		std::promise<DrawableConstructor::FulFilledPromise> promise;
		auto result = promise.get_future();
		construction.FulfillWhenNotPending(std::move(promise));
		return result;
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<Assets::RendererConstruction>& construction,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		std::vector<UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };

		::Assets::WhenAll(ToFuture(*construction)).ThenConstructToPromise(
			std::move(promise),
			[pipelineAcceleratorPool, deformAcceleratorPool](auto&& promise, auto completedConstruction) {
				auto& bufferUploads = Services::GetInstance().GetBufferUploads();
				auto drawableConstructor = std::make_shared<DrawableConstructor>(pipelineAcceleratorPool, bufferUploads, *completedConstruction);

				/*DeformerConstruction deformerConfig;
				ConfigureGPUSkinDeformers(
					deformerConfig,
					*construction, 
					Services::GetInstance().GetDeformerPipelineCollection());
				if (deformerConfig) {

				} else*/ {
					
					::Assets::WhenAll(ToFuture(*drawableConstructor)).ThenConstructToPromise(
						std::move(promise),
						[pipelineAcceleratorPool, deformAcceleratorPool, completedConstruction](auto drawableConstructionPromise) {
							return std::make_shared<SimpleModelRenderer>(
								pipelineAcceleratorPool,
								completedConstruction,
								drawableConstructionPromise._constructor,
								nullptr);		// skeleton scaffold
						});
				}
			});

#if 0
		thousandeyes::futures::then(
			ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
			ToFuture(*construction),
			[pipelineAcceleratorPool, deformAcceleratorPool](auto constructionFuture) {
				auto construction = constructionFuture.get();
				auto& bufferUploads = Services::GetInstance().GetBufferUploads();
				auto drawableConstructor = std::make_shared<DrawableConstructor>(pipelineAcceleratorPool, bufferUploads, *construction);

				/*DeformerConstruction deformerConfig;
				ConfigureGPUSkinDeformers(
					deformerConfig,
					*construction, 
					Services::GetInstance().GetDeformerPipelineCollection());
				if (deformerConfig) {

				} else*/ {
					
					thousandeyes::futures::then(
						ConsoleRig::GlobalServices::GetInstance().GetContinuationExecutor(),
						ToFuture(*drawableConstructor),
						[pipelineAcceleratorPool, deformAcceleratorPool](auto drawableConstructionPromise) {
							auto newRenderer = std::make_shared<SimpleModelRenderer>(
								pipelineAcceleratorPool, 
								construction, 
								*drawableConstructionPromise._constructor,
								nullptr);		// skeleton scaffold

							promise.set_value(newRenderer);
						};
				}
			});
#endif
		
#if 0
		::Assets::WhenAll(modelScaffoldFuture, materialScaffoldFuture).ThenConstructToPromise(
			std::move(promise),
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, uniformBufferBindings, modelScaffoldNameString, materialScaffoldNameString, deformAcceleratorPool](
				auto scaffoldActual, auto materialActual) {
				if (deformAcceleratorPool && !deformOperationString.empty()) {
					auto deformAccelerator = CreateAndBindDeformAccelerator(
						*deformAcceleratorPool, 
						MakeStringSection(deformOperationString), 
						scaffoldActual, modelScaffoldNameString);

					if (!deformAccelerator.first)
						deformAccelerator.first = deformAcceleratorPool->CreateDeformAccelerator();
					auto deformParameters = CreateDeformParametersAttachment(scaffoldActual, modelScaffoldNameString);
					if (deformParameters)
						deformAcceleratorPool->Attach(*deformAccelerator.first, deformParameters);

					return std::make_shared<SimpleModelRenderer>(
						pipelineAcceleratorPool, scaffoldActual, materialActual,
						deformAcceleratorPool, deformAccelerator.first, deformAccelerator.second, deformParameters,
						MakeIteratorRange(uniformBufferBindings),
						modelScaffoldNameString, materialScaffoldNameString);
				} else {
					return std::make_shared<SimpleModelRenderer>(
						pipelineAcceleratorPool, scaffoldActual, materialActual,
						nullptr, nullptr, nullptr, nullptr,
						MakeIteratorRange(uniformBufferBindings),
						modelScaffoldNameString, materialScaffoldNameString);
				}
			});
#endif
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<Techniques::IDeformAcceleratorPool>& deformAcceleratorPool,
		StringSection<> modelScaffoldName,
		StringSection<> materialScaffoldName,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		assert(0);
		#if 0
			auto scaffoldFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::ModelScaffold>(modelScaffoldName);
			auto materialFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::MaterialScaffold>(materialScaffoldName, modelScaffoldName);
			ConstructToPromise(std::move(promise), pipelineAcceleratorPool, deformAcceleratorPool, scaffoldFuture, materialFuture, deformOperations, uniformBufferDelegates, modelScaffoldName.AsString(), materialScaffoldName.AsString());
		#endif
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName)
	{
		ConstructToPromise(std::move(promise), pipelineAcceleratorPool, nullptr, modelScaffoldName, modelScaffoldName);
	}

	void SimpleModelRenderer::ConstructToPromise(	// todo -- remove
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates,
		const std::string& modelScaffoldNameString,
		const std::string& materialScaffoldNameString)
	{}

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
		IGeoDeformerInfrastructure& geoDeformerInfrastructure)
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
		std::promise<std::shared_ptr<RendererSkeletonInterface>>&& skeletonInterfacePromise,
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& rendererPromise,
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<Assets::RendererConstruction>& rendererConstruction,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates)
	{
		assert(0);
#if 0
		// note -- skeletonInterfacePromise will never be fulfilled if renderPromise becomes invalid
		auto modelScaffoldNameString = modelScaffoldFuture->Initializer();
		auto materialScaffoldNameString = materialScaffoldFuture->Initializer();

		std::vector<SimpleModelRenderer::UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };
		::Assets::WhenAll(modelScaffoldFuture, materialScaffoldFuture).ThenConstructToPromise(
			std::move(rendererPromise),
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, deformAcceleratorPool, uniformBufferBindings, 
				skeletonInterfacePromise=std::move(skeletonInterfacePromise), modelScaffoldNameString, materialScaffoldNameString](
				std::shared_ptr<RenderCore::Assets::ModelScaffold> scaffoldActual, 
				std::shared_ptr<RenderCore::Assets::MaterialScaffold> materialActual) mutable {

				if (!deformOperationString.empty()) deformOperationString += ";skin";
				else deformOperationString = "skin";

				auto deformAccelerator = CreateAndBindDeformAccelerator(
					*deformAcceleratorPool,
					MakeStringSection(deformOperationString),
					scaffoldActual, modelScaffoldNameString);

				if (!deformAccelerator.first)
					deformAccelerator.first = deformAcceleratorPool->CreateDeformAccelerator();
				auto deformParameters = CreateDeformParametersAttachment(scaffoldActual, modelScaffoldNameString);
				if (deformParameters)
					deformAcceleratorPool->Attach(*deformAccelerator.first, deformParameters);

				if (deformAccelerator.first) {
					auto skeletonInterface = std::make_shared<RendererSkeletonInterface>(
						scaffoldActual->EmbeddedSkeleton().GetOutputInterface(),
						*deformAccelerator.second);
					
					skeletonInterfacePromise.set_value(skeletonInterface);

					return std::make_shared<SimpleModelRenderer>(
						pipelineAcceleratorPool, scaffoldActual, materialActual, 
						deformAcceleratorPool, deformAccelerator.first, deformAccelerator.second, deformParameters,
						uniformBufferBindings,
						modelScaffoldNameString, materialScaffoldNameString);
				} else {
					skeletonInterfacePromise.set_value(nullptr);
					return std::make_shared<SimpleModelRenderer>(
						pipelineAcceleratorPool, scaffoldActual, materialActual, 
						nullptr, nullptr, nullptr, nullptr,
						uniformBufferBindings,
						modelScaffoldNameString, materialScaffoldNameString);
				}
			});
#endif
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
		auto& cmdStream = scaffoldActual->CommandStream();
		RenderCore::Assets::SkeletonBinding binding {
			skeletonActual->GetSkeletonMachine().GetOutputInterface(),
			cmdStream.GetInputInterface() };

		std::vector<Float4x4> defaultTransforms(skeletonActual->GetSkeletonMachine().GetOutputInterface()._outputMatrixNameCount);
		skeletonActual->GetSkeletonMachine().GenerateOutputTransforms(MakeIteratorRange(defaultTransforms));

		auto& immutableData = scaffoldActual->ImmutableData();
		for (const auto&skinnedGeo:MakeIteratorRange(immutableData._boundSkinnedControllers, &immutableData._boundSkinnedControllers[immutableData._boundSkinnedControllerCount])) {
			for (const auto&section:skinnedGeo._preskinningSections) {
				Section finalSection;
				finalSection._sectionMatrixToMachineOutput.reserve(section._jointMatrixCount);
				finalSection._bindShapeByInverseBind = std::vector<Float4x4>(section._bindShapeByInverseBindMatrices.begin(), section._bindShapeByInverseBindMatrices.end());
				finalSection._cbData = std::vector<Float3x4>(section._jointMatrixCount);
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

	SkinningUniformBufferDelegate::~SkinningUniformBufferDelegate()
	{
	}


}}

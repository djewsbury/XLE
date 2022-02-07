// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelRenderer.h"
#include "Drawables.h"
#include "TechniqueUtils.h"
#include "ParsingContext.h"
#include "CommonBindings.h"
#include "CommonUtils.h"
#include "PipelineAccelerator.h"
#include "DescriptorSetAccelerator.h"
#include "DeformAccelerator.h"
#include "DeformGeometryInfrastructure.h"
#include "DeformParametersInfrastructure.h"
#include "CompiledShaderPatchCollection.h"
#include "DrawableDelegates.h"
#include "Services.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Assets/MaterialScaffold.h"
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	const ::Assets::DependencyValidation& SimpleModelRenderer::GetDependencyValidation() const { return _depVal; }

	namespace Internal
	{
		static std::vector<RenderCore::InputElementDesc> MakeIA(IteratorRange<const RenderCore::Assets::VertexElement*> elements, IteratorRange<const uint64_t*> suppressedElements, unsigned streamIdx)
		{
			std::vector<RenderCore::InputElementDesc> result;
			for (const auto&e:elements) {
				auto hash = Hash64(e._semanticName) + e._semanticIndex;
				auto hit = std::lower_bound(suppressedElements.begin(), suppressedElements.end(), hash);
				if (hit != suppressedElements.end() && *hit == hash)
					continue;
				result.push_back(
					InputElementDesc {
						e._semanticName, e._semanticIndex,
						e._nativeFormat, streamIdx,
						e._alignedByteOffset
					});
			}
			return result;
		}

		static std::vector<RenderCore::InputElementDesc> MakeIA(IteratorRange<const InputElementDesc*> elements, unsigned streamIdx)
		{
			std::vector<RenderCore::InputElementDesc> result;
			for (const auto&e:elements) {
				result.push_back(
					InputElementDesc {
						e._semanticName, e._semanticIndex,
						e._nativeFormat, streamIdx,
						e._alignedByteOffset
					});
			}
			return result;
		}

		static std::vector<RenderCore::InputElementDesc> BuildFinalIA(
			const RenderCore::Assets::RawGeometry& geo,
			const DeformerToRendererBinding::GeoBinding* deformStream = nullptr,
			unsigned deformInputSlot = ~0u)
		{
			auto suppressed = deformStream ? MakeIteratorRange(deformStream->_suppressedElements) : IteratorRange<const uint64_t*>{};
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), suppressed, 0);
			if (deformStream) {
				auto t = MakeIA(MakeIteratorRange(deformStream->_generatedElements), deformInputSlot);
				result.insert(result.end(), t.begin(), t.end());
			}
			return result;
		}

		static std::vector<RenderCore::InputElementDesc> BuildFinalIA(
			const RenderCore::Assets::BoundSkinnedGeometry& geo,
			const DeformerToRendererBinding::GeoBinding* deformStream = nullptr,
			unsigned deformInputSlot = ~0u)
		{
			auto suppressed = deformStream ? MakeIteratorRange(deformStream->_suppressedElements) : IteratorRange<const uint64_t*>{};
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), suppressed, 0);
			if (deformStream) {
				auto t1 = MakeIA(MakeIteratorRange(deformStream->_generatedElements), deformInputSlot);
				result.insert(result.end(), t1.begin(), t1.end());
			}
			return result;
		}
	}

	class SimpleModelRenderer::DrawableGeoBuilder
	{
	public:
		std::vector<std::shared_ptr<DrawableGeo>> _geos;
		std::vector<std::shared_ptr<DrawableGeo>> _boundSkinnedControllers;

		using InputLayout = std::vector<InputElementDesc>;
		std::vector<InputLayout> _geosLayout;
		std::vector<InputLayout> _boundSkinnedControllersLayout;

		DrawableGeoBuilder(
			IDevice& device,
			std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
			const std::string& modelScaffoldName,
			std::shared_ptr<DeformAccelerator> deformAccelerator = nullptr,
			std::shared_ptr<IGeoDeformerInfrastructure> geoDeformerInfrastructure = nullptr)
		{
			// Construct the DrawableGeo objects needed by the renderer

			DeformerToRendererBinding deformerBinding;
			if (deformAccelerator && geoDeformerInfrastructure) {
				deformerBinding = geoDeformerInfrastructure->GetDeformerToRendererBinding();
			}

			std::vector<std::pair<unsigned, unsigned>> staticVBLoadRequests;
			unsigned staticVBIterator = 0;
			std::vector<std::pair<unsigned, unsigned>> staticIBLoadRequests;
			unsigned staticIBIterator = 0;
			auto simpleGeoCount = modelScaffold->ImmutableData()._geoCount;

			_geos.reserve(modelScaffold->ImmutableData()._geoCount);
			_geosLayout.reserve(modelScaffold->ImmutableData()._geoCount);
			for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
				const auto& rg = modelScaffold->ImmutableData()._geos[geo];

				// Build the main non-deformed vertex stream
				auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
				drawableGeo->_vertexStreams[0]._vbOffset = staticVBIterator;
				staticVBLoadRequests.push_back({rg._vb._offset, rg._vb._size});
				staticVBIterator += rg._vb._size;
				drawableGeo->_vertexStreamCount = 1;

				// Attach those vertex streams that come from the deform operation
				if (geo < deformerBinding._geoBindings.size() && !deformerBinding._geoBindings[geo]._generatedElements.empty()) {
					drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
					drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformerBinding._geoBindings[geo]._postDeformBufferOffset;
					drawableGeo->_deformAccelerator = deformAccelerator;
					_geosLayout.push_back(Internal::BuildFinalIA(rg, &deformerBinding._geoBindings[geo], drawableGeo->_vertexStreamCount));
					++drawableGeo->_vertexStreamCount;
				} else {
					_geosLayout.push_back(Internal::BuildFinalIA(rg));
				}

				// hack -- we might need this for material deform, as well
				drawableGeo->_deformAccelerator = deformAccelerator;
				
				drawableGeo->_ibOffset = staticIBIterator;
				staticIBLoadRequests.push_back({rg._ib._offset, rg._ib._size});
				staticIBIterator += rg._ib._size;
				drawableGeo->_ibFormat = rg._ib._format;
				_geos.push_back(std::move(drawableGeo));
			}

			_boundSkinnedControllers.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
			_boundSkinnedControllersLayout.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
			for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
				const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];

				// Build the main non-deformed vertex stream
				auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
				drawableGeo->_vertexStreams[0]._vbOffset = staticVBIterator;
				staticVBLoadRequests.push_back({rg._vb._offset, rg._vb._size});
				staticVBIterator += rg._vb._size;
				drawableGeo->_vertexStreamCount = 1;

				// Attach those vertex streams that come from the deform operation
				if ((geo+simpleGeoCount) < deformerBinding._geoBindings.size() && !deformerBinding._geoBindings[geo+simpleGeoCount]._generatedElements.empty()) {
					drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
					drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformerBinding._geoBindings[geo+simpleGeoCount]._postDeformBufferOffset;
					drawableGeo->_deformAccelerator = deformAccelerator;
					_boundSkinnedControllersLayout.push_back(Internal::BuildFinalIA(rg, &deformerBinding._geoBindings[geo+simpleGeoCount], drawableGeo->_vertexStreamCount));
					++drawableGeo->_vertexStreamCount;
				} else {
					drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount++]._vbOffset = staticVBIterator;
					staticVBLoadRequests.push_back({rg._animatedVertexElements._offset, rg._animatedVertexElements._size});
					staticVBIterator += rg._animatedVertexElements._size;
					_boundSkinnedControllersLayout.push_back(Internal::BuildFinalIA(rg));
				}

				drawableGeo->_ibOffset = staticIBIterator;
				staticIBLoadRequests.push_back({rg._ib._offset, rg._ib._size});
				staticIBIterator += rg._ib._size;
				drawableGeo->_ibFormat = rg._ib._format;
				_boundSkinnedControllers.push_back(std::move(drawableGeo));
			}

			if (staticVBIterator) {
				auto vb = LoadStaticResourcePartialAsync(
					device, {staticVBLoadRequests.begin(), staticVBLoadRequests.end()}, staticVBIterator, modelScaffold, BindFlag::VertexBuffer, 
					(StringMeld<64>() << "[vb]" << modelScaffoldName).AsStringSection());
				for (auto&geo:_geos)
					for (auto& stream:MakeIteratorRange(geo->_vertexStreams, &geo->_vertexStreams[geo->_vertexStreamCount]))
						if (stream._type == DrawableGeo::StreamType::Resource && !stream._resource)
							stream._resource = vb.first;
				for (auto&geo:_boundSkinnedControllers)
					for (auto& stream:MakeIteratorRange(geo->_vertexStreams, &geo->_vertexStreams[geo->_vertexStreamCount]))
						if (stream._type == DrawableGeo::StreamType::Resource && !stream._resource)
							stream._resource = vb.first;
			}

			if (staticIBIterator) {
				auto ib = LoadStaticResourcePartialAsync(
					device, {staticIBLoadRequests.begin(), staticIBLoadRequests.end()}, staticIBIterator, modelScaffold, BindFlag::IndexBuffer,
					(StringMeld<64>() << "[ib]" << modelScaffoldName).AsStringSection());
				for (auto&geo:_geos)
					if (geo->_ibStreamType == DrawableGeo::StreamType::Resource && !geo->_ib)
						geo->_ib = ib.first;
				for (auto&geo:_boundSkinnedControllers)
					if (geo->_ibStreamType == DrawableGeo::StreamType::Resource && !geo->_ib)
						geo->_ib = ib.first;
			}
		}

		DrawableGeoBuilder() = default;
		DrawableGeoBuilder(DrawableGeoBuilder&&) = default;
		DrawableGeoBuilder& operator=(DrawableGeoBuilder&&) = default;
	};

	class SimpleModelRenderer::GeoCallBuilder
	{
	public:
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		const RenderCore::Assets::MaterialScaffold* _materialScaffold;
		std::string _materialScaffoldName;
		std::set<::Assets::DependencyValidation> _depVals;

		struct WorkingMaterial
		{
			std::shared_ptr<RenderCore::Assets::ShaderPatchCollection> _patchCollection;
			std::shared_ptr<Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
			std::vector<std::string> _descriptorSetResources;
		};
		std::vector<std::pair<uint64_t, WorkingMaterial>> _drawableMaterials;

		std::vector<std::shared_ptr<DrawableInputAssembly>> _ias;

		template<typename RawGeoType>
			GeoCall MakeGeoCall(
				Techniques::IPipelineAcceleratorPool& acceleratorPool,
				uint64_t materialGuid,
				const RawGeoType& rawGeo,
				const DrawableGeoBuilder::InputLayout& inputElements,
				Techniques::IDeformAcceleratorPool* deformAcceleratorPool,
				const IDeformParametersAttachment* parametersDeformInfrastructure)
		{
			GeoCall resultGeoCall;
			const auto* mat = _materialScaffold->GetMaterial(materialGuid);
			if (!mat) {
				static RenderCore::Assets::MaterialScaffoldMaterial defaultMaterial;
				mat = &defaultMaterial;
			}

			auto i = LowerBound(_drawableMaterials, materialGuid);
			if (i != _drawableMaterials.end() && i->first == materialGuid) {
				resultGeoCall._descriptorSetAccelerator = i->second._descriptorSetAccelerator;
			} else {
				auto patchCollection = _materialScaffold->GetShaderPatchCollection(mat->_patchCollection);
				if (patchCollection)
					_depVals.insert(patchCollection->GetDependencyValidation());

				IteratorRange<const std::pair<uint64_t, SamplerDesc>*> samplerBindings {};
				if (parametersDeformInfrastructure && deformAcceleratorPool) {
					auto paramBinding = parametersDeformInfrastructure->GetOutputParameterBindings();
					resultGeoCall._descriptorSetAccelerator = acceleratorPool.CreateDescriptorSetAccelerator(
						patchCollection,
						mat->_matParams, mat->_constants, mat->_bindings,
						samplerBindings,
						{(const AnimatedParameterBinding*)paramBinding.begin(), (const AnimatedParameterBinding*)paramBinding.end()},
						deformAcceleratorPool->GetDynamicPageResource());
				} else {
					resultGeoCall._descriptorSetAccelerator = acceleratorPool.CreateDescriptorSetAccelerator(
						patchCollection,
						mat->_matParams, mat->_constants, mat->_bindings,
						samplerBindings);
				}

				// Collect up the list of resources in the descriptor set -- we'll use this to filter the "RES_HAS_" selectors
				std::vector<std::string> resourceNames;
				for (const auto&r:mat->_bindings)
					resourceNames.push_back(r.Name().AsString());

				i = _drawableMaterials.insert(i, std::make_pair(materialGuid, WorkingMaterial{patchCollection, resultGeoCall._descriptorSetAccelerator, std::move(resourceNames)}));
			}

			// Figure out the topology from from the rawGeo. We can't mix topology across the one geo call; all draw calls
			// for the same geo object must share the same toplogy mode
			assert(!rawGeo._drawCalls.empty());
			auto topology = rawGeo._drawCalls[0]._topology;
			#if defined(_DEBUG)
				for (auto r=rawGeo._drawCalls.begin()+1; r!=rawGeo._drawCalls.end(); ++r)
					assert(topology == r->_topology);
			#endif

			auto matSelectors = mat->_matParams;
			// Also append the "RES_HAS_" constants for each resource that is both in the descriptor set and that we have a binding for
			for (const auto&r:i->second._descriptorSetResources)
				if (mat->_bindings.HasParameter(MakeStringSection(r)))
					matSelectors.SetParameter(MakeStringSection(std::string{"RES_HAS_"} + r).Cast<utf8>(), 1);

			resultGeoCall._pipelineAccelerator =
				_pipelineAcceleratorPool->CreatePipelineAccelerator(
					i->second._patchCollection,
					matSelectors,
					MakeIteratorRange(inputElements),
					topology,
					mat->_stateSet);

			resultGeoCall._batchFilter = (unsigned)Batch::Opaque;
			if (mat->_stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::ForwardBlend && mat->_stateSet._forwardBlendOp != BlendOp::NoBlending) {
                if (mat->_stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType) {
                    switch (mat->_stateSet._blendType) {
                    case RenderCore::Assets::RenderStateSet::BlendType::Basic: 
					case RenderCore::Assets::RenderStateSet::BlendType::Ordered:
						resultGeoCall._batchFilter = (unsigned)Batch::Blending;
						break;
                    case RenderCore::Assets::RenderStateSet::BlendType::DeferredDecal:
					default:
						resultGeoCall._batchFilter = (unsigned)Batch::Opaque; 
						break;
                    }
                } else {
                    resultGeoCall._batchFilter = (unsigned)Batch::Blending;
                }
            }

			auto ia = std::make_shared<DrawableInputAssembly>(MakeIteratorRange(inputElements), topology);
			auto w = std::find_if(_ias.begin(), _ias.end(), [hash=ia->GetHash()](const auto& q) { return q->GetHash() == hash; });
			if (w == _ias.end()) {
				resultGeoCall._iaIdx = (unsigned)_ias.size();
				_ias.push_back(ia);
			} else {
				resultGeoCall._iaIdx = (unsigned)std::distance(_ias.begin(), w);
			}

			return resultGeoCall;
		}
	};

	SimpleModelRenderer::SimpleModelRenderer(
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& materialScaffold,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator,
		const std::shared_ptr<IGeoDeformerInfrastructure>& geoDeformerInfrastructure,
		const std::shared_ptr<IDeformParametersAttachment>& parametersDeformInfrastructure,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates,
		const std::string& modelScaffoldName,
		const std::string& materialScaffoldName)
	: _modelScaffold(modelScaffold)
	, _materialScaffold(materialScaffold)
	, _modelScaffoldName(modelScaffoldName)
	, _materialScaffoldName(materialScaffoldName)
	{
		using namespace RenderCore::Assets;
		if (deformAccelerator && deformAcceleratorPool) {  // need both or neither
			_deformAccelerator = deformAccelerator;
			_deformAcceleratorPool = deformAcceleratorPool;
			_geoDeformerInfrastructure = geoDeformerInfrastructure;
		} else {
			assert(!geoDeformerInfrastructure);
		}

        const auto& skeleton = modelScaffold->EmbeddedSkeleton();
        _skeletonBinding = SkeletonBinding(
            skeleton.GetOutputInterface(),
            modelScaffold->CommandStream().GetInputInterface());

        _baseTransformCount = skeleton.GetOutputMatrixCount();
        _baseTransforms = std::make_unique<Float4x4[]>(_baseTransformCount);
        skeleton.GenerateOutputTransforms(MakeIteratorRange(_baseTransforms.get(), _baseTransforms.get() + _baseTransformCount), &skeleton.GetDefaultParameters());

		_geoCalls.reserve(modelScaffold->ImmutableData()._geoCount);
		_boundSkinnedControllerGeoCalls.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);

		DrawableGeoBuilder geos{*pipelineAcceleratorPool->GetDevice(), modelScaffold, _modelScaffoldName, deformAccelerator, geoDeformerInfrastructure};
		_geos = std::move(geos._geos);
		_boundSkinnedControllers = std::move(geos._boundSkinnedControllers);

		// Setup the materials
		GeoCallBuilder geoCallBuilder { pipelineAcceleratorPool, _materialScaffold.get(), _materialScaffoldName };

		const auto& cmdStream = _modelScaffold->CommandStream();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._geos[geoCall._geoId];
			// todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
			assert(geoCall._materialCount);
            for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_geoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, geos._geosLayout[geoCall._geoId], _deformAcceleratorPool.get(), parametersDeformInfrastructure.get()));
			}
		}

		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._boundSkinnedControllers[geoCall._geoId];
            // todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
			assert(geoCall._materialCount);
			for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_boundSkinnedControllerGeoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, geos._boundSkinnedControllersLayout[geoCall._geoId], _deformAcceleratorPool.get(), parametersDeformInfrastructure.get()));
			}
		}

		_drawableIAs = std::move(geoCallBuilder._ias);

		_usi = std::make_shared<UniformsStreamInterface>();
		_usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
		_usi->BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);

		unsigned c=2;
		for (const auto&u:uniformBufferDelegates)
			_usi->BindImmediateData(c++, u.first, u.second->GetLayout());

		_usi = pipelineAcceleratorPool->CombineWithLike(std::move(_usi));

		// Check to make sure we've got a skeleton binding for each referenced geo call to world referenced
		// Also count up the number of drawables that are going to be requires
		static_assert(dimof(_drawablesCount) == (size_t)Batch::Max);
		XlZeroMemory(_drawablesCount);
		auto geoCallIterator = _geoCalls.begin();
		for (unsigned g=0; g<cmdStream.GetGeoCallCount(); g++) {
			unsigned machineOutput = ~0u;
			auto& geoCall = cmdStream.GetGeoCall(g);
			if (geoCall._transformMarker < _skeletonBinding.GetModelJointCount())
				machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			if (machineOutput >= _baseTransformCount)
				Throw(std::runtime_error("Geocall to world unbound in skeleton binding"));

			auto& rawGeo = modelScaffold->ImmutableData()._geos[geoCall._geoId];
			for (const auto&d:rawGeo._drawCalls) {
				const auto& compiledGeoCall = geoCallIterator[d._subMaterialIndex];
				++_drawablesCount[compiledGeoCall._batchFilter];
			}
			geoCallIterator += geoCall._materialCount;
		}

		geoCallIterator = _boundSkinnedControllerGeoCalls.begin();
		for (unsigned g=0; g<cmdStream.GetSkinCallCount(); g++) {
			unsigned machineOutput = ~0u;
			auto& geoCall = cmdStream.GetSkinCall(g);
			if (geoCall._transformMarker < _skeletonBinding.GetModelJointCount())
				machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
			if (machineOutput >= _baseTransformCount)
				Throw(std::runtime_error("Geocall to world unbound in skeleton binding"));

			auto& rawGeo = modelScaffold->ImmutableData()._boundSkinnedControllers[geoCall._geoId];
			for (const auto&d:rawGeo._drawCalls) {
				const auto& compiledGeoCall = geoCallIterator[d._subMaterialIndex];
				++_drawablesCount[compiledGeoCall._batchFilter];
			}
			geoCallIterator += geoCall._materialCount;
		}

		_depVal = ::Assets::GetDepValSys().Make();
		_depVal.RegisterDependency(_modelScaffold->GetDependencyValidation());
		_depVal.RegisterDependency(_materialScaffold->GetDependencyValidation());
		for (const auto&depVal:geoCallBuilder._depVals)
			_depVal.RegisterDependency(depVal);
	}

	SimpleModelRenderer::~SimpleModelRenderer() {}

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
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates,
		const std::string& modelScaffoldNameStringInit,
		const std::string& materialScaffoldNameStringInit)
	{
		auto modelScaffoldNameString = !modelScaffoldNameStringInit.empty() ? modelScaffoldNameStringInit : modelScaffoldFuture->Initializer();
		auto materialScaffoldNameString = !materialScaffoldNameStringInit.empty() ? materialScaffoldNameStringInit : materialScaffoldFuture->Initializer();
		std::vector<UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };
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
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<Techniques::IDeformAcceleratorPool>& deformAcceleratorPool,
		StringSection<> modelScaffoldName,
		StringSection<> materialScaffoldName,
		StringSection<> deformOperations,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		auto scaffoldFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::ModelScaffold>(modelScaffoldName);
		auto materialFuture = ::Assets::MakeAssetPtr<RenderCore::Assets::MaterialScaffold>(materialScaffoldName, modelScaffoldName);
		ConstructToPromise(std::move(promise), pipelineAcceleratorPool, deformAcceleratorPool, scaffoldFuture, materialFuture, deformOperations, uniformBufferDelegates, modelScaffoldName.AsString(), materialScaffoldName.AsString());
	}

	void SimpleModelRenderer::ConstructToPromise(
		std::promise<std::shared_ptr<SimpleModelRenderer>>&& promise,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName)
	{
		ConstructToPromise(std::move(promise), pipelineAcceleratorPool, nullptr, modelScaffoldName, modelScaffoldName);
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
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
		const ::Assets::PtrToMarkerPtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates)
	{
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
			skeletonActual->GetTransformationMachine().GetOutputInterface(),
			cmdStream.GetInputInterface() };

		std::vector<Float4x4> defaultTransforms(skeletonActual->GetTransformationMachine().GetOutputInterface()._outputMatrixNameCount);
		skeletonActual->GetTransformationMachine().GenerateOutputTransforms(
			MakeIteratorRange(defaultTransforms),
			&skeletonActual->GetTransformationMachine().GetDefaultParameters());

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

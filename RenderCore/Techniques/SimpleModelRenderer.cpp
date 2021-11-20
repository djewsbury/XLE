// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleModelRenderer.h"
#include "SimpleModelDeform.h"
#include "Drawables.h"
#include "TechniqueUtils.h"
#include "ParsingContext.h"
#include "CommonBindings.h"
#include "CommonUtils.h"
#include "PipelineAccelerator.h"
#include "DescriptorSetAccelerator.h"
#include "CompiledShaderPatchCollection.h"
#include "DrawableDelegates.h"
#include "Services.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/ModelScaffoldInternal.h"
#include "../Assets/ModelImmutableData.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../GeoProc/MeshDatabase.h"		// for Copy()
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

namespace RenderCore { namespace Techniques 
{
	static IResourcePtr LoadVertexBuffer(
		IDevice& device,
        const RenderCore::Assets::ModelScaffold& scaffold,
        const RenderCore::Assets::VertexData& vb);
	static IResourcePtr LoadIndexBuffer(
		IDevice& device,
        const RenderCore::Assets::ModelScaffold& scaffold,
        const RenderCore::Assets::IndexData& ib);

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
		SimpleModelDrawable* drawables[dimof(_drawablesCount)];
		for (unsigned c=0; c<dimof(_drawablesCount); ++c) {
			if (!_drawablesCount[c]) {
				drawables[c] = nullptr;
				continue;
			}
			if (!pkts[c])
				Throw(::Exceptions::BasicLabel("Drawables packet not provided for batch filter %i", c));
			drawables[c] = pkts[c]->_drawables.Allocate<SimpleModelDrawable>(_drawablesCount[c]);
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

		SimpleModelDrawable_Delegate* drawables[dimof(_drawablesCount)];
		for (unsigned c=0; c<dimof(_drawablesCount); ++c) {
			if (!_drawablesCount[c]) {
				drawables[c] = nullptr;
				continue;
			}
			if (!pkts[c])
				Throw(::Exceptions::BasicLabel("Drawables packet not provided for batch filter %i", c));
			drawables[c] = pkts[c]->_drawables.Allocate<SimpleModelDrawable_Delegate>(_drawablesCount[c]);
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
			if (!pkts[c])
				Throw(::Exceptions::BasicLabel("Drawables packet not provided for batch filter %i", c));
			drawables[c] = pkts[c]->_drawables.Allocate<GeometryProcable>(_drawablesCount[c]);
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

	static Techniques::DrawableGeo::VertexStream MakeVertexStream(
		IDevice& device,
		const RenderCore::Assets::ModelScaffold& modelScaffold,
		const RenderCore::Assets::VertexData& vertices)
	{
		return Techniques::DrawableGeo::VertexStream { LoadVertexBuffer(device, modelScaffold, vertices ) };
	}

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
			const RendererGeoDeformInterface* deformStream)
		{
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), deformStream ? MakeIteratorRange(deformStream->_suppressedElements) : IteratorRange<const uint64_t*>{}, 0);
			if (deformStream) {
				auto t = MakeIA(MakeIteratorRange(deformStream->_generatedElements), 1);
				result.insert(result.end(), t.begin(), t.end());
			}
			return result;
		}

		static std::vector<RenderCore::InputElementDesc> BuildFinalIA(
			const RenderCore::Assets::BoundSkinnedGeometry& geo,
			const RendererGeoDeformInterface* deformStream)
		{
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), deformStream ? MakeIteratorRange(deformStream->_suppressedElements) : IteratorRange<const uint64_t*>{}, 0);
			auto t0 = MakeIA(MakeIteratorRange(geo._animatedVertexElements._ia._elements), deformStream ? MakeIteratorRange(deformStream->_suppressedElements) : IteratorRange<const uint64_t*>{}, 1);
			result.insert(result.end(), t0.begin(), t0.end());
			if (deformStream) {
				auto t1 = MakeIA(MakeIteratorRange(deformStream->_generatedElements), 2);
				result.insert(result.end(), t1.begin(), t1.end());
			}
			return result;
		}
	}

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
				const RendererGeoDeformInterface* deformStream)
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
				resultGeoCall._descriptorSetAccelerator = acceleratorPool.CreateDescriptorSetAccelerator(
					patchCollection,
					mat->_matParams, mat->_constants, mat->_bindings,
					samplerBindings);

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

			auto inputElements = Internal::BuildFinalIA(rawGeo, deformStream);

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

			resultGeoCall._batchFilter = (unsigned)BatchFilter::General;
			if (mat->_stateSet._forwardBlendOp == BlendOp::NoBlending) {
                resultGeoCall._batchFilter = (unsigned)BatchFilter::General;
            } else {
                if (mat->_stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType) {
                    switch (RenderCore::Assets::RenderStateSet::BlendType(mat->_stateSet._blendType)) {
                    case RenderCore::Assets::RenderStateSet::BlendType::DeferredDecal: resultGeoCall._batchFilter = (unsigned)BatchFilter::General; break;
                    case RenderCore::Assets::RenderStateSet::BlendType::Ordered: resultGeoCall._batchFilter = (unsigned)BatchFilter::SortedBlending; break;
                    default: resultGeoCall._batchFilter = (unsigned)BatchFilter::PostOpaque; break;
                    }
                } else {
                    resultGeoCall._batchFilter = (unsigned)BatchFilter::General;
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
		const std::shared_ptr<DeformAccelerator>& deformAccelerator,
		IteratorRange<const RendererGeoDeformInterface*> deformInterface,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates,
		const std::string& modelScaffoldName,
		const std::string& materialScaffoldName)
	: _modelScaffold(modelScaffold)
	, _materialScaffold(materialScaffold)
	, _modelScaffoldName(modelScaffoldName)
	, _materialScaffoldName(materialScaffoldName)
	{
		using namespace RenderCore::Assets;

        const auto& skeleton = modelScaffold->EmbeddedSkeleton();
        _skeletonBinding = SkeletonBinding(
            skeleton.GetOutputInterface(),
            modelScaffold->CommandStream().GetInputInterface());

        _baseTransformCount = skeleton.GetOutputMatrixCount();
        _baseTransforms = std::make_unique<Float4x4[]>(_baseTransformCount);
        skeleton.GenerateOutputTransforms(MakeIteratorRange(_baseTransforms.get(), _baseTransforms.get() + _baseTransformCount), &skeleton.GetDefaultParameters());

		_geos.reserve(modelScaffold->ImmutableData()._geoCount);
		_geoCalls.reserve(modelScaffold->ImmutableData()._geoCount);
		auto simpleGeoCount = modelScaffold->ImmutableData()._geoCount;
		
		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._geos[geo];

			// Build the main non-deformed vertex stream
			auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
			drawableGeo->_vertexStreams[0] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._vb);
			drawableGeo->_vertexStreamCount = 1;

			// Attach those vertex streams that come from the deform operation
			if (deformInterface.size() > geo && !deformInterface[geo]._generatedElements.empty()) {
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformInterface[geo]._vbOffset;
				++drawableGeo->_vertexStreamCount;
				drawableGeo->_deformAccelerator = deformAccelerator;
			}
			
			drawableGeo->_ib = LoadIndexBuffer(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._ib);
			drawableGeo->_ibFormat = rg._ib._format;
			_geos.push_back(std::move(drawableGeo));
		}

		_boundSkinnedControllers.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		_boundSkinnedControllerGeoCalls.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		
		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];

			// Build the main non-deformed vertex stream
			auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
			drawableGeo->_vertexStreams[0] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._vb);
			drawableGeo->_vertexStreams[1] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._animatedVertexElements);
			drawableGeo->_vertexStreamCount = 2;

			// Attach those vertex streams that come from the deform operation
			if ((geo+simpleGeoCount) < deformInterface.size() && !deformInterface[geo+simpleGeoCount]._generatedElements.empty()) {
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformInterface[geo+simpleGeoCount]._vbOffset;
				++drawableGeo->_vertexStreamCount;
				drawableGeo->_deformAccelerator = deformAccelerator;
			}

			drawableGeo->_ib = LoadIndexBuffer(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._ib);
			drawableGeo->_ibFormat = rg._ib._format;
			_boundSkinnedControllers.push_back(std::move(drawableGeo));
		}

		// Setup the materials
		GeoCallBuilder geoCallBuilder { pipelineAcceleratorPool, _materialScaffold.get(), _materialScaffoldName };

		const auto& cmdStream = _modelScaffold->CommandStream();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._geos[geoCall._geoId];
			auto* deform = (geoCall._geoId < deformInterface.size()) ? &deformInterface[geoCall._geoId] : nullptr;
			// todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
			assert(geoCall._materialCount);
            for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_geoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, deform));
			}
		}

		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._boundSkinnedControllers[geoCall._geoId];
			auto* deform = ((geoCall._geoId+simpleGeoCount) < deformInterface.size()) ? &deformInterface[geoCall._geoId+simpleGeoCount] : nullptr;
            // todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
			assert(geoCall._materialCount);
			for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_boundSkinnedControllerGeoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, deform));
			}
		}

		_drawableIAs = std::move(geoCallBuilder._ias);

		_usi = std::make_shared<UniformsStreamInterface>();
		// HACK --> use the fallback LocalTransform -->
		_usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
		// <------------------------------------
		_usi->BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);

		unsigned c=2;
		for (const auto&u:uniformBufferDelegates) {
			_usi->BindImmediateData(c++, u.first, u.second->GetLayout());
		}

		_usi = pipelineAcceleratorPool->CombineWithLike(std::move(_usi));

		// Check to make sure we've got a skeleton binding for each referenced geo call to world referenced
		// Also count up the number of drawables that are going to be requires
		static_assert(dimof(_drawablesCount) == (size_t)BatchFilter::Max);
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

	struct DeformConstructionFuture
	{
	public:
		std::string _pendingConstruction;
		std::vector<DeformOperationInstantiation> _deformOps;
	};

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
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, uniformBufferBindings, modelScaffoldNameString, materialScaffoldNameString](
				std::shared_ptr<RenderCore::Assets::ModelScaffold> scaffoldActual, std::shared_ptr<RenderCore::Assets::MaterialScaffold> materialActual) {
				
				/*auto deformOps = DeformOperationFactory::GetInstance().CreateDeformOperations(
					MakeStringSection(deformOperationString),
					scaffoldActual);*/

				return std::make_shared<SimpleModelRenderer>(
					pipelineAcceleratorPool, scaffoldActual, materialActual, 
					nullptr, IteratorRange<const RendererGeoDeformInterface*>{},
					MakeIteratorRange(uniformBufferBindings),
					modelScaffoldNameString, materialScaffoldNameString);
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

	static IResourcePtr LoadVertexBuffer(
		IDevice& device,
        const RenderCore::Assets::ModelScaffold& scaffold,
        const RenderCore::Assets::VertexData& vb)
    {
        auto buffer = std::make_unique<uint8[]>(vb._size);
		{
            auto inputFile = scaffold.OpenLargeBlocks();
            inputFile->Seek(vb._offset, OSServices::FileSeekAnchor::Current);
            inputFile->Read(buffer.get(), vb._size, 1);
        }
		return CreateStaticVertexBuffer(
			device,
			MakeIteratorRange(buffer.get(), PtrAdd(buffer.get(), vb._size)));
    }

    static IResourcePtr LoadIndexBuffer(
		IDevice& device,
        const RenderCore::Assets::ModelScaffold& scaffold,
        const RenderCore::Assets::IndexData& ib)
    {
        auto buffer = std::make_unique<uint8[]>(ib._size);
        {
            auto inputFile = scaffold.OpenLargeBlocks();
            inputFile->Seek(ib._offset, OSServices::FileSeekAnchor::Current);
            inputFile->Read(buffer.get(), ib._size, 1);
        }
		return CreateStaticIndexBuffer(
			device,
			MakeIteratorRange(buffer.get(), PtrAdd(buffer.get(), ib._size)));
    }

	ICustomDrawDelegate::~ICustomDrawDelegate() {}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RendererSkeletonInterface::FeedInSkeletonMachineResults(
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

	void RendererSkeletonInterface::WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst)
	{
		std::memcpy(dst.begin(), _sections[0]._cbData.data(), std::min(dst.size(), _sections[0]._cbData.size() * sizeof(Float3x4)));
	}

	size_t RendererSkeletonInterface::GetSize()
	{
		return _sections[0]._cbData.size() * sizeof(Float3x4);
	}

	RendererSkeletonInterface::RendererSkeletonInterface(
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

	RendererSkeletonInterface::~RendererSkeletonInterface()
	{
	}

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
		std::promise<std::shared_ptr<RendererSkeletonInterface>> intermediateSkeletonInterface;
		auto intermediateSkeletonInterfaceFuture = intermediateSkeletonInterface.get_future();
		::Assets::WhenAll(modelScaffoldFuture, skeletonScaffoldFuture).ThenConstructToPromise(std::move(intermediateSkeletonInterface));

		std::vector<SimpleModelRenderer::UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };
		::Assets::WhenAll(modelScaffoldFuture, materialScaffoldFuture, std::move(intermediateSkeletonInterfaceFuture)).ThenConstructToPromise(
			std::move(rendererPromise),
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, deformAcceleratorPool, uniformBufferBindings, 
				skeletonInterfacePromise=std::move(skeletonInterfacePromise)](
				std::shared_ptr<RenderCore::Assets::ModelScaffold> scaffoldActual, 
				std::shared_ptr<RenderCore::Assets::MaterialScaffold> materialActual,
				std::shared_ptr<RendererSkeletonInterface> skeletonInterface) mutable {
				
				/*auto deformOps = DeformOperationFactory::GetInstance().CreateDeformOperations(
					MakeStringSection(deformOperationString),
					scaffoldActual);

				auto skinDeform = DeformOperationFactory::GetInstance().CreateDeformOperations("skin", scaffoldActual);
				deformOps.insert(deformOps.end(), skinDeform.begin(), skinDeform.end());

				// Add a uniform buffer binding delegate for the joint transforms
				auto ubb = uniformBufferBindings;
				ubb.push_back({Hash64("BoneTransforms"), skeletonInterface});*/

				skeletonInterfacePromise.set_value(skeletonInterface);

				return std::make_shared<SimpleModelRenderer>(
					pipelineAcceleratorPool, scaffoldActual, materialActual, 
					nullptr, IteratorRange<const RendererGeoDeformInterface*>{}, 
					uniformBufferBindings);
			});
	}


}}

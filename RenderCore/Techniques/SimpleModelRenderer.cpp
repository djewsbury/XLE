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
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetFutureContinuation.h"
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleModelDrawable : public Techniques::Drawable
	{
	public:
		RenderCore::Assets::DrawCallDesc _drawCall;
		Float4x4 _objectToWorld;
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
			auto bindingField = drawFnContext.GetBoundLooseImmediateDatas();
			auto localTransform = Techniques::MakeLocalTransform(
				drawable._objectToWorld, 
				ExtractTranslation(parserContext.GetProjectionDesc()._cameraToWorld));
			DrawCallProperties drawCallProps{drawable._materialGuid, drawable._drawCallIdx};
			drawFnContext.ApplyLooseUniforms(ImmediateDataStream{localTransform, drawCallProps});
		}

        drawFnContext.DrawIndexed(
			drawable._drawCall._indexCount, drawable._drawCall._firstIndex, drawable._drawCall._firstVertex);
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<Techniques::DrawablesPacket** const> pkts,
		const Float4x4& localToWorld) const
	{
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

		unsigned drawCallCounter = 0;
		const auto& cmdStream = _modelScaffold->CommandStream();
        const auto& immData = _modelScaffold->ImmutableData();
		auto geoCallIterator = _geoCalls.begin();
        for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
            auto& rawGeo = immData._geos[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
            assert(machineOutput < _baseTransformCount);
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

            for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
                const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _geos[geoCall._geoId];
				drawable._pipeline = compiledGeoCall._pipelineAccelerator;
				drawable._descriptorSet = compiledGeoCall._descriptorSetAccelerator;
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
                drawable._objectToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);

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
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

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
				drawable._drawFn = (Techniques::ExecuteDrawableFn*)&DrawFn_SimpleModelStatic;
				drawable._drawCall = drawCall;
				drawable._looseUniformsInterface = _usi;
				drawable._materialGuid = geoCall._materialGuids[drawCall._subMaterialIndex];
				drawable._drawCallIdx = drawCallCounter;
				drawable._objectToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);

				++drawCallCounter;
            }

			geoCallIterator += geoCall._materialCount;
        }
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class SimpleModelDrawable_Delegate : public SimpleModelDrawable
	{
	public:
		std::shared_ptr<IPreDrawDelegate> _delegate;
	};

	static void DrawFn_SimpleModelDelegate(
		Techniques::ParsingContext& parserContext,
		const Techniques::ExecuteDrawableContext& drawFnContext,
        const SimpleModelDrawable_Delegate& drawable)
	{
		bool delegateResult = drawable._delegate->OnDraw(drawFnContext, parserContext, drawable, drawable._materialGuid, drawable._drawCallIdx);
		if (delegateResult)
			DrawFn_SimpleModelStatic(parserContext, drawFnContext, drawable);
	}

	void SimpleModelRenderer::BuildDrawables(
		IteratorRange<Techniques::DrawablesPacket** const> pkts,
		const Float4x4& localToWorld,
		const std::shared_ptr<IPreDrawDelegate>& delegate) const
	{
		if (!delegate) {
			BuildDrawables(pkts, localToWorld);
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

		unsigned drawCallCounter = 0;
		const auto& cmdStream = _modelScaffold->CommandStream();
        const auto& immData = _modelScaffold->ImmutableData();
		auto geoCallIterator = _geoCalls.begin();
        for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
            auto& rawGeo = immData._geos[geoCall._geoId];

			auto machineOutput = _skeletonBinding.ModelJointToMachineOutput(geoCall._transformMarker);
            assert(machineOutput < _baseTransformCount);
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

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
                drawable._objectToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);

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
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

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
				drawable._objectToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);

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
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

            for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
                const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _geos[geoCall._geoId];
				drawable._inputAssembly = _drawableIAs[compiledGeoCall._iaIdx];
				drawable._localToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);
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
			auto geoCallToWorld = Combine(_baseTransforms[machineOutput], localToWorld);

            for (unsigned d = 0; d < unsigned(rawGeo._drawCalls.size()); ++d) {
                const auto& drawCall = rawGeo._drawCalls[d];
				const auto& compiledGeoCall = geoCallIterator[drawCall._subMaterialIndex];

				auto& drawable = *drawables[compiledGeoCall._batchFilter]++;
				drawable._geo = _boundSkinnedControllers[geoCall._geoId];
				drawable._inputAssembly = _drawableIAs[compiledGeoCall._iaIdx];
				drawable._localToWorld = Combine(rawGeo._geoSpaceToNodeSpace, geoCallToWorld);
				drawable._indexCount = drawCall._indexCount;
				drawable._startIndexLocation = drawCall._firstIndex;
				assert(drawCall._firstVertex == 0);
            }

			geoCallIterator += geoCall._materialCount;
        }
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct SimpleModelRenderer::DeformOp
	{
		std::shared_ptr<IDeformOperation> _deformOp;

		struct Element { Format _format = Format(0); unsigned _offset = 0; unsigned _stride = 0; unsigned _vbIdx = ~0u; };
		std::vector<Element> _inputElements;
		std::vector<Element> _outputElements;
	};

	static unsigned VB_StaticData = 0;
	static unsigned VB_TemporaryDeform = 1;
	static unsigned VB_PostDeform = 2;

	void SimpleModelRenderer::GenerateDeformBuffer(IThreadContext& context)
	{
		if (!_dynVB) return;

		auto& metalContext = *RenderCore::Metal::DeviceContext::Get(context);

		auto* res = (Metal::Resource*)_dynVB->QueryInterface(typeid(Metal::Resource).hash_code());
		assert(res);

		Metal::ResourceMap map(metalContext, *res, Metal::ResourceMap::Mode::WriteDiscardPrevious);
		auto dst = map.GetData();

		auto staticDataPartRange = MakeIteratorRange(_deformStaticDataInput);
		auto temporaryDeformRange = MakeIteratorRange(_deformTemporaryBuffer);

		for (const auto&d:_deformOps) {

			IDeformOperation::VertexElementRange inputElementRanges[16];
			assert(d._inputElements.size() <= dimof(inputElementRanges));
			for (unsigned c=0; c<d._inputElements.size(); ++c) {
				if (d._inputElements[c]._vbIdx == VB_StaticData) {
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(staticDataPartRange.begin()), d._inputElements[c]._offset), AsPointer(staticDataPartRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				} else {
					assert(d._inputElements[c]._vbIdx == VB_TemporaryDeform);
					inputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._inputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._inputElements[c]._stride, d._inputElements[c]._format);
				}
			}

			auto outputPartRange = dst;
			assert(outputPartRange.begin() < outputPartRange.end() && PtrDiff(outputPartRange.end(), dst.begin()) <= ptrdiff_t(dst.size()));

			IDeformOperation::VertexElementRange outputElementRanges[16];
			assert(d._outputElements.size() <= dimof(outputElementRanges));
			for (unsigned c=0; c<d._outputElements.size(); ++c) {
				if (d._outputElements[c]._vbIdx == VB_PostDeform) {
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(outputPartRange.begin(), d._outputElements[c]._offset), outputPartRange.end()),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				} else {
					assert(d._outputElements[c]._vbIdx == VB_TemporaryDeform);
					outputElementRanges[c] = MakeVertexIteratorRangeConst(
						MakeIteratorRange(PtrAdd(AsPointer(temporaryDeformRange.begin()), d._outputElements[c]._offset), AsPointer(temporaryDeformRange.end())),
						d._outputElements[c]._stride, d._outputElements[c]._format);
				}
			}

			// Execute the actual deform op
			d._deformOp->Execute(
				MakeIteratorRange(inputElementRanges, &inputElementRanges[d._inputElements.size()]),
				MakeIteratorRange(outputElementRanges, &outputElementRanges[d._outputElements.size()]));
		}
	}

	unsigned SimpleModelRenderer::DeformOperationCount() const { return (unsigned)_deformOps.size(); }
	IDeformOperation& SimpleModelRenderer::DeformOperation(unsigned idx) { return *_deformOps[idx]._deformOp; } 

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
		struct SourceDataTransform
		{
			unsigned	_geoId;
			uint64_t	_sourceStream;
			Format		_targetFormat;
			unsigned	_targetOffset;
			unsigned	_targetStride;
			unsigned	_vertexCount;
		};

		struct NascentDeformStream
		{
			std::vector<SimpleModelRenderer::DeformOp> _deformOps;

			std::vector<uint64_t> _suppressedElements;
			std::vector<InputElementDesc> _generatedElements;
			std::vector<SourceDataTransform> _staticDataLoadRequests;

			unsigned _vbOffsets[3] = {0,0,0};
			unsigned _vbSizes[3] = {0,0,0};
		};

		static NascentDeformStream BuildNascentDeformStream(
			IteratorRange<const DeformOperationInstantiation*> globalDeformAttachments,
			unsigned geoId,
			unsigned vertexCount,
			unsigned& preDeformStaticDataVBIterator,
			unsigned& deformTemporaryVBIterator,
			unsigned& postDeformVBIterator)
		{
			// Calculate which elements are suppressed by the deform operations
			// We can only support a single deform operation per geo
			std::vector<const DeformOperationInstantiation*> deformAttachments;
			for (const auto& def:globalDeformAttachments)
				if (def._geoId == geoId) {
					deformAttachments.push_back(&def);
				}

			if (!deformAttachments.size()) return {};

			std::vector<uint64_t> workingSuppressedElements;
			std::vector<std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>> workingGeneratedElements;
			std::vector<std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>> workingTemporarySpaceElements;
			std::vector<std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>> workingSourceDataElements;
			unsigned nextStreamId = 0;

			struct WorkingDeformOp
			{
				std::shared_ptr<IDeformOperation> _deformOp;
				std::vector<unsigned> _inputStreamIds;
				std::vector<unsigned> _outputStreamIds;
			};
			std::vector<WorkingDeformOp> workingDeformOps;

			for (auto d=deformAttachments.begin(); d!=deformAttachments.end(); ++d) {
				const auto&def = **d;
				WorkingDeformOp workingDeformOp;

				for (auto&e:def._upstreamSourceElements) {
					// find a matching source element generated from another deform op
					auto i = std::find_if(
						workingGeneratedElements.begin(), workingGeneratedElements.end(),
						[e](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& wge) {
							return wge.first._semantic == e._semantic && wge.first._semanticIndex == e._semanticIndex;
						});
					if (i != workingGeneratedElements.end()) {
						assert(i->first._format == e._format);
						workingDeformOp._inputStreamIds.push_back(i->second);
						workingTemporarySpaceElements.push_back(*i);
						workingGeneratedElements.erase(i);
					} else {
						// If it's not generated by some deform op, we look for it in the static data
						auto streamId = nextStreamId++;
						workingDeformOp._inputStreamIds.push_back(streamId);
						workingSourceDataElements.push_back(std::make_pair(e, streamId));
					}
				}

				// Before we add our own static data, we should remove any working elements that have been
				// suppressed
				auto i = std::remove_if(
					workingGeneratedElements.begin(), workingGeneratedElements.end(),
					[&def](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& wge) {
						auto hash = Hash64(wge.first._semantic) + wge.first._semanticIndex;
						return (std::find(def._suppressElements.begin(), def._suppressElements.end(), hash) != def._suppressElements.end())
							|| std::find(def._generatedElements.begin(), def._generatedElements.end(), wge.first) != def._generatedElements.end();
					});
				workingGeneratedElements.erase(i, workingGeneratedElements.end());		// these get removed and don't go into temporary space. They are just never used

				for (auto e=def._generatedElements.begin(); e!=def._generatedElements.end(); ++e) {
					auto streamId = nextStreamId++;
					workingGeneratedElements.push_back(std::make_pair(*e, streamId));
					workingDeformOp._outputStreamIds.push_back(streamId);
				}

				workingSuppressedElements.insert(
					workingSuppressedElements.end(),
					def._suppressElements.begin(), def._suppressElements.end());

				workingDeformOp._deformOp = def._operation;
				workingDeformOps.push_back(workingDeformOp);
			}

			NascentDeformStream result;
			result._suppressedElements = workingSuppressedElements;
			for (const auto&wge:workingGeneratedElements)
				result._suppressedElements.push_back(Hash64(wge.first._semantic) + wge.first._semanticIndex);		// (also suppress all elements generated by the final deform step, because they are effectively overriden)
			std::sort(result._suppressedElements.begin(), result._suppressedElements.end());
			result._suppressedElements.erase(
				std::unique(result._suppressedElements.begin(), result._suppressedElements.end()),
				result._suppressedElements.end());

			// Figure out how to arrange all of the input and output vertices in the 
			// deform VBs.
			// We've got 3 to use
			//		1. an input static data buffer; which contains values read directly from the source data (perhaps processed for format)
			//		2. a deform temporary buffer; which contains data written out from deform operations, and read in by others
			//		3. a final output buffer; which contains resulting vertex data that is fed into the render operation
			
			std::vector<SourceDataTransform> sourceDataTransforms;
			std::vector<SimpleModelRenderer::DeformOp::Element> sourceDataStreams;
			{
				sourceDataTransforms.reserve(workingSourceDataElements.size());
				unsigned targetStride = 0, offsetIterator = 0;
				for (unsigned c=0; c<workingSourceDataElements.size(); ++c)
					targetStride += BitsPerPixel(workingSourceDataElements[c].first._format) / 8;
				for (unsigned c=0; c<workingSourceDataElements.size(); ++c) {
					const auto& workingE = workingSourceDataElements[c];
					sourceDataTransforms.push_back({
						geoId, Hash64(workingE.first._semantic) + workingE.first._semanticIndex,
						workingE.first._format, preDeformStaticDataVBIterator + offsetIterator, targetStride, vertexCount});
					sourceDataStreams.push_back({workingE.first._format, preDeformStaticDataVBIterator + offsetIterator, targetStride, VB_StaticData});
					offsetIterator += BitsPerPixel(workingE.first._format) / 8;
				}
				result._vbOffsets[VB_StaticData] = preDeformStaticDataVBIterator;
				result._vbSizes[VB_StaticData] = targetStride * vertexCount;
				preDeformStaticDataVBIterator += targetStride * vertexCount;
			}

			std::vector<SimpleModelRenderer::DeformOp::Element> temporaryDataStreams;
			{
				temporaryDataStreams.reserve(workingTemporarySpaceElements.size());
				unsigned targetStride = 0, offsetIterator = 0;
				for (unsigned c=0; c<workingTemporarySpaceElements.size(); ++c)
					targetStride += BitsPerPixel(workingTemporarySpaceElements[c].first._format) / 8;
				for (unsigned c=0; c<workingTemporarySpaceElements.size(); ++c) {
					const auto& workingE = workingTemporarySpaceElements[c];
					temporaryDataStreams.push_back({workingE.first._format, deformTemporaryVBIterator + offsetIterator, targetStride, VB_TemporaryDeform});
					offsetIterator += BitsPerPixel(workingE.first._format) / 8;
				}
				result._vbOffsets[VB_TemporaryDeform] = deformTemporaryVBIterator;
				result._vbSizes[VB_TemporaryDeform] = targetStride * vertexCount;
				deformTemporaryVBIterator += targetStride * vertexCount;
			}

			std::vector<SimpleModelRenderer::DeformOp::Element> generatedDataStreams;
			{
				generatedDataStreams.reserve(workingGeneratedElements.size());
				unsigned targetStride = 0, offsetIterator = 0;
				for (unsigned c=0; c<workingGeneratedElements.size(); ++c)
					targetStride += BitsPerPixel(workingGeneratedElements[c].first._format) / 8;
				for (unsigned c=0; c<workingGeneratedElements.size(); ++c) {
					const auto& workingE = workingGeneratedElements[c];
					generatedDataStreams.push_back({workingE.first._format, postDeformVBIterator + offsetIterator, targetStride, VB_PostDeform});
					offsetIterator += BitsPerPixel(workingE.first._format) / 8;
				}
				result._vbOffsets[VB_PostDeform] = postDeformVBIterator;
				result._vbSizes[VB_PostDeform] = targetStride * vertexCount;
				postDeformVBIterator += targetStride * vertexCount;
			}

			// Collate the WorkingDeformOp into the SimpleModelRenderer::DeformOp format
			result._deformOps.reserve(workingDeformOps.size());
			for (const auto&wdo:workingDeformOps) {
				SimpleModelRenderer::DeformOp finalDeformOp;
				// input streams
				for (auto s:wdo._inputStreamIds) {
					auto i = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), [s](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& p) { return p.second == s; });
					if (i != workingGeneratedElements.end()) {
						finalDeformOp._inputElements.push_back(generatedDataStreams[i-workingGeneratedElements.begin()]);
					} else {
						i = std::find_if(workingTemporarySpaceElements.begin(), workingTemporarySpaceElements.end(), [s](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& p) { return p.second == s; });
						if (i != workingTemporarySpaceElements.end()) {
							finalDeformOp._inputElements.push_back(temporaryDataStreams[i-workingTemporarySpaceElements.begin()]);
						} else {
							i = std::find_if(workingSourceDataElements.begin(), workingSourceDataElements.end(), [s](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& p) { return p.second == s; });
							if (i != workingSourceDataElements.end()) {
								finalDeformOp._inputElements.push_back(sourceDataStreams[i-workingSourceDataElements.begin()]);
							} else {
								finalDeformOp._inputElements.push_back({});
							}
						}
					}
				}
				// output streams
				for (auto s:wdo._outputStreamIds) {
					auto i = std::find_if(workingGeneratedElements.begin(), workingGeneratedElements.end(), [s](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& p) { return p.second == s; });
					if (i != workingGeneratedElements.end()) {
						finalDeformOp._outputElements.push_back(generatedDataStreams[i-workingGeneratedElements.begin()]);
					} else {
						i = std::find_if(workingTemporarySpaceElements.begin(), workingTemporarySpaceElements.end(), [s](const std::pair<DeformOperationInstantiation::NameAndFormat, unsigned>& p) { return p.second == s; });
						if (i != workingTemporarySpaceElements.end()) {
							finalDeformOp._outputElements.push_back(temporaryDataStreams[i-workingTemporarySpaceElements.begin()]);
						} else {
							finalDeformOp._outputElements.push_back({});
						}
					}
				}
				finalDeformOp._deformOp = wdo._deformOp;
				result._deformOps.emplace_back(std::move(finalDeformOp));
			}

			result._generatedElements.reserve(workingGeneratedElements.size());
			for (const auto&wge:workingGeneratedElements)
				result._generatedElements.push_back(InputElementDesc{wge.first._semantic, wge.first._semanticIndex, wge.first._format});

			result._staticDataLoadRequests = std::move(sourceDataTransforms);

			return result;
		}

		static const RenderCore::Assets::VertexElement* FindElement(IteratorRange<const RenderCore::Assets::VertexElement*> ele, uint64_t semanticHash)
		{
			return std::find_if(
				ele.begin(), ele.end(),
				[semanticHash](const RenderCore::Assets::VertexElement& ele) {
					return (Hash64(ele._semanticName) + ele._semanticIndex) == semanticHash;
				});
		}

		static IteratorRange<VertexElementIterator> AsVertexElementIteratorRange(
			IteratorRange<void*> vbData,
			Format format,
			unsigned byteOffset,
			unsigned vertexStride)
		{
			VertexElementIterator begin {
				MakeIteratorRange(PtrAdd(vbData.begin(), byteOffset), AsPointer(vbData.end())),
				vertexStride, format };
			VertexElementIterator end {
				MakeIteratorRange(AsPointer(vbData.end()), AsPointer(vbData.end())),
				vertexStride, format };
			return { begin, end };
		}

		static void ReadStaticData(
			IteratorRange<void*> destinationVB,
			IteratorRange<void*> sourceVB,
			const SourceDataTransform& transform,
			const RenderCore::Assets::VertexElement& srcElement,
			unsigned srcStride)
		{
			assert(destinationVB.size() >= transform._targetStride * transform._vertexCount);
			assert(sourceVB.size() >= srcStride * transform._vertexCount);
			auto dstRange = AsVertexElementIteratorRange(destinationVB, transform._targetFormat, transform._targetOffset, transform._targetStride);
			auto srcRange = AsVertexElementIteratorRange(sourceVB, srcElement._nativeFormat, srcElement._alignedByteOffset, srcStride);
			auto dstCount = dstRange.size();
			auto srcCount = srcRange.size();
			(void)dstCount; (void)srcCount;
			Assets::GeoProc::Copy(dstRange, srcRange, transform._vertexCount);
		}

		static std::vector<uint8_t> GenerateDeformStaticInput(
			const RenderCore::Assets::ModelScaffold& modelScaffold,
			IteratorRange<const SourceDataTransform*> inputLoadRequests,
			unsigned destinationBufferSize)
		{
			if (inputLoadRequests.empty())
				return {};

			std::vector<uint8_t> result;
			result.resize(destinationBufferSize, 0);

			std::vector<SourceDataTransform> loadRequests { inputLoadRequests.begin(), inputLoadRequests.end() };
			std::stable_sort(
				loadRequests.begin(), loadRequests.end(),
				[](const SourceDataTransform& lhs, const SourceDataTransform& rhs) {
					return lhs._geoId < rhs._geoId;
				});

			auto largeBlocks = modelScaffold.OpenLargeBlocks();
			auto base = largeBlocks->TellP();

			auto& immData = modelScaffold.ImmutableData();
			for (const auto&r:loadRequests) {
				bool initializedElement = false;
				if (r._geoId < immData._geoCount) {
					auto& geo = immData._geos[r._geoId];
					auto& vb = geo._vb;
					auto sourceEle = FindElement(MakeIteratorRange(vb._ia._elements), r._sourceStream);
					if (sourceEle != vb._ia._elements.end()) {
						auto vbData = std::make_unique<uint8_t[]>(vb._size);
						largeBlocks->Seek(base + vb._offset);
						largeBlocks->Read(vbData.get(), vb._size);
						ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), vb._size)), r, *sourceEle, vb._ia._vertexStride);
						initializedElement = true;
					}
				} else {
					auto& geo = immData._boundSkinnedControllers[r._geoId - immData._geoCount];
					auto sourceEle = FindElement(MakeIteratorRange(geo._vb._ia._elements), r._sourceStream);
					if (sourceEle != geo._vb._ia._elements.end()) {
						auto vbData = std::make_unique<uint8_t[]>(geo._vb._size);
						largeBlocks->Seek(base + geo._vb._offset);
						largeBlocks->Read(vbData.get(), geo._vb._size);
						ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), geo._vb._size)), r, *sourceEle, geo._animatedVertexElements._ia._vertexStride);
						initializedElement = true;
					} else {
						sourceEle = FindElement(MakeIteratorRange(geo._animatedVertexElements._ia._elements), r._sourceStream);
						if (sourceEle != geo._animatedVertexElements._ia._elements.end()) {
							auto vbData = std::make_unique<uint8_t[]>(geo._animatedVertexElements._size);
							largeBlocks->Seek(base + geo._animatedVertexElements._offset);
							largeBlocks->Read(vbData.get(), geo._animatedVertexElements._size);
							ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), geo._animatedVertexElements._size)), r, *sourceEle, geo._animatedVertexElements._ia._vertexStride);
							initializedElement = true;
						} else {
							sourceEle = FindElement(MakeIteratorRange(geo._skeletonBinding._ia._elements), r._sourceStream);
							if (sourceEle != geo._skeletonBinding._ia._elements.end()) {
								auto vbData = std::make_unique<uint8_t[]>(geo._skeletonBinding._size);
								largeBlocks->Seek(base + geo._skeletonBinding._offset);
								largeBlocks->Read(vbData.get(), geo._skeletonBinding._size);
								ReadStaticData(MakeIteratorRange(result), MakeIteratorRange(vbData.get(), PtrAdd(vbData.get(), geo._skeletonBinding._size)), r, *sourceEle, geo._skeletonBinding._ia._vertexStride);
								initializedElement = true;
							}
						}
					}
				}

				if (!initializedElement)
					Throw(std::runtime_error("Could not initialize deform input element"));
			}

			return result;
		}

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
			const NascentDeformStream& deformStream)
		{
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), MakeIteratorRange(deformStream._suppressedElements), 0);
			auto t = MakeIA(MakeIteratorRange(deformStream._generatedElements), 1);
			result.insert(result.end(), t.begin(), t.end());
			return result;
		}

		static std::vector<RenderCore::InputElementDesc> BuildFinalIA(
			const RenderCore::Assets::BoundSkinnedGeometry& geo,
			const NascentDeformStream& deformStream)
		{
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._vb._ia._elements), MakeIteratorRange(deformStream._suppressedElements), 0);
			auto t0 = MakeIA(MakeIteratorRange(geo._animatedVertexElements._ia._elements), MakeIteratorRange(deformStream._suppressedElements), 1);
			auto t1 = MakeIA(MakeIteratorRange(deformStream._generatedElements), 2);
			result.insert(result.end(), t0.begin(), t0.end());
			result.insert(result.end(), t1.begin(), t1.end());
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
				const Internal::NascentDeformStream& deformStream)
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
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::shared_ptr<RenderCore::Assets::MaterialScaffold>& materialScaffold,
		IteratorRange<const DeformOperationInstantiation*> deformAttachments,
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

		unsigned preDeformStaticDataVBIterator = 0;
		unsigned deformTemporaryVBIterator = 0;
		unsigned postDeformVBIterator = 0;
		std::vector<Internal::SourceDataTransform> deformStaticLoadDataRequests;

		std::vector<Internal::NascentDeformStream> geoDeformStreams;
		std::vector<Internal::NascentDeformStream> skinControllerDeformStreams;

		_geos.reserve(modelScaffold->ImmutableData()._geoCount);
		_geoCalls.reserve(modelScaffold->ImmutableData()._geoCount);
		geoDeformStreams.reserve(modelScaffold->ImmutableData()._geoCount);
		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._geoCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._geos[geo];

			unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformStream(
				deformAttachments, geo, vertexCount, 
				preDeformStaticDataVBIterator, deformTemporaryVBIterator, postDeformVBIterator);

			// Build the main non-deformed vertex stream
			auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
			drawableGeo->_vertexStreams[0] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._vb);
			drawableGeo->_vertexStreamCount = 1;

			// Attach those vertex streams that come from the deform operation
			if (deform._vbSizes[VB_PostDeform]) {
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount] = DrawableGeo::VertexStream{nullptr, deform._vbOffsets[VB_PostDeform]};
				++drawableGeo->_vertexStreamCount;
			}
			_deformOps.insert(_deformOps.end(), deform._deformOps.begin(), deform._deformOps.end());

			deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._staticDataLoadRequests.begin(), deform._staticDataLoadRequests.end());

			drawableGeo->_ib = LoadIndexBuffer(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._ib);
			drawableGeo->_ibFormat = rg._ib._format;
			_geos.push_back(std::move(drawableGeo));
			geoDeformStreams.push_back(std::move(deform));
		}

		_boundSkinnedControllers.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		_boundSkinnedControllerGeoCalls.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		skinControllerDeformStreams.reserve(modelScaffold->ImmutableData()._boundSkinnedControllerCount);
		for (unsigned geo=0; geo<modelScaffold->ImmutableData()._boundSkinnedControllerCount; ++geo) {
			const auto& rg = modelScaffold->ImmutableData()._boundSkinnedControllers[geo];

			unsigned vertexCount = rg._vb._size / rg._vb._ia._vertexStride;
			auto deform = Internal::BuildNascentDeformStream(
				deformAttachments, geo + (unsigned)modelScaffold->ImmutableData()._geoCount, vertexCount,
				preDeformStaticDataVBIterator, deformTemporaryVBIterator, postDeformVBIterator);

			// Build the main non-deformed vertex stream
			auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
			drawableGeo->_vertexStreams[0] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._vb);
			drawableGeo->_vertexStreams[1] = MakeVertexStream(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._animatedVertexElements);
			drawableGeo->_vertexStreamCount = 2;

			// Attach those vertex streams that come from the deform operation
			if (deform._vbSizes[VB_PostDeform]) {
				drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount] = DrawableGeo::VertexStream{nullptr, deform._vbOffsets[VB_PostDeform]};
				++drawableGeo->_vertexStreamCount;
			}
			_deformOps.insert(_deformOps.end(), deform._deformOps.begin(), deform._deformOps.end());

			deformStaticLoadDataRequests.insert(
				deformStaticLoadDataRequests.end(),
				deform._staticDataLoadRequests.begin(), deform._staticDataLoadRequests.end());

			drawableGeo->_ib = LoadIndexBuffer(*pipelineAcceleratorPool->GetDevice(), *modelScaffold, rg._ib);
			drawableGeo->_ibFormat = rg._ib._format;
			_boundSkinnedControllers.push_back(std::move(drawableGeo));
			skinControllerDeformStreams.push_back(std::move(deform));
		}

		// Setup the materials
		GeoCallBuilder geoCallBuilder { pipelineAcceleratorPool, _materialScaffold.get(), _materialScaffoldName };

		const auto& cmdStream = _modelScaffold->CommandStream();
		for (unsigned c = 0; c < cmdStream.GetGeoCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetGeoCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._geos[geoCall._geoId];
			auto& deform = geoDeformStreams[geoCall._geoId];
			// todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
            for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_geoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, deform));
			}
		}

		for (unsigned c = 0; c < cmdStream.GetSkinCallCount(); ++c) {
            const auto& geoCall = cmdStream.GetSkinCall(c);
			auto& rawGeo = modelScaffold->ImmutableData()._boundSkinnedControllers[geoCall._geoId];
			auto& deform = skinControllerDeformStreams[geoCall._geoId];
            // todo -- we should often get duplicate pipeline accelerators & descriptor set accelerators 
			// here (since many draw calls will share the same materials, etc). We should avoid unnecessary
			// duplication of objects and construction work
			for (unsigned d = 0; d < unsigned(geoCall._materialCount); ++d) {
				_boundSkinnedControllerGeoCalls.emplace_back(geoCallBuilder.MakeGeoCall(*pipelineAcceleratorPool, geoCall._materialGuids[d], rawGeo, deform));
			}
		}

		_drawableIAs = std::move(geoCallBuilder._ias);

		// Create the dynamic VB and assign it to all of the slots it needs to go to
		if (postDeformVBIterator) {
			_dynVB = Services::GetDevice().CreateResource(
				CreateDesc(
					BindFlag::VertexBuffer,
					CPUAccess::WriteDynamic, GPUAccess::Read,
					LinearBufferDesc::Create(postDeformVBIterator),
					"ModelRendererDynVB"));

			for (auto&g:_geos)
				for (unsigned s=0; s<g->_vertexStreamCount; ++s)
					if (!g->_vertexStreams[s]._resource)
						g->_vertexStreams[s]._resource = _dynVB;

			for (auto&g:_boundSkinnedControllers)
				for (unsigned s=0; s<g->_vertexStreamCount; ++s)
					if (!g->_vertexStreams[s]._resource)
						g->_vertexStreams[s]._resource = _dynVB;
		}

		if (preDeformStaticDataVBIterator) {
			_deformStaticDataInput = Internal::GenerateDeformStaticInput(
				*_modelScaffold,
				MakeIteratorRange(deformStaticLoadDataRequests),
				preDeformStaticDataVBIterator);
		}

		if (deformTemporaryVBIterator) {
			_deformTemporaryBuffer.resize(deformTemporaryVBIterator, 0);
		}

		_usi = std::make_shared<UniformsStreamInterface>();
		// HACK --> use the fallback LocalTransform -->
		_usi->BindImmediateData(0, Techniques::ObjectCB::LocalTransform);
		// <------------------------------------
		_usi->BindImmediateData(1, Techniques::ObjectCB::DrawCallProperties);

		unsigned c=2;
		for (const auto&u:uniformBufferDelegates) {
			_usi->BindImmediateData(c++, u.first, u.second->GetLayout());
		}

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

	void SimpleModelRenderer::ConstructToFuture(
		::Assets::FuturePtr<SimpleModelRenderer>& future,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const ::Assets::PtrToFuturePtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
		const ::Assets::PtrToFuturePtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates,
		const std::string& modelScaffoldNameString,
		const std::string& materialScaffoldNameString)
	{
		std::vector<UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };
		::Assets::WhenAll(modelScaffoldFuture, materialScaffoldFuture).ThenConstructToFuture(
			future,
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, uniformBufferBindings, modelScaffoldNameString, materialScaffoldNameString](
				std::shared_ptr<RenderCore::Assets::ModelScaffold> scaffoldActual, std::shared_ptr<RenderCore::Assets::MaterialScaffold> materialActual) {
				
				auto deformOps = DeformOperationFactory::GetInstance().CreateDeformOperations(
					MakeStringSection(deformOperationString),
					scaffoldActual);

				return std::make_shared<SimpleModelRenderer>(
					pipelineAcceleratorPool, scaffoldActual, materialActual, 
					MakeIteratorRange(deformOps), 
					MakeIteratorRange(uniformBufferBindings),
					modelScaffoldNameString, materialScaffoldNameString);
			});
	}

	void SimpleModelRenderer::ConstructToFuture(
		::Assets::FuturePtr<SimpleModelRenderer>& future,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName,
		StringSection<> materialScaffoldName,
		StringSection<> deformOperations,
		IteratorRange<const UniformBufferBinding*> uniformBufferDelegates)
	{
		auto scaffoldFuture = ::Assets::MakeAsset<RenderCore::Assets::ModelScaffold>(modelScaffoldName);
		auto materialFuture = ::Assets::MakeAsset<RenderCore::Assets::MaterialScaffold>(materialScaffoldName, modelScaffoldName);
		ConstructToFuture(future, pipelineAcceleratorPool, scaffoldFuture, materialFuture, deformOperations, uniformBufferDelegates, modelScaffoldName.AsString(), materialScaffoldName.AsString());
	}

	void SimpleModelRenderer::ConstructToFuture(
		::Assets::FuturePtr<SimpleModelRenderer>& future,
		const std::shared_ptr<Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		StringSection<> modelScaffoldName)
	{
		ConstructToFuture(future, pipelineAcceleratorPool, modelScaffoldName, modelScaffoldName);
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

	IPreDrawDelegate::~IPreDrawDelegate() {}

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

		auto& device = Services::GetDevice();

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

	void RendererSkeletonInterface::ConstructToFuture(
		::Assets::PtrToFuturePtr<RendererSkeletonInterface>& skeletonInterfaceFuture,
		::Assets::PtrToFuturePtr<SimpleModelRenderer>& rendererFuture,
		const std::shared_ptr<IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const ::Assets::PtrToFuturePtr<RenderCore::Assets::ModelScaffold>& modelScaffoldFuture,
		const ::Assets::PtrToFuturePtr<RenderCore::Assets::MaterialScaffold>& materialScaffoldFuture,
		const ::Assets::PtrToFuturePtr<RenderCore::Assets::SkeletonScaffold>& skeletonScaffoldFuture,
		StringSection<> deformOperations,
		IteratorRange<const SimpleModelRenderer::UniformBufferBinding*> uniformBufferDelegates)
	{
		::Assets::WhenAll(modelScaffoldFuture, skeletonScaffoldFuture).ThenConstructToFuture(*skeletonInterfaceFuture);

		std::vector<SimpleModelRenderer::UniformBufferBinding> uniformBufferBindings { uniformBufferDelegates.begin(), uniformBufferDelegates.end() };
		::Assets::WhenAll(modelScaffoldFuture, materialScaffoldFuture, skeletonInterfaceFuture).ThenConstructToFuture(
			*rendererFuture,
			[deformOperationString{deformOperations.AsString()}, pipelineAcceleratorPool, uniformBufferBindings](
				std::shared_ptr<RenderCore::Assets::ModelScaffold> scaffoldActual, 
				std::shared_ptr<RenderCore::Assets::MaterialScaffold> materialActual,
				std::shared_ptr<RendererSkeletonInterface> skeletonInterface) {
				
				auto deformOps = DeformOperationFactory::GetInstance().CreateDeformOperations(
					MakeStringSection(deformOperationString),
					scaffoldActual);

				auto skinDeform = DeformOperationFactory::GetInstance().CreateDeformOperations("skin", scaffoldActual);
				deformOps.insert(deformOps.end(), skinDeform.begin(), skinDeform.end());

				// Add a uniform buffer binding delegate for the joint transforms
				auto ubb = uniformBufferBindings;
				ubb.push_back({Hash64("BoneTransforms"), skeletonInterface});

				return std::make_shared<SimpleModelRenderer>(
					pipelineAcceleratorPool, scaffoldActual, materialActual, 
					MakeIteratorRange(deformOps), 
					MakeIteratorRange(ubb));
			});
	}


}}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawableProvider.h"
#include "Drawables.h"
#include "DeformGeometryInfrastructure.h"
#include "PipelineAccelerator.h"
#include "CommonUtils.h"
#include "../Assets/ScaffoldCmdStream.h"
#include "../Assets/ModelMachine.h"
#include "../Assets/MaterialMachine.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/MaterialScaffold.h"
#include "../../Assets/Marker.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/Streams/StreamFormatter.h"

namespace RenderCore { namespace Techniques
{
	namespace Internal
	{
		static std::vector<InputElementDesc> MakeIA(IteratorRange<const Assets::VertexElement*> elements, IteratorRange<const uint64_t*> suppressedElements, unsigned streamIdx)
		{
			std::vector<InputElementDesc> result;
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

		static std::vector<InputElementDesc> MakeIA(IteratorRange<const InputElementDesc*> elements, unsigned streamIdx)
		{
			std::vector<InputElementDesc> result;
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

		static std::vector<InputElementDesc> BuildFinalIA(
			const Assets::RawGeometryDesc& geo,
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

		static Batch CalculateBatchForStateSet(const Assets::RenderStateSet& stateSet)
		{
			if (stateSet._flag & Assets::RenderStateSet::Flag::ForwardBlend && stateSet._forwardBlendOp != BlendOp::NoBlending) {
				if (stateSet._flag & Assets::RenderStateSet::Flag::BlendType) {
					switch (stateSet._blendType) {
					case Assets::RenderStateSet::BlendType::Basic: 
					case Assets::RenderStateSet::BlendType::Ordered:
						return Batch::Blending;
						break;
					case Assets::RenderStateSet::BlendType::DeferredDecal:
					default:
						return Batch::Opaque; 
						break;
					}
				} else {
					return Batch::Blending;
				}
			}
			return Batch::Opaque;
		}

		class DrawableGeoBuilder
		{
		public:
			std::vector<std::shared_ptr<DrawableGeo>> _geos;
			using InputLayout = std::vector<InputElementDesc>;
			std::vector<InputLayout> _geosLayout;
			std::vector<Topology> _geosTopologies;

			enum class LoadBuffer { VB, IB };
			enum class DrawableStream { IB, Vertex0, Vertex1, Vertex2, Vertex3 };
			struct LoadRequest
			{
				unsigned _scaffoldIdx;
				unsigned _drawableGeoIdx;
				unsigned _srcOffset, _srcSize;
				LoadBuffer _loadBuffer;
				DrawableStream _drawableStream;
			};
			std::vector<LoadRequest> _staticLoadRequests;

			void AddStaticLoadRequest(
				LoadBuffer loadBuffer, DrawableStream drawableStream,
				unsigned scaffoldIdx, unsigned drawableGeoIdx,
				unsigned largeBlocksOffset, unsigned largeBlocksSize)
			{
				if (!largeBlocksSize) return;
				// note -- we could throw in a hash check here to avoid reuploading the same data
				_staticLoadRequests.emplace_back(
					LoadRequest{
						scaffoldIdx, drawableGeoIdx,
						largeBlocksOffset, largeBlocksSize,
						loadBuffer, drawableStream});
			}

			std::vector<std::shared_ptr<Assets::ModelScaffoldCmdStreamForm>> _registeredScaffolds;
			unsigned GetScaffoldIdx(const std::shared_ptr<Assets::ModelScaffoldCmdStreamForm>& scaffold)
			{
				auto i = std::find(_registeredScaffolds.begin(), _registeredScaffolds.end(), scaffold);
				if (i != _registeredScaffolds.end())
					return std::distance(_registeredScaffolds.begin(), i);
				_registeredScaffolds.push_back(scaffold);
				return (unsigned)_registeredScaffolds.size()-1;
			}

			unsigned AddGeo(
				IteratorRange<Assets::ScaffoldCmdIterator> geoMachine,
				const std::shared_ptr<Assets::ModelScaffoldCmdStreamForm>& scaffold,
				const std::shared_ptr<DeformAccelerator>& deformAccelerator,
				const DeformerToRendererBinding& deformerBinding,
				unsigned geoIdx)
			{
				const Assets::RawGeometryDesc* rawGeometry = nullptr;
				const Assets::SkinningDataDesc* skinningData = nullptr;
				for (auto cmd:geoMachine) {
					switch (cmd.Cmd()) {
					case (uint32_t)Assets::GeoCommand::AttachRawGeometry:
						assert(!rawGeometry);
						rawGeometry = (Assets::RawGeometryDesc*)cmd.RawData().begin();
						break;

					case (uint32_t)Assets::GeoCommand::AttachSkinningData:
						assert(!skinningData);
						skinningData = (const Assets::SkinningDataDesc*)cmd.RawData().begin();
						break;

					default:
						break;
					}
				}

				if (rawGeometry) {
					auto& rg = *rawGeometry;

					// Build the main non-deformed vertex stream
					auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
					auto drawableGeoIdx = (unsigned)_geos.size();
					auto scaffoldIdx = GetScaffoldIdx(scaffold);

					AddStaticLoadRequest(LoadBuffer::VB, DrawableStream::Vertex0, scaffoldIdx, drawableGeoIdx, rg._vb._offset, rg._vb._size);
					drawableGeo->_vertexStreamCount = 1;

					// Attach those vertex streams that come from the deform operation
					if (geoIdx < deformerBinding._geoBindings.size() && !deformerBinding._geoBindings[geoIdx]._generatedElements.empty()) {
						drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
						drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformerBinding._geoBindings[geoIdx]._postDeformBufferOffset;
						drawableGeo->_deformAccelerator = deformAccelerator;
						_geosLayout.push_back(BuildFinalIA(rg, &deformerBinding._geoBindings[geoIdx], drawableGeo->_vertexStreamCount));
						++drawableGeo->_vertexStreamCount;
					} else {
						if (skinningData) {
							AddStaticLoadRequest(
								LoadBuffer::VB, DrawableStream((unsigned)DrawableStream::Vertex0+drawableGeo->_vertexStreamCount), scaffoldIdx, drawableGeoIdx, 
								skinningData->_animatedVertexElements._offset, skinningData->_animatedVertexElements._size);
						}

						_geosLayout.push_back(BuildFinalIA(rg));
					}

					if (!rg._drawCalls.empty()) {
						// Figure out the topology from from the rawGeo. We can't mix topology across the one geo call; all draw calls
						// for the same geo object must share the same toplogy mode
						auto topology = rg._drawCalls[0]._topology;
						#if defined(_DEBUG)
							for (auto r=rg._drawCalls.begin()+1; r!=rg._drawCalls.end(); ++r)
								assert(topology == r->_topology);
						#endif
						_geosTopologies.push_back(topology);
					} else
						_geosTopologies.push_back(Topology::TriangleList);

					// hack -- we might need this for material deform, as well
					drawableGeo->_deformAccelerator = deformAccelerator;
					
					AddStaticLoadRequest(LoadBuffer::IB, DrawableStream::IB, scaffoldIdx, drawableGeoIdx, rg._ib._offset, rg._ib._size);
					drawableGeo->_ibFormat = rg._ib._format;
					_geos.push_back(std::move(drawableGeo));
					return (unsigned)_geos.size()-1;
				} else {
					assert(0);
					// expecting a raw geometry here somewhere
					return ~0u;
				}
			}

			void LoadPendingStaticResources(
				std::promise<BufferUploads::CommandListID>&& completionCmdListPromise,
				BufferUploads::IManager& bufferUploads)
			{
				// collect all of the various uploads we need to make, and engage!
				std::sort(
					_staticLoadRequests.begin(), _staticLoadRequests.end(),
					[](const auto& lhs, const auto& rhs) {
						if (lhs._loadBuffer < rhs._loadBuffer) return true;
						if (lhs._loadBuffer > rhs._loadBuffer) return false;
						if (lhs._scaffoldIdx < rhs._scaffoldIdx) return true;
						if (lhs._scaffoldIdx < rhs._scaffoldIdx) return false;
						return lhs._srcOffset < rhs._srcOffset;
					});

				struct PendingTransactions
				{
					std::vector<BufferUploads::TransactionMarker> _markers;

					struct ResAssignment
					{
						std::shared_ptr<DrawableGeo> _drawableGeo;
						unsigned _markerIdx = ~0u;
						DrawableStream _drawableStream = DrawableStream::IB;
					};
					std::vector<ResAssignment> _resAssignments;
				};
				auto pendingTransactions = std::make_shared<PendingTransactions>();
				for (auto i=_staticLoadRequests.begin(); i!=_staticLoadRequests.end();) {
					auto start = i;
					while (i!=_staticLoadRequests.end() && i->_loadBuffer == start->_loadBuffer && i->_scaffoldIdx == start->_scaffoldIdx) ++i;

					std::vector<std::pair<unsigned, unsigned>> localLoadRequests;
					localLoadRequests.reserve(i-start);
					unsigned offset = 0;
					for (auto i2=start; i2!=i; ++i2) {
						localLoadRequests.emplace_back(i2->_srcOffset, i2->_srcSize);
						// set the offset value in the DrawableGeo now (though the resource won't be filled in immediately)
						if (i2->_drawableStream == DrawableStream::IB) {
							_geos[i2->_drawableGeoIdx]->_ibOffset = offset;
						} else {
							_geos[i2->_drawableGeoIdx]->_vertexStreams[unsigned(i2->_drawableStream)-unsigned(DrawableStream::Vertex0)]._vbOffset = offset;
						}
						offset += i2->_srcSize;	// todo -- alignment?

						pendingTransactions->_resAssignments.emplace_back(
							PendingTransactions::ResAssignment{_geos[i2->_drawableGeoIdx], (unsigned)pendingTransactions->_markers.size(), i2->_drawableStream});
					}
					auto transMarker = LoadStaticResourceFullyAsync(
						bufferUploads,
						MakeIteratorRange(localLoadRequests),
						offset, _registeredScaffolds[start->_scaffoldIdx],
						(start->_loadBuffer == LoadBuffer::IB) ? BindFlag::IndexBuffer : BindFlag::VertexBuffer,
						"[vb]");
					pendingTransactions->_markers.emplace_back(std::move(transMarker));
				}
				_staticLoadRequests.clear();

				::Assets::PollToPromise(
					std::move(completionCmdListPromise),
					[pendingTransactions](auto timeout) {
						auto timeoutTime = std::chrono::steady_clock::now() + timeout;
						for (const auto& t:pendingTransactions->_markers) {
							auto status = t._future.wait_until(timeoutTime);
							if (status == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
						}
						return ::Assets::PollStatus::Finish;
					},
					[pendingTransactions]() {
						std::vector<BufferUploads::ResourceLocator> locators;
						locators.reserve(pendingTransactions->_markers.size());
						for (auto& t:pendingTransactions->_markers)
							locators.emplace_back(t._future.get());

						BufferUploads::CommandListID largestCmdList = 0;
						for (const auto& l:locators)
							largestCmdList = std::max(l.GetCompletionCommandList(), largestCmdList); 

						// commit the resources back to the drawables, as needed
						// note -- no threading protection for this
						for (const auto& assign:pendingTransactions->_resAssignments) {
							if (assign._drawableStream == DrawableStream::IB) {
								assign._drawableGeo->_ib = locators[assign._markerIdx].GetContainingResource();
								assign._drawableGeo->_ibOffset += locators[assign._markerIdx].GetRangeInContainingResource().first;
							} else {
								auto& vertexStream = assign._drawableGeo->_vertexStreams[unsigned(assign._drawableStream)-unsigned(DrawableStream::Vertex0)];
								vertexStream._resource = locators[assign._markerIdx].GetContainingResource();
								vertexStream._vbOffset += locators[assign._markerIdx].GetRangeInContainingResource().first;
							}
						}

						return largestCmdList;
					});
			}
		};

		class PipelineBuilder
		{
		public:
			std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
			std::set<::Assets::DependencyValidation> _depVals;

			struct WorkingMaterial
			{
				uint64_t _guid;
				std::shared_ptr<Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;

				std::shared_ptr<Assets::ShaderPatchCollection> _patchCollection;
				ParameterBox _selectors;
				ParameterBox _resourceBindings;
				Assets::RenderStateSet _stateSet;
				unsigned _batchFilter;
			};
			std::vector<WorkingMaterial> _drawableMaterials;

			std::vector<std::shared_ptr<DrawableInputAssembly>> _ias;

			const WorkingMaterial* AddMaterial(
				IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
				const Assets::MaterialScaffoldCmdStreamForm& materialScaffold,
				uint64_t materialGuid,
				Techniques::IDeformAcceleratorPool* deformAcceleratorPool,
				const IDeformParametersAttachment* parametersDeformInfrastructure)
			{
				auto i = std::lower_bound(_drawableMaterials.begin(), _drawableMaterials.end(), materialGuid, [](const auto& q, uint64_t materialGuid) { return q._guid < materialGuid; });
				if (i != _drawableMaterials.end() && i->_guid == materialGuid) {
					return AsPointer(i);
				} else {
					i = _drawableMaterials.insert(i, WorkingMaterial{materialGuid});

					// Fill in _selectors, _resourceBindings, _stateSet, etc
					// We'll need to walk through the material machine to do this
					ParameterBox resHasParameters;
					for (auto cmd:materialMachine) {
						if (cmd.Cmd() == (uint32_t)Assets::MaterialCommand::AttachPatchCollectionId) {
							assert(!i->_patchCollection);
							auto id = *(const uint64_t*)cmd.RawData().begin();
							i->_patchCollection = materialScaffold.GetShaderPatchCollection(id);
						} else if (cmd.Cmd() == (uint32_t)Assets::MaterialCommand::AttachShaderResourceBindings) {
							assert(resHasParameters.GetCount() == 0);
							assert(!cmd.RawData().empty());
							auto& shaderResourceParameterBox = *(const ParameterBox*)cmd.RawData().begin();
							// Append the "RES_HAS_" constants for each resource that is both in the descriptor set and that we have a binding for
							for (const auto&r:shaderResourceParameterBox)
								resHasParameters.SetParameter(std::string{"RES_HAS_"} + r.Name().AsString(), 1);
						} else if (cmd.Cmd() == (uint32_t)Assets::MaterialCommand::AttachStateSet) {
							assert(cmd.RawData().size() == sizeof(Assets::RenderStateSet));
							i->_stateSet = *(const Assets::RenderStateSet*)cmd.RawData().begin();
						} else if (cmd.Cmd() == (uint32_t)Assets::MaterialCommand::AttachSelectors) {
							assert(i->_selectors.GetCount() == 0);
							assert(!cmd.RawData().empty());
							i->_selectors = *(const ParameterBox*)cmd.RawData().begin();
						}
					}
					i->_selectors.MergeIn(resHasParameters);

					// Descriptor set accelerator
					if (parametersDeformInfrastructure && deformAcceleratorPool) {
						auto paramBinding = parametersDeformInfrastructure->GetOutputParameterBindings();
						i->_descriptorSetAccelerator = _pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
							i->_patchCollection,
							i->_selectors,
							ParameterBox{},		// constantBindings
							ParameterBox{},		// resourceBindings
							IteratorRange<const std::pair<uint64_t, SamplerDesc>*>{},		// samplerBindings
							{(const AnimatedParameterBinding*)paramBinding.begin(), (const AnimatedParameterBinding*)paramBinding.end()},
							deformAcceleratorPool->GetDynamicPageResource());
					} else {
						i->_descriptorSetAccelerator = _pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
							i->_patchCollection,
							i->_selectors,
							ParameterBox{},
							ParameterBox{});
					}

					i->_batchFilter = (unsigned)CalculateBatchForStateSet(i->_stateSet);
					return AsPointer(i);
				}
			}

			unsigned AddDrawableInputAssembly(
				IteratorRange<const InputElementDesc*> inputElements,
				Topology topology)
			{
				auto ia = std::make_shared<DrawableInputAssembly>(MakeIteratorRange(inputElements), topology);
				auto w = std::find_if(_ias.begin(), _ias.end(), [hash=ia->GetHash()](const auto& q) { return q->GetHash() == hash; });
				if (w == _ias.end()) {
					_ias.push_back(ia);
					return (unsigned)_ias.size() - 1;
				} else {
					return (unsigned)std::distance(_ias.begin(), w);
				}
			}

			struct CompiledPipeline
			{
				std::shared_ptr<PipelineAccelerator> _pipelineAccelerator;
				unsigned _iaIdx;
			};

			CompiledPipeline MakePipeline(
				const WorkingMaterial& material,
				IteratorRange<const InputElementDesc*> inputElements,
				Topology topology)
			{
				CompiledPipeline resultGeoCall;
				resultGeoCall._pipelineAccelerator =
					_pipelineAcceleratorPool->CreatePipelineAccelerator(
						material._patchCollection,
						material._selectors,
						inputElements,
						topology,
						material._stateSet);
				resultGeoCall._iaIdx = AddDrawableInputAssembly(inputElements, topology);
				return resultGeoCall;
			}
		};
	}

	class DrawableProvider::Pimpl
	{
	public:
		Internal::PipelineBuilder _pendingPipelines;
		Internal::DrawableGeoBuilder _pendingGeos;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::future<BufferUploads::CommandListID> _uploadFuture;
		std::atomic<bool> _fulfillWhenNotPendingCalled = false;

		using Machine = IteratorRange<Assets::ScaffoldCmdIterator>;

		void AddModel(
			const std::shared_ptr<Assets::ModelScaffoldCmdStreamForm>& modelScaffold,
			const std::shared_ptr<Assets::MaterialScaffoldCmdStreamForm>& materialScaffold,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator)
		{
			struct DrawableSrc
			{
				std::shared_ptr<PipelineAccelerator> _pipelineAccelerator;
				std::shared_ptr<Techniques::DescriptorSetAccelerator> _descriptorSetAccelerator;
				unsigned 	_batchFilter;

				uint64_t 	_materialGuid;
				unsigned	_firstIndex, _indexCount;
				unsigned	_firstVertex;
			};
			std::vector<DrawableSrc> drawableSrcs;

			IteratorRange<const uint64_t*> currentMaterialAssignments;
			unsigned currentTransformMarker = ~0u;

			RenderCore::Techniques::IGeoDeformerInfrastructure* geoDeformerInfrastructure = nullptr;
			RenderCore::Techniques::IDeformParametersAttachment* deformParametersAttachment = nullptr;
			DeformerToRendererBinding deformerBinding;
			if (deformAcceleratorPool && deformAccelerator) { 
				deformParametersAttachment = deformAcceleratorPool->GetDeformParametersAttachment(*deformAccelerator).get();
				geoDeformerInfrastructure = dynamic_cast<IGeoDeformerInfrastructure*>(deformAcceleratorPool->GetDeformAttachment(*deformAccelerator).get());
				if (geoDeformerInfrastructure)
					deformerBinding = geoDeformerInfrastructure->GetDeformerToRendererBinding();				
			}

			std::vector<std::pair<unsigned, unsigned>> modelGeoIdToPendingGeoIndex;
			for (auto cmd:modelScaffold->CommandStream()) {
				switch (cmd.Cmd()) {
				case (uint32_t)Assets::ModelCommand::BeginSubModel:
				case (uint32_t)Assets::ModelCommand::EndSubModel:
				case (uint32_t)Assets::ModelCommand::SetLevelOfDetail:
					break;		// submodel stuff not used at the moment

				case (uint32_t)Assets::ModelCommand::SetTransformMarker:
					currentTransformMarker = cmd.As<unsigned>();
					break;

				case (uint32_t)Assets::ModelCommand::SetMaterialAssignments:
					currentMaterialAssignments = cmd.RawData().Cast<const uint64_t*>();
					break;

				case (uint32_t)Assets::ModelCommand::GeoCall:
					{
						auto& geoCallDesc = cmd.As<Assets::GeoCallDesc>();
						auto geoMachine = modelScaffold->GetGeoMachine(geoCallDesc._geoId);
						assert(!geoMachine.empty());

						// Find the referenced geo object, and create the DrawableGeo object, etc
						unsigned pendingGeoIdx = ~0u;
						auto i = std::find_if(
							modelGeoIdToPendingGeoIndex.begin(), modelGeoIdToPendingGeoIndex.end(),
							[geoId=geoCallDesc._geoId](const auto& q) { return q.first == geoId; });
						if (i == modelGeoIdToPendingGeoIndex.end()) {
							pendingGeoIdx = _pendingGeos.AddGeo(
								geoMachine, modelScaffold,
								deformAccelerator, deformerBinding,
								geoCallDesc._geoId);
							modelGeoIdToPendingGeoIndex.emplace_back(geoCallDesc._geoId, pendingGeoIdx);
						} else {
							pendingGeoIdx = i->second;
						}

						// configure the draw calls that we're going to need to make for this geocall
						// while doing this we'll also sort out materials
						const Assets::RawGeometryDesc* rawGeometry = nullptr;
						for (auto cmd:geoMachine) {
							switch (cmd.Cmd()) {
							case (uint32_t)Assets::GeoCommand::AttachRawGeometry:
								assert(!rawGeometry);
								rawGeometry = (const Assets::RawGeometryDesc*)cmd.RawData().begin();
								break;
							}
						}

						if (rawGeometry) {
							auto& pendingGeo = _pendingGeos._geos[pendingGeoIdx];
							for (const auto& dc:rawGeometry->_drawCalls) {
								// note -- there's some redundancy here, because we'll end up calling 
								// AddMaterial & MakePipeline over and over again for the same parameters. There's
								// some caching in those to precent allocating dupes, but it might still be more
								// efficient to avoid some of the redundancy
								auto matAssignment = currentMaterialAssignments[dc._subMaterialIndex];
								auto* workingMaterial = _pendingPipelines.AddMaterial(
									materialScaffold->GetMaterialMachine(matAssignment),
									*materialScaffold,
									matAssignment,
									deformAcceleratorPool.get(), deformParametersAttachment);
								auto compiledPipeline = _pendingPipelines.MakePipeline(
									*workingMaterial, 
									_pendingGeos._geosLayout[pendingGeoIdx],
									_pendingGeos._geosTopologies[pendingGeoIdx]);
								drawableSrcs.push_back(DrawableSrc{
									std::move(compiledPipeline._pipelineAccelerator),
									workingMaterial->_descriptorSetAccelerator,
									workingMaterial->_batchFilter,
									matAssignment,
									dc._firstIndex, dc._indexCount, dc._firstVertex});
							}
						}
					}
					break;
				}
			}
		}

		Pimpl(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators, std::shared_ptr<BufferUploads::IManager> bufferUploads)
		{
			_pendingPipelines._pipelineAcceleratorPool = std::move(pipelineAccelerators);
			_bufferUploads = std::move(bufferUploads);
		}

		~Pimpl()
		{}
	};

	void DrawableProvider::Add(const Assets::RendererConstruction& construction)
	{
		assert(construction.GetAssetState() == ::Assets::AssetState::Ready);
		auto& internal = construction.GetInternal();
		auto msmi = internal._modelScaffoldMarkers.begin();
		auto mspi = internal._modelScaffoldPtrs.begin();
		auto matsmi = internal._materialScaffoldMarkers.begin();
		auto matspi = internal._materialScaffoldPtrs.begin();

		// wallk through all of the registered elements, and depending on what has been registered
		// with them, trigger AddModel()
		for (unsigned e=0; e<internal._elementCount; ++e) {
			while (msmi!=internal._modelScaffoldMarkers.end() && msmi->first < e) ++msmi;
			while (mspi!=internal._modelScaffoldPtrs.end() && mspi->first < e) ++mspi;
			while (matsmi!=internal._materialScaffoldMarkers.end() && matsmi->first < e) ++matsmi;
			while (matspi!=internal._materialScaffoldPtrs.end() && matspi->first < e) ++matspi;
			
			std::shared_ptr<Assets::ModelScaffoldCmdStreamForm> modelScaffold;
			std::shared_ptr<Assets::MaterialScaffoldCmdStreamForm> materialScaffold;
			if (mspi!=internal._modelScaffoldPtrs.end() && mspi->first == e)
				modelScaffold = mspi->second;
			else if (msmi!=internal._modelScaffoldMarkers.end() && msmi->first == e)
				modelScaffold = msmi->second->Actualize();
			
			if (matspi!=internal._materialScaffoldPtrs.end() && matspi->first == e)
				materialScaffold = matspi->second;
			else if (matsmi!=internal._materialScaffoldMarkers.end() && matsmi->first == e)
				materialScaffold = matsmi->second->Actualize();
			
			if (modelScaffold && materialScaffold) {
				_pimpl->AddModel(modelScaffold, materialScaffold, nullptr, nullptr);
			}
		}
	}

	void DrawableProvider::FulfillWhenNotPending(std::promise<FulFilledProvider>&& promise)
	{
		// prevent multiple calls, because this introduces a lot of threading complications
		auto prevCalled = _pimpl->_fulfillWhenNotPendingCalled.exchange(true);
		if (prevCalled)
			Throw(std::runtime_error("Attempting to call DrawableProvider::FulfillWhenNotPending multiple times. This can only be called once"));

		auto strongThis = shared_from_this();
		::Assets::PollToPromise(
			std::move(promise),
			[strongThis](auto timeout) {
				auto futureStatus = strongThis->_pimpl->_uploadFuture.wait_for(timeout);
				return (futureStatus == std::future_status::timeout) ? ::Assets::PollStatus::Continue : ::Assets::PollStatus::Finish;
			},
			[strongThis]() {
				FulFilledProvider result;
				result._provider = strongThis;
				result._completionCmdList = strongThis->_pimpl->_uploadFuture.get();
				return result;
			});
	}

	DrawableProvider::DrawableProvider(
		std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
		std::shared_ptr<BufferUploads::IManager> bufferUploads,
		const Assets::RendererConstruction& construction)
	{
		_pimpl = std::make_unique<Pimpl>(std::move(pipelineAccelerators), std::move(bufferUploads));
		Add(construction);
		std::promise<BufferUploads::CommandListID> uploadPromise;
		_pimpl->_uploadFuture = uploadPromise.get_future();
		_pimpl->_pendingGeos.LoadPendingStaticResources(std::move(uploadPromise), *_pimpl->_bufferUploads);
	}

	DrawableProvider::~DrawableProvider() {}

}}
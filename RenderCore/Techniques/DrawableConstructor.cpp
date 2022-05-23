// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawableConstructor.h"
#include "Drawables.h"
#include "DeformGeometryInfrastructure.h"
#include "DeformUniformsInfrastructure.h"
#include "DescriptorSetAccelerator.h"
#include "PipelineAccelerator.h"
#include "CommonUtils.h"
#include "ModelRendererConstruction.h"
#include "../Assets/ModelMachine.h"
#include "../Assets/MaterialMachine.h"
#include "../Assets/ModelScaffold.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/SkeletonMachine.h"
#include "../Assets/AnimationBindings.h"		// required for extracting base transforms
#include "../../Assets/Marker.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/Streams/StreamFormatter.h"

namespace RenderCore { namespace Techniques
{
	static_assert((uint32_t)DrawableConstructor::Command::BeginElement == Assets::s_scaffoldCmdBegin_DrawableConstructor);

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
			std::shared_ptr<IDrawablesPool> _drawablesPool;

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
				// we don't need to merge identical requests, because later on we sort and ensure each
				// block is loaded only once
				// however, there's no check for overlapping blocks
				_staticLoadRequests.emplace_back(
					LoadRequest{
						scaffoldIdx, drawableGeoIdx,
						largeBlocksOffset, largeBlocksSize,
						loadBuffer, drawableStream});
			}

			std::vector<std::shared_ptr<Assets::ModelScaffold>> _registeredScaffolds;
			std::vector<std::string> _registeredScaffoldNames;
			unsigned GetScaffoldIdx(const std::shared_ptr<Assets::ModelScaffold>& scaffold, const std::string& name)
			{
				auto i = std::find(_registeredScaffolds.begin(), _registeredScaffolds.end(), scaffold);
				if (i != _registeredScaffolds.end())
					return std::distance(_registeredScaffolds.begin(), i);
				_registeredScaffolds.push_back(scaffold);
				_registeredScaffoldNames.push_back(name);
				return (unsigned)_registeredScaffolds.size()-1;
			}

			unsigned AddGeo(
				IteratorRange<Assets::ScaffoldCmdIterator> geoMachine,
				const std::shared_ptr<Assets::ModelScaffold>& scaffold,
				const std::shared_ptr<DeformAccelerator>& deformAccelerator,
				const DeformerToRendererBinding::GeoBinding* deformerBinding,
				const std::string modelScaffoldName)
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
					auto drawableGeo = _drawablesPool->CreateGeo();
					auto drawableGeoIdx = (unsigned)_geos.size();
					auto scaffoldIdx = GetScaffoldIdx(scaffold, modelScaffoldName);

					AddStaticLoadRequest(LoadBuffer::VB, DrawableStream::Vertex0, scaffoldIdx, drawableGeoIdx, rg._vb._offset, rg._vb._size);
					drawableGeo->_vertexStreamCount = 1;

					// Attach those vertex streams that come from the deform operation
					if (deformerBinding && !deformerBinding->_generatedElements.empty()) {
						drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._type = DrawableGeo::StreamType::Deform;
						drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount]._vbOffset = deformerBinding->_postDeformBufferOffset;
						drawableGeo->_deformAccelerator = deformAccelerator;
						_geosLayout.push_back(BuildFinalIA(rg, deformerBinding, drawableGeo->_vertexStreamCount));
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

						// set the offset value in the DrawableGeo now (though the resource won't be filled in immediately)
						if (i2->_drawableStream == DrawableStream::IB) {
							_geos[i2->_drawableGeoIdx]->_ibOffset = offset;
						} else {
							_geos[i2->_drawableGeoIdx]->_vertexStreams[unsigned(i2->_drawableStream)-unsigned(DrawableStream::Vertex0)]._vbOffset = offset;
						}
						pendingTransactions->_resAssignments.emplace_back(
							PendingTransactions::ResAssignment{_geos[i2->_drawableGeoIdx], (unsigned)pendingTransactions->_markers.size(), i2->_drawableStream});

						// The same block can be requested multiple times for different DrawableGeos. Multiples will be sequential, though, 
						// because it's sorted... so don't register the upload until we hit the last of a string of identical ones
						if ((i2+1) == i || (i2+1)->_srcOffset != i2->_srcOffset || (i2+1)->_srcSize != i2->_srcSize) {
							// check for overlap with the previous upload
							assert(localLoadRequests.empty() || (localLoadRequests.back().first + localLoadRequests.back().second) <= i2->_srcOffset);

							localLoadRequests.emplace_back(i2->_srcOffset, i2->_srcSize);
							offset += i2->_srcSize;	// todo -- alignment?
						}
					}
					auto transMarker = LoadStaticResourceFullyAsync(
						bufferUploads,
						MakeIteratorRange(localLoadRequests),
						offset, _registeredScaffolds[start->_scaffoldIdx],
						(start->_loadBuffer == LoadBuffer::IB) ? BindFlag::IndexBuffer : BindFlag::VertexBuffer,
						(StringMeld<128>() << "[vb]" << _registeredScaffoldNames[start->_scaffoldIdx]).AsStringSection());
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
								auto offset = locators[assign._markerIdx].GetRangeInContainingResource().first;
								if (offset != ~size_t(0)) assign._drawableGeo->_ibOffset += offset;
							} else {
								auto& vertexStream = assign._drawableGeo->_vertexStreams[unsigned(assign._drawableStream)-unsigned(DrawableStream::Vertex0)];
								vertexStream._resource = locators[assign._markerIdx].GetContainingResource();
								auto offset = locators[assign._markerIdx].GetRangeInContainingResource().first;
								if (offset != ~size_t(0)) vertexStream._vbOffset += offset;
							}
						}

						return largestCmdList;
					});
			}
		};

		class PipelineBuilder
		{
		public:
			std::shared_ptr<IDrawablesPool> _drawablesPool;
			std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
			std::vector<std::shared_ptr<PipelineAccelerator>> _pipelineAccelerators;
			std::vector<std::shared_ptr<DescriptorSetAccelerator>> _descriptorSetAccelerators;

			struct WorkingMaterial
			{
				uint64_t _guid;
				unsigned _descriptorSetAcceleratorIdx;

				std::shared_ptr<Assets::ShaderPatchCollection> _patchCollection;
				ParameterBox _selectors;
				ParameterBox _resourceBindings;
				Assets::RenderStateSet _stateSet;
				unsigned _batchFilter;
			};
			std::vector<WorkingMaterial> _drawableMaterials;

			std::vector<std::shared_ptr<DrawableInputAssembly>> _pendingInputAssemblies;

			const WorkingMaterial* AddMaterial(
				IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
				const std::shared_ptr<Assets::MaterialScaffold>& materialScaffold,
				unsigned elementIdx, uint64_t materialGuid,
				Techniques::IDeformAcceleratorPool* deformAcceleratorPool,
				const IDeformUniformsAttachment* parametersDeformInfrastructure)
			{
				std::shared_ptr<DeformerToDescriptorSetBinding> deformBinding;
				if (parametersDeformInfrastructure && deformAcceleratorPool) {
					auto& rendererBinding = parametersDeformInfrastructure->GetDeformerToRendererBinding();
					for (auto& b:rendererBinding._materialBindings)
						if (b.first == std::make_pair(elementIdx, materialGuid)) {
							deformBinding = std::make_shared<DeformerToDescriptorSetBinding>();
							deformBinding->_animatedSlots = b.second._animatedSlots;
							deformBinding->_dynamicPageResource = deformAcceleratorPool->GetDynamicPageResource();
							break;
						}
				}

				auto materialAndDeformerHash = materialGuid;
				if (deformBinding)
					materialAndDeformerHash = HashCombine(materialGuid, deformBinding->GetHash());

				auto i = std::lower_bound(_drawableMaterials.begin(), _drawableMaterials.end(), materialAndDeformerHash, [](const auto& q, uint64_t materialGuid) { return q._guid < materialGuid; });
				if (i != _drawableMaterials.end() && i->_guid == materialAndDeformerHash) {
					return AsPointer(i);
				} else {
					i = _drawableMaterials.insert(i, WorkingMaterial{materialAndDeformerHash});

					// Fill in _selectors, _resourceBindings, _stateSet, etc
					// We'll need to walk through the material machine to do this
					ParameterBox resHasParameters;
					for (auto cmd:materialMachine) {
						if (cmd.Cmd() == (uint32_t)Assets::MaterialCommand::AttachPatchCollectionId) {
							assert(!i->_patchCollection);
							auto id = *(const uint64_t*)cmd.RawData().begin();
							i->_patchCollection = materialScaffold->GetShaderPatchCollection(id);
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
					auto descSet = _pipelineAcceleratorPool->CreateDescriptorSetAccelerator(
						i->_patchCollection,
						materialMachine,
						materialScaffold,
						deformBinding);

					i->_descriptorSetAcceleratorIdx = AddDescriptorSetAccelerator(std::move(descSet));
					i->_batchFilter = (unsigned)CalculateBatchForStateSet(i->_stateSet);
					return AsPointer(i);
				}
			}

			unsigned AddDescriptorSetAccelerator(std::shared_ptr<DescriptorSetAccelerator> accelerator)
			{
				_descriptorSetAccelerators.emplace_back(std::move(accelerator));
				return (unsigned)_descriptorSetAccelerators.size()-1;
			}

			unsigned AddPipelineAccelerator(std::shared_ptr<PipelineAccelerator> accelerator)
			{
				auto i = std::find(_pipelineAccelerators.begin(), _pipelineAccelerators.end(), accelerator);
				if (i != _pipelineAccelerators.end())
					return std::distance(_pipelineAccelerators.begin(), i);
				_pipelineAccelerators.emplace_back(std::move(accelerator));
				return (unsigned)_pipelineAccelerators.size()-1;
			}

			unsigned AddDrawableInputAssembly(
				IteratorRange<const InputElementDesc*> inputElements,
				Topology topology)
			{
				auto hash = DrawableInputAssembly{MakeIteratorRange(inputElements), topology}.GetHash();
				auto w = std::find_if(_pendingInputAssemblies.begin(), _pendingInputAssemblies.end(), [hash](const auto& q) { return q->GetHash() == hash; });
				if (w == _pendingInputAssemblies.end()) {
					auto ia = _drawablesPool->CreateInputAssembly(MakeIteratorRange(inputElements), topology);
					_pendingInputAssemblies.push_back(std::move(ia));
					return (unsigned)_pendingInputAssemblies.size() - 1;
				} else {
					return (unsigned)std::distance(_pendingInputAssemblies.begin(), w);
				}
			}

			struct CompiledPipeline
			{
				unsigned _pipelineAcceleratorIdx;
				unsigned _iaIdx;
			};

			CompiledPipeline MakePipeline(
				const WorkingMaterial& material,
				IteratorRange<const InputElementDesc*> inputElements,
				Topology topology)
			{
				CompiledPipeline resultGeoCall;
				resultGeoCall._pipelineAcceleratorIdx =
					AddPipelineAccelerator(
						_pipelineAcceleratorPool->CreatePipelineAccelerator(
							material._patchCollection,
							material._selectors,
							inputElements,
							topology,
							material._stateSet));
				resultGeoCall._iaIdx = AddDrawableInputAssembly(inputElements, topology);
				return resultGeoCall;
			}
		};
	}

	static const DeformerToRendererBinding::GeoBinding* FindDeformerBinding(
		const DeformerToRendererBinding& binding,
		unsigned elementIdx, unsigned geoIdx)
	{
		auto i = std::find_if(binding._geoBindings.begin(), binding._geoBindings.end(), [p=std::make_pair(elementIdx, geoIdx)](const auto& q) { return q.first == p; });
		if (i != binding._geoBindings.end())
			return &i->second;
		return nullptr;
	}

	static const uint64_t s_topologicalCmdStream = Hash64("adjacency");

	class DrawableConstructor::Pimpl
	{
	public:
		Internal::PipelineBuilder _pendingPipelines;
		Internal::DrawableGeoBuilder _pendingGeos;
		std::future<BufferUploads::CommandListID> _uploadFuture;
		std::atomic<bool> _fulfillWhenNotPendingCalled = false;
		std::vector<::Assets::DependencyValidation> _pendingDepVals;
		std::vector<Float4x4> _pendingBaseTransforms;
		std::vector<std::pair<unsigned, unsigned>> _pendingBaseTransformsPerElement;

		struct PendingCmdStream
		{
			std::vector<DrawCall> _drawCalls;
			std::vector<uint8_t> _translatedCmdStream;
		};
		std::vector<std::pair<uint64_t, PendingCmdStream>> _pendingCmdStreams;

		using Machine = IteratorRange<Assets::ScaffoldCmdIterator>;

		void AddModel(
			const std::shared_ptr<Assets::ModelScaffold>& modelScaffold,
			const std::shared_ptr<Assets::MaterialScaffold>& materialScaffold,
			const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
			const std::shared_ptr<DeformAccelerator>& deformAccelerator,
			unsigned elementIdx, const std::string& modelScaffoldName)
		{
			_pendingDepVals.push_back(modelScaffold->GetDependencyValidation());
			_pendingDepVals.push_back(materialScaffold->GetDependencyValidation());

			RenderCore::Techniques::IDeformGeoAttachment* geoDeformerInfrastructure = nullptr;
			RenderCore::Techniques::IDeformUniformsAttachment* deformParametersAttachment = nullptr;
			DeformerToRendererBinding deformerBinding;
			if (deformAcceleratorPool && deformAccelerator) { 
				deformParametersAttachment = deformAcceleratorPool->GetDeformUniformsAttachment(*deformAccelerator).get();
				geoDeformerInfrastructure = deformAcceleratorPool->GetDeformGeoAttachment(*deformAccelerator).get();
				if (geoDeformerInfrastructure)
					deformerBinding = geoDeformerInfrastructure->GetDeformerToRendererBinding();
			}

			// there can be multiple cmd streams in a single model scaffold. We will load and interpret each one
			int maxTransformMarker = -1;
			for (auto cmdStreamGuid:modelScaffold->CollateCommandStreams()) {
				PendingCmdStream* dstCmdStream;
				auto existingCmdStream = std::find_if(_pendingCmdStreams.begin(), _pendingCmdStreams.end(), [cmdStreamGuid](const auto& q) { return q.first == cmdStreamGuid; });
				if (existingCmdStream == _pendingCmdStreams.end()) {
					_pendingCmdStreams.emplace_back(cmdStreamGuid, PendingCmdStream{});
					dstCmdStream = &_pendingCmdStreams.back().second;
				} else
					dstCmdStream = &existingCmdStream->second;

				{
					// BeginElement command
					auto cmdId = (uint32_t)Command::BeginElement, blockSize = 4u;
					dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&cmdId, (const uint8_t*)(&cmdId+1));
					dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&blockSize, (const uint8_t*)(&blockSize+1));
					dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&elementIdx, (const uint8_t*)(&elementIdx+1));
				}

				IteratorRange<const uint64_t*> currentMaterialAssignments;
				std::vector<std::pair<unsigned, unsigned>> modelGeoIdToPendingGeoIndex;
				std::optional<Float4x4> currentGeoSpaceToNodeSpace;
				for (auto cmd:modelScaffold->CommandStream(cmdStreamGuid)) {
					switch (cmd.Cmd()) {
					default:
						{
							if (cmd.Cmd() == (uint32_t)Assets::ModelCommand::SetMaterialAssignments) {
								currentMaterialAssignments = cmd.RawData().Cast<const uint64_t*>();
							} else if (cmd.Cmd() == (uint32_t)Assets::ModelCommand::SetTransformMarker) {
								maxTransformMarker = std::max(maxTransformMarker, (int)cmd.As<unsigned>());
							}

							auto cmdId = cmd.Cmd(), blockSize = cmd.BlockSize();
							dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&cmdId, (const uint8_t*)(&cmdId+1));
							dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&blockSize, (const uint8_t*)(&blockSize+1));
							dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)cmd.RawData().begin(), (const uint8_t*)cmd.RawData().end());
						}
						break;

					case (uint32_t)Assets::ModelCommand::GeoCall:
						{
							auto& geoCallDesc = cmd.As<Assets::GeoCallDesc>();
							auto geoMachine = modelScaffold->GetGeoMachine(geoCallDesc._geoId);
							assert(!geoMachine.empty());
							assert(!currentMaterialAssignments.empty());

							// Find the referenced geo object, and create the DrawableGeo object, etc
							unsigned pendingGeoIdx = ~0u;
							auto i = std::find_if(
								modelGeoIdToPendingGeoIndex.begin(), modelGeoIdToPendingGeoIndex.end(),
								[geoId=geoCallDesc._geoId](const auto& q) { return q.first == geoId; });
							if (i == modelGeoIdToPendingGeoIndex.end()) {
								pendingGeoIdx = _pendingGeos.AddGeo(
									geoMachine, modelScaffold,
									deformAccelerator,
									FindDeformerBinding(deformerBinding, elementIdx, geoCallDesc._geoId),
									modelScaffoldName);
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
								unsigned drawCallIterators[2] = {(unsigned)dstCmdStream->_drawCalls.size()};

								if (!Equivalent(rawGeometry->_geoSpaceToNodeSpace, Identity<Float4x4>(), 1e-3f)) {
									if (!currentGeoSpaceToNodeSpace.has_value() || currentGeoSpaceToNodeSpace.value() != rawGeometry->_geoSpaceToNodeSpace) {		// binary comparison intentional
										auto cmdId = (uint32_t)Command::SetGeoSpaceToNodeSpace, blockSize = (uint32_t)sizeof(Float4x4);
										dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&cmdId, (const uint8_t*)(&cmdId+1));
										dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&blockSize, (const uint8_t*)(&blockSize+1));
										dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&rawGeometry->_geoSpaceToNodeSpace, (const uint8_t*)(&rawGeometry->_geoSpaceToNodeSpace+1));
										currentGeoSpaceToNodeSpace = rawGeometry->_geoSpaceToNodeSpace;
									}
								} else if (currentGeoSpaceToNodeSpace.has_value()) {
									auto cmdId = (uint32_t)Command::SetGeoSpaceToNodeSpace, blockSize = (uint32_t)0;
									dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&cmdId, (const uint8_t*)(&cmdId+1));
									dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&blockSize, (const uint8_t*)(&blockSize+1));
									currentGeoSpaceToNodeSpace = {};
								}

								auto& pendingGeo = _pendingGeos._geos[pendingGeoIdx];
								unsigned materialIterator = 0;
								assert(rawGeometry->_drawCalls.size() == currentMaterialAssignments.size());
								for (const auto& dc:rawGeometry->_drawCalls) {
									// note -- there's some redundancy here, because we'll end up calling 
									// AddMaterial & MakePipeline over and over again for the same parameters. There's
									// some caching in those to precent allocating dupes, but it might still be more
									// efficient to avoid some of the redundancy
									assert(materialIterator < currentMaterialAssignments.size());
									auto matAssignment = currentMaterialAssignments[materialIterator++];
									auto* workingMaterial = _pendingPipelines.AddMaterial(
										materialScaffold->GetMaterialMachine(matAssignment),
										materialScaffold,
										elementIdx, matAssignment,
										deformAcceleratorPool.get(), deformParametersAttachment);
									auto compiledPipeline = _pendingPipelines.MakePipeline(
										*workingMaterial, 
										_pendingGeos._geosLayout[pendingGeoIdx],
										_pendingGeos._geosTopologies[pendingGeoIdx]);

									DrawCall drawCall;
									drawCall._drawableGeoIdx = pendingGeoIdx;
									drawCall._pipelineAcceleratorIdx = compiledPipeline._pipelineAcceleratorIdx;
									drawCall._descriptorSetAcceleratorIdx = workingMaterial->_descriptorSetAcceleratorIdx;
									drawCall._iaIdx = compiledPipeline._iaIdx;
									drawCall._batchFilter = workingMaterial->_batchFilter;
									drawCall._firstIndex = dc._firstIndex;
									drawCall._indexCount = dc._indexCount;
									drawCall._firstVertex = dc._firstVertex;

									if (cmdStreamGuid == s_topologicalCmdStream) {
										if (drawCall._batchFilter != (unsigned)Batch::Opaque) continue;		// drop this draw call
										drawCall._batchFilter = (unsigned)Batch::Topological;
									}

									dstCmdStream->_drawCalls.push_back(drawCall);
								}

								{
									// The ModelCommand::GeoCall cmd is not added to the translated command stream, but instead
									// we add a ExecuteDrawCalls command
									drawCallIterators[1] = (unsigned)dstCmdStream->_drawCalls.size();
									auto cmdId = (uint32_t)Command::ExecuteDrawCalls, blockSize = 8u;
									dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&cmdId, (const uint8_t*)(&cmdId+1));
									dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&blockSize, (const uint8_t*)(&blockSize+1));
									dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), (const uint8_t*)&drawCallIterators, (const uint8_t*)&drawCallIterators[2]);
								}
							}
						}
						break;
					}
				}
			}

			if (maxTransformMarker >= 0)
				AddBaseTransforms(*modelScaffold, elementIdx, maxTransformMarker+1);
		}

		void AddBaseTransforms(Assets::ModelScaffold& scaffold, unsigned elementIdx, unsigned transformMarkerCount)
		{
			// Record the embedded skeleton transform marker -> local transforms
			// these can be useful when using light weight renderers, because this is the last
			// bit of information required to use a model scaffold for basic rendering
			auto* embeddedSkeleton = scaffold.EmbeddedSkeleton();
			if (embeddedSkeleton) {
				Float4x4 skeleOutputTransforms[embeddedSkeleton->GetOutputMatrixCount()];
				embeddedSkeleton->GenerateOutputTransforms(MakeIteratorRange(skeleOutputTransforms, &skeleOutputTransforms[embeddedSkeleton->GetOutputMatrixCount()]));

				transformMarkerCount = std::min(transformMarkerCount, (unsigned)scaffold.FindCommandStreamInputInterface().size());
				size_t start = _pendingBaseTransforms.size();
				_pendingBaseTransforms.resize(start+transformMarkerCount, Identity<Float4x4>());

				// still have to do mapping from skeleton output to the command stream input interface
				Assets::SkeletonBinding skeleBinding{embeddedSkeleton->GetOutputInterface(), scaffold.FindCommandStreamInputInterface()};
				for (unsigned c=0; c<transformMarkerCount; ++c) {
					auto machineOutput = skeleBinding.ModelJointToMachineOutput(c);
					if (machineOutput < embeddedSkeleton->GetOutputMatrixCount()) {
						_pendingBaseTransforms[start+c] = skeleOutputTransforms[machineOutput];
					} else
						_pendingBaseTransforms[start+c] = Identity<Float4x4>();
				}
				_pendingBaseTransformsPerElement.emplace_back(elementIdx, transformMarkerCount);
			}
		}

		void FillIn(DrawableConstructor& dst)
		{
			unsigned geoIdxOffset = dst._drawableGeos.size();
			unsigned pipelineAcceleratorIdxOffset = dst._pipelineAccelerators.size();
			unsigned descSetAcceleratorIdxOffset = dst._descriptorSetAccelerators.size();
			unsigned iaIdxOffset = dst._drawableInputAssemblies.size();
			dst._drawableGeos.insert(dst._drawableGeos.end(), _pendingGeos._geos.begin(), _pendingGeos._geos.end());
			dst._pipelineAccelerators.insert(dst._pipelineAccelerators.end(), _pendingPipelines._pipelineAccelerators.begin(), _pendingPipelines._pipelineAccelerators.end());
			dst._descriptorSetAccelerators.insert(dst._descriptorSetAccelerators.end(), _pendingPipelines._descriptorSetAccelerators.begin(), _pendingPipelines._descriptorSetAccelerators.end());
			dst._drawableInputAssemblies.insert(dst._drawableInputAssemblies.end(), _pendingPipelines._pendingInputAssemblies.begin(), _pendingPipelines._pendingInputAssemblies.end());
			dst._baseTransforms.insert(dst._baseTransforms.end(), _pendingBaseTransforms.begin(), _pendingBaseTransforms.end());
			dst._baseTransformsPerElement.insert(dst._baseTransformsPerElement.end(), _pendingBaseTransformsPerElement.begin(), _pendingBaseTransformsPerElement.end());

			if (!dst._depVal) {
				std::vector<::Assets::DependencyValidationMarker> depValMarkers;
				depValMarkers.reserve(_pendingDepVals.size());
				for (const auto& d:_pendingDepVals) depValMarkers.push_back(d);
				std::sort(depValMarkers.begin(), depValMarkers.end());
				depValMarkers.erase(std::unique(depValMarkers.begin(), depValMarkers.end()), depValMarkers.end());
				dst._depVal = ::Assets::GetDepValSys().MakeOrReuse(depValMarkers);
			} else {
				for (const auto& d:_pendingDepVals)
					dst._depVal.RegisterDependency(d);
			}
			
			_pendingGeos = {};
			_pendingPipelines = {};
			_pendingDepVals.clear();
			_pendingBaseTransforms.clear();
			_pendingBaseTransformsPerElement.clear();

			// per-command-stream stuff --

			for (auto& srcCmdStream:_pendingCmdStreams) {
				auto dstCmdStream = std::find_if(dst._cmdStreams.begin(), dst._cmdStreams.end(), [guid=srcCmdStream.first](const auto& q) { return q._guid == guid; });
				if (dstCmdStream == dst._cmdStreams.end()) {
					dst._cmdStreams.emplace_back(CommandStream{srcCmdStream.first});
					dstCmdStream = dst._cmdStreams.end()-1;
				}

				unsigned drawCallIdxOffset = dstCmdStream->_drawCalls.size();
				for (auto& p:srcCmdStream.second._drawCalls) {
					p._drawableGeoIdx += geoIdxOffset;
					p._pipelineAcceleratorIdx += pipelineAcceleratorIdxOffset;
					p._descriptorSetAcceleratorIdx += descSetAcceleratorIdxOffset;
					p._iaIdx += iaIdxOffset;
				}
				dstCmdStream->_drawCalls.insert(dstCmdStream->_drawCalls.end(), srcCmdStream.second._drawCalls.begin(), srcCmdStream.second._drawCalls.end());

				// offset draw call indices in _pendingTranslatedCmdStream and append
				for (auto cmd:Assets::MakeScaffoldCmdRange(MakeIteratorRange(srcCmdStream.second._translatedCmdStream)))
					if (cmd.Cmd() == (uint32_t)Command::ExecuteDrawCalls) {
						auto range = cmd.RawData().Cast<const unsigned*>();
						for (auto& r:range) const_cast<unsigned&>(r) += drawCallIdxOffset;
					}
				dstCmdStream->_translatedCmdStream.insert(dstCmdStream->_translatedCmdStream.end(), srcCmdStream.second._translatedCmdStream.begin(), srcCmdStream.second._translatedCmdStream.end());

				// count up draw calls
				static_assert(dimof(dstCmdStream->_drawCallCounts) == (size_t)Batch::Max);
				for (auto& count:dstCmdStream->_drawCallCounts) count = 0;
				for (const auto& drawCall:dstCmdStream->_drawCalls)
					++dstCmdStream->_drawCallCounts[(unsigned)drawCall._batchFilter];
			}

			_pendingCmdStreams.clear();
			std::sort(dst._cmdStreams.begin(), dst._cmdStreams.begin(), [](const auto& lhs, const auto& rhs) { return lhs._guid < rhs._guid; });
		}

		Pimpl(std::shared_ptr<IDrawablesPool> drawablesPool, std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators)
		{
			_pendingPipelines._drawablesPool = drawablesPool;
			_pendingPipelines._pipelineAcceleratorPool = std::move(pipelineAccelerators);
			_pendingGeos._drawablesPool = std::move(drawablesPool);
		}

		~Pimpl()
		{}
	};

	void DrawableConstructor::Add(
		const ModelRendererConstruction& construction,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator)
	{
		assert(construction.GetAssetState() == ::Assets::AssetState::Ready);
		unsigned elementIdx = 0;
		for (auto e:construction) {
			auto modelScaffold = e.GetModelScaffold();
			auto materialScaffold = e.GetMaterialScaffold();
			if (modelScaffold && materialScaffold)
				_pimpl->AddModel(
					modelScaffold, materialScaffold,
					deformAcceleratorPool, deformAccelerator, 
					elementIdx, e.GetModelScaffoldName());
			++elementIdx;
		}
	}

	void DrawableConstructor::FulfillWhenNotPending(std::promise<std::shared_ptr<DrawableConstructor>>&& promise)
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
				strongThis->_pimpl->FillIn(*strongThis);
				auto cmdList = strongThis->_pimpl->_uploadFuture.get();
				strongThis->_completionCommandList = std::max(strongThis->_completionCommandList, cmdList);
				return strongThis;
			});
	}

	DrawableConstructor::DrawableConstructor(
		std::shared_ptr<IDrawablesPool> drawablesPool,
		std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators,
		BufferUploads::IManager& bufferUploads,
		const ModelRendererConstruction& construction,
		const std::shared_ptr<IDeformAcceleratorPool>& deformAcceleratorPool,
		const std::shared_ptr<DeformAccelerator>& deformAccelerator)
	{
		_completionCommandList = 0;
		_pimpl = std::make_unique<Pimpl>(std::move(drawablesPool), std::move(pipelineAccelerators));
		Add(construction, deformAcceleratorPool, deformAccelerator);
		std::promise<BufferUploads::CommandListID> uploadPromise;
		_pimpl->_uploadFuture = uploadPromise.get_future();
		_pimpl->_pendingGeos.LoadPendingStaticResources(std::move(uploadPromise), bufferUploads);
	}

	DrawableConstructor::~DrawableConstructor() {}

}}
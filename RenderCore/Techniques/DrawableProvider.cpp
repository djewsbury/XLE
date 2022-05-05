// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawableProvider.h"
#include "Drawables.h"
#include "DeformGeometryInfrastructure.h"
#include "PipelineAccelerator.h"
#include "../Assets/ModelMachine.h"
#include "../Assets/MaterialMachine.h"
#include "../Assets/ScaffoldCmdStream.h"

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
			std::vector<InputElementDesc> result = MakeIA(MakeIteratorRange(geo._staticVertexIA._elements), suppressed, 0);
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

			unsigned AddGeo(
				IteratorRange<Assets::ScaffoldCmdIterator> geoMachine,
				const std::shared_ptr<DeformAccelerator>& deformAccelerator,
				const DeformerToRendererBinding& deformerBinding,
				unsigned geoIdx)
			{
				std::vector<std::pair<unsigned, unsigned>> staticVBLoadRequests;
				unsigned staticVBIterator = 0;
				std::vector<std::pair<unsigned, unsigned>> staticIBLoadRequests;
				unsigned staticIBIterator = 0;

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

					case (uint32_t)Assets::GeoCommand::AttachGeometryBuffer:
						// handle geometry buffers somehow
						break;

					default:
						break;
					}
				}

				if (rawGeometry) {
					auto& rg = *rawGeometry;

					// Build the main non-deformed vertex stream
					auto drawableGeo = std::make_shared<Techniques::DrawableGeo>();
					drawableGeo->_vertexStreams[0]._vbOffset = staticVBIterator;
					// staticVBLoadRequests.push_back({rg._vb._offset, rg._vb._size});
					// staticVBIterator += rg._vb._size;
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
							drawableGeo->_vertexStreams[drawableGeo->_vertexStreamCount++]._vbOffset = staticVBIterator;
							// staticVBLoadRequests.push_back({skinningData->_animatedVertexElements._offset, skinningData->_animatedVertexElements._size});
							// staticVBIterator += skinningData->_animatedVertexElements._size;
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
					
					drawableGeo->_ibOffset = staticIBIterator;
					// staticIBLoadRequests.push_back({rg._ib._offset, rg._ib._size});
					// staticIBIterator += rg._ib._size;
					drawableGeo->_ibFormat = rg._indexFormat;
					_geos.push_back(std::move(drawableGeo));
					return (unsigned)_geos.size()-1;
				} else {
					assert(0);
					// expecting a raw geometry here somewhere
					return ~0u;
				}
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
				uint64_t materialGuid,
				Techniques::IDeformAcceleratorPool* deformAcceleratorPool,
				const IDeformParametersAttachment* parametersDeformInfrastructure)
			{
				auto i = std::lower_bound(_drawableMaterials.begin(), _drawableMaterials.end(), [materialGuid](const auto& q) { return q._guid < materialGuid; });
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
							auto* patchCollectionSrc = materialMachine.begin().Navigation()->GetShaderPatchCollection(id);
							if (patchCollectionSrc) {
								_depVals.insert(patchCollectionSrc->GetDependencyValidation());
								i->_patchCollection = DupeShaderPatchCollection(*patchCollectionSrc);
							}
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

			std::vector<std::pair<uint64_t, std::shared_ptr<Assets::ShaderPatchCollection>>> _dupedShaderPatchCollections;
			std::shared_ptr<Assets::ShaderPatchCollection> DupeShaderPatchCollection(const Assets::ShaderPatchCollection& src)
			{
				auto hash = src.GetHash();
				auto i = LowerBound(_dupedShaderPatchCollections, hash);
				if (i != _dupedShaderPatchCollections.end() && i->first == hash) {
					return i->second;
				} else {
					auto newPatchCollection = std::make_shared<Assets::ShaderPatchCollection>(src);
					_dupedShaderPatchCollections.insert(i, std::make_pair(hash, newPatchCollection));
					return newPatchCollection;
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

	class DrawableProvider
	{
	public:
		Internal::PipelineBuilder _pendingPipelines;
		Internal::DrawableGeoBuilder _pendingGeos;

		void AddModel(
			IteratorRange<Assets::ScaffoldCmdIterator> modelMachine,
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

			auto& navigation = *modelMachine.first.Navigation();
			IteratorRange<const uint64_t*> currentMaterialAssignments;
			unsigned currentTransformMarker = ~0u;

			auto* geoDeformerInfrastructure = dynamic_cast<IGeoDeformerInfrastructure*>(deformAcceleratorPool->GetDeformAttachment(*deformAccelerator).get());
			auto deformParametersAttachment = deformAcceleratorPool->GetDeformParametersAttachment(*deformAccelerator);
			DeformerToRendererBinding deformerBinding;
			if (deformAccelerator && geoDeformerInfrastructure)
				deformerBinding = geoDeformerInfrastructure->GetDeformerToRendererBinding();

			std::vector<std::pair<unsigned, unsigned>> modelGeoIdToPendingGeoIndex;

			for (auto cmd:modelMachine) {
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
						auto geoMachine = navigation.GetGeoMachine(geoCallDesc._geoId);
						assert(!geoMachine.empty());

						// Find the referenced geo object, and create the DrawableGeo object, etc
						unsigned pendingGeoIdx = ~0u;
						auto i = std::find_if(modelGeoIdToPendingGeoIndex.begin(), modelGeoIdToPendingGeoIndex.end(), [geoId=geoCallDesc._geoId](const auto& q) { return q.first == geoId; });
						if (i == modelGeoIdToPendingGeoIndex.end()) {
							pendingGeoIdx = _pendingGeos.AddGeo(
								geoMachine,
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
									navigation.GetMaterialMachine(matAssignment),
									matAssignment,
									deformAcceleratorPool.get(), deformParametersAttachment.get());
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

		DrawableProvider(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators)
		{
			_pendingPipelines._pipelineAcceleratorPool = std::move(pipelineAccelerators);
		}

		~DrawableProvider()
		{}
	};

}}
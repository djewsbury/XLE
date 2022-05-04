// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Drawables.h"
#include "DrawableDelegates.h"
#include "DrawablesInternal.h"
#include "Techniques.h"
#include "ParsingContext.h"
#include "PipelineAcceleratorInternal.h"
#include "DescriptorSetAccelerator.h"
#include "DeformAccelerator.h"
#include "BasicDelegates.h"
#include "CommonUtils.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"		// for DescriptorSetLayoutAndBinding
#include "../UniformsStream.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../../Assets/AsyncMarkerGroup.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/BitUtils.h"

namespace RenderCore { namespace Techniques
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	class RealExecuteDrawableContext
	{
	public:
		Metal::DeviceContext*				_metalContext;
		Metal::GraphicsEncoder_Optimized*	_encoder;
		const Metal::GraphicsPipeline*		_pipeline;
		const Metal::BoundUniforms*			_boundUniforms;
	};

	struct TemporaryStorageLocator
	{
		IResource* _res = nullptr;
		size_t _begin = 0, _end = 0;
	};

	constexpr unsigned s_uniformGroupSequencer = 0;
	constexpr unsigned s_uniformGroupMaterial = 1;
	constexpr unsigned s_uniformGroupDraw = 2;
	static const auto s_materialDescSetName = Hash64("Material");

	struct PreStalledResources
	{
		std::vector<std::shared_ptr<::Assets::Marker<IPipelineAcceleratorPool::Pipeline>>> _pendingPipelineMarkers;
		std::vector<std::shared_ptr<::Assets::Marker<ActualizedDescriptorSet>>> _pendingDescriptorSetMarkers;

		void Setup(const IPipelineAcceleratorPool& pipelineAccelerators, const SequencerConfig& sequencerConfig, const DrawablesPacket& drawablePkt)
		{
			_pendingPipelineMarkers.resize(drawablePkt._drawables.size());
			_pendingDescriptorSetMarkers.resize(drawablePkt._drawables.size());

			bool stallOnMarkers = false;
			unsigned idx=0;
			PipelineAccelerator* lastPipelineAccelerator = nullptr;
			for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d, ++idx) {
				const auto& drawable = *(Drawable*)d.get();
				if (drawable._pipeline.get() != lastPipelineAccelerator) {
					auto* pipeline = pipelineAccelerators.TryGetPipeline(*drawable._pipeline, sequencerConfig);
					if (!pipeline) {
						_pendingPipelineMarkers[idx] = pipelineAccelerators.GetPipelineMarker(*drawable._pipeline, sequencerConfig);
						stallOnMarkers = true;
					}
					lastPipelineAccelerator = drawable._pipeline.get();
				}
				if (drawable._descriptorSet) {
					auto* actualizedDescSet = pipelineAccelerators.TryGetDescriptorSet(*drawable._descriptorSet);
					if (!actualizedDescSet) {
						_pendingDescriptorSetMarkers[idx] = pipelineAccelerators.GetDescriptorSetMarker(*drawable._descriptorSet);
						stallOnMarkers = true;
					}
				}
			}

			// we should avoid being in the lock while stalling for these resources -- since this can lock up other threads
			pipelineAccelerators.UnlockForReading();
			for (const auto& c:_pendingPipelineMarkers) if (c) c->StallWhilePending();
			for (const auto& c:_pendingDescriptorSetMarkers) if (c) c->StallWhilePending();
			pipelineAccelerators.LockForReading();
		}
	};

	IDeformAcceleratorPool* g_hack_deformAccelerators = nullptr;
	RenderCore::Metal_Vulkan::TemporaryStorageResourceMap AllocateFromDynamicPageResource(IDeformAcceleratorPool& accelerators, unsigned bytes);

	template<bool UsePreStalledResources>
		static void Draw(
			RenderCore::Metal::DeviceContext& metalContext,
			RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
			ParsingContext& parserContext,
			const IPipelineAcceleratorPool& pipelineAccelerators,
			const SequencerConfig& sequencerConfig,
			const DrawablesPacket& drawablePkt,
			const TemporaryStorageLocator& temporaryVB, 
			const TemporaryStorageLocator& temporaryIB,
			PreStalledResources& preStalledResources)
	{
		auto& uniformDelegateMan = *parserContext.GetUniformDelegateManager();
		uniformDelegateMan.InvalidateUniforms();
		uniformDelegateMan.BringUpToDateGraphics(parserContext);

		const UniformsStreamInterface& globalUSI = uniformDelegateMan.GetInterface();

		auto& deformAccelerators = *g_hack_deformAccelerators;
		
		UniformsStreamInterface materialUSI;
		materialUSI.BindFixedDescriptorSet(0, s_materialDescSetName);
		if (parserContext._extraSequencerDescriptorSet.second)
			materialUSI.BindFixedDescriptorSet(1, parserContext._extraSequencerDescriptorSet.first);

		UniformsStreamInterface emptyUSI;
		DrawableGeo* currentGeo = nullptr;
		PipelineAccelerator* currentPipelineAccelerator = nullptr;
		const IPipelineAcceleratorPool::Pipeline* currentPipeline = nullptr;
		uint64_t currentSequencerUniformRules = 0;
		UniformsStreamInterface* currentLooseUniformsInterface = nullptr;
		Metal::BoundUniforms* currentBoundUniforms = nullptr;
		unsigned idx = 0;

		Metal::CapturedStates capturedStates;
		encoder.BeginStateCapture(capturedStates);

		unsigned pipelineLookupCount = 0;
		unsigned boundUniformLookupCount = 0;
		unsigned fullDescSetCount = 0;
		unsigned justMatDescSetCount = 0;
		unsigned executeCount = 0;
		RenderCore::Metal_Vulkan::TemporaryStorageResourceMap dynamicPageResourceWorkingAllocation;
		const auto dynamicPageResourceAlignment = deformAccelerators.GetDynamicPageResourceAlignment();
		unsigned dynamicPageMovingGPUOffset = 0, dynamicPageMovingGPUEnd = 0;

		TRY {
			for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d, ++idx) {
				const auto& drawable = *(Drawable*)d.get();
				assert(drawable._pipeline);
				if (drawable._pipeline.get() != currentPipelineAccelerator) {
					auto* pipeline = pipelineAccelerators.TryGetPipeline(*drawable._pipeline, sequencerConfig);
					if (UsePreStalledResources && !pipeline && preStalledResources._pendingPipelineMarkers[idx])
						pipeline = &preStalledResources._pendingPipelineMarkers[idx]->ActualizeBkgrnd();
					if (!pipeline) continue;

					currentPipeline = pipeline;
					currentPipelineAccelerator = drawable._pipeline.get();

					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						globalUSI, materialUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface.get() : &emptyUSI));
					currentLooseUniformsInterface = drawable._looseUniformsInterface.get();
					++boundUniformLookupCount;
					++pipelineLookupCount;
				} else if (currentLooseUniformsInterface != drawable._looseUniformsInterface.get()) {
					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						globalUSI, materialUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface.get() : &emptyUSI));
					currentLooseUniformsInterface = drawable._looseUniformsInterface.get();
					++boundUniformLookupCount;
				}

				const ActualizedDescriptorSet* matDescSet = nullptr;
				if (drawable._descriptorSet) {
					matDescSet = pipelineAccelerators.TryGetDescriptorSet(*drawable._descriptorSet);
					if (UsePreStalledResources && !matDescSet && preStalledResources._pendingDescriptorSetMarkers[idx])
						matDescSet = &preStalledResources._pendingDescriptorSetMarkers[idx]->ActualizeBkgrnd();
					if (!matDescSet) continue;
					parserContext.RequireCommandList(matDescSet->GetCompletionCommandList());
				}

				////////////////////////////////////////////////////////////////////////////// 
			
				VertexBufferView vbv[4];
				if (drawable._geo.get() != currentGeo && drawable._geo.get()) {
					for (unsigned c=0; c<drawable._geo->_vertexStreamCount; ++c) {
						auto& stream = drawable._geo->_vertexStreams[c];
						if (stream._type == DrawableGeo::StreamType::Resource) {
							vbv[c]._resource = stream._resource.get();
							vbv[c]._offset = stream._vbOffset;
						} else if (stream._type == DrawableGeo::StreamType::Deform) {
							assert(drawable._geo->_deformAccelerator);
							// todo -- we can make GetOutputVBV() accessible without the pool and avoid the need for a pool passed to this function
							auto deformVbv = Techniques::Internal::GetOutputVBV(*drawable._geo->_deformAccelerator, drawable._deformInstanceIdx);
							vbv[c]._resource = deformVbv._resource;
							vbv[c]._offset = stream._vbOffset + deformVbv._offset;
						} else {
							assert(stream._type == DrawableGeo::StreamType::PacketStorage);
							vbv[c]._resource = temporaryVB._res;
							vbv[c]._offset = stream._vbOffset + temporaryVB._begin;
						}
					}

					if (drawable._geo->_ibFormat != Format(0)) {
						if (drawable._geo->_ibStreamType == DrawableGeo::StreamType::Resource) {
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{drawable._geo->_ib.get(), drawable._geo->_ibFormat, drawable._geo->_ibOffset});
						} else {
							assert(drawable._geo->_ibStreamType == DrawableGeo::StreamType::PacketStorage);
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{temporaryIB._res, drawable._geo->_ibFormat, unsigned(drawable._geo->_ibOffset + temporaryIB._begin)});
						}
					} else {
						encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{});
					}
					currentGeo = drawable._geo.get();
				}

				//////////////////////////////////////////////////////////////////////////////

				if (currentBoundUniforms->GetGroupRulesHash(0) != currentSequencerUniformRules) {
					ApplyUniformsGraphics(uniformDelegateMan, metalContext, encoder, parserContext, *currentBoundUniforms, s_uniformGroupSequencer);
					currentSequencerUniformRules = currentBoundUniforms->GetGroupRulesHash(0);
					++fullDescSetCount;
				} 
				{
					if (matDescSet) {
						auto dynamicSize = Internal::GetDynamicPageResourceSize(*matDescSet);
						if (dynamicSize) {
							
							// if it fits in the existing block, go with that; otherwise allocate a new block
							unsigned preAlign = CeilToMultiple((size_t)dynamicPageMovingGPUOffset, dynamicPageResourceAlignment) - (size_t)dynamicPageMovingGPUOffset;
							if ((dynamicPageMovingGPUOffset+preAlign+dynamicSize) > dynamicPageMovingGPUEnd) {
								const unsigned defaultBlockSize = 16*1024;
								dynamicPageResourceWorkingAllocation = AllocateFromDynamicPageResource(deformAccelerators, std::max(dynamicSize, defaultBlockSize));
								dynamicPageMovingGPUOffset = 0;
								dynamicPageMovingGPUEnd = dynamicPageMovingGPUOffset + dynamicPageResourceWorkingAllocation.GetData().size();
								preAlign = 0;
							}

							// deal with alignments and allocate our space from the allocated block
							IteratorRange<void*> dynamicPageBufferSpace;
							dynamicPageBufferSpace.first = PtrAdd(dynamicPageResourceWorkingAllocation.GetData().begin(), dynamicPageMovingGPUOffset+preAlign);
							dynamicPageBufferSpace.second = PtrAdd(dynamicPageBufferSpace.first, dynamicSize);
							unsigned dynamicOffset = dynamicPageResourceWorkingAllocation.GetBeginAndEndInResource().first+dynamicPageMovingGPUOffset+preAlign;
							assert((dynamicOffset % dynamicPageResourceAlignment) == 0);
							dynamicPageMovingGPUOffset += preAlign+dynamicSize;

							assert(drawable._geo->_deformAccelerator);
							Internal::PrepareDynamicPageResource(
								*matDescSet,
								Internal::GetOutputParameterState(*drawable._geo->_deformAccelerator, drawable._deformInstanceIdx),
								dynamicPageBufferSpace);
							currentBoundUniforms->ApplyDescriptorSet(metalContext, encoder, *matDescSet->GetDescriptorSet(), s_uniformGroupMaterial, 0, MakeIteratorRange(&dynamicOffset, &dynamicOffset+1));
						} else {
							unsigned dynamicOffset = 0;
							currentBoundUniforms->ApplyDescriptorSet(metalContext, encoder, *matDescSet->GetDescriptorSet(), s_uniformGroupMaterial, 0, MakeIteratorRange(&dynamicOffset, &dynamicOffset+1));
						}
					}
					if (parserContext._extraSequencerDescriptorSet.second)
						currentBoundUniforms->ApplyDescriptorSet(metalContext, encoder, *parserContext._extraSequencerDescriptorSet.second, s_uniformGroupMaterial, 1);
					++justMatDescSetCount;
				}

				//////////////////////////////////////////////////////////////////////////////

				RealExecuteDrawableContext drawFnContext { &metalContext, &encoder, currentPipeline->_metalPipeline.get(), currentBoundUniforms };
				drawable._drawFn(parserContext, *(ExecuteDrawableContext*)&drawFnContext, drawable);
				++executeCount;
			}
		} CATCH (...) {
			encoder.EndStateCapture();
			throw;
		} CATCH_END

		encoder.EndStateCapture();
	}

	void Draw(
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const DrawOptions& drawOptions)
	{
		TemporaryStorageLocator temporaryVB, temporaryIB;
		if (!drawablePkt.GetStorage(DrawablesPacket::Storage::Vertex).empty()) {
			auto srcData = drawablePkt.GetStorage(DrawablesPacket::Storage::Vertex);
			auto mappedData = metalContext.MapTemporaryStorage(srcData.size(), BindFlag::VertexBuffer);
			assert(mappedData.GetData().size() == srcData.size());
			std::memcpy(mappedData.GetData().begin(), srcData.begin(), srcData.size());
			temporaryVB = { mappedData.GetResource().get(), mappedData.GetBeginAndEndInResource().first, mappedData.GetBeginAndEndInResource().second };
		}
		if (!drawablePkt.GetStorage(DrawablesPacket::Storage::Index).empty()) {
			auto srcData = drawablePkt.GetStorage(DrawablesPacket::Storage::Index);
			auto mappedData = metalContext.MapTemporaryStorage(srcData.size(), BindFlag::IndexBuffer);
			assert(mappedData.GetData().size() == srcData.size());
			std::memcpy(mappedData.GetData().begin(), srcData.begin(), srcData.size());
			temporaryIB = { mappedData.GetResource().get(), mappedData.GetBeginAndEndInResource().first, mappedData.GetBeginAndEndInResource().second };
		}
		assert(drawablePkt.GetStorage(DrawablesPacket::Storage::Uniform).empty());

		PreStalledResources preStalledResources;
		if (drawOptions._stallForResources) {
			preStalledResources.Setup(pipelineAccelerators, sequencerConfig, drawablePkt);
			Draw<true>(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, drawablePkt, temporaryVB, temporaryIB, preStalledResources);
		} else {
			Draw<false>(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, drawablePkt, temporaryVB, temporaryIB, preStalledResources);
		}
	}

	void Draw(
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const DrawOptions& drawOptions)
	{
		pipelineAccelerators.LockForReading();
		TRY {
			auto& metalContext = *Metal::DeviceContext::Get(parserContext.GetThreadContext());
			auto pipelineLayout = pipelineAccelerators.TryGetCompiledPipelineLayout(sequencerConfig);
			if (!pipelineLayout) {
				pipelineAccelerators.UnlockForReading();
				return;
			}
			auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
			auto viewport = parserContext.GetViewport();
			ScissorRect scissorRect { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
			encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));
			Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, drawablePkt, drawOptions);
		} CATCH (...) {
			pipelineAccelerators.UnlockForReading();
			throw;
		} CATCH_END
		pipelineAccelerators.UnlockForReading();
	}

	static const std::string s_graphicsPipeline { "graphics-pipeline" };
	static const std::string s_descriptorSet { "descriptor-set" };

	std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt)
	{
		std::shared_ptr<::Assets::AsyncMarkerGroup> result;

		for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d) {
			const auto& drawable = *(Drawable*)d.get();
			assert(drawable._pipeline);
			auto pipelineFuture = pipelineAccelerators.GetPipelineMarker(*drawable._pipeline, sequencerConfig);
			if (pipelineFuture && pipelineFuture->GetAssetState() != ::Assets::AssetState::Ready) {
				if (!result)
					result = std::make_shared<::Assets::AsyncMarkerGroup>();
				result->Add(pipelineFuture, s_graphicsPipeline);
			}

			if (drawable._descriptorSet) {
				auto descriptorSetFuture = pipelineAccelerators.GetDescriptorSetMarker(*drawable._descriptorSet);
				if (descriptorSetFuture && descriptorSetFuture->GetAssetState() != ::Assets::AssetState::Ready) {
					if (!result)
						result = std::make_shared<::Assets::AsyncMarkerGroup>();
					result->Add(descriptorSetFuture, s_descriptorSet);
				}
			}
		}

		return result;
	}

	void ExecuteDrawableContext::ApplyLooseUniforms(const UniformsStream& stream) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyLooseUniforms(*realContext._metalContext, *realContext._encoder, stream, s_uniformGroupDraw);
	}

	void ExecuteDrawableContext::ApplyDescriptorSets(IteratorRange<const IDescriptorSet* const*> descSets) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyDescriptorSets(*realContext._metalContext, *realContext._encoder, descSets, s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseImmediateDatas() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseImmediateDatas(s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseResources() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseResources(s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseSamplers() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseSamplers(s_uniformGroupDraw);
	}

	bool ExecuteDrawableContext::AtLeastOneBoundLooseUniform() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return (realContext._boundUniforms->GetBoundLooseImmediateDatas(s_uniformGroupDraw) | realContext._boundUniforms->GetBoundLooseResources(s_uniformGroupDraw) | realContext._boundUniforms->GetBoundLooseSamplers(s_uniformGroupDraw)) != 0;
	}

	void ExecuteDrawableContext::Draw(unsigned vertexCount, unsigned startVertexLocation) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->Draw(*realContext._pipeline, vertexCount, startVertexLocation);
	}

	void ExecuteDrawableContext::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation) const
	{
		assert(baseVertexLocation == 0);		// parameter deprecated
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndexed(*realContext._pipeline, indexCount, startIndexLocation);
	}

	void ExecuteDrawableContext::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->DrawInstances(*realContext._pipeline, vertexCount, instanceCount, startVertexLocation);
	}

	void ExecuteDrawableContext::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation) const
	{
		assert(baseVertexLocation == 0);		// parameter deprecated
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndexedInstances(*realContext._pipeline, indexCount, instanceCount, startIndexLocation);
	}

	void ExecuteDrawableContext::DrawAuto() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->DrawAuto(*realContext._pipeline);
	}

	void ExecuteDrawableContext::SetStencilRef(unsigned frontFaceStencil, unsigned backFaceStencil) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._encoder->SetStencilRef(frontFaceStencil, backFaceStencil);
	}

	static DrawablesPacket::AllocateStorageResult AllocateFrom(std::vector<uint8_t>& vector, size_t size, unsigned alignment)
	{
		unsigned preAlignmentBuffer = 0;
		if (alignment != 0) {
			preAlignmentBuffer = alignment - (vector.size() % alignment);
			if (preAlignmentBuffer == alignment) preAlignmentBuffer = 0;
		}

		assert(vector.size() + preAlignmentBuffer + size < 10 * 1024 * 1024);

		size_t startOffset = vector.size() + preAlignmentBuffer;
		vector.resize(vector.size() + preAlignmentBuffer + size);
		return {
			MakeIteratorRange(AsPointer(vector.begin() + startOffset), AsPointer(vector.begin() + startOffset + size)),
			(unsigned)startOffset
		};
	}

	auto DrawablesPacket::AllocateStorage(Storage storageType, size_t size) -> AllocateStorageResult
	{
		if (storageType == Storage::Index) {
			return AllocateFrom(_ibStorage, size, _storageAlignment);
		} else {
			assert(storageType == Storage::Vertex);
			return AllocateFrom(_vbStorage, size, _storageAlignment);
		}
	}

	IteratorRange<const void*> DrawablesPacket::GetStorage(Storage storageType) const
	{
		if (storageType == Storage::Index) {
			return MakeIteratorRange(_ibStorage);
		} else if (storageType == Storage::Uniform) {
			return MakeIteratorRange(_ubStorage);
		} else {
			assert(storageType == Storage::Vertex);
			return MakeIteratorRange(_vbStorage);
		}
	}

	DrawablesPacket::DrawablesPacket() {}
	DrawablesPacket::DrawablesPacket(DrawablesPacketPool& pool)
	: _pool(&pool) {}
	DrawablesPacket::~DrawablesPacket()
	{
		if (_pool) {
			Reset();
			_pool->ReturnToPool(std::move(*this));
		}
	}
	DrawablesPacket::DrawablesPacket(DrawablesPacket&& moveFrom) never_throws
	: _drawables(std::move(moveFrom._drawables))
	, _vbStorage(std::move(moveFrom._vbStorage))
	, _ibStorage(std::move(moveFrom._ibStorage))
	{
		_pool = moveFrom._pool;
		moveFrom._pool = nullptr;
		_storageAlignment = moveFrom._storageAlignment;
	}
	DrawablesPacket& DrawablesPacket::operator=(DrawablesPacket&& moveFrom) never_throws
	{
		if (&moveFrom == this)
			return *this;

		if (_pool)
			_pool->ReturnToPool(std::move(*this));

		_drawables = std::move(moveFrom._drawables);
		_vbStorage = std::move(moveFrom._vbStorage);
		_ibStorage = std::move(moveFrom._ibStorage);
		_pool = moveFrom._pool;
		moveFrom._pool = nullptr;
		_storageAlignment = moveFrom._storageAlignment;
		return *this;
	}

	DrawablesPacket DrawablesPacketPool::Allocate()
	{
		ScopedLock(_lock);
		if (_availablePackets.empty())
			return DrawablesPacket(*this);
		auto res = std::move(*(_availablePackets.end()-1));
		_availablePackets.erase(_availablePackets.end()-1);
		return res;
	}

	void DrawablesPacketPool::ReturnToPool(DrawablesPacket&& pkt)
	{
		assert(pkt._drawables.empty() && pkt._vbStorage.empty() && pkt._ibStorage.empty());
		ScopedLock(_lock);
		_availablePackets.push_back(std::move(pkt));
	}

	DrawablesPacketPool::DrawablesPacketPool()
	{
		_availablePackets.reserve(8);
	}

	DrawablesPacketPool::~DrawablesPacketPool()
	{
		// ensure packets in _availablePackets don't try to call ReturnToPool when shutting down
		for (auto& p:_availablePackets)
			p._pool = nullptr;
	}


	DrawableInputAssembly::DrawableInputAssembly(
		IteratorRange<const InputElementDesc*> inputElements,
		Topology topology)
	{
		_inputElements = NormalizeInputAssembly(inputElements);
		_strides = CalculateVertexStrides(_inputElements);
		_topology = topology;
		_hash = rotl64(HashInputAssembly(MakeIteratorRange(_inputElements), DefaultSeed64), (unsigned)_topology);
	}

}}

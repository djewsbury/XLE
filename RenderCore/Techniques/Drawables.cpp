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
#include "BasicDelegates.h"
#include "CommonUtils.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"		// for DescriptorSetLayoutAndBinding
#include "../UniformsStream.h"
#include "../BufferView.h"
#include "../IThreadContext.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../../Assets/AsyncMarkerGroup.h"
#include "../../Utility/ArithmeticUtils.h"

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

	static void Draw(
		RenderCore::Metal::DeviceContext& metalContext,
        RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
		ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		SequencerUniformsHelper& uniformsHelper,
		const DrawablesPacket& drawablePkt,
		const TemporaryStorageLocator& temporaryVB, 
		const TemporaryStorageLocator& temporaryIB)
	{
		const auto sequencerDescriptorSetSlot = 0;
		const auto materialDescriptorSetSlot = 1;
		static const auto sequencerDescSetName = Hash64("Sequencer");
		static const auto materialDescSetName = Hash64("Material");
		
		auto sequencerDescriptorSet = uniformsHelper.CreateDescriptorSet(
			*pipelineAccelerators.GetDevice(), parserContext);

		UniformsStreamInterface sequencerUSI = uniformsHelper.GetLooseUniformsStreamInterface();
		auto matDescSetLayout = pipelineAccelerators.GetMaterialDescriptorSetLayout().GetLayout()->MakeDescriptorSetSignature(&parserContext.GetTechniqueContext()._commonResources->_samplerPool);
		sequencerUSI.BindFixedDescriptorSet(0, sequencerDescSetName, &sequencerDescriptorSet.second);
		sequencerUSI.BindFixedDescriptorSet(1, materialDescSetName, &matDescSetLayout);
		if (parserContext._extraSequencerDescriptorSet.second)
			sequencerUSI.BindFixedDescriptorSet(2, parserContext._extraSequencerDescriptorSet.first);

		UniformsStreamInterface emptyUSI;
		DrawableGeo* currentGeo = nullptr;
		PipelineAccelerator* currentPipelineAccelerator = nullptr;
		const IPipelineAcceleratorPool::Pipeline* currentPipeline = nullptr;
		uint64_t currentSequencerUniformRules = 0;
		UniformsStreamInterface* currentLooseUniformsInterface = nullptr;
		Metal::BoundUniforms* currentBoundUniforms = nullptr;

		Metal::CapturedStates capturedStates;
		encoder.BeginStateCapture(capturedStates);

		unsigned pipelineLookupCount = 0;
		unsigned boundUniformLookupCount = 0;
		unsigned applyLooseCount = 0;
		unsigned fullDescSetCount = 0;
		unsigned justMatDescSetCount = 0;
		unsigned executeCount = 0;

		TRY {
			for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d) {
				const auto& drawable = *(Drawable*)d.get();
				assert(drawable._pipeline);
				if (drawable._pipeline.get() != currentPipelineAccelerator) {
					auto* pipeline = pipelineAccelerators.TryGetPipeline(*drawable._pipeline, sequencerConfig);
					if (!pipeline)
						continue;

					currentPipeline = pipeline;
					currentPipelineAccelerator = drawable._pipeline.get();

					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						sequencerUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface.get() : &emptyUSI));
					currentLooseUniformsInterface = drawable._looseUniformsInterface.get();
					++boundUniformLookupCount;
					++pipelineLookupCount;
				} else if (currentLooseUniformsInterface != drawable._looseUniformsInterface.get()) {
					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						sequencerUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface.get() : &emptyUSI));
					currentLooseUniformsInterface = drawable._looseUniformsInterface.get();
					++boundUniformLookupCount;
				}

				const IDescriptorSet* matDescSet = nullptr;
				if (drawable._descriptorSet) {
					auto* actualizedDescSet = pipelineAccelerators.TryGetDescriptorSet(*drawable._descriptorSet);
					if (!actualizedDescSet)
						continue;
					matDescSet = actualizedDescSet->GetDescriptorSet().get();
					parserContext.RequireCommandList(actualizedDescSet->GetCompletionCommandList());
				}

				////////////////////////////////////////////////////////////////////////////// 
			
				VertexBufferView vbv[4];
				if (drawable._geo.get() != currentGeo && drawable._geo.get()) {
					for (unsigned c=0; c<drawable._geo->_vertexStreamCount; ++c) {
						auto& stream = drawable._geo->_vertexStreams[c];
						if (stream._resource) {
							vbv[c]._resource = stream._resource.get();
							vbv[c]._offset = stream._vbOffset;
						} else {
							vbv[c]._resource = temporaryVB._res;
							vbv[c]._offset = stream._vbOffset + temporaryVB._begin;
						}
					}

					if (drawable._geo->_ibFormat != Format(0)) {
						if (drawable._geo->_ib) {
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{drawable._geo->_ib.get(), drawable._geo->_ibFormat});
						} else {
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{temporaryIB._res, drawable._geo->_ibFormat, unsigned(drawable._geo->_dynIBBegin + temporaryIB._begin)});
						}
					} else {
						encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{});
					}
					currentGeo = drawable._geo.get();
				}

				//////////////////////////////////////////////////////////////////////////////

				if (currentBoundUniforms->GetGroupRulesHash(0) != currentSequencerUniformRules) {
					const IDescriptorSet* descriptorSets[3];
					descriptorSets[0] = sequencerDescriptorSet.first.get();
					descriptorSets[1] = matDescSet;
					descriptorSets[2] = parserContext._extraSequencerDescriptorSet.second;
					currentBoundUniforms->ApplyDescriptorSets(
						metalContext, encoder,
						MakeIteratorRange(descriptorSets), 0);
					if (__builtin_expect(currentBoundUniforms->GetBoundLooseImmediateDatas(0) | currentBoundUniforms->GetBoundLooseResources(0) | currentBoundUniforms->GetBoundLooseResources(0), 0ull)) {
						ApplyLooseUniforms(uniformsHelper, metalContext, encoder, parserContext, *currentBoundUniforms, 0);
						++applyLooseCount;
					}
					currentSequencerUniformRules = currentBoundUniforms->GetGroupRulesHash(0);
					++fullDescSetCount;
				} else {
					// When the shader interface hasn't changed, we'll set just the material descriptor set
					currentBoundUniforms->ApplyDescriptorSet(metalContext, encoder, *matDescSet, 0, 1);
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
		SequencerUniformsHelper& uniformsHelper,
		const DrawablesPacket& drawablePkt)
	{
		TemporaryStorageLocator temporaryVB, temporaryIB;
		if (!drawablePkt.GetStorage(DrawablesPacket::Storage::VB).empty()) {
			auto srcData = drawablePkt.GetStorage(DrawablesPacket::Storage::VB);
			auto mappedData = metalContext.MapTemporaryStorage(srcData.size(), BindFlag::VertexBuffer);
			assert(mappedData.GetData().size() == srcData.size());
			std::memcpy(mappedData.GetData().begin(), srcData.begin(), srcData.size());
			temporaryVB = { mappedData.GetResource().get(), mappedData.GetBeginAndEndInResource().first, mappedData.GetBeginAndEndInResource().second };
		}
		if (!drawablePkt.GetStorage(DrawablesPacket::Storage::IB).empty()) {
			auto srcData = drawablePkt.GetStorage(DrawablesPacket::Storage::IB);
			auto mappedData = metalContext.MapTemporaryStorage(srcData.size(), BindFlag::IndexBuffer);
			assert(mappedData.GetData().size() == srcData.size());
			std::memcpy(mappedData.GetData().begin(), srcData.begin(), srcData.size());
			temporaryIB = { mappedData.GetResource().get(), mappedData.GetBeginAndEndInResource().first, mappedData.GetBeginAndEndInResource().second };
		}

		Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, uniformsHelper, drawablePkt, temporaryVB, temporaryIB);
	}

	void Draw(
		IThreadContext& context,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		SequencerUniformsHelper& uniformsHelper,
		const DrawablesPacket& drawablePkt)
	{
		pipelineAccelerators.LockForReading();
		TRY {
			auto& metalContext = *Metal::DeviceContext::Get(context);
			auto pipelineLayout = pipelineAccelerators.TryGetCompiledPipelineLayout(sequencerConfig);
			if (!pipelineLayout) {
				pipelineAccelerators.UnlockForReading();
				return;
			}
			auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
			auto viewport = parserContext.GetViewport();
			ScissorRect scissorRect { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
			encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));
			Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, uniformsHelper, drawablePkt);
		} CATCH (...) {
			pipelineAccelerators.UnlockForReading();
			throw;
		} CATCH_END
		pipelineAccelerators.UnlockForReading();
	}

	void Draw(
		IThreadContext& context,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt)
	{
		pipelineAccelerators.LockForReading();
		TRY {
			auto& metalContext = *Metal::DeviceContext::Get(context);
			auto pipelineLayout = pipelineAccelerators.TryGetCompiledPipelineLayout(sequencerConfig);
			if (!pipelineLayout) {
				pipelineAccelerators.UnlockForReading();
				return;
			}
			auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
			auto viewport = parserContext.GetViewport();
			ScissorRect scissorRect { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
			encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));
			SequencerUniformsHelper uniformsHelper { parserContext };
			Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, uniformsHelper, drawablePkt);
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
			auto pipelineFuture = pipelineAccelerators.GetPipelineFuture(*drawable._pipeline, sequencerConfig);
			if (pipelineFuture && pipelineFuture->GetAssetState() != ::Assets::AssetState::Ready) {
				if (!result)
					result = std::make_shared<::Assets::AsyncMarkerGroup>();
				result->Add(pipelineFuture, s_graphicsPipeline);
			}

			if (drawable._descriptorSet) {
				auto descriptorSetFuture = pipelineAccelerators.GetDescriptorSetFuture(*drawable._descriptorSet);
				if (descriptorSetFuture && descriptorSetFuture->GetAssetState() != ::Assets::AssetState::Ready) {
					if (!result)
						result = std::make_shared<::Assets::AsyncMarkerGroup>();
					result->Add(pipelineFuture, s_descriptorSet);
				}
			}
		}

		return result;
	}

	void ExecuteDrawableContext::ApplyLooseUniforms(const UniformsStream& stream) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyLooseUniforms(*realContext._metalContext, *realContext._encoder, stream, 1);
	}

	void ExecuteDrawableContext::ApplyDescriptorSets(IteratorRange<const IDescriptorSet* const*> descSets) const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyDescriptorSets(*realContext._metalContext, *realContext._encoder, descSets, 1);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseImmediateDatas() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseImmediateDatas(1);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseResources() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseResources(1);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseSamplers() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseSamplers(1);
	}

	bool ExecuteDrawableContext::AtLeastOneBoundLooseUniform() const
	{
		auto& realContext = *(RealExecuteDrawableContext*)this;
		return (realContext._boundUniforms->GetBoundLooseImmediateDatas(1) | realContext._boundUniforms->GetBoundLooseResources(1) | realContext._boundUniforms->GetBoundLooseSamplers(1)) != 0;
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
		if (storageType == Storage::IB) {
			return AllocateFrom(_ibStorage, size, _storageAlignment);
		} else {
			assert(storageType == Storage::VB);
			return AllocateFrom(_vbStorage, size, _storageAlignment);
		}
	}

	IteratorRange<const void*> DrawablesPacket::GetStorage(Storage storageType) const
	{
		if (storageType == Storage::IB) {
			return MakeIteratorRange(_ibStorage);
		} else {
			assert(storageType == Storage::VB);
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

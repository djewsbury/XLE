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

		for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d) {
			const auto& drawable = *(Drawable*)d.get();
			assert(drawable._pipeline);
			auto* pipeline = pipelineAccelerators.TryGetPipeline(*drawable._pipeline, sequencerConfig);
			if (!pipeline)
				continue;

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
			if (drawable._geo) {
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
			}

			//////////////////////////////////////////////////////////////////////////////

			auto& boundUniforms = pipeline->_boundUniformsPool.Get(
				*pipeline->_metalPipeline,
				sequencerUSI,
				drawable._looseUniformsInterface ? *drawable._looseUniformsInterface : UniformsStreamInterface{});

			const IDescriptorSet* descriptorSets[3];
			descriptorSets[0] = sequencerDescriptorSet.first.get();
			descriptorSets[1] = matDescSet;
			descriptorSets[2] = parserContext._extraSequencerDescriptorSet.second;
			boundUniforms.ApplyDescriptorSets(
				metalContext, encoder,
				MakeIteratorRange(descriptorSets), 0);
			if (__builtin_expect(boundUniforms.GetBoundLooseImmediateDatas(0) | boundUniforms.GetBoundLooseResources(0) | boundUniforms.GetBoundLooseResources(0), 0ull)) {
				ApplyLooseUniforms(uniformsHelper, metalContext, encoder, parserContext, boundUniforms, 0);
			}

			//////////////////////////////////////////////////////////////////////////////

			RealExecuteDrawableContext drawFnContext { &metalContext, &encoder, pipeline->_metalPipeline.get(), &boundUniforms };
			drawable._drawFn(parserContext, *(ExecuteDrawableContext*)&drawFnContext, drawable);
		}
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
		auto& metalContext = *Metal::DeviceContext::Get(context);
		auto pipelineLayout = pipelineAccelerators.TryGetCompiledPipelineLayout(sequencerConfig);
		if (!pipelineLayout) return;
		auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
		Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, uniformsHelper, drawablePkt);
	}

	void Draw(
		IThreadContext& context,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt)
	{
		auto& metalContext = *Metal::DeviceContext::Get(context);
		auto pipelineLayout = pipelineAccelerators.TryGetCompiledPipelineLayout(sequencerConfig);
		if (!pipelineLayout) return;
		auto encoder = metalContext.BeginGraphicsEncoder(pipelineLayout);
		SequencerUniformsHelper uniformsHelper { parserContext };
		Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, uniformsHelper, drawablePkt);
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
			auto pipelineFuture = pipelineAccelerators.GetPipeline(*drawable._pipeline, sequencerConfig);
			if (pipelineFuture->GetAssetState() != ::Assets::AssetState::Ready) {
				if (!result)
					result = std::make_shared<::Assets::AsyncMarkerGroup>();
				result->Add(pipelineFuture, s_graphicsPipeline);
			}

			if (drawable._descriptorSet) {
				auto descriptorSetFuture = pipelineAccelerators.GetDescriptorSet(*drawable._descriptorSet);
				if (descriptorSetFuture->GetAssetState() != ::Assets::AssetState::Ready) {
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


}}

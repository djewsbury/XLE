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
#include "CommonUtils.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"		// for DescriptorSetLayoutAndBinding
#include "Services.h"
#include "SubFrameEvents.h"
#include "../UniformsStream.h"
#include "../BufferView.h"
#include "../IDevice.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../Metal/Resource.h"
#include "../BufferUploads/BatchedResources.h"
#include "../../Assets/AsyncMarkerGroup.h"
#include "../../Assets/Marker.h"
#include "../../Assets/ContinuationUtil.h"		// for PrepareResources
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/BitUtils.h"

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
{
///////////////////////////////////////////////////////////////////////////////////////////////////

	constexpr unsigned s_uniformGroupSequencer = 0;
	constexpr unsigned s_uniformGroupMaterial = 1;
	constexpr unsigned s_uniformGroupDraw = 2;
	constexpr auto s_materialDescSetName = "Material"_h;

	namespace Internal
	{
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

		static unsigned GetMaterialDescSetDynamicOffset(
			DeformAccelerator& deformAccelerator,
			const ActualizedDescriptorSet& descSet,
			unsigned deformInstanceIdx)
		{
			return descSet.ApplyDeformAcceleratorOffset() ? GetUniformPageBufferOffset(deformAccelerator, deformInstanceIdx) : 0u;
		}

		static void ApplyPerDrawableUniforms(ParsingContext& parsingContext, RealExecuteDrawableContext& context, IShaderResourceDelegate& delegate, const Drawable& drawable, unsigned drawableIndex, unsigned uniformGroupIdx);
	}

	static void Draw(
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
		ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const ICompiledPipelineLayout& initialPipelineLayout,
		const Internal::TemporaryStorageLocator& temporaryVB, 
		const Internal::TemporaryStorageLocator& temporaryIB,
		const DrawOptions& drawOptions)
	{
		auto& uniformDelegateMan = *parserContext.GetUniformDelegateManager();
		uniformDelegateMan.BringUpToDateGraphics(parserContext);

		const UniformsStreamInterface& globalUSI = uniformDelegateMan.GetInterfaceGraphics();

		UniformsStreamInterface materialUSI;
		materialUSI.BindFixedDescriptorSet(0, s_materialDescSetName);

		UniformsStreamInterface emptyUSI;
		const UniformsStreamInterface* perDrawableUSI = drawOptions._perDrawableUniforms ? &drawOptions._perDrawableUniforms->_interface : &emptyUSI;

		const DrawableGeo* currentGeo = nullptr;
		PipelineAccelerator* currentPipelineAccelerator = nullptr;
		const IPipelineAcceleratorPool::Pipeline* currentPipeline = nullptr;
		uint64_t currentSequencerUniformRules = 0;
		const UniformsStreamInterface* currentLooseUniformsInterface = nullptr;
		Metal::BoundUniforms* currentBoundUniforms = nullptr;
		unsigned idx = 0;
		const ICompiledPipelineLayout* currentPipelineLayout = &initialPipelineLayout;

		Metal::CapturedStates capturedStates;
		encoder.BeginStateCapture(capturedStates);

		auto stencilRefs = GetStencilRefValues(sequencerConfig);
		encoder.SetStencilRef(stencilRefs.first, stencilRefs.second);
		auto acceleratorVisibilityId = drawOptions._pipelineAcceleratorsVisibility.value_or(parserContext.GetPipelineAcceleratorsVisibility());

		unsigned pipelineLookupCount = 0;
		unsigned pipelineLayoutChangeCount = 0;
		unsigned boundUniformLookupCount = 0;
		unsigned fullDescSetCount = 0;
		unsigned justMatDescSetCount = 0;
		unsigned executeCount = 0;
		bool somethingPending = false;

		TRY {
			for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d, ++idx) {
				const auto& drawable = *(Drawable*)d.get();
				assert(drawable._pipeline);
				if (drawable._pipeline != currentPipelineAccelerator) {
					auto* pipeline = TryGetPipeline(*drawable._pipeline, sequencerConfig, acceleratorVisibilityId);
					if (expect_evaluation(!pipeline, false)) { somethingPending = true; continue; }

					assert(pipeline->_metalPipeline);
					currentPipeline = pipeline;
					currentPipelineAccelerator = drawable._pipeline;

					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						globalUSI, materialUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface : &emptyUSI),
						*perDrawableUSI);
					currentLooseUniformsInterface = drawable._looseUniformsInterface;
					++boundUniformLookupCount;
					++pipelineLookupCount;

					if (currentPipelineLayout != pipeline->_pipelineLayout.get()) {
						encoder.BindPipelineLayout(*pipeline->_pipelineLayout);
						currentPipelineLayout = pipeline->_pipelineLayout.get();
						++pipelineLayoutChangeCount;
					}
				} else if (currentLooseUniformsInterface != drawable._looseUniformsInterface) {
					currentBoundUniforms = &currentPipeline->_boundUniformsPool.Get(
						*currentPipeline->_metalPipeline,
						globalUSI, materialUSI,
						*(drawable._looseUniformsInterface ? drawable._looseUniformsInterface : &emptyUSI),
						*perDrawableUSI);
					currentLooseUniformsInterface = drawable._looseUniformsInterface;
					++boundUniformLookupCount;
				}

				const ActualizedDescriptorSet* matDescSet = nullptr;
				if (drawable._descriptorSet) {
					matDescSet = TryGetDescriptorSet(*drawable._descriptorSet, acceleratorVisibilityId);
					if (expect_evaluation(!matDescSet, false)) { somethingPending = !IsInvalid_UnreliableTest(*drawable._descriptorSet, acceleratorVisibilityId); continue; }
					// hack -- ensure the correct command list is requested (this is intended to have been done earlier)
					parserContext._requiredBufferUploadsCommandList = std::max(parserContext._requiredBufferUploadsCommandList, matDescSet->GetCompletionCommandList());
					assert(parserContext._requiredBufferUploadsCommandList >= matDescSet->GetCompletionCommandList());	// parser context must be configured for this completion cmd list before getting here
					parserContext.RequireCommandList(matDescSet->GetCompletionCommandList());
				}

				////////////////////////////////////////////////////////////////////////////// 
			
				VertexBufferView vbv[4];
				if (drawable._geo != currentGeo && drawable._geo) {
					for (unsigned c=0; c<drawable._geo->_vertexStreamCount; ++c) {
						auto& stream = drawable._geo->_vertexStreams[c];
						if (stream._type == DrawableGeo::StreamType::Resource) {
							vbv[c]._resource = stream._resource.get();
							vbv[c]._offset = stream._vbOffset;
							assert(vbv[c]._resource);
						} else if (stream._type == DrawableGeo::StreamType::Deform) {
							assert(drawable._geo->_deformAccelerator);
							auto deformVbv = Techniques::Internal::GetOutputVBV(*drawable._geo->_deformAccelerator, drawable._deformInstanceIdx);
							vbv[c]._resource = deformVbv._resource;
							vbv[c]._offset = stream._vbOffset + deformVbv._offset;
						} else {
							assert(stream._type == DrawableGeo::StreamType::PacketStorage);
							vbv[c]._resource = temporaryVB._res;
							vbv[c]._offset = unsigned(stream._vbOffset + temporaryVB._begin);
						}
					}

					if (drawable._geo->_ibFormat != Format(0)) {
						if (drawable._geo->_ibStreamType == DrawableGeo::StreamType::Resource) {
							assert(drawable._geo->_ib);
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{drawable._geo->_ib.get(), drawable._geo->_ibFormat, drawable._geo->_ibOffset});
						} else {
							assert(drawable._geo->_ibStreamType == DrawableGeo::StreamType::PacketStorage);
							encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{temporaryIB._res, drawable._geo->_ibFormat, unsigned(drawable._geo->_ibOffset + temporaryIB._begin)});
						}
					} else {
						encoder.Bind(MakeIteratorRange(vbv, &vbv[drawable._geo->_vertexStreamCount]), IndexBufferView{});
					}
					assert(parserContext._requiredBufferUploadsCommandList >= drawable._geo->_completionCmdList);	// parser context must be configured for this completion cmd list before getting here
					currentGeo = drawable._geo;
				}

				//////////////////////////////////////////////////////////////////////////////

				if (currentBoundUniforms->GetGroupRulesHash(0) != currentSequencerUniformRules) {
					ApplyUniformsGraphics(uniformDelegateMan, metalContext, encoder, parserContext, *currentBoundUniforms, s_uniformGroupSequencer);
					currentSequencerUniformRules = currentBoundUniforms->GetGroupRulesHash(0);
					++fullDescSetCount;
				} 
				if (matDescSet) {
					unsigned dynamicOffset = 0;
					if (drawable._geo) dynamicOffset = Internal::GetMaterialDescSetDynamicOffset(*drawable._geo->_deformAccelerator, *matDescSet, drawable._deformInstanceIdx);
					currentBoundUniforms->ApplyDescriptorSet(metalContext, encoder, *matDescSet->GetDescriptorSet(), s_uniformGroupMaterial, 0, MakeIteratorRange(&dynamicOffset, &dynamicOffset+1));
					++justMatDescSetCount;
				}

				//////////////////////////////////////////////////////////////////////////////

				Internal::RealExecuteDrawableContext drawFnContext { &metalContext, &encoder, currentPipeline->_metalPipeline.get(), currentBoundUniforms };

				if (expect_evaluation(drawOptions._perDrawableUniforms != nullptr, false))
					Internal::ApplyPerDrawableUniforms(parserContext, drawFnContext, *drawOptions._perDrawableUniforms, drawable, idx, 3);

				drawable._drawFn(parserContext, *(ExecuteDrawableContext*)&drawFnContext, drawable);
				++executeCount;
			}
		} CATCH (...) {
			encoder.SetStencilRef(0,0);
			encoder.EndStateCapture();
			throw;
		} CATCH_END

		if (somethingPending)		// Ensure we mark the parser content to indicate that something is pending (this can cause GUI windows to refresh, etc)
			StringMeldAppend(parserContext._stringHelpers->_pendingAssets) << "Drawables pipeline or material\n";
		encoder.SetStencilRef(0,0);	// reset to avoid state leakage type issues
		encoder.EndStateCapture();
	}

	void Draw(
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const ICompiledPipelineLayout& initialPipelineLayout,
		const DrawOptions& drawOptions)
	{
		Internal::TemporaryStorageLocator temporaryVB, temporaryIB;
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
		assert(drawOptions._pipelineAcceleratorsVisibility.has_value() || &pipelineAccelerators == parserContext.GetTechniqueContext()._pipelineAccelerators.get());		// if we're not using the default pipeline accelerators, we should explicitly specify the visibility

		Draw(
			metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, drawablePkt, initialPipelineLayout, temporaryVB, temporaryIB, 
			drawOptions);
	}

	void Draw(
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt,
		const DrawOptions& drawOptions)
	{
		uint32_t acceleratorVisibilityId = ~0u;

		pipelineAccelerators.LockForReading();
		TRY {
			auto& metalContext = *Metal::DeviceContext::Get(parserContext.GetThreadContext());
			auto* pipelineLayout = TryGetCompiledPipelineLayout(sequencerConfig, acceleratorVisibilityId);
			assert(pipelineLayout);
			auto encoder = metalContext.BeginGraphicsEncoder(*pipelineLayout);
			auto viewport = parserContext.GetViewport();
			ScissorRect scissorRect { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
			encoder.Bind(MakeIteratorRange(&viewport, &viewport+1), MakeIteratorRange(&scissorRect, &scissorRect+1));
			Draw(metalContext, encoder, parserContext, pipelineAccelerators, sequencerConfig, drawablePkt, *pipelineLayout, drawOptions);
		} CATCH (...) {
			pipelineAccelerators.UnlockForReading();
			throw;
		} CATCH_END
		pipelineAccelerators.UnlockForReading();
	}

	static const std::string s_graphicsPipeline { "graphics-pipeline" };
	static const std::string s_descriptorSet { "descriptor-set" };

	void PrepareResources(
		std::promise<PreparedResourcesVisibility>&& promise,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		const DrawablesPacket& drawablePkt)
	{
		TRY {
			std::shared_ptr<::Assets::AsyncMarkerGroup> result;

			std::set<PipelineAccelerator*> uniquePipelineAccelerators;
			std::set<DescriptorSetAccelerator*> uniqueDescriptorSetAccelerators;
			struct Futures
			{
				std::vector<std::future<VisibilityMarkerId>> _pendingFutures1;
				std::vector<std::future<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>>> _pendingFutures2;

				std::vector<std::future<VisibilityMarkerId>> _readyFutures1;
				std::vector<std::future<std::pair<VisibilityMarkerId, BufferUploads::CommandListID>>> _readyFutures2;

				VisibilityMarkerId _starterVisMarker = 0;
				BufferUploads::CommandListID _starterCmdList = 0;
			};
			auto futures = std::make_shared<Futures>();

			for (auto d=drawablePkt._drawables.begin(); d!=drawablePkt._drawables.end(); ++d) {
				const auto& drawable = *(Drawable*)d.get();
				assert(drawable._pipeline);
				uniquePipelineAccelerators.insert(drawable._pipeline);
				if (drawable._descriptorSet)
					uniqueDescriptorSetAccelerators.insert(drawable._descriptorSet);
				if (drawable._geo)
					futures->_starterCmdList = std::max(futures->_starterCmdList, drawable._geo->_completionCmdList);
			}

			for (const auto& pipeline:uniquePipelineAccelerators) {
				auto pipelineFuture = pipelineAccelerators.GetPipelineMarker(*pipeline, sequencerConfig);
				if (pipelineFuture.valid()) {
					auto initialState = pipelineFuture.wait_for(std::chrono::milliseconds(0));
					if (initialState == std::future_status::timeout) {
						futures->_pendingFutures1.emplace_back(std::move(pipelineFuture));
					} else {
						futures->_starterVisMarker = std::max(futures->_starterVisMarker, pipelineFuture.get());
					}
				}
			}

			for (const auto& descSet:uniqueDescriptorSetAccelerators) {
				auto descSetFuture = pipelineAccelerators.GetDescriptorSetMarker(*descSet);
				if (descSetFuture.valid()) {
					auto initialState = descSetFuture.wait_for(std::chrono::milliseconds(0));
					if (initialState == std::future_status::timeout) {
						futures->_pendingFutures2.emplace_back(std::move(descSetFuture));
					} else {
						TRY {
							auto p = descSetFuture.get();
							futures->_starterVisMarker = std::max(futures->_starterVisMarker, p.first);
							futures->_starterCmdList = std::max(futures->_starterCmdList, p.second);
						} CATCH(const std::exception& e) {
							// we have to suppress exceptions from descriptor sets, because missing textures, etc, are not serious enough to bail on the entire prepare
							Log(Warning) << "Descriptor set invalid while preparing resources: " << e.what() << std::endl;
						} CATCH_END
					}
				}
			}

			if (!futures->_pendingFutures1.empty() || !futures->_pendingFutures2.empty()) {
				::Assets::PollToPromise(
					std::move(promise),
					[futures](auto timeout) {
						auto timeoutTime = std::chrono::steady_clock::now() + timeout;
						// If we have a lot of futures, there's a possible scenario where timeout can elapse
						// before we can check each future (even if each future is actually ready). To avoid this,
						// let's record which futures have previously returned ready
						while (!futures->_pendingFutures1.empty()) {
							if ((futures->_pendingFutures1.end()-1)->wait_until(timeoutTime) == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
							futures->_readyFutures1.emplace_back(std::move(*(futures->_pendingFutures1.end()-1)));
							futures->_pendingFutures1.erase(futures->_pendingFutures1.end()-1);
						}
						while (!futures->_pendingFutures2.empty()) {
							if ((futures->_pendingFutures2.end()-1)->wait_until(timeoutTime) == std::future_status::timeout)
								return ::Assets::PollStatus::Continue;
							futures->_readyFutures2.emplace_back(std::move(*(futures->_pendingFutures2.end()-1)));
							futures->_pendingFutures2.erase(futures->_pendingFutures2.end()-1);
						}
						return ::Assets::PollStatus::Finish;
					},
					[futures]() {
						assert(futures->_pendingFutures1.empty() && futures->_pendingFutures2.empty());
						PreparedResourcesVisibility result{futures->_starterVisMarker, futures->_starterCmdList};
						for (auto& f:futures->_readyFutures1)
							result._pipelineAcceleratorsVisibility = std::max(f.get(), result._pipelineAcceleratorsVisibility);
						for (auto& f:futures->_readyFutures2) {
							TRY {
								auto p = f.get();
								result._pipelineAcceleratorsVisibility = std::max(p.first, result._pipelineAcceleratorsVisibility);
								result._bufferUploadsVisibility = std::max(p.second, result._bufferUploadsVisibility);
							} CATCH(const std::exception& e) {
								// we have to suppress exceptions from descriptor sets, because missing textures, etc, are not serious enough to bail on the entire prepare
								Log(Warning) << "Descriptor set invalid while preparing resources: " << e.what() << std::endl;
							} CATCH_END
						}
						return result;
					});
			} else {
				// we can complete immediately
				promise.set_value(PreparedResourcesVisibility{futures->_starterVisMarker, futures->_starterCmdList});
			}
		} CATCH (...) {
			promise.set_exception(std::current_exception());
		} CATCH_END
	}

	void ExecuteDrawableContext::ApplyLooseUniforms(const UniformsStream& stream) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyLooseUniforms(*realContext._metalContext, *realContext._encoder, stream, s_uniformGroupDraw);
	}

	void ExecuteDrawableContext::ApplyDescriptorSets(IteratorRange<const IDescriptorSet* const*> descSets) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._boundUniforms->ApplyDescriptorSets(*realContext._metalContext, *realContext._encoder, descSets, s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseImmediateDatas() const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseImmediateDatas(s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseResources() const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseResources(s_uniformGroupDraw);
	}

	uint64_t ExecuteDrawableContext::GetBoundLooseSamplers() const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		return realContext._boundUniforms->GetBoundLooseSamplers(s_uniformGroupDraw);
	}

	bool ExecuteDrawableContext::AtLeastOneBoundLooseUniform() const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		return (realContext._boundUniforms->GetBoundLooseImmediateDatas(s_uniformGroupDraw) | realContext._boundUniforms->GetBoundLooseResources(s_uniformGroupDraw) | realContext._boundUniforms->GetBoundLooseSamplers(s_uniformGroupDraw)) != 0;
	}

	void ExecuteDrawableContext::Draw(unsigned vertexCount, unsigned startVertexLocation) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->Draw(*realContext._pipeline, vertexCount, startVertexLocation);
	}

	void ExecuteDrawableContext::DrawIndexed(unsigned indexCount, unsigned startIndexLocation, unsigned baseVertexLocation) const
	{
		assert(baseVertexLocation == 0);		// parameter deprecated
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndexed(*realContext._pipeline, indexCount, startIndexLocation);
	}

	void ExecuteDrawableContext::DrawInstances(unsigned vertexCount, unsigned instanceCount, unsigned startVertexLocation) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawInstances(*realContext._pipeline, vertexCount, instanceCount, startVertexLocation);
	}

	void ExecuteDrawableContext::DrawIndexedInstances(unsigned indexCount, unsigned instanceCount, unsigned startIndexLocation, unsigned baseVertexLocation) const
	{
		assert(baseVertexLocation == 0);		// parameter deprecated
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndexedInstances(*realContext._pipeline, indexCount, instanceCount, startIndexLocation);
	}

	void ExecuteDrawableContext::DrawAuto() const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawAuto(*realContext._pipeline);
	}

	void ExecuteDrawableContext::DrawIndirect(const IResource& res, unsigned offset) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndirect(*realContext._pipeline, res, offset);
	}
	void ExecuteDrawableContext::DrawIndexedIndirect(const IResource& res, unsigned offset) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->DrawIndexedIndirect(*realContext._pipeline, res, offset);
	}

	void ExecuteDrawableContext::SetStencilRef(unsigned frontFaceStencil, unsigned backFaceStencil) const
	{
		auto& realContext = *(Internal::RealExecuteDrawableContext*)this;
		realContext._encoder->SetStencilRef(frontFaceStencil, backFaceStencil);
	}

	namespace Internal
	{
		static void ApplyPerDrawableUniforms(ParsingContext& parsingContext, RealExecuteDrawableContext& context, IShaderResourceDelegate& delegate, const Drawable& drawable, unsigned drawableIndex, unsigned uniformGroupIdx)
		{
			// We query everything from the delegate for every drawable, and attempt to apply it
			auto boundSRVs = context._boundUniforms->GetBoundLooseResources(uniformGroupIdx);
			auto boundSamplers = context._boundUniforms->GetBoundLooseSamplers(uniformGroupIdx);
			auto boundImmData = context._boundUniforms->GetBoundLooseImmediateDatas(uniformGroupIdx);
			if (!(boundSRVs|boundSamplers|boundImmData)) return;

			PerDrawableUniformsContext delegateContext { drawableIndex, &drawable };

			IResourceView* srvs[64];
			ISampler* samplers[64];
			if (boundSRVs)
				delegate.WriteResourceViews(parsingContext, &delegateContext, boundSRVs, MakeIteratorRange(srvs));
			if (boundSamplers)
				delegate.WriteSamplers(parsingContext, &delegateContext, boundSamplers, MakeIteratorRange(samplers));
			IteratorRange<void*> immDatas[64];
			std::vector<uint8_t> temporaryStorage;
			if (boundImmData) {
				size_t totalUniformBytes = 0;
				for (uint64_t q=boundImmData; q;) {
					unsigned idx = xl_ctz8(q);
					auto size = delegate.GetImmediateDataSize(parsingContext, &delegateContext, idx);
					immDatas[idx] = {(void*)totalUniformBytes, (void*)(totalUniformBytes+size)};
					totalUniformBytes += size;
					q ^= 1ull<<uint64_t(idx);
				}

				void* dataStorage = nullptr;
				if (totalUniformBytes < 1024) {	// allocate from the stack, but only if it isn't to large (since it's only a single Drawable, we should expect it to be small)
					dataStorage = _alloca(totalUniformBytes);
				} else {
					temporaryStorage.resize(totalUniformBytes);
					dataStorage = temporaryStorage.data();
				}
				for (uint64_t q=boundImmData; q;) {
					unsigned idx = xl_ctz8(q);
					immDatas[idx] = { PtrAdd(dataStorage, (size_t)immDatas[idx].first), PtrAdd(dataStorage, (size_t)immDatas[idx].second) };
					delegate.WriteImmediateData(parsingContext, &delegateContext, idx, immDatas[idx]);
					q ^= 1ull<<uint64_t(idx);
				}
			}

			// now apply the changes to the device
			context._boundUniforms->ApplyLooseUniforms(
				*context._metalContext, *context._encoder,
				UniformsStream {
					MakeIteratorRange(srvs), 
					MakeIteratorRange((UniformsStream::ImmediateData*)immDatas, (UniformsStream::ImmediateData*)&immDatas[dimof(immDatas)]),
					MakeIteratorRange(samplers) }, uniformGroupIdx);
		}

		/// Simple resizable heap with pointer stability and no deletion until we delete all at once
		class DrawableGeoHeap
		{
		public:
			struct Page
			{
				std::unique_ptr<uint8_t[]> _storage;
			};
			std::vector<Page> _pages;
			unsigned _allocatedCount = 0;
			static constexpr unsigned s_geosPerPage = 64;

			DrawableGeo* Allocate();
			void DestroyAll();

			DrawableGeoHeap();
			~DrawableGeoHeap();
			DrawableGeoHeap(DrawableGeoHeap&&);
			DrawableGeoHeap& operator=(DrawableGeoHeap&&);
		};

		DrawableGeo* DrawableGeoHeap::Allocate()
		{
			auto pageForNewItem = _allocatedCount / s_geosPerPage;
			auto indexInPage = _allocatedCount % s_geosPerPage;
			++_allocatedCount;
			while (pageForNewItem >= _pages.size()) {
				auto newStorage = std::make_unique<uint8_t[]>(sizeof(DrawableGeo)*s_geosPerPage);
				_pages.emplace_back(Page{std::move(newStorage)});
			}
			auto* result = (DrawableGeo*)PtrAdd(_pages[pageForNewItem]._storage.get(), sizeof(DrawableGeo)*indexInPage);
			#pragma push_macro("new")
			#undef new
			new(result) DrawableGeo();
			#pragma pop_macro("new")
			return result;
		}

		DrawableGeoHeap::DrawableGeoHeap() = default;
		DrawableGeoHeap::~DrawableGeoHeap() 
		{
			DestroyAll();
		}
		DrawableGeoHeap::DrawableGeoHeap(DrawableGeoHeap&& moveFrom)
		: _pages(std::move(moveFrom._pages))
		{
			_allocatedCount = moveFrom._allocatedCount;
			moveFrom._allocatedCount = 0;
		}
		DrawableGeoHeap& DrawableGeoHeap::operator=(DrawableGeoHeap&& moveFrom)
		{
			if (&moveFrom == this) return *this;
			DestroyAll();
			_pages = std::move(moveFrom._pages);
			_allocatedCount = moveFrom._allocatedCount;
			moveFrom._allocatedCount = 0;
			return *this;
		}

		void DrawableGeoHeap::DestroyAll()
		{
			auto pageI = _pages.begin();
			while (_allocatedCount) {
				assert(pageI != _pages.end());
				unsigned geosThisPage = std::min(_allocatedCount, s_geosPerPage);
				auto* geos = (DrawableGeo*)pageI->_storage.get();
				for (unsigned c=0; c<geosThisPage; ++c) geos[c].~DrawableGeo();
				++pageI;
				_allocatedCount -= geosThisPage;
			}
		}
	}

	template<typename T>
		static DrawablesPacket::AllocateStorageResult AllocateFrom(T& vector, size_t size, unsigned alignment)
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
		} else if (storageType == Storage::Vertex) {
			return AllocateFrom(_vbStorage, size, _storageAlignment);
		} else if (storageType == Storage::Uniform) {
			return AllocateFrom(_ubStorage, size, _ubStorageAlignment);
		} else {
			// The caller may hold onto the pointers we pass back, so we need to use a paging system
			assert(storageType == Storage::CPU);
			const unsigned CPUPageSize = 16 * 1024;
			for (auto i=_cpuStoragePages.begin(); i!=_cpuStoragePages.end(); ++i) {
				if ((i->_used + size) <= i->_allocated) {
					AllocateStorageResult result { 
						MakeIteratorRange(
							PtrAdd(i->_memory.get(), i->_used),
							PtrAdd(i->_memory.get(), i->_used+size)),
						unsigned(std::distance(_cpuStoragePages.begin(), i) * CPUPageSize + i->_used) };
					i->_used += size;
					return result;
				}
			}
			CPUStoragePage newPage;
			newPage._memory = std::make_unique<uint8_t[]>(CPUPageSize);
			newPage._allocated = CPUPageSize;
			newPage._used = size;
			AllocateStorageResult result { 
				MakeIteratorRange(newPage._memory.get(), PtrAdd(newPage._memory.get(), size)),
				unsigned(_cpuStoragePages.size() * CPUPageSize) };
			_cpuStoragePages.emplace_back(std::move(newPage));
			return result;
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

	DrawableGeo* DrawablesPacket::CreateTemporaryGeo()
	{
		assert(_geoHeap);
		return _geoHeap->Allocate();
	}

	void DrawablesPacket::Reset()
	{ 
		_drawables.clear(); 
		_vbStorage.clear();
		_ibStorage.clear();
		_ubStorage.clear();
		_cpuStoragePages.clear();
		_geoHeap->DestroyAll();
	}

	DrawablesPacket::DrawablesPacket()
	{
		_geoHeap = std::make_unique<Internal::DrawableGeoHeap>();
	}
	DrawablesPacket::DrawablesPacket(IDrawablesPool& pool, unsigned poolMarker)
	: _pool(&pool), _poolMarker(poolMarker)
	{
		_geoHeap = std::make_unique<Internal::DrawableGeoHeap>();
	}
	DrawablesPacket::~DrawablesPacket()
	{
		if (_pool) {
			Reset();
			_pool->ReturnToPool(std::move(*this), _poolMarker);
		}
	}
	DrawablesPacket::DrawablesPacket(DrawablesPacket&& moveFrom) never_throws
	: _drawables(std::move(moveFrom._drawables))
	, _vbStorage(std::move(moveFrom._vbStorage))
	, _ibStorage(std::move(moveFrom._ibStorage))
	, _ubStorage(std::move(moveFrom._ubStorage))
	, _cpuStoragePages(std::move(moveFrom._cpuStoragePages))
	, _geoHeap(std::move(moveFrom._geoHeap))
	{
		_pool = moveFrom._pool;
		_poolMarker = moveFrom._poolMarker;
		moveFrom._pool = nullptr;
		moveFrom._poolMarker = ~0u;
		_storageAlignment = moveFrom._storageAlignment;
		_ubStorageAlignment = moveFrom._ubStorageAlignment;
	}
	DrawablesPacket& DrawablesPacket::operator=(DrawablesPacket&& moveFrom) never_throws
	{
		if (&moveFrom == this)
			return *this;

		if (_pool)
			_pool->ReturnToPool(std::move(*this), _poolMarker);

		_drawables = std::move(moveFrom._drawables);
		_vbStorage = std::move(moveFrom._vbStorage);
		_ibStorage = std::move(moveFrom._ibStorage);
		_ubStorage = std::move(moveFrom._ubStorage);
		_cpuStoragePages = std::move(moveFrom._cpuStoragePages);
		_geoHeap = std::move(moveFrom._geoHeap);
		_pool = moveFrom._pool;
		_poolMarker = moveFrom._poolMarker;
		moveFrom._pool = nullptr;
		moveFrom._poolMarker = ~0u;
		_storageAlignment = moveFrom._storageAlignment;
		_ubStorageAlignment = moveFrom._ubStorageAlignment;
		return *this;
	}

	class DrawablesPool : public IDrawablesPool
	{
	public:
		DrawablesPacket CreatePacket() override;
		std::shared_ptr<DrawableGeo> CreateGeo() override;
		std::shared_ptr<DrawableInputAssembly> CreateInputAssembly(IteratorRange<const InputElementDesc*>, Topology) override;
		std::shared_ptr<UniformsStreamInterface> CreateProtectedLifetime(UniformsStreamInterface&& input) override;
		
		void ProtectedDestroy(void* object, DestructionFunctionSig* destructionFunction) override;

		void IncreaseAliveCount() override { ++_aliveCount; }
		int EstimateAliveClientObjectsCount() const override { return _aliveCount.load(); }

		DrawablesPool();
		~DrawablesPool();
		DrawablesPool(const DrawablesPool&) = delete;
		DrawablesPool& operator=(const DrawablesPool&) = delete;
	private:
		Threading::Mutex _lock;
		std::vector<DrawablesPacket> _availablePackets;

		using Marker = unsigned;
		std::vector<Marker> _queuedDestroyedPacket;		// packets destroyed out of order, waiting to increase _destroyedPktMarker
		Marker _destroyedPktMarker = 0;					// every packet <= this number has been destroyed already
		Marker _createdPktMarker = 0;					// highest marker given to a newly created packet

		ResizableCircularBuffer<std::pair<Marker, unsigned>, 32> _markerCounts;
		std::deque<std::pair<void*, DestructionFunctionSig*>> _holdingPendingDestruction;

		void ReturnToPool(DrawablesPacket&& pkt, unsigned marker) override;

		std::atomic<int> _aliveCount;

		Threading::Mutex _usisLock;
		std::vector<std::pair<uint64_t, std::shared_ptr<UniformsStreamInterface>>> _usis;
	};

	std::shared_ptr<DrawableInputAssembly> DrawablesPool::CreateInputAssembly(IteratorRange<const InputElementDesc*> elements, Topology topology)
	{
		return MakeProtectedPtr<DrawableInputAssembly>(elements, topology);
	}

	std::shared_ptr<UniformsStreamInterface> DrawablesPool::CreateProtectedLifetime(UniformsStreamInterface&& input)
	{
		ScopedLock(_usisLock);
		auto hash = input.GetHash();
		auto i = LowerBound(_usis, hash);
		if (i != _usis.end() && i->first == hash)
			return i->second;
		auto result = MakeProtectedPtr<UniformsStreamInterface>(std::move(input));
		_usis.insert(i, std::make_pair(hash, result));
		return result;
	}

	std::shared_ptr<DrawableGeo> DrawablesPool::CreateGeo()
	{
		return MakeProtectedPtr<DrawableGeo>();
	}

	DrawablesPacket DrawablesPool::CreatePacket()
	{
		ScopedLock(_lock);
		auto marker = ++_createdPktMarker;
		if (_availablePackets.empty())
			return DrawablesPacket(*this, marker);
		auto res = std::move(*(_availablePackets.end()-1));
		_availablePackets.erase(_availablePackets.end()-1);
		res._pool = this;
		res._poolMarker = marker;
		return res;
	}

	void DrawablesPool::ReturnToPool(DrawablesPacket&& pkt, unsigned marker)
	{
		assert(pkt._drawables.empty() && pkt._vbStorage.empty() && pkt._ibStorage.empty() && pkt._ubStorage.empty() && pkt._cpuStoragePages.empty());
		assert(marker != ~0u);
		pkt._poolMarker = ~0u;
		std::unique_lock<decltype(_lock)> locker(_lock);
		_availablePackets.push_back(std::move(pkt));
		if (marker == _destroyedPktMarker+1) {
			++_destroyedPktMarker;
			// integrate out-of-order packet destruction
			while (!_queuedDestroyedPacket.empty() && *_queuedDestroyedPacket.begin() == _destroyedPktMarker+1) {
				++_destroyedPktMarker;
				_queuedDestroyedPacket.erase(_queuedDestroyedPacket.begin());
			}

			// catch up on our queue -- if we have some pending destroys, service them now
			unsigned queuedObjectsToDestroy = 0;
			while (!_markerCounts.empty() && _markerCounts.front().first <= _destroyedPktMarker) { queuedObjectsToDestroy += _markerCounts.front().second; _markerCounts.pop_front(); }
			if (queuedObjectsToDestroy) {
				assert(queuedObjectsToDestroy <= _holdingPendingDestruction.size());
				std::vector<std::pair<void*, DestructionFunctionSig*>> toDestroy{_holdingPendingDestruction.begin(), _holdingPendingDestruction.begin()+queuedObjectsToDestroy};
				_holdingPendingDestruction.erase(_holdingPendingDestruction.begin(), _holdingPendingDestruction.begin()+queuedObjectsToDestroy);
				_aliveCount -= queuedObjectsToDestroy;
				
				locker = {};	// unlock because the destruction function could cause us to reenter a DrawablesPool fn
				for (const auto& t:toDestroy) t.second(t.first);
			}
		} else {
			auto i = std::lower_bound(_queuedDestroyedPacket.begin(), _queuedDestroyedPacket.end(), marker);
			assert(i == _queuedDestroyedPacket.end() || *i != marker);
			_queuedDestroyedPacket.insert(i, marker);
		}
	}

	void DrawablesPool::ProtectedDestroy(void* object, DestructionFunctionSig* destructionFunction)
	{
		std::unique_lock<decltype(_lock)> locker{_lock};
		if (_destroyedPktMarker == _createdPktMarker) {
			locker = {};	// unlock while destroying to avoid recursive locks
			destructionFunction(object);	// no packets alive currently
			--_aliveCount;
		} else {
			auto marker = _createdPktMarker;
			if (_markerCounts.empty()) {
				assert(_holdingPendingDestruction.empty());
				_markerCounts.emplace_back(std::make_pair(marker, 1u));
			} else if (_markerCounts.back().first == marker) {
				++_markerCounts.back().second;
			} else {
				assert(_markerCounts.front().first < marker);
				_markerCounts.emplace_back(std::make_pair(marker, 1u));
			}
			_holdingPendingDestruction.emplace_back(object, destructionFunction);
		}
	}

	static unsigned s_nextDrawablesPoolGUID = 1;

	DrawablesPool::DrawablesPool()
	{
		_guid = s_nextDrawablesPoolGUID++;
		_availablePackets.reserve(8);
		_aliveCount.store(0);
	}

	DrawablesPool::~DrawablesPool()
	{
		{
			ScopedLock(_usisLock);
			_usis.clear();
		}

		// do this in a loop to try to catch objects queued for destroy during destruction of other objects
		for (;;) {
			std::deque<std::pair<void*, DestructionFunctionSig*>> toDestroy;
			{
				ScopedLock(_lock);
				// ensure packets in _availablePackets don't try to call ReturnToPool when shutting down
				for (auto& p:_availablePackets) p._pool = nullptr;
				_availablePackets.clear();
				_destroyedPktMarker = ~0u;	// max value -- just destroy everything now
				_aliveCount -= (int)_holdingPendingDestruction.size();
				toDestroy = std::move(_holdingPendingDestruction);
			}
			if (toDestroy.empty()) break;
			for (const auto& t:toDestroy) t.second(t.first);
		}
		
		assert(_aliveCount.load() == 0);		// if you hit this, it means some client objects are still alive. This will end up holding dangling pointers to this
	}

	std::shared_ptr<IDrawablesPool> CreateDrawablesPool()
	{
		return std::make_shared<DrawablesPool>();
	}

	IDrawablesPool::~IDrawablesPool() = default;

	DrawableInputAssembly::DrawableInputAssembly(
		IteratorRange<const InputElementDesc*> inputElements,
		Topology topology)
	{
		_inputElements = NormalizeInputAssembly(inputElements);
		_strides = CalculateVertexStrides(_inputElements);
		_topology = topology;
		_hash = rotl64(HashInputAssembly(MakeIteratorRange(_inputElements), DefaultSeed64), (unsigned)_topology);
	}
	DrawableInputAssembly::~DrawableInputAssembly() = default;

	DrawableGeo::DrawableGeo() = default;
	DrawableGeo::~DrawableGeo()
	{
		if (_repositionalGeometry) _repositionalGeometry->Remove(*this);
	}
	
	std::shared_ptr<BufferUploads::IBatchedResources> RepositionableGeometryConduit::GetIBResourcePool() { return _ib; }
	std::shared_ptr<BufferUploads::IBatchedResources> RepositionableGeometryConduit::GetVBResourcePool() { return _vb; }

	void RepositionableGeometryConduit::Attach(DrawableGeo& geo, IteratorRange<BufferUploads::ResourceLocator*> attachedLocators)
	{
		ScopedLock(_lock);
		assert(!geo._repositionalGeometry);
		geo._repositionalGeometry = shared_from_this();
		
		VLA(AttachedRange, ibRanges, attachedLocators.size());
		VLA(AttachedRange, vbRanges, attachedLocators.size());
		unsigned ibRangeCount = 0, vbRangeCount = 0;
		for (const auto& l:attachedLocators) {
			assert(!l.IsWholeResource());
			auto range = l.GetRangeInContainingResource();
			// might be ideal if we just stole the reference count from the resource locator passed in -- but we're just re-reffing here
			if (_vb->AddRef(*l.GetContainingResource(), range.first, range.second-range.first)) {
				vbRanges[vbRangeCount++] = {&geo, l.GetContainingResource().get(), (unsigned)range.first, unsigned(range.second-range.first)};
			} else if (_ib->AddRef(*l.GetContainingResource(), range.first, range.second-range.first)) {
				ibRanges[ibRangeCount++] = {&geo, l.GetContainingResource().get(), (unsigned)range.first, unsigned(range.second-range.first)};
			} else {
				UNREACHABLE();
			}
		}

		if (ibRangeCount) {
			auto i = std::lower_bound(_ibAttachedRanges.begin(), _ibAttachedRanges.end(), &geo, [](const auto& q, auto* w) { return q._geo < w; });
			_ibAttachedRanges.insert(i, ibRanges, &ibRanges[ibRangeCount]);
		}
		if (vbRangeCount) {
			auto i = std::lower_bound(_vbAttachedRanges.begin(), _vbAttachedRanges.end(), &geo, [](const auto& q, auto* w) { return q._geo < w; });
			_vbAttachedRanges.insert(i, vbRanges, &vbRanges[vbRangeCount]);
		}
	}

	void RepositionableGeometryConduit::Remove(DrawableGeo& geo)
	{
		ScopedLock(_lock);
		auto i = std::lower_bound(_vbAttachedRanges.begin(), _vbAttachedRanges.end(), &geo, [](const auto& q, auto* w) { return q._geo < w; });
		if (i != _vbAttachedRanges.end() && i->_geo == &geo) {
			auto end = i+1;
			while (end != _vbAttachedRanges.end() && end->_geo == &geo) ++end;

			for (auto i2=i; i2!=end; ++i2) {
				bool goodRelease = _vb->Release(*i2->_batchResource, i2->_rangeBegin, i2->_rangeSize);
				assert(goodRelease);
			}

			_vbAttachedRanges.erase(i, end);
		}

		i = std::lower_bound(_ibAttachedRanges.begin(), _ibAttachedRanges.end(), &geo, [](const auto& q, auto* w) { return q._geo < w; });
		if (i != _ibAttachedRanges.end() && i->_geo == &geo) {
			auto end = i+1;
			while (end != _ibAttachedRanges.end() && end->_geo == &geo) ++end;

			for (auto i2=i; i2!=end; ++i2) {
				bool goodRelease = _ib->Release(*i2->_batchResource, i2->_rangeBegin, i2->_rangeSize);
				assert(goodRelease);
			}

			_ibAttachedRanges.erase(i, end);
		}
	}

	BufferUploads::CommandListID RepositionableGeometryConduit::HandleRepositions(IteratorRange<const BufferUploads::Event_ResourceReposition*> evnts, bool vb)
	{
		BufferUploads::CommandListID result = 0;
		ScopedLock(_lock);
		auto& originalAttachedRanges = vb ? _vbAttachedRanges : _ibAttachedRanges;
		std::vector<std::pair<AttachedRange*, unsigned>> attachedRanges;
		attachedRanges.reserve(originalAttachedRanges.size());

		for (const auto& evnt:evnts) {
			// look through our attached ranges and find 
			attachedRanges.clear();
			// The range list is not in a good sorted order to help us find the ranges within this resource, unfortunately
			// we have to check each one
			for (unsigned c=0; c<originalAttachedRanges.size(); c++)
				if (originalAttachedRanges[c]._batchResource == evnt._originalResource)
					attachedRanges.emplace_back(&originalAttachedRanges[c], originalAttachedRanges[c]._rangeBegin);
			if (attachedRanges.empty()) continue;
			std::sort(attachedRanges.begin(), attachedRanges.end(),
				[](const auto& lhs, const auto& rhs) {
					return lhs.second < rhs.second;
				});

			// we're expecting evnt._defragSteps to be sorted by _sourceStart
			#if defined(_DEBUG)
				for (auto i=evnt._defragSteps.begin(); i!=evnt._defragSteps.end()-1; ++i)
					assert(i->_sourceStart < (i+1)->_sourceStart);
			#endif

			auto evntIterator = evnt._defragSteps.begin();
			auto r = attachedRanges.begin();
			while (r != attachedRanges.end()) {
				while (evntIterator != evnt._defragSteps.end() && evntIterator->_sourceEnd <= r->first->_rangeBegin) ++evntIterator;
				if (evntIterator == evnt._defragSteps.end()) break;
				while (r != attachedRanges.end() && (r->first->_rangeBegin+r->first->_rangeSize) <= evntIterator->_sourceStart) ++r;

				while (r != attachedRanges.end() && r->first->_rangeBegin < evntIterator->_sourceEnd) {
					// should be some overlap here -- check for repositioning
					// evntIterator range should completely cover the range in 'r' -- otherwise we would end up splitting
					// the resource into mutliple parts
					assert(evntIterator->_sourceStart <= r->first->_rangeBegin && evntIterator->_sourceEnd >= (r->first->_rangeBegin+r->first->_rangeSize));
					auto* geo = r->first->_geo;
					for (auto& stream:geo->_vertexStreams)
						if (stream._resource.get() == evnt._originalResource) {
							stream._resource = evnt._newResource;
							assert(stream._vbOffset >= evntIterator->_sourceStart && stream._vbOffset < evntIterator->_sourceEnd);
							stream._vbOffset += evntIterator->_destination - evntIterator->_sourceStart;
						}
					if (geo->_ib.get() == evnt._originalResource) {
						geo->_ib = evnt._newResource;
						assert(geo->_ibOffset >= evntIterator->_sourceStart && geo->_ibOffset < evntIterator->_sourceEnd);
						geo->_ibOffset += evntIterator->_destination - evntIterator->_sourceStart;
					}

					// update our bookkeeping, and release the old range and addref the new range
					bool goodOp = _vb->Release(*r->first->_batchResource, r->first->_rangeBegin, r->first->_rangeSize)
						|| _ib->Release(*r->first->_batchResource, r->first->_rangeBegin, r->first->_rangeSize);
					assert(goodOp);

					r->first->_batchResource = evnt._newResource.get();
					r->first->_rangeBegin += evntIterator->_destination - evntIterator->_sourceStart;

					goodOp = _vb->AddRef(*r->first->_batchResource, r->first->_rangeBegin, r->first->_rangeSize)
						|| _ib->AddRef(*r->first->_batchResource, r->first->_rangeBegin, r->first->_rangeSize);
					assert(goodOp);

					++r;
				}
			}

			#if defined(_DEBUG)
				// double check to ensure there are no geo references within the old range
				for (auto i=evnt._defragSteps.begin(); i!=evnt._defragSteps.end()-1; ++i)
					for (auto r=attachedRanges.begin(); r!=attachedRanges.end();) {
						// we only know the offset to the start of the VB & IB ranges used by this geo 
						// -- unfortunately we don't know where the range ends
						auto* geo = r->first->_geo;
						for (auto& stream:geo->_vertexStreams)
							if (stream._resource.get() == evnt._originalResource)
								assert(stream._vbOffset < i->_sourceStart || stream._vbOffset >= i->_sourceEnd);
						if (geo->_ib.get() == evnt._originalResource)
							assert(geo->_ibOffset < i->_sourceStart || geo->_ibOffset >= i->_sourceEnd);
						r++;
						while (r!=attachedRanges.end() && r->first->_geo == (r-1)->first->_geo) ++r;
					}
			#endif

			result = std::max(result, evnt._cmdList);
		}
		return result;
	}

	RepositionableGeometryConduit::RepositionableGeometryConduit(std::shared_ptr<BufferUploads::IBatchedResources> vb, std::shared_ptr<BufferUploads::IBatchedResources> ib)
	: _vb(std::move(vb)), _ib(std::move(ib))
	{
		_backgroundFrameMarker = Services::GetBufferUploads().BindOnBackgroundFrame(
			[this]() {
				// TickDefrag in the buffer uploads thread since it's a little expensive
				_vb->TickDefrag();
				_ib->TickDefrag();
			});

		_beginFrameMarker = Services::GetSubFrameEvents()._onBeginFrame.Bind(
			[this](auto& parsingContext) {
				BufferUploads::CommandListID cmdList = 0;
				auto nextVb = _vb->EventList_GetPublishedID();
				if (nextVb > _lastProcessedVB) {
					auto evntList = _vb->EventList_Get(nextVb);
					auto cl = HandleRepositions(evntList, true);
					_vb->EventList_Release(nextVb);
					_lastProcessedVB = nextVb;
					for (auto e:evntList)
					cmdList = std::max(cmdList, cl);
				}

				auto nextIb = _ib->EventList_GetPublishedID();
				if (nextIb > _lastProcessedIB) {
					auto evntList = _ib->EventList_Get(nextIb);
					auto cl = HandleRepositions(evntList, false);
					_ib->EventList_Release(nextIb);
					_lastProcessedIB = nextIb;
					cmdList = std::max(cmdList, cl);
				}

				if (cmdList)
					parsingContext.RequireCommandList(cmdList);
			});
	}

	RepositionableGeometryConduit::~RepositionableGeometryConduit()
	{
		if (_beginFrameMarker != ~0u)
			Services::GetSubFrameEvents()._onBeginFrame.Unbind(_beginFrameMarker);
		if (_backgroundFrameMarker != ~0u)
			Services::GetBufferUploads().UnbindOnBackgroundFrame(_backgroundFrameMarker);
	}

}}

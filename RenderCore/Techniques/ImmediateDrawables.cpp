// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ImmediateDrawables.h"
#include "PipelineAccelerator.h"
#include "DrawableDelegates.h"
#include "TechniqueDelegates.h"
#include "CompiledShaderPatchCollection.h"
#include "Drawables.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "PipelineOperators.h"
#include "PipelineLayoutDelegate.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Format.h"
#include "../FrameBufferDesc.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ParameterBox.h"

#include "../Metal/DeviceContext.h"		// for GraphicsPipelineBuilder

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
{

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	struct DrawableWithVertexCount : public Drawable 
	{ 
		unsigned _vertexCount = 0, _vertexStride = 0, _vertexStartLocation = 0, _bytesAllocated = 0;
		DEBUG_ONLY(bool _userGeo = false;)
		RetainedUniformsStream _uniforms;
		uint64_t _matHash = ~0ull;

		static void ExecuteFn(ParsingContext&, const ExecuteDrawableContext& drawContext, const Drawable& drawable)
		{
			auto* customDrawable = (DrawableWithVertexCount*)&drawable;
			if (drawContext.AtLeastOneBoundLooseUniform())
				customDrawable->ApplyUniforms(drawContext);
			drawContext.Draw(customDrawable->_vertexCount, customDrawable->_vertexStartLocation);
		};

		static void IndexedExecuteFn(ParsingContext&, const ExecuteDrawableContext& drawContext, const Drawable& drawable)
		{
			auto* customDrawable = (DrawableWithVertexCount*)&drawable;
			if (drawContext.AtLeastOneBoundLooseUniform())
				customDrawable->ApplyUniforms(drawContext);
			drawContext.DrawIndexed(customDrawable->_vertexCount, customDrawable->_vertexStartLocation);
		};

	private:
		void ApplyUniforms(const ExecuteDrawableContext& drawContext)
		{
			VLA(const IResourceView*, res, _uniforms._resourceViews.size());
			for (size_t c=0; c<_uniforms._resourceViews.size(); ++c) res[c] = _uniforms._resourceViews[c].get();
			VLA_UNSAFE_FORCE(UniformsStream::ImmediateData, immData, _uniforms._immediateData.size());
			for (size_t c=0; c<_uniforms._immediateData.size(); ++c) immData[c] = _uniforms._immediateData[c];
			VLA(const ISampler*, samplers, _uniforms._samplers.size());
			for (size_t c=0; c<_uniforms._samplers.size(); ++c) samplers[c] = _uniforms._samplers[c].get();
			drawContext.ApplyLooseUniforms(
				UniformsStream { 
					MakeIteratorRange(res, &res[_uniforms._resourceViews.size()]),
					MakeIteratorRange(immData, &immData[_uniforms._immediateData.size()]),
					MakeIteratorRange(samplers, &samplers[_uniforms._samplers.size()]) });
		}
	};

	template<typename Chain>
		struct EncoderStateDrawable
	{
		Chain _chain;
		ExecuteDrawableFn* _chainFn;
		EncoderState _encoderState;

		static void ExecuteFn(ParsingContext& parsingContext, const ExecuteDrawableContext& drawContext, const Drawable& drawable)
		{
			auto& customDrawable = *((EncoderStateDrawable<Chain>*)&drawable);
			if (customDrawable._encoderState._states & (EncoderState::States::Scissor|EncoderState::States::Viewport|EncoderState::States::NoScissor)) {
				ViewportDesc viewport = parsingContext.GetViewport();
				Rect2D scissor { (int)viewport._x, (int)viewport._y, (unsigned)viewport._width, (unsigned)viewport._height };
				if (customDrawable._encoderState._states & EncoderState::States::Viewport)
					viewport = customDrawable._encoderState._viewport;
				if (customDrawable._encoderState._states & EncoderState::States::Scissor)
					scissor = customDrawable._encoderState._scissor;

				drawContext.SetViewports({&viewport, &viewport+1}, {&scissor, &scissor+1});
			}
			if (customDrawable._encoderState._states & EncoderState::States::DepthBounds)
				drawContext.SetDepthBounds(customDrawable._encoderState._depthBounds.first, customDrawable._encoderState._depthBounds.second);
			if (customDrawable._encoderState._states & EncoderState::States::StencilRef)
				drawContext.SetStencilRef(customDrawable._encoderState._stencilRef.first, customDrawable._encoderState._stencilRef.second);

			customDrawable._chainFn(parsingContext, drawContext, customDrawable._chain);
		}
	};

	class ImmediateDrawables : public IImmediateDrawables
	{
	public:
		template<typename Drawable>
			Drawable* AllocateDrawable(ExecuteDrawableFn* drawableFn)
		{
			if (_pendingEncoderState._states) {
				auto* d = _workingPkt._drawables.Allocate<EncoderStateDrawable<Drawable>>();
				d->_chain._drawFn = &EncoderStateDrawable<Drawable>::ExecuteFn;
				d->_chainFn = drawableFn;
				d->_encoderState = _pendingEncoderState;
				_pendingEncoderState._states = 0;
				return &d->_chain;
			} else {
				auto* d = _workingPkt._drawables.Allocate<Drawable>();
				d->_drawFn = drawableFn;
				return d;
			}
		}

		IteratorRange<void*> QueueDraw(
			size_t vertexCount,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material,
			RetainedUniformsStream&& uniforms,
			Topology topology) override
		{
			auto vStride = CalculateVertexStride(inputAssembly);
			auto vertexDataSize = vertexCount * vStride;
			// if (!vertexDataSize) return {};

			auto pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors, material._patchCollection);

			// check if we can just merge it into the previous draw call. If so we're just going to
			// increase the vertex count on that draw call
			// note that we're not checking the values of the uniforms in the material when we do this
			// comparison, because that comparison would just be too expensive
			assert(!_lastQueuedDrawable || !_lastQueuedDrawable->_userGeo); 
			bool compatibleWithLastDraw =
				    _lastQueuedDrawable && _lastQueuedDrawable->_pipeline == pipeline && _lastQueuedDrawable->_vertexStride == vStride
				&& topology != Topology::TriangleStrip
				&& topology != Topology::LineStrip
				&& material._combinable
				;
			assert(material._hash != ~0ull);	// used as a sentinel for non-combinable materials
			#if defined(_DEBUG)
				static const auto s_emptyRenderStateHash = RenderCore::Assets::RenderStateSet{}.GetHash();
				// material._hash should be filled in for anything with material settings (unless it's marked as non-combinable)
				assert((!material._uniformStreamInterface && !material._shaderSelectors && material._stateSet.GetHash() == s_emptyRenderStateHash && !material._patchCollection) || !material._combinable || material._hash != 0ull);
			#endif
			if (compatibleWithLastDraw)
				compatibleWithLastDraw &= _lastQueuedDrawable->_matHash == material._hash;

			if (compatibleWithLastDraw) {
				_lastQueuedDrawVertexCountOffset = _lastQueuedDrawable->_vertexCount;
				return UpdateLastDrawCallVertexCount(vertexCount);
			} else {
				auto* drawable = AllocateDrawable<DrawableWithVertexCount>(&DrawableWithVertexCount::ExecuteFn);
				auto* geo = _workingPkt.CreateTemporaryGeo();
				DrawablesPacket::AllocateStorageResult vertexStorage;
				if (vertexDataSize) {
					vertexStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexDataSize);
					geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
					geo->_vertexStreams[0]._vbOffset = vertexStorage._startOffset;
					geo->_vertexStreamCount = 1;
				}
				geo->_ibFormat = Format(0);
				drawable->_geo = geo;
				drawable->_pipeline = pipeline;

				drawable->_descriptorSet = nullptr;
				drawable->_vertexCount = (unsigned)vertexCount;
				drawable->_vertexStride = vStride;
				drawable->_bytesAllocated = (unsigned)vertexDataSize;
				drawable->_matHash = material._hash;
				if (material._uniformStreamInterface) {
					drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
					drawable->_uniforms = std::move(uniforms);
					drawable->_matHash = HashCombine(drawable->_matHash, drawable->_uniforms._hashForCombining);
				}
				_lastQueuedDrawable = drawable;
				_lastQueuedDrawVertexCountOffset = 0;
				return vertexStorage._data;
			}
		}

		void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			DrawableGeo& customGeo,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material,
			RetainedUniformsStream&& uniforms,
			Topology topology) override
		{
			bool indexed = customGeo._ibFormat != Format(0);
			auto* drawable = AllocateDrawable<DrawableWithVertexCount>(indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn);
			drawable->_geo = &customGeo;
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors, material._patchCollection);
			drawable->_vertexCount = (unsigned)indexOrVertexCount;
			drawable->_descriptorSet = nullptr;
			drawable->_vertexStartLocation = (unsigned)indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			drawable->_matHash = material._hash;
			if (material._uniformStreamInterface && material._uniformStreamInterface->GetHash()) {
				drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
				drawable->_uniforms = std::move(uniforms);
				drawable->_matHash = HashCombine(drawable->_matHash, drawable->_uniforms._hashForCombining);
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
		}

		void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			DrawableGeo& customGeo,
			IteratorRange<const InputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material,
			RetainedUniformsStream&& uniforms,
			Topology topology) override
		{
			bool indexed = customGeo._ibFormat != Format(0);
			auto* drawable = AllocateDrawable<DrawableWithVertexCount>(indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn);
			drawable->_geo = &customGeo;
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors, material._patchCollection);
			drawable->_descriptorSet = nullptr;
			drawable->_vertexCount = (unsigned)indexOrVertexCount;
			drawable->_vertexStartLocation = (unsigned)indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			drawable->_matHash = material._hash;
			if (material._uniformStreamInterface && material._uniformStreamInterface->GetHash()) {
				drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
				drawable->_uniforms = std::move(uniforms);
				drawable->_matHash = HashCombine(drawable->_matHash, drawable->_uniforms._hashForCombining);
			}
			drawable->_matHash = material._hash;
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
		}

		void QueueDraw(
			size_t vertexCount,
			DrawableGeo& customGeo,
			PipelineAccelerator& pipeline,
			DescriptorSetAccelerator& prebuiltDescriptorSet,
			const UniformsStreamInterface* uniformStreamInterface,
			RetainedUniformsStream&& uniforms,
			Topology topology) override
		{
			auto* drawable = AllocateDrawable<DrawableWithVertexCount>(&DrawableWithVertexCount::ExecuteFn);
			drawable->_geo = &customGeo;
			drawable->_pipeline = &pipeline;
			drawable->_descriptorSet = &prebuiltDescriptorSet;
			drawable->_vertexCount = (unsigned)vertexCount;
			drawable->_vertexStride = drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			drawable->_matHash = "do-not-combine"_h;
			if (uniformStreamInterface) {
				drawable->_looseUniformsInterface = ProtectLifetime(*uniformStreamInterface);
				drawable->_uniforms = std::move(uniforms);
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
		}

		IteratorRange<void*> QueueDraw(
			size_t vertexCount, size_t vStride,
			PipelineAccelerator& pipeline,
			DescriptorSetAccelerator& prebuiltDescriptorSet,
			const UniformsStreamInterface* uniformStreamInterface,
			RetainedUniformsStream&& uniforms,
			Topology topology) override
		{
			auto vertexDataSize = vertexCount * vStride;

			auto* drawable = AllocateDrawable<DrawableWithVertexCount>(&DrawableWithVertexCount::ExecuteFn);
			auto* geo = _workingPkt.CreateTemporaryGeo();
			DrawablesPacket::AllocateStorageResult vertexStorage;
			if (vertexDataSize) {
				vertexStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexDataSize);
				geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
				geo->_vertexStreams[0]._vbOffset = vertexStorage._startOffset;
				geo->_vertexStreamCount = 1;
			}
			geo->_ibFormat = Format(0);
			drawable->_geo = geo;
			drawable->_pipeline = &pipeline;

			drawable->_descriptorSet = &prebuiltDescriptorSet;
			drawable->_vertexCount = (unsigned)vertexCount;
			drawable->_vertexStride = (unsigned)vStride;
			drawable->_bytesAllocated = (unsigned)vertexDataSize;
			drawable->_matHash = "do-not-combine"_h;
			if (uniformStreamInterface) {
				drawable->_looseUniformsInterface = ProtectLifetime(*uniformStreamInterface);
				drawable->_uniforms = std::move(uniforms);
			}
			_lastQueuedDrawable = drawable;		// we must set this to ensure that we can call UpdateLastDrawCallVertexCount later
			_lastQueuedDrawVertexCountOffset = 0;
			return vertexStorage._data;
		}

		void QueueEncoderState(const EncoderState& encoderState) override
		{
			_pendingEncoderState.MergeIn(encoderState);
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

		IteratorRange<void*> UpdateLastDrawCallVertexCount(size_t newVertexCount) override
		{
			if (!_lastQueuedDrawable)
				Throw(std::runtime_error("Calling UpdateLastDrawCallVertexCount, but no previous draw call to update"));

			auto offsetPlusNewCount = _lastQueuedDrawVertexCountOffset + newVertexCount;
			if (offsetPlusNewCount == _lastQueuedDrawable->_vertexCount) {
				// no update necessary			
			} else if (offsetPlusNewCount > _lastQueuedDrawable->_vertexCount) {
				size_t allocationRequired = offsetPlusNewCount * _lastQueuedDrawable->_vertexStride;
				if (allocationRequired <= _lastQueuedDrawable->_bytesAllocated) {
					_lastQueuedDrawable->_vertexCount = (unsigned)offsetPlusNewCount;
				} else {
					auto extraStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::Vertex, allocationRequired-_lastQueuedDrawable->_bytesAllocated);
					if (!_lastQueuedDrawable->_bytesAllocated) const_cast<DrawableGeo*>(_lastQueuedDrawable->_geo)->_vertexStreams[0]._vbOffset = extraStorage._startOffset;
					assert(_lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawable->_bytesAllocated == extraStorage._startOffset);
					_lastQueuedDrawable->_bytesAllocated = (unsigned)allocationRequired;
					_lastQueuedDrawable->_vertexCount = (unsigned)offsetPlusNewCount;
				}
			} else {
				_lastQueuedDrawable->_vertexCount = (unsigned)offsetPlusNewCount;
			}

			auto fullStorage = _workingPkt.GetStorage(DrawablesPacket::Storage::Vertex);
			return MakeIteratorRange(
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawVertexCountOffset * _lastQueuedDrawable->_vertexStride)),
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + offsetPlusNewCount * _lastQueuedDrawable->_vertexStride)));
		}

		void AbandonDraws() override
		{
			_workingPkt.Reset();
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

		SequencerConfig& GetSequencerConfig(const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate, const FrameBufferDesc& fbDesc, unsigned subpassIndex)
		{
			auto hash = Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(fbDesc, subpassIndex);
			auto i = LowerBound(_sequencerConfigs, hash);
			if (i==_sequencerConfigs.end() || i->first != hash) {
				auto result = _pipelineAcceleratorPool->CreateSequencerConfig("immediate-drawables");
				_pipelineAcceleratorPool->SetTechniqueDelegate(*result, techniqueDelegate);
				_pipelineAcceleratorPool->SetFrameBufferDesc(*result, fbDesc, subpassIndex);
				i = _sequencerConfigs.insert(i, std::make_pair(hash, std::move(result)));
			}
			return *i->second;
		}

		void ExecuteDraws(
			ParsingContext& parserContext,
			const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			assert(parserContext.GetViewport()._width * parserContext.GetViewport()._height);
			if (!_workingPkt._drawables.empty()) {
				parserContext.GetUniformDelegateManager()->InvalidateUniforms();
				auto& sequencerConfig = GetSequencerConfig(techniqueDelegate, fbDesc, subpassIndex);
				Techniques::DrawOptions options;
				options._pipelineAcceleratorsVisibility = _pipelineAcceleratorsVisibility;
				Draw(
					parserContext,
					*_pipelineAcceleratorPool,
					sequencerConfig,
					_workingPkt,
					options);
			}

			AbandonDraws();	// (this just clears out everything prepared)
		}

		void PrepareResources(
			std::promise<PreparedResourcesVisibility>&& promise,
			const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			auto& sequencerConfig = GetSequencerConfig(techniqueDelegate, fbDesc, subpassIndex);
			Techniques::PrepareResources(std::move(promise), *_pipelineAcceleratorPool, sequencerConfig, _workingPkt);
		}

		PreparedResourcesVisibility StallAndPrepareResources(
			const std::shared_ptr<ITechniqueDelegate>& techniqueDelegate,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			std::promise<Techniques::PreparedResourcesVisibility> promisedPrepare;
				auto futurePrepare = promisedPrepare.get_future();
			PrepareResources(std::move(promisedPrepare), techniqueDelegate, fbDesc, subpassIndex);
			YieldToPool(futurePrepare);
			auto prepare = futurePrepare.get();
			_pipelineAcceleratorsVisibility = _pipelineAcceleratorPool->VisibilityBarrier(prepare._pipelineAcceleratorsVisibility);
			return prepare;
		}

		virtual DrawablesPacket* GetDrawablesPacket() override
		{
			return &_workingPkt;
		}

		virtual std::shared_ptr<IPipelineAcceleratorPool> GetPipelineAcceleratorPool() override
		{
			return _pipelineAcceleratorPool;
		}

		virtual void OnFrameBarrier() override
		{
			// Removed assertions related to keeping drawable packets empty here. This is because OnFrameBarrier()
			// can be called on a non-framebarrier -- ie, if we just want to advance the visibility barrier for the
			// pipeline accelerators
			_pipelineAcceleratorsVisibility = _pipelineAcceleratorPool->VisibilityBarrier();
		}

		ImmediateDrawables(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators)
		: _pipelineAcceleratorPool(std::move(pipelineAccelerators))
		{
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

	protected:
		DrawablesPacket _workingPkt;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::vector<std::pair<uint64_t, std::shared_ptr<PipelineAccelerator>>> _pipelineAccelerators;
		DrawableWithVertexCount* _lastQueuedDrawable = nullptr;
		unsigned _lastQueuedDrawVertexCountOffset = 0;
		std::vector<std::pair<uint64_t, std::shared_ptr<SequencerConfig>>> _sequencerConfigs;
		std::vector<std::pair<uint64_t, std::shared_ptr<UniformsStreamInterface>>> _usis;
		VisibilityMarkerId _pipelineAcceleratorsVisibility = 0;
		EncoderState _pendingEncoderState;

		template<typename InputAssemblyType>
			PipelineAccelerator* GetPipelineAccelerator(
				IteratorRange<const InputAssemblyType*> inputAssembly,
				const RenderCore::Assets::RenderStateSet& stateSet,
				Topology topology,
				const ParameterBox* shaderSelectors,
				const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& patchCollection)
		{
			uint64_t hashCode = HashInputAssembly(inputAssembly, stateSet.GetHash());
			if (topology != Topology::TriangleList)
				hashCode = HashCombine((uint64_t)topology, hashCode);	// awkward because it's just a small integer value
			if (shaderSelectors && shaderSelectors->GetCount() != 0) {
				hashCode = HashCombine(shaderSelectors->GetParameterNamesHash(), hashCode);
				hashCode = HashCombine(shaderSelectors->GetHash(), hashCode);
			}
			if (patchCollection)
				hashCode = HashCombine(patchCollection->GetHash(), hashCode);

			auto existing = LowerBound(_pipelineAccelerators, hashCode);
			if (existing != _pipelineAccelerators.end() && existing->first == hashCode)
				return existing->second.get();

			auto newAccelerator = _pipelineAcceleratorPool->CreatePipelineAccelerator(
				patchCollection, nullptr,
				shaderSelectors ? *shaderSelectors : ParameterBox{}, inputAssembly,
				topology, stateSet);
			// Note that we keep this pipeline accelerator alive indefinitely 
			_pipelineAccelerators.insert(existing, std::make_pair(hashCode, newAccelerator));
			return newAccelerator.get();
		}

		UniformsStreamInterface* ProtectLifetime(const UniformsStreamInterface& usi)
		{
			auto hash = usi.GetHash();
			assert(hash != 0);
			auto i = LowerBound(_usis, hash);
			if (i != _usis.end() && i->first == hash)
				return i->second.get();
			auto result = std::make_shared<UniformsStreamInterface>(usi);
			_usis.insert(i, std::make_pair(hash, result));
			return result.get();
		}
	};

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(std::shared_ptr<IPipelineAcceleratorPool> pipelineAccelerators)
	{
		return std::make_shared<ImmediateDrawables>(std::move(pipelineAccelerators));
	}

	void IImmediateDrawables::ExecuteDraws(ParsingContext& parsingContext, const std::shared_ptr<ITechniqueDelegate>& techDel, const RenderPassInstance& rpi)
	{
		ExecuteDraws(parsingContext, techDel, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
	}

	IteratorRange<void*> QueueDraw(
		DrawablesPacket& pkt,
		size_t vertexCount, size_t vStride,
		PipelineAccelerator& pipeline,
		DescriptorSetAccelerator& prebuiltDescriptorSet,
		const UniformsStreamInterface* uniformStreamInterface,
		RetainedUniformsStream&& uniforms,
		Topology topology)
	{
		auto vertexDataSize = vertexCount * vStride;

		auto* drawable = pkt._drawables.Allocate<DrawableWithVertexCount>();
		drawable->_drawFn = &DrawableWithVertexCount::ExecuteFn;
		auto* geo = pkt.CreateTemporaryGeo();
		DrawablesPacket::AllocateStorageResult vertexStorage;
		if (vertexDataSize) {
			vertexStorage = pkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexDataSize);
			geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
			geo->_vertexStreams[0]._vbOffset = vertexStorage._startOffset;
			geo->_vertexStreamCount = 1;
		}
		geo->_ibFormat = Format(0);
		drawable->_geo = geo;
		drawable->_pipeline = &pipeline;

		drawable->_descriptorSet = &prebuiltDescriptorSet;
		drawable->_vertexCount = (unsigned)vertexCount;
		drawable->_vertexStride = (unsigned)vStride;
		drawable->_bytesAllocated = (unsigned)vertexDataSize;
		drawable->_matHash = "do-not-combine"_h;
		if (uniformStreamInterface) {
			drawable->_looseUniformsInterface = uniformStreamInterface;		// note lifetime must be preserved by the caller
			drawable->_uniforms = std::move(uniforms);
		}
		return vertexStorage._data;
	}

	IImmediateDrawables::~IImmediateDrawables() {}
	RetainedUniformsStream::RetainedUniformsStream() = default;
	RetainedUniformsStream::RetainedUniformsStream(const RetainedUniformsStream&) = default;
	RetainedUniformsStream::RetainedUniformsStream(RetainedUniformsStream&&) = default;
	RetainedUniformsStream& RetainedUniformsStream::operator=(const RetainedUniformsStream&) = default;
	RetainedUniformsStream& RetainedUniformsStream::operator=(RetainedUniformsStream&&) = default;
	RetainedUniformsStream::~RetainedUniformsStream() = default;

}}


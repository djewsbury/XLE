// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ImmediateDrawables.h"
#include "PipelineAccelerator.h"
#include "DrawableDelegates.h"
#include "TechniqueDelegates.h"
#include "CommonResources.h"
#include "CompiledShaderPatchCollection.h"
#include "Drawables.h"
#include "RenderPass.h"
#include "ParsingContext.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/AssetFutureContinuation.h"
#include "../Assets/Assets.h"
#include "../Format.h"
#include "../FrameBufferDesc.h"
#include "../../ShaderParser/AutomaticSelectorFiltering.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../xleres/FileList.h"
#include <deque>

namespace RenderCore { namespace Techniques
{
	class ImmediateRendererTechniqueDelegate : public ITechniqueDelegate
	{
	public:
		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> Resolve(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates)
		{
			unsigned dsMode = 0;
			// We're re-purposing the _writeMask flag for depth test and write
			if (renderStates._flag & RenderCore::Assets::RenderStateSet::Flag::WriteMask) {
				bool depthWrite = renderStates._writeMask & 1<<0;
				bool depthTest = renderStates._writeMask & 1<<1;
				if (depthTest) {
					dsMode = depthWrite ? 0 : 1;
				} else {
					dsMode = 2;
				}
			}
			return _pipelineDescFuture[dsMode];
		}

		ImmediateRendererTechniqueDelegate() 
		{
			auto templateDesc = std::make_shared<GraphicsPipelineDesc>();
			templateDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":frameworkEntry:vs_*";
			templateDesc->_shaders[(unsigned)ShaderStage::Pixel] = BASIC_PIXEL_HLSL ":frameworkEntry:ps_*";
			templateDesc->_selectorPreconfigurationFile = "xleres/TechniqueLibrary/Framework/SelectorPreconfiguration.hlsl";

			templateDesc->_rasterization = CommonResourceBox::s_rsDefault;
			templateDesc->_blend.push_back(CommonResourceBox::s_abStraightAlpha);

			DepthStencilDesc dsModes[] = {
				CommonResourceBox::s_dsReadWrite,
				CommonResourceBox::s_dsReadOnly,
				CommonResourceBox::s_dsDisable
			};
			for (unsigned c=0; c<dimof(dsModes); ++c) {
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>(*templateDesc);
				nascentDesc->_depthStencil = dsModes[c];

				_pipelineDescFuture[c] = std::make_shared<::Assets::FuturePtr<GraphicsPipelineDesc>>("immediate-renderer");
				_pipelineDescFuture[c]->SetAsset(std::move(nascentDesc), {});
			}
		}
		~ImmediateRendererTechniqueDelegate() {}
	private:
		::Assets::PtrToFuturePtr<GraphicsPipelineDesc> _pipelineDescFuture[3];
	};

	struct DrawableWithVertexCount : public Drawable 
	{ 
		unsigned _vertexCount = 0, _vertexStride = 0, _vertexStartLocation = 0, _bytesAllocated = 0;
		DEBUG_ONLY(bool _userGeo = false;)
		RetainedUniformsStream _uniforms;

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
			const IResourceView* res[_uniforms._resourceViews.size()];
			for (size_t c=0; c<_uniforms._resourceViews.size(); ++c) res[c] = _uniforms._resourceViews[c].get();
			UniformsStream::ImmediateData immData[_uniforms._immediateData.size()];
			for (size_t c=0; c<_uniforms._immediateData.size(); ++c) immData[c] = _uniforms._immediateData[c];
			const ISampler* samplers[_uniforms._samplers.size()];
			for (size_t c=0; c<_uniforms._samplers.size(); ++c) samplers[c] = _uniforms._samplers[c].get();
			drawContext.ApplyLooseUniforms(
				UniformsStream { 
					MakeIteratorRange(res, &res[_uniforms._resourceViews.size()]),
					MakeIteratorRange(immData, &immData[_uniforms._immediateData.size()]),
					MakeIteratorRange(samplers, &samplers[_uniforms._samplers.size()]) });
		}
	};

	class ImmediateDrawables : public IImmediateDrawables
	{
	public:
		IteratorRange<void*> QueueDraw(
			size_t vertexCount,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material,
			Topology topology) override
		{
			auto vStride = CalculateVertexStride(inputAssembly);
			auto vertexDataSize = vertexCount * vStride;
			if (!vertexDataSize) return {};	

			auto pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors);

			// check if we can just merge it into the previous draw call. If so we're just going to
			// increase the vertex count on that draw call
			assert(!_lastQueuedDrawable || !_lastQueuedDrawable->_userGeo); 
			bool compatibleWithLastDraw =
				    _lastQueuedDrawable && _lastQueuedDrawable->_pipeline == pipeline && _lastQueuedDrawable->_vertexStride == vStride
				&& topology != Topology::TriangleStrip
				&& topology != Topology::LineStrip
				;
			if (compatibleWithLastDraw) {
				if (material._uniformStreamInterface) {
					compatibleWithLastDraw &= _lastQueuedDrawable->_looseUniformsInterface && (material._uniformStreamInterface->GetHash() == _lastQueuedDrawable->_looseUniformsInterface->GetHash());
				} else
					compatibleWithLastDraw &= _lastQueuedDrawable->_looseUniformsInterface == nullptr;
			}
			if (compatibleWithLastDraw) {
				_lastQueuedDrawVertexCountOffset = _lastQueuedDrawable->_vertexCount;
				return UpdateLastDrawCallVertexCount(vertexCount);
			} else {
				auto vertexStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::VB, vertexDataSize);
				auto* drawable = _workingPkt._drawables.Allocate<DrawableWithVertexCount>();
				drawable->_geo = AllocateDrawableGeo();
				drawable->_geo->_vertexStreams[0]._resource = nullptr;
				drawable->_geo->_vertexStreams[0]._vbOffset = vertexStorage._startOffset;
				drawable->_geo->_vertexStreamCount = 1;
				drawable->_geo->_ibFormat = Format(0);
				drawable->_pipeline = std::move(pipeline);
				drawable->_vertexCount = vertexCount;
				drawable->_vertexStride = vStride;
				drawable->_bytesAllocated = vertexDataSize;
				drawable->_drawFn = &DrawableWithVertexCount::ExecuteFn;
				if (material._uniformStreamInterface) {
					drawable->_looseUniformsInterface = material._uniformStreamInterface;
					drawable->_uniforms = material._uniforms;
				}
				_lastQueuedDrawable = drawable;
				_lastQueuedDrawVertexCountOffset = 0;
				return vertexStorage._data;
			}
		}

		void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const MiniInputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) override
		{
			auto* drawable = _workingPkt._drawables.Allocate<DrawableWithVertexCount>();
			drawable->_geo = std::move(customGeo);
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors);
			drawable->_vertexCount = indexOrVertexCount;
			drawable->_vertexStartLocation = indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			bool _indexed = drawable->_geo->_ibFormat != Format(0);
			drawable->_drawFn = _indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn;
			if (material._uniformStreamInterface) {
				drawable->_looseUniformsInterface = material._uniformStreamInterface;
				drawable->_uniforms = material._uniforms;
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
		}

		void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const InputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) override
		{
			auto* drawable = _workingPkt._drawables.Allocate<DrawableWithVertexCount>();
			drawable->_geo = std::move(customGeo);
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors);
			drawable->_vertexCount = indexOrVertexCount;
			drawable->_vertexStartLocation = indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			bool _indexed = drawable->_geo->_ibFormat != Format(0);
			drawable->_drawFn = _indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn;
			if (material._uniformStreamInterface) {
				drawable->_looseUniformsInterface = material._uniformStreamInterface;
				drawable->_uniforms = material._uniforms;
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
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
					_lastQueuedDrawable->_vertexCount = offsetPlusNewCount;
				} else {
					auto extraStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::VB, allocationRequired-_lastQueuedDrawable->_bytesAllocated);
					assert(_lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawable->_bytesAllocated == extraStorage._startOffset);
					_lastQueuedDrawable->_bytesAllocated = allocationRequired;
					_lastQueuedDrawable->_vertexCount = offsetPlusNewCount;
				}
			} else {
				_lastQueuedDrawable->_vertexCount = offsetPlusNewCount;
			}

			auto fullStorage = _workingPkt.GetStorage(DrawablesPacket::Storage::VB);
			return MakeIteratorRange(
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawVertexCountOffset * _lastQueuedDrawable->_vertexStride)),
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + offsetPlusNewCount * _lastQueuedDrawable->_vertexStride)));
		}

		void AbandonDraws() override
		{
			_workingPkt.Reset();
			_reservedDrawableGeos.insert(_reservedDrawableGeos.end(), _drawableGeosInWorkingPkt.begin(), _drawableGeosInWorkingPkt.end());
			_drawableGeosInWorkingPkt.clear();
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

		void ExecuteDraws(
			IThreadContext& context,
			ParsingContext& parserContext,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			auto sequencerConfig = _pipelineAcceleratorPool->CreateSequencerConfig(
				_techniqueDelegate, ParameterBox{},
				fbDesc, subpassIndex);

			assert(parserContext.GetViewport()._width * parserContext.GetViewport()._height);
			SequencerUniformsHelper uniformsHelper{parserContext};
			Draw(
				context, parserContext,
				*_pipelineAcceleratorPool,
				*sequencerConfig.get(),
				uniformsHelper,
				_workingPkt);

			AbandonDraws();	// (this just clears out everything prepared)
		}

		std::shared_ptr<::Assets::IAsyncMarker> PrepareResources(
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			auto sequencerConfig = _pipelineAcceleratorPool->CreateSequencerConfig(
				_techniqueDelegate, ParameterBox{},
				fbDesc, subpassIndex);
			return Techniques::PrepareResources(*_pipelineAcceleratorPool, *sequencerConfig, _workingPkt);
		}

		virtual DrawablesPacket* GetDrawablesPacket() override
		{
			return &_workingPkt;
		}

		ImmediateDrawables(
			const std::shared_ptr<IDevice>& device,
			const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
			const DescriptorSetLayoutAndBinding& matDescSetLayout,
			const DescriptorSetLayoutAndBinding& sequencerDescSetLayout)
		{
			_pipelineAcceleratorPool = CreatePipelineAcceleratorPool(device, pipelineLayout, 0, matDescSetLayout, sequencerDescSetLayout);
			_techniqueDelegate = std::make_shared<ImmediateRendererTechniqueDelegate>();
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

	protected:
		DrawablesPacket _workingPkt;
		std::vector<std::shared_ptr<DrawableGeo>> _drawableGeosInWorkingPkt;
		std::deque<std::shared_ptr<DrawableGeo>> _reservedDrawableGeos;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::vector<std::pair<uint64_t, std::shared_ptr<PipelineAccelerator>>> _pipelineAccelerators;
		std::shared_ptr<ITechniqueDelegate> _techniqueDelegate;
		DrawableWithVertexCount* _lastQueuedDrawable = nullptr;
		unsigned _lastQueuedDrawVertexCountOffset = 0;

		std::shared_ptr<DrawableGeo> AllocateDrawableGeo()
		{
			// Not super efficient caching scheme here! But it's simple
			// Would be better if we just used std::shared_ptr<>s with custom deallocate functions,
			// and a contiguous custom heap
			std::shared_ptr<DrawableGeo> res;
			if (!_reservedDrawableGeos.empty()) {
				res = std::move(_reservedDrawableGeos.front());
				_reservedDrawableGeos.pop_front();
			} else 
				res = std::make_shared<DrawableGeo>();
			_drawableGeosInWorkingPkt.push_back(res);
			return res;
		}

		template<typename InputAssemblyType>
			std::shared_ptr<PipelineAccelerator> GetPipelineAccelerator(
				IteratorRange<const InputAssemblyType*> inputAssembly,
				const RenderCore::Assets::RenderStateSet& stateSet,
				Topology topology,
				const ParameterBox& shaderSelectors)
		{
			uint64_t hashCode = HashInputAssembly(inputAssembly, stateSet.GetHash());
			if (topology != Topology::TriangleList)
				hashCode = HashCombine((uint64_t)topology, hashCode);	// awkward because it's just a small integer value
			if (shaderSelectors.GetCount() != 0) {
				hashCode = HashCombine(shaderSelectors.GetParameterNamesHash(), hashCode);
				hashCode = HashCombine(shaderSelectors.GetHash(), hashCode);
			}

			auto existing = LowerBound(_pipelineAccelerators, hashCode);
			if (existing != _pipelineAccelerators.end() && existing->first == hashCode)
				return existing->second;

			auto newAccelerator = _pipelineAcceleratorPool->CreatePipelineAccelerator(
				nullptr, 
				shaderSelectors, inputAssembly,
				topology, stateSet);
			// Note that we keep this pipeline accelerator alive indefinitely 
			_pipelineAccelerators.insert(existing, std::make_pair(hashCode, newAccelerator));
			return newAccelerator;
		}
	};

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
		const DescriptorSetLayoutAndBinding& sequencerDescSetLayout)
	{
		return std::make_shared<ImmediateDrawables>(device, pipelineLayout, matDescSetLayout, sequencerDescSetLayout);
	}

	void IImmediateDrawables::ExecuteDraws(IThreadContext& threadContext, ParsingContext& parsingContext, const RenderPassInstance& rpi)
	{
		ExecuteDraws(threadContext, parsingContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
	}

	IImmediateDrawables::~IImmediateDrawables() {}

}}


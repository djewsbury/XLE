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
#include "PipelineOperators.h"
#include "CompiledLayoutPool.h"
#include "../Assets/RawMaterial.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../Assets/ShaderPatchCollection.h"
#include "../Metal/DeviceContext.h"		// for CalculateFrameBufferRelevance
#include "../Format.h"
#include "../FrameBufferDesc.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
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
		std::shared_ptr<GraphicsPipelineDesc> GetPipelineDesc(
			const CompiledShaderPatchCollection::Interface& shaderPatches,
			const RenderCore::Assets::RenderStateSet& renderStates) override
		{
			static const uint64_t s_patchShape = Hash64("IShape2D_Calculate");
			static const uint64_t s_patchFill = Hash64("IFill_Calculate");
			static const uint64_t s_patchOutline = Hash64("IOutline_Calculate");
			static const uint64_t s_patchTwoLayersShader = Hash64("TwoLayersShader");

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

			if (shaderPatches.HasPatchType(s_patchShape)) {
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				*nascentDesc = *_pipelineDesc[dsMode];

				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = RENDEROVERLAYS_SHAPES_HLSL ":frameworkEntry:ps_*";
				nascentDesc->_patchExpansions.emplace_back(s_patchShape, ShaderStage::Pixel);
				nascentDesc->_patchExpansions.emplace_back(s_patchFill, ShaderStage::Pixel);
				nascentDesc->_patchExpansions.emplace_back(s_patchOutline, ShaderStage::Pixel);
				nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

				return nascentDesc;
			} else if (shaderPatches.HasPatchType(s_patchTwoLayersShader)) {
				auto nascentDesc = std::make_shared<GraphicsPipelineDesc>();
				*nascentDesc = *_pipelineDesc[dsMode];

				nascentDesc->_shaders[(unsigned)ShaderStage::Pixel] = RENDEROVERLAYS_SHAPES_HLSL ":frameworkEntryForTwoLayersShader:ps_*";
				nascentDesc->_patchExpansions.emplace_back(s_patchTwoLayersShader, ShaderStage::Pixel);
				nascentDesc->_materialPreconfigurationFile = shaderPatches.GetPreconfigurationFileName();

				return nascentDesc;
			} else {
				return _pipelineDesc[dsMode];
			}
		}

		virtual std::string GetPipelineLayout() override
		{
			return IMMEDIATE_PIPELINE ":ImmediateDrawables";
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() override
		{
			return {};
		}

		ImmediateRendererTechniqueDelegate() 
		{
			auto templateDesc = std::make_shared<GraphicsPipelineDesc>();
			templateDesc->_shaders[(unsigned)ShaderStage::Vertex] = BASIC2D_VERTEX_HLSL ":frameworkEntry:vs_*";
			templateDesc->_shaders[(unsigned)ShaderStage::Pixel] = BASIC_PIXEL_HLSL ":frameworkEntry:ps_*";
			templateDesc->_techniquePreconfigurationFile = RENDEROVERLAYS_SEL_PRECONFIG;

			templateDesc->_rasterization = CommonResourceBox::s_rsDefault;
			templateDesc->_blend.push_back(CommonResourceBox::s_abStraightAlpha);

			DepthStencilDesc dsModes[] = {
				CommonResourceBox::s_dsReadWrite,
				CommonResourceBox::s_dsReadOnly,
				CommonResourceBox::s_dsDisable
			};
			for (unsigned c=0; c<dimof(dsModes); ++c) {
				_pipelineDesc[c] = std::make_shared<GraphicsPipelineDesc>(*templateDesc);
				_pipelineDesc[c]->_depthStencil = dsModes[c];
			}
		}
		~ImmediateRendererTechniqueDelegate() {}
	private:
		std::shared_ptr<GraphicsPipelineDesc> _pipelineDesc[3];
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
				;
			auto matUSIHash = material._uniformStreamInterface ? material._uniformStreamInterface->GetHash() : 0;
			if (compatibleWithLastDraw) {
				if (matUSIHash) {
					compatibleWithLastDraw &= _lastQueuedDrawable->_looseUniformsInterface && (matUSIHash == _lastQueuedDrawable->_looseUniformsInterface->GetHash());
				} else
					compatibleWithLastDraw &= _lastQueuedDrawable->_looseUniformsInterface == nullptr;
			}
			if (compatibleWithLastDraw) {
				_lastQueuedDrawVertexCountOffset = _lastQueuedDrawable->_vertexCount;
				return UpdateLastDrawCallVertexCount(vertexCount);
			} else {
				auto vertexStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::Vertex, vertexDataSize);
				auto* drawable = _workingPkt._drawables.Allocate<DrawableWithVertexCount>();
				auto* geo = _workingPkt.CreateTemporaryGeo();
				geo->_vertexStreams[0]._type = DrawableGeo::StreamType::PacketStorage;
				geo->_vertexStreams[0]._vbOffset = vertexStorage._startOffset;
				geo->_vertexStreamCount = 1;
				geo->_ibFormat = Format(0);
				drawable->_geo = geo;
				drawable->_pipeline = pipeline;

				drawable->_descriptorSet = nullptr;
				drawable->_vertexCount = vertexCount;
				drawable->_vertexStride = vStride;
				drawable->_bytesAllocated = vertexDataSize;
				drawable->_drawFn = &DrawableWithVertexCount::ExecuteFn;
				if (matUSIHash) {
					drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
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
			drawable->_geo = customGeo.get();
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors, material._patchCollection);
			drawable->_vertexCount = indexOrVertexCount;
			drawable->_descriptorSet = nullptr;
			drawable->_vertexStartLocation = indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			bool _indexed = drawable->_geo->_ibFormat != Format(0);
			drawable->_drawFn = _indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn;
			if (material._uniformStreamInterface && material._uniformStreamInterface->GetHash()) {
				drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
				drawable->_uniforms = material._uniforms;
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
			_customGeosInWorkingPkt.emplace_back(std::move(customGeo));
		}

		void QueueDraw(
			size_t indexOrVertexCount, size_t indexOrVertexStartLocation,
			std::shared_ptr<DrawableGeo> customGeo,
			IteratorRange<const InputElementDesc*> inputAssembly,
			const ImmediateDrawableMaterial& material = {},
			Topology topology = Topology::TriangleList) override
		{
			auto* drawable = _workingPkt._drawables.Allocate<DrawableWithVertexCount>();
			drawable->_geo = customGeo.get();
			drawable->_pipeline = GetPipelineAccelerator(inputAssembly, material._stateSet, topology, material._shaderSelectors, material._patchCollection);
			drawable->_descriptorSet = nullptr;
			drawable->_vertexCount = indexOrVertexCount;
			drawable->_vertexStartLocation = indexOrVertexStartLocation;
			drawable->_vertexStride = 0;
			drawable->_bytesAllocated = 0;
			DEBUG_ONLY(drawable->_userGeo = true;)
			bool _indexed = drawable->_geo->_ibFormat != Format(0);
			drawable->_drawFn = _indexed ? &DrawableWithVertexCount::IndexedExecuteFn : &DrawableWithVertexCount::ExecuteFn;
			if (material._uniformStreamInterface && material._uniformStreamInterface->GetHash()) {
				drawable->_looseUniformsInterface = ProtectLifetime(*material._uniformStreamInterface);
				drawable->_uniforms = material._uniforms;
			}
			_lastQueuedDrawable = nullptr;		// this is always null, because we can't modify or extend a user geo
			_lastQueuedDrawVertexCountOffset = 0;
			_customGeosInWorkingPkt.emplace_back(std::move(customGeo));
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
					auto extraStorage = _workingPkt.AllocateStorage(DrawablesPacket::Storage::Vertex, allocationRequired-_lastQueuedDrawable->_bytesAllocated);
					assert(_lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawable->_bytesAllocated == extraStorage._startOffset);
					_lastQueuedDrawable->_bytesAllocated = allocationRequired;
					_lastQueuedDrawable->_vertexCount = offsetPlusNewCount;
				}
			} else {
				_lastQueuedDrawable->_vertexCount = offsetPlusNewCount;
			}

			auto fullStorage = _workingPkt.GetStorage(DrawablesPacket::Storage::Vertex);
			return MakeIteratorRange(
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + _lastQueuedDrawVertexCountOffset * _lastQueuedDrawable->_vertexStride)),
				const_cast<void*>(PtrAdd(fullStorage.begin(), _lastQueuedDrawable->_geo->_vertexStreams[0]._vbOffset + offsetPlusNewCount * _lastQueuedDrawable->_vertexStride)));
		}

		void AbandonDraws() override
		{
			_workingPkt.Reset();
			_customGeosInWorkingPkt.clear();
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

		SequencerConfig& GetSequencerConfig(const FrameBufferDesc& fbDesc, unsigned subpassIndex)
		{
			auto hash = Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(fbDesc, subpassIndex);
			auto i = LowerBound(_sequencerConfigs, hash);
			if (i==_sequencerConfigs.end() || i->first != hash) {
				auto result = _pipelineAcceleratorPool->CreateSequencerConfig(
					"immediate-drawables",
					_techniqueDelegate, ParameterBox{},
					fbDesc, subpassIndex);
				i = _sequencerConfigs.insert(i, std::make_pair(hash, std::move(result)));
			}
			return *i->second;
		}

		void ExecuteDraws(
			ParsingContext& parserContext,
			const FrameBufferDesc& fbDesc,
			unsigned subpassIndex) override
		{
			auto& sequencerConfig = GetSequencerConfig(fbDesc, subpassIndex);
			assert(parserContext.GetViewport()._width * parserContext.GetViewport()._height);
			if (!_workingPkt._drawables.empty()) {
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

		void PrepareResources(std::promise<PreparedResourcesVisibility>&& promise, const FrameBufferDesc& fbDesc, unsigned subpassIndex) override
		{
			auto& sequencerConfig = GetSequencerConfig(fbDesc, subpassIndex);
			Techniques::PrepareResources(std::move(promise), *_pipelineAcceleratorPool, sequencerConfig, _workingPkt);
		}

		virtual DrawablesPacket* GetDrawablesPacket() override
		{
			return &_workingPkt;
		}

		virtual void OnFrameBarrier() override
		{
			// Removed assertions related to keeping drawable packets empty here. This is because OnFrameBarrier()
			// can be called on a non-framebarrier -- ie, if we just want to advance the visibility barrier for the
			// pipeline accelerators
			_pipelineAcceleratorsVisibility = _pipelineAcceleratorPool->VisibilityBarrier();
		}

		ImmediateDrawables(const std::shared_ptr<IDevice>& device)
		{
			_techniqueDelegate = std::make_shared<ImmediateRendererTechniqueDelegate>();
			auto pipelineLayout = ::Assets::ActualizeAssetPtr<Assets::PredefinedPipelineLayout>(_techniqueDelegate->GetPipelineLayout());
			auto matDescSetLayout = FindLayout(*pipelineLayout, "Material", PipelineType::Graphics);
			auto pipelineCollection = std::make_shared<PipelineCollection>(device);
			auto compiledLayoutPool = CreateCompiledLayoutPool(device, pipelineCollection, matDescSetLayout);
			_pipelineAcceleratorPool = CreatePipelineAcceleratorPool(device, nullptr, pipelineCollection, compiledLayoutPool, 0);
			_lastQueuedDrawable = nullptr;
			_lastQueuedDrawVertexCountOffset = 0;
		}

	protected:
		DrawablesPacket _workingPkt;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAcceleratorPool;
		std::vector<std::pair<uint64_t, std::shared_ptr<PipelineAccelerator>>> _pipelineAccelerators;
		std::shared_ptr<ITechniqueDelegate> _techniqueDelegate;
		DrawableWithVertexCount* _lastQueuedDrawable = nullptr;
		unsigned _lastQueuedDrawVertexCountOffset = 0;
		std::vector<std::pair<uint64_t, std::shared_ptr<SequencerConfig>>> _sequencerConfigs;
		std::vector<std::pair<uint64_t, std::shared_ptr<UniformsStreamInterface>>> _usis;
		std::vector<std::shared_ptr<DrawableGeo>> _customGeosInWorkingPkt;
		VisibilityMarkerId _pipelineAcceleratorsVisibility = 0;

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
				patchCollection, 
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

	std::shared_ptr<IImmediateDrawables> CreateImmediateDrawables(const std::shared_ptr<IDevice>& device)
	{
		return std::make_shared<ImmediateDrawables>(device);
	}

	void IImmediateDrawables::ExecuteDraws(ParsingContext& parsingContext, const RenderPassInstance& rpi)
	{
		ExecuteDraws(parsingContext, rpi.GetFrameBufferDesc(), rpi.GetCurrentSubpassIndex());
	}

	IImmediateDrawables::~IImmediateDrawables() {}

}}


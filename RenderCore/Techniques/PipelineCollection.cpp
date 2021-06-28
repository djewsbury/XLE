// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineCollection.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/InputLayout.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Assets/Assets.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/ArithmeticUtils.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Techniques
{
    uint64_t FrameBufferTarget::GetHash() const 
    {
		assert(_fbDesc);
		assert(_subpassIdx < _fbDesc->GetSubpasses().size());
        return RenderCore::Metal::GraphicsPipelineBuilder::CalculateFrameBufferRelevance(*_fbDesc, _subpassIdx); 
    }

    uint64_t PixelOutputStates::GetHash() const
	{
		assert(_attachmentBlend.size() == _fbTarget._fbDesc->GetSubpasses()[_fbTarget._subpassIdx].GetOutputs().size());
		uint64_t renderPassRelevance = _fbTarget.GetHash();
		auto result = HashCombine(_depthStencil.HashDepthAspect() ^ _depthStencil.HashStencilAspect(), renderPassRelevance);
		result = HashCombine(_rasterization.Hash(), result);
		for (const auto& a:_attachmentBlend)
			result = HashCombine(a.Hash(), result);
		return result;
	}

	uint64_t VertexInputStates::GetHash() const
	{
		auto seed = DefaultSeed64;
		lrot(seed, (int)_topology);
		return HashInputAssembly(_inputLayout, seed);
	}

	template<typename Type>
		std::vector<Type> AsVector(IteratorRange<const Type*> range) { return std::vector<Type>{range.begin(), range.end()}; }

    ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> GraphicsPipelineCollection::CreatePipeline(
        StringSection<> vsName, StringSection<> vsDefines,
        StringSection<> psName, StringSection<> psDefines,
        const VertexInputStates& inputStates,
        const PixelOutputStates& outputStates)
    {
        auto hash = HashCombine(inputStates.GetHash(), outputStates.GetHash());
        hash = Hash64(vsName, hash);
        hash = Hash64(vsDefines, hash);
        hash = Hash64(psName, hash);
        hash = Hash64(psDefines, hash);

        std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
        bool replaceExisting = false;
        auto i = LowerBound(_pipelines, hash);
        if (i != _pipelines.end() && i->first == hash) {
            if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
                return i->second;
            replaceExisting = true;
        }

        auto result = std::make_shared<::Assets::FuturePtr<Metal::GraphicsPipeline>>();
        if (replaceExisting) {
            i->second = result;
        } else
            _pipelines.insert(i, std::make_pair(hash, result));
        lk = {};
        ConstructToFuture(result, vsName, vsDefines, psName, psDefines, inputStates, outputStates);
        return result;
    }

    ::Assets::PtrToFuturePtr<Metal::GraphicsPipeline> GraphicsPipelineCollection::CreatePipeline(
        StringSection<> vsName, StringSection<> vsDefines,
        StringSection<> gsName, StringSection<> gsDefines,
        StringSection<> psName, StringSection<> psDefines,
        const VertexInputStates& inputStates,
        const PixelOutputStates& outputStates)
    {
        auto hash = HashCombine(inputStates.GetHash(), outputStates.GetHash());
        hash = Hash64(vsName, hash);
        hash = Hash64(vsDefines, hash);
        hash = Hash64(gsName, hash);
        hash = Hash64(gsDefines, hash);
        hash = Hash64(psName, hash);
        hash = Hash64(psDefines, hash);

        std::unique_lock<Threading::Mutex> lk(_pipelinesLock);
        bool replaceExisting = false;
        auto i = LowerBound(_pipelines, hash);
        if (i != _pipelines.end() && i->first == hash) {
            if (i->second->GetDependencyValidation().GetValidationIndex() == 0)
                return i->second;
            replaceExisting = true;
        }

        auto result = std::make_shared<::Assets::FuturePtr<Metal::GraphicsPipeline>>();
        if (replaceExisting) {
            i->second = result;
        } else
            _pipelines.insert(i, std::make_pair(hash, result));
        lk = {};
        ConstructToFuture(result, vsName, vsDefines, gsName, gsDefines, psName, psDefines, inputStates, outputStates);
        return result;
    }

    static ::Assets::PtrToFuturePtr<CompiledShaderByteCode> MakeByteCodeFuture(
        ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
    {
        char temp[MaxPath];
        auto meld = StringMeldInPlace(temp);
        meld << initializer;

        // shader profile
        {
            char profileStr[] = "?s_";
            switch (stage) {
            case ShaderStage::Vertex: profileStr[0] = 'v'; break;
            case ShaderStage::Geometry: profileStr[0] = 'g'; break;
            case ShaderStage::Pixel: profileStr[0] = 'p'; break;
            case ShaderStage::Domain: profileStr[0] = 'd'; break;
            case ShaderStage::Hull: profileStr[0] = 'h'; break;
            case ShaderStage::Compute: profileStr[0] = 'c'; break;
            default: assert(0); break;
            }
            if (!XlFindStringI(initializer, profileStr))
                meld << ":" << profileStr << "*";
        }

        return ::Assets::MakeAsset<CompiledShaderByteCode>(MakeStringSection(temp), definesTable);
    }

    void GraphicsPipelineCollection::ConstructToFuture(
        std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
        StringSection<> vsName, StringSection<> vsDefines,
        StringSection<> psName, StringSection<> psDefines,
        const VertexInputStates& inputStates,
        const PixelOutputStates& outputStates)
    {
        auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, vsDefines);
        auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, psDefines);
        ::Assets::WhenAll(vsFuture, psFuture).ThenConstructToFuture(
            *future,
            [pipelineLayout=_pipelineLayout,
            attachmentBlends=AsVector(outputStates._attachmentBlend),
            depthStencil=outputStates._depthStencil,
            rasterization=outputStates._rasterization,
            inputAssembly=AsVector(inputStates._inputLayout), topology=inputStates._topology,
            fbDesc=*outputStates._fbTarget._fbDesc, subpassIdx=outputStates._fbTarget._subpassIdx
            ](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
                Metal::ShaderProgram shader(Metal::GetObjectFactory(), pipelineLayout, *vsActual, *psActual);
                Metal::GraphicsPipelineBuilder builder;
                builder.Bind(shader);
                builder.Bind(attachmentBlends);
                builder.Bind(depthStencil);
                builder.Bind(rasterization);

                Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(inputAssembly), 0 };
                Metal::BoundInputLayout ia(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
                builder.Bind(ia, topology);

                builder.SetRenderPassConfiguration(fbDesc, subpassIdx);

                return builder.CreatePipeline(Metal::GetObjectFactory());
            });
    }

    void GraphicsPipelineCollection::ConstructToFuture(
        std::shared_ptr<::Assets::FuturePtr<Metal::GraphicsPipeline>> future,
        StringSection<> vsName, StringSection<> vsDefines,
        StringSection<> gsName, StringSection<> gsDefines,
        StringSection<> psName, StringSection<> psDefines,
        const VertexInputStates& inputStates,
        const PixelOutputStates& outputStates)
    {
        auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, vsDefines);
        auto gsFuture = MakeByteCodeFuture(ShaderStage::Geometry, gsName, gsDefines);
        auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, psDefines);
        ::Assets::WhenAll(vsFuture, gsFuture, psFuture).ThenConstructToFuture(
            *future,
            [pipelineLayout=_pipelineLayout,
            attachmentBlends=AsVector(outputStates._attachmentBlend),
            depthStencil=outputStates._depthStencil,
            rasterization=outputStates._rasterization,
            inputAssembly=AsVector(inputStates._inputLayout), topology=inputStates._topology,
            fbDesc=*outputStates._fbTarget._fbDesc, subpassIdx=outputStates._fbTarget._subpassIdx
            ](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> gsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
                Metal::ShaderProgram shader(Metal::GetObjectFactory(), pipelineLayout, *vsActual, *gsActual, *psActual);
                Metal::GraphicsPipelineBuilder builder;
                builder.Bind(shader);
                builder.Bind(attachmentBlends);
                builder.Bind(depthStencil);
                builder.Bind(rasterization);

                Metal::BoundInputLayout::SlotBinding slotBinding { MakeIteratorRange(inputAssembly), 0 };
                Metal::BoundInputLayout ia(MakeIteratorRange(&slotBinding, &slotBinding+1), shader);
                builder.Bind(ia, topology);

                builder.SetRenderPassConfiguration(fbDesc, subpassIdx);

                return builder.CreatePipeline(Metal::GetObjectFactory());
            });
    }

    GraphicsPipelineCollection::GraphicsPipelineCollection(
        std::shared_ptr<IDevice> device,
        std::shared_ptr<ICompiledPipelineLayout> pipelineLayout)
    : _device(std::move(device)), _pipelineLayout(std::move(pipelineLayout)) {}

}}


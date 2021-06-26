// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../UniformsStream.h"
#include "../../Types.h"
#include "../../../Utility/IteratorUtils.h"
#include "../../../Utility/OCUtils.h"

@class MTLVertexDescriptor;
@class MTLRenderPipelineReflection;
@protocol MTLRenderPipelineState;

namespace RenderCore { class VertexBufferView; class SharedPkt; }

namespace RenderCore { namespace Metal_AppleMetal
{
    class ShaderProgram;
    class PipelineLayoutConfig;
    class DeviceContext;

    /*class BoundVertexBuffers
    {
    public:
        void Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws;
    };*/

    class BoundInputLayout
    {
    public:
        // void Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws;

        // const BoundVertexBuffers& GetBoundVertexBuffers() const { return _boundVertexBuffers; };

        uint64_t GetGUID() const { return _hash; }

        BoundInputLayout();
        BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& program);

        struct SlotBinding
        {
            IteratorRange<const MiniInputElementDesc*> _elements;
            unsigned _instanceStepDataRate;     // set to 0 for per vertex, otherwise a per-instance rate
        };
        BoundInputLayout(
            IteratorRange<const SlotBinding*> layouts,
            const ShaderProgram& program);

        bool AllAttributesBound() const { return _allAttributesBound; }
        MTLVertexDescriptor* GetVertexDescriptor() const { return _vertexDescriptor.get(); }

    private:
        OCPtr<MTLVertexDescriptor> _vertexDescriptor;
        bool _allAttributesBound = true; // Metal HACK - Metal validation can help determine that bindings are correct
        uint64_t _hash = 0;

        // BoundVertexBuffers _boundVertexBuffers;
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    class DeviceContext;
    using ConstantBufferPacket = SharedPkt;
    class ConstantBuffer;
    class ShaderResourceView;
    class GraphicsPipeline;
    class GraphicsEncoder;
    class IDescriptorSet;

    class StreamMapping
    {
    public:
        struct BufferBinding { unsigned _uniformStreamSlot; unsigned _shaderSlot; unsigned _cbSize; DEBUG_ONLY(std::string _name;) };
        std::vector<BufferBinding> _immediateDataToBuffers;
        std::vector<BufferBinding> _resourceViewToBuffers;

        struct TextureBinding { unsigned _uniformStreamSlot; unsigned _shaderSlot; unsigned _textureType; bool _isDepth; DEBUG_ONLY(std::string _name;) };
        std::vector<TextureBinding> _resourceViewToTextures;

        struct Sampler { unsigned _uniformStreamSlot; unsigned _shaderSlot; DEBUG_ONLY(std::string _name;) };
        std::vector<Sampler> _samplers;

        uint64_t _boundImmediateDataSlots = 0ull;
        uint64_t _boundResourceViewSlots = 0ull;
        uint64_t _boundSamplerSlots = 0ull;

        uint64_t _boundArgs = 0ull;
    };

    struct UnboundInterface
    {
        UniformsStreamInterface _interface[4];
    };

    class BoundUniforms
    {
    public:
        void ApplyLooseUniforms(
            DeviceContext& context, 
            GraphicsEncoder& encoder,
            const UniformsStream& stream,
            unsigned groupIdx = 0) const;
        void UnbindLooseUniforms(DeviceContext& context, GraphicsEncoder& encoder, unsigned groupIdx=0) const;

        void ApplyDescriptorSets(
			DeviceContext& context,
			GraphicsEncoder& encoder,
			IteratorRange<const IDescriptorSet* const*> descriptorSets,
			unsigned groupIdx = 0) const;

        uint64_t GetBoundLooseImmediateDatas(unsigned groupIdx = 0) const;
		uint64_t GetBoundLooseResources(unsigned groupIdx = 0) const;
		uint64_t GetBoundLooseSamplers(unsigned groupIdx = 0) const;

        BoundUniforms(
            const ShaderProgram& shader,
            const UniformsStreamInterface& interface0 = {},
            const UniformsStreamInterface& interface1 = {},
            const UniformsStreamInterface& interface2 = {},
            const UniformsStreamInterface& interface3 = {});
        BoundUniforms(
            const GraphicsPipeline& pipeline,
            const UniformsStreamInterface& interface0 = {},
            const UniformsStreamInterface& interface1 = {},
            const UniformsStreamInterface& interface2 = {},
            const UniformsStreamInterface& interface3 = {});
        BoundUniforms();
        ~BoundUniforms();
        BoundUniforms(const BoundUniforms&);
        BoundUniforms& operator=(const BoundUniforms&);
        BoundUniforms(BoundUniforms&& moveFrom) never_throws;
        BoundUniforms& operator=(BoundUniforms&& moveFrom) never_throws;

        struct BoundArguments { uint64_t _vsArguments = 0ull; uint64_t _psArguments = 0ull; };
        static BoundArguments Apply_UnboundInterfacePath(
            GraphicsEncoder& encoder,
            MTLRenderPipelineReflection* pipelineReflection,
            const UnboundInterface& unboundInterface,
            unsigned streamIdx,
            const UniformsStream& stream);

        static void Apply_Standins(
            GraphicsEncoder& encoder,
            MTLRenderPipelineReflection* pipelineReflection,
            uint64_t vsArguments, uint64_t psArguments);

    private:
        StreamMapping _preboundInterfaceVS[4];
        StreamMapping _preboundInterfacePS[4];
        std::vector<std::pair<ShaderStage, unsigned>> _unboundCBs;
        std::vector<std::tuple<ShaderStage, unsigned, bool>> _unbound2DSRVs;
        std::vector<std::pair<ShaderStage, unsigned>> _unboundCubeSRVs;
        std::vector<std::pair<ShaderStage, unsigned>> _unboundSamplers;

        std::shared_ptr<UnboundInterface> _unboundInterface;
        uint64_t _boundImmediateDataSlots[4];
        uint64_t _boundResourceViewSlots[4];
        uint64_t _boundSamplerSlots[4];
    };
}}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UniformsStream.h"
#include "../RenderUtils.h"
#include "../IDevice_Forward.h"
#include "../StateDesc.h"
#include "../Metal/Forward.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"

namespace Utility { class ParameterBox; }
namespace RenderCore { namespace Assets { class PredefinedDescriptorSetLayout; }}
namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; }}

namespace RenderCore { namespace Techniques 
{
	class ParsingContext;

    class IUniformBufferDelegate
    {
    public:
        virtual void WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst) = 0;
        virtual size_t GetSize() = 0;
        virtual IteratorRange<const ConstantBufferElementDesc*> GetLayout();
        virtual ~IUniformBufferDelegate();
    };

    class IShaderResourceDelegate
    {
    public:
        virtual void WriteResourceViews(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst);
        virtual void WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst);
        virtual void WriteImmediateData(ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst);
        virtual size_t GetImmediateDataSize(ParsingContext& context, const void* objectContext, unsigned idx);
        virtual ~IShaderResourceDelegate();

        UniformsStreamInterface _interface;
        BufferUploads::CommandListID _completionCmdList = 0;
    protected:
		void BindResourceView(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements = {});
		void BindImmediateData(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements = {});
		void BindSampler(unsigned slot, uint64_t hashName);
    };

    class SemiConstantDescriptorSet;

    class IUniformDelegateManager
    {
    public:
        virtual void BindShaderResourceDelegate(std::shared_ptr<IShaderResourceDelegate>) = 0;
		virtual void UnbindShaderResourceDelegate(IShaderResourceDelegate&) = 0;
        
        virtual void BindUniformDelegate(uint64_t binding, std::shared_ptr<IUniformBufferDelegate>) = 0;
		virtual void UnbindUniformDelegate(IUniformBufferDelegate&) = 0;

        virtual void BindSemiConstantDescriptorSet(uint64_t binding, std::shared_ptr<SemiConstantDescriptorSet>) = 0;
        virtual void UnbindSemiConstantDescriptorSet(SemiConstantDescriptorSet& descSet) = 0;

        virtual void BindFixedDescriptorSet(uint64_t binding, IDescriptorSet& descSet) = 0;
        virtual void UnbindFixedDescriptorSet(IDescriptorSet& descSet) = 0;

        virtual void AddBase(std::shared_ptr<IUniformDelegateManager>) = 0;
		virtual void RemoveBase(IUniformDelegateManager&) = 0;

        virtual void InvalidateUniforms() = 0;
        virtual void BringUpToDateGraphics(ParsingContext& parsingContext) = 0;
        virtual void BringUpToDateCompute(ParsingContext& parsingContext) = 0;

        virtual const UniformsStreamInterface& GetInterfaceGraphics() = 0;
        virtual const UniformsStreamInterface& GetInterfaceCompute() = 0;
        virtual ~IUniformDelegateManager();
    };

    std::shared_ptr<IUniformDelegateManager> CreateUniformDelegateManager();

    std::shared_ptr<SemiConstantDescriptorSet> CreateSemiConstantDescriptorSet(
        const RenderCore::Assets::PredefinedDescriptorSetLayout&, StringSection<> name,
        PipelineType pipelineType, IDevice& device);

}}

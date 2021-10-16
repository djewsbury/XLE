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
    protected:
		void BindResourceView(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements = {});
		void BindImmediateData(unsigned slot, uint64_t hashName, IteratorRange<const ConstantBufferElementDesc*> cbElements = {});
		void BindSampler(unsigned slot, uint64_t hashName);
    };

    class ISemiConstantDescriptorSet
    {
    public:
        virtual void AddShaderResourceDelegate(const std::shared_ptr<IShaderResourceDelegate>&) = 0;
		virtual void RemoveShaderResourceDelegate(IShaderResourceDelegate&) = 0;
        
        virtual void AddUniformDelegate(uint64_t binding, const std::shared_ptr<IUniformBufferDelegate>&) = 0;
		virtual void RemoveUniformDelegate(IUniformBufferDelegate&) = 0;

        virtual void InvalidateUniforms() = 0;
        virtual const UniformsStreamInterface& GetInterface(IThreadContext&, ParsingContext& parsingContext) = 0;
        virtual ~ISemiConstantDescriptorSet();
    };

    class CommonResources;
    std::shared_ptr<ISemiConstantDescriptorSet> CreateSemiConstantDescriptorSet(
        IDevice& device,
        uint64_t descriptorSetName,
        const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
        const CommonResources&);

    XLE_DEPRECATED_ATTRIBUTE class IMaterialDelegate
    {
    public:
        virtual UniformsStreamInterface GetInterface(const void* objectContext) const = 0;
        virtual uint64_t GetInterfaceHash(const void* objectContext) const = 0;
		virtual const ParameterBox* GetShaderSelectors(const void* objectContext) const = 0;
        virtual void ApplyUniforms(
            ParsingContext& context,
            Metal::DeviceContext& devContext,
            const Metal::BoundUniforms& boundUniforms,
            unsigned streamIdx,
            const void* objectContext) const = 0;
        virtual ~IMaterialDelegate();
    };

	class DrawableMaterial;

	XLE_DEPRECATED_ATTRIBUTE class ITechniqueDelegate_Old
	{
	public:
		virtual Metal::ShaderProgram* GetShader(
			ParsingContext& context,
			const ParameterBox* shaderSelectors[],		// SelectorStages::Max
			const DrawableMaterial& material);

		virtual RenderCore::Metal::ShaderProgram* GetShader(
			ParsingContext& context,
			const ParameterBox* shaderSelectors[],		// SelectorStages::Max
			const DrawableMaterial& material,
			unsigned techniqueIndex);

		virtual ~ITechniqueDelegate_Old();
	};

}}

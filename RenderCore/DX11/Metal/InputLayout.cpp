// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputLayout.h"
#include "Shader.h"
#include "Buffer.h"
#include "TextureView.h"
#include "DeviceContext.h"
#include "DX11Utils.h"
#include "Shader.h"
#include "ObjectFactory.h"
#include "Format.h"
#include "State.h"
#include "../../Format.h"
#include "../../Types.h"
#include "../../BufferView.h"
#include "../../RenderUtils.h"
#include "../../ShaderService.h"
#include "../../UniformsStream.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/ParameterBox.h"
#include <D3D11Shader.h>

namespace RenderCore { namespace Metal_DX11
{
	static bool CalculateAllAttributesBound(
		IteratorRange<const D3D11_INPUT_ELEMENT_DESC*> inputElements,
		ID3D::ShaderReflection& reflection)
	{
		D3D11_SHADER_DESC shaderDesc;
		auto hresult = reflection.GetDesc(&shaderDesc);
		assert(SUCCEEDED(hresult));

		auto systemValuePrefix = MakeStringSectionLiteral("SV_");

		bool allAttributeBound = true;
		for (unsigned c=0; c<shaderDesc.InputParameters; ++c) {

			D3D11_SIGNATURE_PARAMETER_DESC desc;
			hresult = reflection.GetInputParameterDesc(c, &desc);
			assert(SUCCEEDED(hresult));

			if (!desc.SemanticName || XlBeginsWith(MakeStringSection(desc.SemanticName), systemValuePrefix))		// skip system values
				continue;

			auto i = std::find_if(inputElements.begin(), inputElements.end(),
				[&desc](const D3D11_INPUT_ELEMENT_DESC& e) { return XlEqString(e.SemanticName, desc.SemanticName) && e.SemanticIndex == desc.SemanticIndex; });
			if (i != inputElements.end())
				continue;

			// slot was not found. Log a warning message. We could also early out here,
			// but we'll continue just for the logging
			
			Log(Warning) << "Attribute was not found in CalculateAllAttributesBound : (" << desc.SemanticName << " : " << desc.SemanticIndex << ")" << std::endl;
			allAttributeBound = false;
		}

		return allAttributeBound;
	}

	static intrusive_ptr<ID3D::InputLayout> BuildInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader, bool& allAttributesBound)
	{
		auto byteCode = shader.GetByteCode();

		D3D11_INPUT_ELEMENT_DESC nativeLayout[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		auto slots = std::min(dimof(nativeLayout), layout.size());
		for (unsigned c = 0; c<slots; ++c) {
			nativeLayout[c].SemanticName = layout.first[c]._semanticName.c_str();
			nativeLayout[c].SemanticIndex = layout.first[c]._semanticIndex;
			nativeLayout[c].Format = AsDXGIFormat(layout.first[c]._nativeFormat);
			nativeLayout[c].InputSlot = layout.first[c]._inputSlot;
			nativeLayout[c].AlignedByteOffset = layout.first[c]._alignedByteOffset;
			nativeLayout[c].InputSlotClass = D3D11_INPUT_CLASSIFICATION(layout.first[c]._inputSlotClass);
			nativeLayout[c].InstanceDataStepRate = layout.first[c]._instanceDataStepRate;
		}

		// in DX11 all attributes must be bound anyway, but we can still do the check here --
		allAttributesBound = CalculateAllAttributesBound(MakeIteratorRange(nativeLayout, nativeLayout+slots), *CreateReflection(shader));

		return GetObjectFactory().CreateInputLayout(
			nativeLayout, (unsigned)slots,
			byteCode.begin(), byteCode.size());
	}

	static std::vector<std::pair<uint64_t, std::pair<const char*, UINT>>> GetInputParameters(ID3D::ShaderReflection& reflection)
	{
		std::vector<std::pair<uint64_t, std::pair<const char*, UINT>>> result;
		D3D11_SHADER_DESC shaderDesc;
		auto hresult = reflection.GetDesc(&shaderDesc);
		assert(SUCCEEDED(hresult));
		auto systemValuePrefix = MakeStringSectionLiteral("SV_");
		for (unsigned p=0;p<shaderDesc.InputParameters; ++p) {
			D3D11_SIGNATURE_PARAMETER_DESC desc;
			hresult = reflection.GetInputParameterDesc(p, &desc);
			assert(SUCCEEDED(hresult));
			if (!desc.SemanticName || XlBeginsWith(MakeStringSection(desc.SemanticName), systemValuePrefix))		// skip system values
				continue;
			auto hash = Hash64(desc.SemanticName) + desc.SemanticIndex;
			result.push_back({hash, {desc.SemanticName, desc.SemanticIndex}});
		}
		std::sort(result.begin(), result.end(), CompareFirst<uint64_t, std::pair<const char*, UINT>>());
		return result;
	}

	static intrusive_ptr<ID3D::InputLayout> BuildInputLayout(IteratorRange<const BoundInputLayout::SlotBinding*> layouts, const CompiledShaderByteCode& shader, bool& allAttributesBound)
	{
		auto byteCode = shader.GetByteCode();
		auto reflection = CreateReflection(shader);
		auto inputParameters = GetInputParameters(*reflection);
		std::vector<bool> boundAttributes(inputParameters.size(), false);

		D3D11_INPUT_ELEMENT_DESC nativeLayout[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		unsigned c=0;
		for (unsigned slot=0; slot<layouts.size(); ++slot) {
			UINT accumulatingOffset = 0;
			for (unsigned e=0; e<layouts[slot]._elements.size() && c<D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; ++e) {

				// We have to lookup the name of an input parameter that matches the hash,
				// because CreateInputLayout requires the full semantic name
				const auto& ele = layouts[slot]._elements[e];
				auto i = LowerBound(inputParameters, ele._semanticHash);
				if (i == inputParameters.end() || i->first != ele._semanticHash) {
					accumulatingOffset += BitsPerPixel(ele._nativeFormat) / 8;
					continue;
				}

				if (boundAttributes[i-inputParameters.begin()])
					Throw(::Exceptions::BasicLabel("Input attribute is being bound twice to the same shader input (for semantic %s and index %i)", i->second.first, i->second.second));
				boundAttributes[i-inputParameters.begin()] = true;

				nativeLayout[c].SemanticName = i->second.first;
				nativeLayout[c].SemanticIndex = i->second.second;
				nativeLayout[c].Format = AsDXGIFormat(ele._nativeFormat);
				nativeLayout[c].InputSlot = slot;
				nativeLayout[c].AlignedByteOffset = accumulatingOffset;
				nativeLayout[c].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
				nativeLayout[c].InstanceDataStepRate = layouts[slot]._instanceStepDataRate;
				++c;

				accumulatingOffset += BitsPerPixel(ele._nativeFormat) / 8;
			}
		}

		// in DX11 all attributes must be bound anyway, but we can still do the check here --
		allAttributesBound = true;
		for (unsigned b=0; b<boundAttributes.size(); ++b)
			if (!boundAttributes[b]) {
				Log(Warning) << "Attribute was not found in BuildInputLayout : (" << inputParameters[b].second.first << " : " << inputParameters[b].second.second << ")" << std::endl;
				allAttributesBound = false;
			}

		return GetObjectFactory().CreateInputLayout(
			nativeLayout, c,
			byteCode.begin(), byteCode.size());
	}

	static std::vector<unsigned> CalculateVertexStrides(IteratorRange<const BoundInputLayout::SlotBinding*> layouts)
	{
		std::vector<unsigned> result;
		result.reserve(layouts.size());
		for (const auto&l:layouts)
			result.push_back(RenderCore::CalculateVertexStride(l._elements));
		return result;
	}

	void BoundInputLayout::Apply(DeviceContext& context, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws
	{
		ID3D::Buffer* buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		UINT offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
		auto vbCount = std::min(vertexBuffers.size(), _vertexStrides.size());
		assert(vbCount < D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT);
		for (unsigned c = 0; c < vbCount; ++c) {
			assert(vertexBuffers[c]._resource);
			auto* res = AsID3DResource(*const_cast<IResource*>(vertexBuffers[c]._resource));
			assert(QueryInterfaceCast<ID3D::Buffer>(res));
			buffers[c] = (ID3D::Buffer*)res;
			offsets[c] = vertexBuffers[c]._offset;
			strides[c] = _vertexStrides[c];
		}
		const unsigned startSlot = 0;
		context.GetUnderlying()->IASetVertexBuffers(startSlot, (UINT)vbCount, buffers, strides, offsets);
		context.GetUnderlying()->IASetInputLayout(_underlying.get());
	}

    BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& shader)
    {
            // need constructor deferring!
		_allAttributesBound = false;
        _underlying = BuildInputLayout(layout, shader.GetCompiledCode(ShaderStage::Vertex), _allAttributesBound);
		_vertexStrides = CalculateVertexStrides(layout);
    }

    BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const CompiledShaderByteCode& shader)
    {
		_allAttributesBound = false;
        _underlying = BuildInputLayout(layout, shader, _allAttributesBound);
		_vertexStrides = CalculateVertexStrides(layout);
    }

	BoundInputLayout::BoundInputLayout(IteratorRange<const SlotBinding*> layouts, const ShaderProgram& shader)
    {
            // need constructor deferring!
		_allAttributesBound = false;
        _underlying = BuildInputLayout(layouts, shader.GetCompiledCode(ShaderStage::Vertex), _allAttributesBound);
		_vertexStrides = CalculateVertexStrides(layouts);
    }

    BoundInputLayout::BoundInputLayout(DeviceContext& context)
    {
        ID3D::InputLayout* rawptr = nullptr;
        context.GetUnderlying()->IAGetInputLayout(&rawptr);
        _underlying = moveptr(rawptr);
		_allAttributesBound = true;
		// todo -- getting the vertex strides would require also querying the vertex buffer bindings 
    }

    BoundInputLayout::BoundInputLayout() {}
    BoundInputLayout::~BoundInputLayout() {}

	BoundInputLayout::BoundInputLayout(BoundInputLayout&& moveFrom) never_throws
	: _underlying(std::move(moveFrom._underlying))
	, _vertexStrides(std::move(moveFrom._vertexStrides))
	, _allAttributesBound(moveFrom._allAttributesBound)
	{
	}

	BoundInputLayout& BoundInputLayout::operator=(BoundInputLayout&& moveFrom) never_throws
	{
		_underlying = std::move(moveFrom._underlying);
		_vertexStrides = std::move(moveFrom._vertexStrides);
		_allAttributesBound = moveFrom._allAttributesBound;
		return *this;
	}

        ////////////////////////////////////////////////////////////////////////////////////////////////

	class BoundUniforms::InterfaceResourcesHelper
	{
	public:
		struct Resource { unsigned _stage; unsigned _resourceIdex; };
		std::vector<std::pair<uint64_t, Resource>> _interfaceResources;
		std::vector<std::pair<uint64_t, Resource>> _samplerInterfaceResources;

		InterfaceResourcesHelper(
			IteratorRange<const intrusive_ptr<ID3D::ShaderReflection>*> reflections)
		{
			StringSection<> samplerPostFix("_sampler");

			for (unsigned s=0; s<reflections.size(); ++s) {
				if (!reflections[s]) continue;

				D3D11_SHADER_DESC shaderDesc;
				auto hresult = reflections[s]->GetDesc(&shaderDesc);
				if (!SUCCEEDED(hresult))
					continue;

				for (unsigned c2=0; c2<shaderDesc.BoundResources; ++c2) {
					D3D11_SHADER_INPUT_BIND_DESC bindingDesc;
					hresult = reflections[s]->GetResourceBindingDesc(c2, &bindingDesc);
					if (SUCCEEDED(hresult)) {
						if (bindingDesc.Type == D3D_SIT_SAMPLER) {
							// special case -- check for "_sampler" postfix
							StringSection<char> n = bindingDesc.Name;
							if (XlEndsWith(n, samplerPostFix)) {
								const uint64 hash = ParameterBox::MakeParameterNameHash(MakeStringSection(n.begin(), n.end() - samplerPostFix.Length()));
								_samplerInterfaceResources.push_back(std::make_pair(hash, Resource{s, c2}));
							}
							continue;
						}

						// To bind correctly, we must use the same name hashing as the parameter box -- so we might as well just
						// go ahead and call that here
						const uint64 hash = ParameterBox::MakeParameterNameHash(MakeStringSection(bindingDesc.Name, XlStringEnd(bindingDesc.Name)));
						_interfaceResources.push_back(std::make_pair(hash, Resource{s, c2}));
					}
				}
			}

			std::sort(_interfaceResources.begin(), _interfaceResources.end(), CompareFirst<uint64_t, Resource>());
			std::sort(_samplerInterfaceResources.begin(), _samplerInterfaceResources.end(), CompareFirst<uint64_t, Resource>());
		}
	};

    BoundUniforms::BoundUniforms(
		const ShaderProgram& shader,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
    {
		for (unsigned c=0; c<4; ++c)
			_boundUniformBufferSlots[c] = _boundResourceSlots[c] = _boundSamplerSlots[0] = 0;

		intrusive_ptr<ID3D::ShaderReflection> reflections[(unsigned)ShaderStage::Max];

            //  In this case, we must bind with every shader stage 
            //      (since a shader program actually reflects the state of the entire stage pipeline) 
		for (unsigned c=0; c<(unsigned)ShaderStage::Max; ++c) {
			const auto& compiledCode = shader.GetCompiledCode((ShaderStage)c);
			if (compiledCode.GetStage() == (ShaderStage)c)
				reflections[c] = CreateReflection(compiledCode);
		}

		InterfaceResourcesHelper helper(MakeIteratorRange(reflections));

		const UniformsStreamInterface* streams[] = { &interface0, &interface1, &interface2, &interface3 };
		for (unsigned streamIdx=0; streamIdx<dimof(streams); ++streamIdx) {
			const auto& stream = *streams[streamIdx];
			for (unsigned slot=0; slot<(unsigned)stream._cbBindings.size(); ++slot) {
				auto bound = BindConstantBuffer(
					helper,
					MakeIteratorRange(reflections),
					stream._cbBindings[slot]._hashName, slot, streamIdx,
					MakeIteratorRange(stream._cbBindings[slot]._elements));
				if (bound)
					_boundUniformBufferSlots[streamIdx] |= 1ull << uint64_t(slot);
			}
			for (unsigned slot=0; slot<(unsigned)stream._srvBindings.size(); ++slot) {
				auto bound = BindShaderResource(
					helper,
					MakeIteratorRange(reflections),
					stream._srvBindings[slot], slot, streamIdx);
				if (bound)
					_boundResourceSlots[streamIdx] |= 1ull << uint64_t(slot);

				auto boundSampler = BindSampler(
					helper,
					MakeIteratorRange(reflections),
					stream._srvBindings[slot], slot, streamIdx);
				if (boundSampler)
					_boundSamplerSlots[streamIdx] |= 1ull << uint64_t(slot);
			}
		}
    }

	BoundUniforms::BoundUniforms(
        const ComputeShader& shader,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
	{
		for (unsigned c=0; c<4; ++c)
			_boundUniformBufferSlots[c] = _boundResourceSlots[c] = _boundSamplerSlots[0] = 0;

		intrusive_ptr<ID3D::ShaderReflection> reflections[(unsigned)ShaderStage::Max];
		reflections[(unsigned)ShaderStage::Compute] = CreateReflection(shader.GetCompiledCode());

		InterfaceResourcesHelper helper(MakeIteratorRange(reflections));

		const UniformsStreamInterface* streams[] = { &interface0, &interface1, &interface2, &interface3 };
		for (unsigned streamIdx=0; streamIdx<dimof(streams); ++streamIdx) {
			const auto& stream = *streams[streamIdx];
			for (unsigned slot=0; slot<(unsigned)stream._cbBindings.size(); ++slot) {
				auto bound = BindConstantBuffer(
					helper,
					MakeIteratorRange(reflections),
					stream._cbBindings[slot]._hashName, slot, streamIdx,
					MakeIteratorRange(stream._cbBindings[slot]._elements));
				if (bound)
					_boundUniformBufferSlots[streamIdx] |= 1ull << uint64_t(slot);
			}
			for (unsigned slot=0; slot<(unsigned)stream._srvBindings.size(); ++slot) {
				auto bound = BindShaderResource(
					helper,
					MakeIteratorRange(reflections),
					stream._srvBindings[slot], slot, streamIdx);
				if (bound)
					_boundResourceSlots[streamIdx] |= 1ull << uint64_t(slot);

				auto boundSampler = BindSampler(
					helper,
					MakeIteratorRange(reflections),
					stream._srvBindings[slot], slot, streamIdx);
				if (boundSampler)
					_boundSamplerSlots[streamIdx] |= 1ull << uint64_t(slot);
			}
		}
	}

	BoundUniforms::BoundUniforms(
        const GraphicsPipeline& pipeline,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
	: BoundUniforms(
		pipeline.GetShaderProgram(),
		pipelineLayout,
		interface0, interface1, interface2, interface3)
	{
	}

    BoundUniforms::BoundUniforms() 
	{
		for (unsigned c=0; c<4; ++c)
			_boundUniformBufferSlots[c] = _boundResourceSlots[c] = _boundSamplerSlots[0] = 0;
	}

    BoundUniforms::~BoundUniforms() {}

    bool BoundUniforms::BindConstantBuffer(
		const InterfaceResourcesHelper& helper,
		IteratorRange<const intrusive_ptr<ID3D::ShaderReflection>*> reflections,
		uint64_t hashName, unsigned slot, unsigned stream,
        IteratorRange<const ConstantBufferElementDesc*> elements)
    {

            //
            //    Look for this constant buffer in the shader interface.
            //        If it exists, let's validate that the input layout is similar
            //        to what the shader expects.
            //

		auto range = EqualRange(helper._interfaceResources, hashName);
		if (range.first == range.second)
			return false;

		for (auto i=range.first; i!=range.second; ++i) {
			auto reflection = reflections[i->second._stage];
			assert(reflection);

			D3D11_SHADER_INPUT_BIND_DESC bindingDesc;
			auto hresult = reflection->GetResourceBindingDesc(i->second._resourceIdex, &bindingDesc);
			assert(SUCCEEDED(hresult));
			(void)hresult;

			ID3D::ShaderReflectionConstantBuffer* cbReflection = reflection->GetConstantBufferByName(bindingDesc.Name);
			if (!cbReflection) 
				continue;

			D3D11_SHADER_BUFFER_DESC cbDesc;
			XlZeroMemory(cbDesc);
			cbReflection->GetDesc(&cbDesc); // when GetConstantBufferByName() fails, it returns a dummy object that doesn't do anything in GetDesc();
			if (!cbDesc.Size) 
				continue;

			assert(Hash64(cbDesc.Name) == hashName);        // double check we got the correct reflection object

			#if defined(_DEBUG)
				for (size_t c=0; c<elements.size(); ++c) {
					ID3D::ShaderReflectionVariable* variable = nullptr;

					// search through to find a variable a name that matches the hash value
					for (unsigned v=0; v<cbDesc.Variables; ++v) {
						auto* var = cbReflection->GetVariableByIndex(v);
						if (!var) continue;
						D3D11_SHADER_VARIABLE_DESC desc;
						hresult = var->GetDesc(&desc);
						if (!SUCCEEDED(hresult)) continue;
						auto h = Hash64(desc.Name);
						if (h == elements[c]._semanticHash) {
							variable = var;
							break;
						}
					}

					if (variable) {
						D3D11_SHADER_VARIABLE_DESC variableDesc;
						XlZeroMemory(variableDesc);
						variable->GetDesc(&variableDesc);
						assert(variableDesc.Name!=nullptr);
						#if defined(_DEBUG)
							D3D11_SHADER_VARIABLE_DESC desc;
							variable->GetDesc(&desc);
							if (variableDesc.StartOffset != elements[c]._offset)
								Log(Warning) << "CB element offset not correct for element (" << desc.Name << ")" << std::endl;
							if (variableDesc.Size != std::max(1u, elements[c]._arrayElementCount) * BitsPerPixel(elements[c]._nativeFormat) / 8)
								Log(Warning) << "CB element size not correct for element (" << desc.Name << ")" << std::endl;
						#endif
					} else {
						Log(Warning) << "Missing while binding constant buffer elements to shader (" << elements[c]._semanticHash << ")" << std::endl;
					}
				}
			#endif

			StageBinding::Binding newBinding;
			newBinding._shaderSlot = bindingDesc.BindPoint;
			newBinding._inputInterfaceSlot = slot | (stream<<16);
			newBinding._savedCB = MakeConstantBuffer(GetObjectFactory(), cbDesc.Size);
			_stageBindings[i->second._stage]._shaderConstantBindings.push_back(newBinding);
		}

        return true;
    }

    bool BoundUniforms::BindShaderResource(
		const InterfaceResourcesHelper& helper,
		IteratorRange<const intrusive_ptr<ID3D::ShaderReflection>*> reflections,
		uint64 hashName, unsigned slot, unsigned stream)
    {
		auto range = EqualRange(helper._interfaceResources, hashName);
		if (range.first == range.second)
			return false;

		for (auto i=range.first; i!=range.second; ++i) {
			auto reflection = reflections[i->second._stage];
			assert(reflection);

			D3D11_SHADER_INPUT_BIND_DESC bindingDesc;
			auto hresult = reflection->GetResourceBindingDesc(i->second._resourceIdex, &bindingDesc);
			assert(SUCCEEDED(hresult));
			(void)hresult;

			StageBinding::Binding newBinding = {bindingDesc.BindPoint, slot | (stream<<16)};
			_stageBindings[i->second._stage]._shaderResourceBindings.push_back(newBinding);
		}

        return true;
    }

	bool BoundUniforms::BindSampler(
		const InterfaceResourcesHelper& helper,
		IteratorRange<const intrusive_ptr<ID3D::ShaderReflection>*> reflections,
		uint64_t hashName, unsigned slot, unsigned stream)
	{
		auto range = EqualRange(helper._samplerInterfaceResources, hashName);
		if (range.first == range.second)
			return false;

		for (auto i=range.first; i!=range.second; ++i) {
			auto reflection = reflections[i->second._stage];
			assert(reflection);

			D3D11_SHADER_INPUT_BIND_DESC bindingDesc;
			auto hresult = reflection->GetResourceBindingDesc(i->second._resourceIdex, &bindingDesc);
			assert(SUCCEEDED(hresult));
			assert(bindingDesc.Type == D3D_SIT_SAMPLER);
			(void)hresult;

			StageBinding::Binding newBinding = {bindingDesc.BindPoint, slot | (stream<<16)};
			_stageBindings[i->second._stage]._shaderSamplerBindings.push_back(newBinding);
		}

		return true;
	}

    void BoundUniforms::Apply(  
		DeviceContext& context, 
        unsigned streamIdx,
        const UniformsStream& stream) const
    {
        Internal::SetConstantBuffersFn scb[(unsigned)ShaderStage::Max] = 
        {
            &ID3D::DeviceContext::VSSetConstantBuffers,
            &ID3D::DeviceContext::PSSetConstantBuffers,
            &ID3D::DeviceContext::GSSetConstantBuffers,
            &ID3D::DeviceContext::HSSetConstantBuffers,
            &ID3D::DeviceContext::DSSetConstantBuffers,
            &ID3D::DeviceContext::CSSetConstantBuffers,
        };

        Internal::SetShaderResourcesFn scr[(unsigned)ShaderStage::Max] = 
        {
            &ID3D::DeviceContext::VSSetShaderResources,
            &ID3D::DeviceContext::PSSetShaderResources,
            &ID3D::DeviceContext::GSSetShaderResources,
            &ID3D::DeviceContext::HSSetShaderResources,
            &ID3D::DeviceContext::DSSetShaderResources,
            &ID3D::DeviceContext::CSSetShaderResources,
        };

		Internal::SetSamplersFn ss[(unsigned)ShaderStage::Max] = 
        {
            &ID3D::DeviceContext::VSSetSamplers,
            &ID3D::DeviceContext::PSSetSamplers,
            &ID3D::DeviceContext::GSSetSamplers,
            &ID3D::DeviceContext::HSSetSamplers,
            &ID3D::DeviceContext::DSSetSamplers,
            &ID3D::DeviceContext::CSSetSamplers,
        };

        for (unsigned s=0; s<dimof(_stageBindings); ++s) {
            const StageBinding& stage = _stageBindings[s];

            {
                unsigned lowestShaderSlot = ~unsigned(0x0), highestShaderSlot = 0;
                auto* currentCBS = context._currentCBs[s];
                uint32 setMask = 0x0;
                for (auto   i =stage._shaderConstantBindings.begin(); 
                            i!=stage._shaderConstantBindings.end(); ++i) {

					if ((i->_inputInterfaceSlot >> 16) != streamIdx)
						continue;

                    unsigned slot = i->_inputInterfaceSlot & 0xff;

					#if defined(_DEBUG)
						if (slot >= stream._constantBuffers.size())
							Throw(::Exceptions::BasicLabel("Uniform stream does not include Constant Buffer for bound resource. Expected CB bound at index (%u) of stream (%u). Only (%u) CBs were provided in the UniformsStream passed to BoundUniforms::Apply", slot, streamIdx, stream._constantBuffers.size()));
					#endif

                    if (slot < stream._constantBuffers.size()) {
						const auto& cb = stream._constantBuffers[slot];
                        if (cb._prebuiltBuffer) {

							auto* res = checked_cast<const Resource*>(cb._prebuiltBuffer)->_underlying.get();
							assert(QueryInterfaceCast<ID3D::Buffer>(res));

                            setMask |= 1 << (i->_shaderSlot);
                            if (res != currentCBS[i->_shaderSlot]) {
                                currentCBS[i->_shaderSlot] = (ID3D::Buffer*)res;
                                lowestShaderSlot = std::min(lowestShaderSlot, i->_shaderSlot);
                                highestShaderSlot = std::max(highestShaderSlot, i->_shaderSlot);
                            }

                        } else if (cb._packet.size()) {

                            i->_savedCB.Update(context, cb._packet.begin(), cb._packet.size(), 0, Buffer::UpdateFlags::Internal_Copy);
                            setMask |= 1 << (i->_shaderSlot);
                            if (i->_savedCB.GetUnderlying() != currentCBS[i->_shaderSlot]) {
                                currentCBS[i->_shaderSlot] = i->_savedCB.GetUnderlying();
                                lowestShaderSlot = std::min(lowestShaderSlot, i->_shaderSlot);
                                highestShaderSlot = std::max(highestShaderSlot, i->_shaderSlot);
                            }

                        } else {
							Log(Warning) << "Did not find valid CB data to bind in BoundUniforms::Apply for index ("  << slot << ") in stream (" << streamIdx << ")" << std::endl;
						}
                    }

                }

                if (lowestShaderSlot <= highestShaderSlot) {
                    
                        //  We have to clear out the pointers to CBs that aren't explicit set. This is because
                        //  we don't know if those pointers are still valid currently -- they may have been deleted
                        //  somewhere else in the pipeline
                    /*for (unsigned c=lowestShaderSlot; c<=highestShaderSlot; ++c)
                        if (!(setMask & (1<<c)))
                            currentCBS[c] = nullptr;*/

                    Internal::SetConstantBuffersFn fn = scb[s];
                    (context.GetUnderlying()->*fn)(lowestShaderSlot, highestShaderSlot-lowestShaderSlot+1, &currentCBS[lowestShaderSlot]);
                }
            }

            {
                unsigned lowestShaderSlot = ~unsigned(0x0), highestShaderSlot = 0;
                auto* currentSRVs = context._currentSRVs[s];
                uint32 setMask = 0;
                for (auto   i =stage._shaderResourceBindings.cbegin(); 
                            i!=stage._shaderResourceBindings.cend(); ++i) {

					if ((i->_inputInterfaceSlot >> 16) != streamIdx)
						continue;

					unsigned slot = i->_inputInterfaceSlot & 0xff;
					#if defined(_DEBUG)
						if (slot >= stream._resources.size())
							Throw(::Exceptions::BasicLabel("Uniform stream does not include SRV for bound resource. Expected SRV bound at index (%u) of stream (%u). Only (%u) SRVs were provided in the UniformsStream passed to BoundUniforms::Apply", slot, streamIdx, stream._resources.size()));
					#endif
                    
                    if (stream._resources[slot]) {
                        currentSRVs[i->_shaderSlot] = ((ShaderResourceView*)stream._resources[slot])->GetUnderlying();
                        lowestShaderSlot = std::min(lowestShaderSlot, i->_shaderSlot);
                        highestShaderSlot = std::max(highestShaderSlot, i->_shaderSlot);
                        setMask |= 1<<(i->_shaderSlot);
                    }
                }

                if (lowestShaderSlot <= highestShaderSlot) {

                    /*for (unsigned c=lowestShaderSlot; c<=highestShaderSlot; ++c)
                        if (!(setMask & (1<<c)))
                            currentSRVs[c] = nullptr;*/

                    Internal::SetShaderResourcesFn fn = scr[s];
                    (context.GetUnderlying()->*fn)(
                        lowestShaderSlot, highestShaderSlot-lowestShaderSlot+1,
                        &currentSRVs[lowestShaderSlot]);
                }
            }

			{
                unsigned lowestShaderSlot = ~unsigned(0x0), highestShaderSlot = 0;
                auto* currentSSs = context._currentSSs[s];
                uint32 setMask = 0;
                for (auto   i =stage._shaderSamplerBindings.cbegin(); 
                            i!=stage._shaderSamplerBindings.cend(); ++i) {

					if ((i->_inputInterfaceSlot >> 16) != streamIdx)
						continue;

					unsigned slot = i->_inputInterfaceSlot & 0xff;
					if (slot >= stream._samplers.size())
						continue;		// samplers are optionally set; so we just skip in this case (no warning, exception)
                    
                    if (stream._samplers[slot]) {
                        currentSSs[i->_shaderSlot] = ((SamplerState*)stream._samplers[slot])->GetUnderlying();
                        lowestShaderSlot = std::min(lowestShaderSlot, i->_shaderSlot);
                        highestShaderSlot = std::max(highestShaderSlot, i->_shaderSlot);
                        setMask |= 1<<(i->_shaderSlot);
                    }
                }

                if (lowestShaderSlot <= highestShaderSlot) {

                    /*for (unsigned c=lowestShaderSlot; c<=highestShaderSlot; ++c)
                        if (!(setMask & (1<<c)))
                            currentSSs[c] = nullptr;*/

                    Internal::SetSamplersFn fn = ss[s];
                    (context.GetUnderlying()->*fn)(
                        lowestShaderSlot, highestShaderSlot-lowestShaderSlot+1,
                        &currentSSs[lowestShaderSlot]);
                }
            }
        }
    }

    void BoundUniforms::UnbindShaderResources(DeviceContext& context, unsigned streamIndex) const
    {
		ID3D::ShaderResourceView* srv = nullptr;
        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Vertex]._shaderResourceBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
				context.GetUnderlying()->VSSetShaderResources(b._shaderSlot, 1, &srv);
                context._currentSRVs[(unsigned)ShaderStage::Vertex][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Pixel]._shaderResourceBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
				context.GetUnderlying()->PSSetShaderResources(b._shaderSlot, 1, &srv);
                context._currentSRVs[(unsigned)ShaderStage::Pixel][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Geometry]._shaderResourceBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
                context.GetUnderlying()->GSSetShaderResources(b._shaderSlot, 1, &srv);
                context._currentSRVs[(unsigned)ShaderStage::Geometry][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Compute]._shaderResourceBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
                context.GetUnderlying()->CSSetShaderResources(b._shaderSlot, 1, &srv);
                context._currentSRVs[(unsigned)ShaderStage::Compute][b._shaderSlot] = nullptr;
            }
        }

		ID3D::SamplerState* ss = nullptr;
        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Vertex]._shaderSamplerBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
				context.GetUnderlying()->VSSetSamplers(b._shaderSlot, 1, &ss);
                context._currentSSs[(unsigned)ShaderStage::Vertex][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Pixel]._shaderSamplerBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
				context.GetUnderlying()->PSSetSamplers(b._shaderSlot, 1, &ss);
                context._currentSSs[(unsigned)ShaderStage::Pixel][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Geometry]._shaderSamplerBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
                context.GetUnderlying()->GSSetSamplers(b._shaderSlot, 1, &ss);
                context._currentSSs[(unsigned)ShaderStage::Geometry][b._shaderSlot] = nullptr;
            }
        }

        for (const auto& b:_stageBindings[(unsigned)ShaderStage::Compute]._shaderSamplerBindings) {
            if ((b._inputInterfaceSlot >> 16)==streamIndex) {
                context.GetUnderlying()->CSSetSamplers(b._shaderSlot, 1, &ss);
                context._currentSSs[(unsigned)ShaderStage::Compute][b._shaderSlot] = nullptr;
            }
        }
    }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    void BoundClassInterfaces::Bind(uint64 hashName, unsigned bindingArrayIndex, const char instance[])
    {
        for (auto s=0; s<(unsigned)ShaderStage::Max; ++s) {
            if (!_stageBindings[s]._linkage || !_stageBindings[s]._reflection) continue;

            bool gotoNextStage = false;
            D3D11_SHADER_DESC shaderDesc;
            auto hresult = _stageBindings[s]._reflection->GetDesc(&shaderDesc);
            if (SUCCEEDED(hresult)) {
                for (unsigned c=0; c<shaderDesc.ConstantBuffers && !gotoNextStage; ++c) {
				    auto cb = _stageBindings[s]._reflection->GetConstantBufferByIndex(c);
				    if (cb) {
                        D3D11_SHADER_BUFFER_DESC cbDesc;
                        hresult = cb->GetDesc(&cbDesc);
                        if (SUCCEEDED(hresult)) {
                            for (unsigned q=0; q<cbDesc.Variables; ++q) {
                                auto var = cb->GetVariableByIndex(q);
                                if (!var) continue;

                                auto type = var->GetType();
                                if (!type) continue;

                                D3D11_SHADER_TYPE_DESC typeDesc;
                                hresult = type->GetDesc(&typeDesc);
                                if (!SUCCEEDED(hresult)) continue;

                                if (typeDesc.Class != D3D11_SVC_INTERFACE_POINTER) continue;

                                D3D11_SHADER_VARIABLE_DESC varDesc;
                                hresult  = var->GetDesc(&varDesc);
                                if (!SUCCEEDED(hresult)) continue;

                                const uint64 hash = Hash64(varDesc.Name, XlStringEnd(varDesc.Name));
					            if (hash != hashName) continue;

                                auto finalSlot = var->GetInterfaceSlot(bindingArrayIndex);
                                if (finalSlot >= _stageBindings[s]._classInstanceArray.size()) continue;

                                ID3D::ClassInstance* classInstance = nullptr;
                                hresult = _stageBindings[s]._linkage->CreateClassInstance(
                                    instance, 
                                    0, 0, 0, 0, 
                                    &classInstance);

                                if (!SUCCEEDED(hresult)) continue;
                                _stageBindings[s]._classInstanceArray[finalSlot] = moveptr(classInstance);

                                gotoNextStage = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    const std::vector<intrusive_ptr<ID3D::ClassInstance>>& BoundClassInterfaces::GetClassInstances(ShaderStage stage) const
    {
        return _stageBindings[unsigned(stage)]._classInstanceArray;
    }

	static uint64_t s_nextBoundClassInterfaceGuid = 1;

    BoundClassInterfaces::BoundClassInterfaces(const ShaderProgram& shader)
	: _guid(s_nextBoundClassInterfaceGuid++)
    {
		for (unsigned c=0; c<(unsigned)ShaderStage::Max; ++c) {
			auto* classLinkage = shader.GetClassLinkage((ShaderStage)c);
			if (classLinkage) {
				_stageBindings[c]._linkage = classLinkage; 
				_stageBindings[c]._reflection = CreateReflection(shader.GetCompiledCode((ShaderStage)c));
				if (_stageBindings[c]._reflection)
					_stageBindings[c]._classInstanceArray.resize(
						_stageBindings[c]._reflection->GetNumInterfaceSlots(), nullptr);
			}
		}
    }

    BoundClassInterfaces::BoundClassInterfaces() : _guid(0) {}
    BoundClassInterfaces::~BoundClassInterfaces() {}

	void NumericUniformsInterface::Reset() {}

	NumericUniformsInterface::NumericUniformsInterface(DeviceContext& context, ShaderStage shaderStage)
	: _context(&context)
	, _stage(shaderStage)
	{
		_setUnorderedAccessViews = nullptr;
		switch (shaderStage)
		{
		case ShaderStage::Vertex:
			_setShaderResources = &ID3D::DeviceContext::VSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::VSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::VSSetConstantBuffers;
			break;
		case ShaderStage::Pixel:
			_setShaderResources = &ID3D::DeviceContext::PSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::PSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::PSSetConstantBuffers;
			break;
		case ShaderStage::Geometry:
			_setShaderResources = &ID3D::DeviceContext::GSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::GSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::GSSetConstantBuffers;
			break;
		case ShaderStage::Hull:
			_setShaderResources = &ID3D::DeviceContext::HSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::HSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::HSSetConstantBuffers;
			break;
		case ShaderStage::Domain:
			_setShaderResources = &ID3D::DeviceContext::DSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::DSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::DSSetConstantBuffers;
			break;
		case ShaderStage::Compute:
			_setShaderResources = &ID3D::DeviceContext::CSSetShaderResources;
			_setSamplers = &ID3D::DeviceContext::CSSetSamplers;
			_setConstantBuffers = &ID3D::DeviceContext::CSSetConstantBuffers;
			_setUnorderedAccessViews = &ID3D::DeviceContext::CSSetUnorderedAccessViews;
			break;
		default:
			assert(0);
			_setShaderResources = nullptr;
			_setSamplers = nullptr;
			_setConstantBuffers = nullptr;
			break;
		}
	}

	NumericUniformsInterface::NumericUniformsInterface()
	{
		_context = nullptr;
		_stage = ShaderStage::Null;
		_setShaderResources = nullptr;
		_setSamplers = nullptr;
		_setConstantBuffers = nullptr;
		_setUnorderedAccessViews = nullptr;
	}

    NumericUniformsInterface::~NumericUniformsInterface()
	{
	}

#if 0
    static Format AsNativeFormat(D3D11_SHADER_TYPE_DESC typeDesc)
    {
        switch (typeDesc.Type) {
        case D3D10_SVT_INT:
            if (typeDesc.Rows <= 1) {
                if (typeDesc.Columns == 4) {
                    return Format::R32G32B32A32_SINT;
                } else if (typeDesc.Columns == 2) {
                    return Format::R32G32_SINT;
                } else if (typeDesc.Columns == 1) {
                    return Format::R32_SINT;
                } else {
                    return Format::Unknown;
                }
            }
            break;
        case D3D10_SVT_UINT:
            if (typeDesc.Rows <= 1) {
                if (typeDesc.Columns == 4) {
                    return Format::R32G32B32A32_UINT;
                } else if (typeDesc.Columns == 2) {
                    return Format::R32G32_UINT;
                } else if (typeDesc.Columns == 1) {
                    return Format::R32_UINT;
                } else {
                    return Format::Unknown;
                }
            }
            break;
        case D3D10_SVT_UINT8:
            if (typeDesc.Rows <= 1) {
                if (typeDesc.Columns == 4) {
                    return Format::R8G8B8A8_UINT;
                } else if (typeDesc.Columns == 2) {
                    return Format::R8G8B8A8_UINT;
                } else if (typeDesc.Columns == 1) {
                    return Format::R8G8B8A8_UINT;
                } else {
                    return Format::Unknown;
                }
            }
            break;
        case D3D10_SVT_FLOAT:
            if (typeDesc.Rows <= 1) {
                if (typeDesc.Columns == 4) {
                    return Format::R32G32B32A32_FLOAT;
                } else if (typeDesc.Columns == 2) {
                    return Format::R32G32_FLOAT;
                } else if (typeDesc.Columns == 1) {
                    return Format::R32_FLOAT;
                } else {
                    return Format::Unknown;
                }
            }
            break;
        case D3D11_SVT_DOUBLE:
        default:
            break;
        }
        return Format::Unknown;
    }
#endif

}}


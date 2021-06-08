// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderService.h"
#include "Types.h"	// For PS_DefShaderModel
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/MemoryUtils.h"
#include <assert.h>

namespace RenderCore
{

        ////////////////////////////////////////////////////////////

    static ShaderStage AsShaderStage(StringSection<::Assets::ResChar> shaderModel)
    {
		assert(shaderModel.size() >= 1);
        switch (shaderModel[0]) {
        case 'v':   return ShaderStage::Vertex;
        case 'p':   return ShaderStage::Pixel;
        case 'g':   return ShaderStage::Geometry;
        case 'd':   return ShaderStage::Domain;
        case 'h':   return ShaderStage::Hull;
        case 'c':   return ShaderStage::Compute;
        default:    return ShaderStage::Null;
        }
    }

    ShaderStage ILowLevelCompiler::ResId::AsShaderStage() const { return RenderCore::AsShaderStage(_shaderModel); }

	CompiledShaderByteCode::CompiledShaderByteCode(const ::Assets::Blob& shader, const ::Assets::DependencyValidation& depVal, StringSection<Assets::ResChar>)
	: _shader(shader)
	, _depVal(depVal)
	{
		if (_shader && !_shader->empty()) {
			if (_shader->size() < sizeof(ShaderService::ShaderHeader))
				Throw(::Exceptions::BasicLabel("Shader byte code is too small for shader header"));
			const auto& hdr = *(const ShaderService::ShaderHeader*)_shader->data();
			if (hdr._version != ShaderService::ShaderHeader::Version)
				Throw(::Exceptions::BasicLabel("Unexpected version in shader header. Found (%i), expected (%i)", hdr._version, ShaderService::ShaderHeader::Version));
		}
	}

	CompiledShaderByteCode::CompiledShaderByteCode()
	{
	}

    CompiledShaderByteCode::~CompiledShaderByteCode() {}

	IteratorRange<const void*> CompiledShaderByteCode::GetByteCode() const
	{
		if (!_shader || _shader->empty()) return {};
		return MakeIteratorRange(
			PtrAdd(AsPointer(_shader->begin()), sizeof(ShaderService::ShaderHeader)), 
			AsPointer(_shader->end()));
	}

    bool CompiledShaderByteCode::DynamicLinkingEnabled() const
    {
        if (!_shader || _shader->empty()) return false;

		assert(_shader->size() >= sizeof(ShaderService::ShaderHeader));
        auto& hdr = *(const ShaderService::ShaderHeader*)AsPointer(_shader->begin());
        assert(hdr._version == ShaderService::ShaderHeader::Version);
        return hdr._dynamicLinkageEnabled != 0;
    }

	ShaderStage		CompiledShaderByteCode::GetStage() const
	{
		if (!_shader || _shader->empty()) return ShaderStage::Null;

		assert(_shader->size() >= sizeof(ShaderService::ShaderHeader));
		auto& hdr = *(const ShaderService::ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderService::ShaderHeader::Version);
		return AsShaderStage(hdr._shaderModel);
	}

    StringSection<>             CompiledShaderByteCode::GetIdentifier() const
    {
        if (!_shader || _shader->empty()) return {};

		assert(_shader->size() >= sizeof(ShaderService::ShaderHeader));
		auto& hdr = *(const ShaderService::ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderService::ShaderHeader::Version);
		return MakeStringSection(hdr._identifier);
    }

    StringSection<>             CompiledShaderByteCode::GetEntryPoint() const
    {
        if (!_shader || _shader->empty()) return {};

		assert(_shader->size() >= sizeof(ShaderService::ShaderHeader));
		auto& hdr = *(const ShaderService::ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderService::ShaderHeader::Version);
		return MakeStringSection(hdr._entryPoint);
    }

    const uint64 CompiledShaderByteCode::CompileProcessType = ConstHash64<'Shdr', 'Byte', 'Code'>::Value;

        ////////////////////////////////////////////////////////////

    ILowLevelCompiler::ResId::ResId(StringSection<ResChar> filename, StringSection<ResChar> entryPoint, StringSection<ResChar> shaderModel)
    {
        XlCopyString(_filename, filename);
        XlCopyString(_entryPoint, entryPoint);

        _dynamicLinkageEnabled = shaderModel[0] == '!';
        if (_dynamicLinkageEnabled) {
			XlCopyString(_shaderModel, MakeStringSection(shaderModel.begin()+1, shaderModel.end()));
		} else {
			XlCopyString(_shaderModel, shaderModel);
		}
    }

    ILowLevelCompiler::ResId::ResId()
    {
        _filename[0] = '\0'; _entryPoint[0] = '\0'; _shaderModel[0] = '\0';
        _dynamicLinkageEnabled = false;
    }

    void ShaderService::SetShaderSource(std::shared_ptr<IShaderSource> shaderSource)
    {
        _shaderSource = shaderSource;
    }

    auto ShaderService::GetShaderSource() -> const std::shared_ptr<IShaderSource>&
    {
        return _shaderSource;
    }

    ShaderService::ShaderService() {}
    ShaderService::~ShaderService() {}

    IShaderSource::~IShaderSource() {}
    ILowLevelCompiler::~ILowLevelCompiler() {}


	ShaderService::ShaderHeader::ShaderHeader(StringSection<char> identifier, StringSection<char> shaderModel, StringSection<char> entryPoint, bool dynamicLinkageEnabled)
	: _version(Version)
	, _dynamicLinkageEnabled(unsigned(dynamicLinkageEnabled))
	{
        XlCopyString(_identifier, identifier);
		XlCopyString(_shaderModel, shaderModel);
        XlCopyString(_entryPoint, entryPoint);
	}

    const unsigned ShaderService::ShaderHeader::Version = 3u;
}


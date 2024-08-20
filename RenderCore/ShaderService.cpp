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

    static ShaderStage AsShaderStage(StringSection<> shaderModel)
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

    ShaderStage ShaderCompileResourceName::AsShaderStage() const { return RenderCore::AsShaderStage(_shaderModel); }

	uint64_t ShaderCompileResourceName::CalculateHash(uint64_t seed) const
	{
		return Hash64(_filename, Hash64(_entryPoint, Hash64(_shaderModel, seed)+_compilationFlags));
	}

	CompiledShaderByteCode::CompiledShaderByteCode(const ::Assets::Blob& shader, const ::Assets::DependencyValidation& depVal, StringSection<>)
	: _shader(shader)
	, _depVal(depVal)
	{
		if (_shader && !_shader->empty()) {
			if (_shader->size() < sizeof(ShaderHeader))
				Throw(::Exceptions::BasicLabel("Shader byte code is too small for shader header"));
			const auto& hdr = *(const ShaderHeader*)_shader->data();
			if (hdr._version != ShaderHeader::Version)
				Throw(::Exceptions::BasicLabel("Unexpected version in shader header. Found (%i), expected (%i)", hdr._version, ShaderHeader::Version));
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
			PtrAdd(AsPointer(_shader->begin()), sizeof(ShaderHeader)), 
			AsPointer(_shader->end()));
	}

    bool CompiledShaderByteCode::DynamicLinkingEnabled() const
    {
        if (!_shader || _shader->empty()) return false;

		assert(_shader->size() >= sizeof(ShaderHeader));
        auto& hdr = *(const ShaderHeader*)AsPointer(_shader->begin());
        assert(hdr._version == ShaderHeader::Version);
        return hdr._dynamicLinkageEnabled != 0;
    }

	ShaderStage		CompiledShaderByteCode::GetStage() const
	{
		if (!_shader || _shader->empty()) return ShaderStage::Null;

		assert(_shader->size() >= sizeof(ShaderHeader));
		auto& hdr = *(const ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderHeader::Version);
		return AsShaderStage(hdr._shaderModel);
	}

    StringSection<>             CompiledShaderByteCode::GetIdentifier() const
    {
        if (!_shader || _shader->empty()) return {};

		assert(_shader->size() >= sizeof(ShaderHeader));
		auto& hdr = *(const ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderHeader::Version);
		return MakeStringSection(hdr._identifier);
    }

    StringSection<>             CompiledShaderByteCode::GetEntryPoint() const
    {
        if (!_shader || _shader->empty()) return {};

		assert(_shader->size() >= sizeof(ShaderHeader));
		auto& hdr = *(const ShaderHeader*)AsPointer(_shader->begin());
		assert(hdr._version == ShaderHeader::Version);
		return MakeStringSection(hdr._entryPoint);
    }

        ////////////////////////////////////////////////////////////

    ShaderCompileResourceName::ShaderCompileResourceName(std::string filename, std::string entryPoint, std::string shaderModel)
	: _filename(std::move(filename)), _entryPoint(std::move(entryPoint)), _shaderModel(std::move(shaderModel))
    {
		// Read prefixes from the shader model string
		_compilationFlags = 0;
		while (!_shaderModel.empty()) {
			if (_shaderModel[0] == '!') {
				_compilationFlags |= CompilationFlags::DynamicLinkageEnabled;
				_shaderModel.erase(_shaderModel.begin());
			} else if (_shaderModel[0] == '$') {
				_compilationFlags |= CompilationFlags::DebugSymbols | CompilationFlags::DisableOptimizations;
				_shaderModel.erase(_shaderModel.begin());
			} else
				break;
		}
    }

	ShaderCompileResourceName::ShaderCompileResourceName(std::string filename, std::string entryPoint, std::string shaderModel, CompilationFlags::BitField compilationFlags)
	: _filename(std::move(filename)), _entryPoint(std::move(entryPoint)), _shaderModel(std::move(shaderModel)), _compilationFlags(compilationFlags)
	{
	}

    IShaderSource::~IShaderSource() {}
    ILowLevelCompiler::~ILowLevelCompiler() {}


	CompiledShaderByteCode::ShaderHeader::ShaderHeader(StringSection<char> identifier, StringSection<char> shaderModel, StringSection<char> entryPoint, bool dynamicLinkageEnabled)
	: _version(Version)
	, _dynamicLinkageEnabled(unsigned(dynamicLinkageEnabled))
	{
        XlCopyString(_identifier, identifier);
		XlCopyString(_shaderModel, shaderModel);
        XlCopyString(_entryPoint, entryPoint);
	}

    const unsigned CompiledShaderByteCode::ShaderHeader::Version = 3u;

	ShaderCompileResourceName MakeShaderCompileResourceName(StringSection<> initializer)
	{
		StringSection<> filename, entryPoint, shaderModel;
		auto splitter = MakeFileNameSplitter(initializer);

		filename = splitter.AllExceptParameters();

		if (splitter.Parameters().IsEmpty()) {
			entryPoint = "main";
		} else {
			auto startShaderModel = XlFindChar(splitter.Parameters().begin(), ':');
			if (!startShaderModel) {
				entryPoint = splitter.Parameters();
			} else {
				entryPoint = {splitter.Parameters().begin(), startShaderModel};
				shaderModel = {startShaderModel+1, splitter.Parameters().end()};
			}
		}
		assert(!filename.IsEmpty());
		return { filename.AsString(), entryPoint.AsString(), shaderModel.AsString() };
	}

	std::string s_SMVS = "vs_*";
	std::string s_SMGS = "gs_*";
	std::string s_SMPS = "ps_*";
	std::string s_SMDS = "ds_*";
	std::string s_SMHS = "hs_*";
	std::string s_SMCS = "cs_*";
}


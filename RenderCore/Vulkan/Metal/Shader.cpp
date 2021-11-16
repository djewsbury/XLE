// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "PipelineLayout.h"
#include "ShaderReflection.h"
#include "../../ShaderService.h"
#include "../../Types.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/DepVal.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"

#pragma warning(disable:4702)

namespace RenderCore { namespace Metal_Vulkan
{
    using ::Assets::ResChar;

	static CompiledShaderByteCode s_null;

	std::vector<uint32_t> AppendSODecorations(IteratorRange<const void*> byteCode);

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& ps)
    {
		_pipelineLayout = checked_pointer_cast<CompiledPipelineLayout>(pipelineLayout);
		_interfaceBindingHash = DefaultSeed64;

		_validationCallback = ::Assets::GetDepValSys().Make();

		if (vs.GetStage() != ShaderStage::Null) {
			assert(vs.GetStage() == ShaderStage::Vertex);
			auto byteCode = vs.GetByteCode();
            _modules[(unsigned)ShaderStage::Vertex] = factory.CreateShaderModule(byteCode);
			_compiledCode[(unsigned)ShaderStage::Vertex] = vs;
			assert(_modules[(unsigned)ShaderStage::Vertex]);
			_validationCallback.RegisterDependency(vs.GetDependencyValidation());

			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end(), _interfaceBindingHash);
		}

		if (ps.GetStage() != ShaderStage::Null) {
			assert(ps.GetStage() == ShaderStage::Pixel);
            auto byteCode = ps.GetByteCode();
			_modules[(unsigned)ShaderStage::Pixel] = factory.CreateShaderModule(byteCode);
			_compiledCode[(unsigned)ShaderStage::Pixel] = ps;
			assert(_modules[(unsigned)ShaderStage::Pixel]);
			_validationCallback.RegisterDependency(ps.GetDependencyValidation());

			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end(), _interfaceBindingHash);
		}

		// auto& globals = Internal::VulkanGlobalsTemp::GetInstance();
		// Assets::RegisterAssetDependency(_validationCallback, globals._graphicsRootSignatureFile->GetDependencyValidation());
		// _pipelineLayoutConfig = globals._mainGraphicsConfig;
    }
    
    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									StreamOutputInitializers so)
    :   ShaderProgram(factory, pipelineLayout, vs, ps)
    {
		if (gs.GetStage() != ShaderStage::Null) {
			assert(gs.GetStage() == ShaderStage::Geometry);
			auto byteCode = gs.GetByteCode();
			if (!so._outputElements.empty()) {
				auto decorated = PatchUpStreamOutput(byteCode, so);
				_modules[(unsigned)ShaderStage::Geometry] = factory.CreateShaderModule(decorated);
			} else {
				_modules[(unsigned)ShaderStage::Geometry] = factory.CreateShaderModule(byteCode);
			}
			_compiledCode[(unsigned)ShaderStage::Geometry] = gs;
			assert(_modules[(unsigned)ShaderStage::Geometry]);
			_validationCallback.RegisterDependency(gs.GetDependencyValidation());

			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end(), _interfaceBindingHash);
		}
    }

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									const CompiledShaderByteCode& hs,
									const CompiledShaderByteCode& ds,
									StreamOutputInitializers so)
    :   ShaderProgram(factory, pipelineLayout, vs, gs, ps, so)
    {
		if (hs.GetStage() != ShaderStage::Null) {
			assert(hs.GetStage() == ShaderStage::Hull);
			auto byteCode = hs.GetByteCode();
            _modules[(unsigned)ShaderStage::Hull] = factory.CreateShaderModule(byteCode);
			_compiledCode[(unsigned)ShaderStage::Hull] = hs;
			assert(_modules[(unsigned)ShaderStage::Hull]);
			_validationCallback.RegisterDependency(hs.GetDependencyValidation());

			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end(), _interfaceBindingHash);
		}

		if (ds.GetStage() != ShaderStage::Null) {
			assert(ds.GetStage() == ShaderStage::Domain);
			auto byteCode = ds.GetByteCode();
            _modules[(unsigned)ShaderStage::Domain] = factory.CreateShaderModule(byteCode);
			_compiledCode[(unsigned)ShaderStage::Domain] = ds;
			assert(_modules[(unsigned)ShaderStage::Domain]);
			_validationCallback.RegisterDependency(ds.GetDependencyValidation());

			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end(), _interfaceBindingHash);
		}
    }

	ShaderProgram::ShaderProgram() {}
    ShaderProgram::~ShaderProgram() {}

    bool ShaderProgram::DynamicLinkingEnabled() const { return false; }

	/*void        ShaderProgram::Apply(GraphicsPipelineBuilder& pipeline) const
    {
        if (pipeline._shaderProgram != this) {
            pipeline._shaderProgram = this;
			pipeline._PipelineDescriptorsLayoutBuilder.SetShaderBasedDescriptorSets(*_pipelineLayoutConfig);
            pipeline._pipelineStale = true;
        }
    }

	void        ShaderProgram::Apply(GraphicsPipelineBuilder& pipeline, const BoundClassInterfaces&) const
	{
		if (pipeline._shaderProgram != this) {
            pipeline._shaderProgram = this;
			pipeline._PipelineDescriptorsLayoutBuilder.SetShaderBasedDescriptorSets(*_pipelineLayoutConfig);
            pipeline._pipelineStale = true;
        }
	}*/

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ComputeShader::ComputeShader(
		ObjectFactory& factory,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		const CompiledShaderByteCode& compiledShader)
    : _compiledCode(compiledShader)
    {
		_pipelineLayout = checked_pointer_cast<CompiledPipelineLayout>(pipelineLayout);
		_interfaceBindingHash = 0;

		if (compiledShader.GetStage() != ShaderStage::Null) {
			assert(compiledShader.GetStage() == ShaderStage::Compute);
			auto byteCode = compiledShader.GetByteCode();
            _module = factory.CreateShaderModule(byteCode);
			_interfaceBindingHash = Hash64(byteCode.begin(), byteCode.end());
		}

        _validationCallback = compiledShader.GetDependencyValidation();

		// auto& globals = Internal::VulkanGlobalsTemp::GetInstance();
		// Assets::RegisterAssetDependency(_validationCallback, globals._computeRootSignatureFile->GetDependencyValidation());
		// _pipelineLayoutConfig = globals._mainComputeConfig;
    }

    ComputeShader::ComputeShader() {}
    ComputeShader::~ComputeShader() {}

        ////////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_ptr<::Assets::Marker<CompiledShaderByteCode>> MakeByteCodeFuture(ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
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
		if (!XlFindStringI(initializer, profileStr)) {
			ResChar temp[MaxPath];
			StringMeldInPlace(temp) << initializer << ":" << profileStr << "*";
			return ::Assets::MakeAsset<CompiledShaderByteCode>(temp, definesTable);
		}
		else {
			return ::Assets::MakeAsset<CompiledShaderByteCode>(initializer, definesTable);
		}
	}

	static void TryRegisterDependency(
		::Assets::DependencyValidation& dst,
		const ::Assets::DependencyValidation& dependency)
	{
		if (dependency)
			dst.RegisterDependency(dependency);
	}

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		::Assets::WhenAll(vsFuture, psFuture).ThenConstructToPromise(
			std::move(promise),
			[pipelineLayout](const CompiledShaderByteCode& vsActual, const CompiledShaderByteCode& psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, vsActual, psActual);
			});
	}

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> gsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto gsFuture = MakeByteCodeFuture(ShaderStage::Geometry, gsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		::Assets::WhenAll(vsFuture, gsFuture, psFuture).ThenConstructToPromise(
			std::move(promise),
			[pipelineLayout](const CompiledShaderByteCode& vsActual, const CompiledShaderByteCode& gsActual, const CompiledShaderByteCode& psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, vsActual, gsActual, psActual);
			});
	}

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> gsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> hsName,
		StringSection<::Assets::ResChar> dsName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto gsFuture = MakeByteCodeFuture(ShaderStage::Geometry, gsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);
		auto hsFuture = MakeByteCodeFuture(ShaderStage::Hull, hsName, definesTable);
		auto dsFuture = MakeByteCodeFuture(ShaderStage::Domain, dsName, definesTable);

		::Assets::WhenAll(vsFuture, gsFuture, psFuture, hsFuture, dsFuture).ThenConstructToPromise(
			std::move(promise),
			[pipelineLayout](const auto& vsActual, const auto& gsActual, const auto& psActual, const auto& hsActual, const auto& dsActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, vsActual, gsActual, psActual, hsActual, dsActual);
			});
	}

	void ComputeShader::ConstructToPromise(
		std::promise<std::shared_ptr<ComputeShader>>&& promise,
		std::shared_ptr<ICompiledPipelineLayout> pipelineLayout,
		StringSection<::Assets::ResChar> codeName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto code = MakeByteCodeFuture(ShaderStage::Compute, codeName, definesTable);

		::Assets::WhenAll(code).ThenConstructToPromise(
			std::move(promise),
			[pipelineLayout](const auto& csActual) {
				return std::make_shared<ComputeShader>(GetObjectFactory(), pipelineLayout, csActual);
			});
	}

}}


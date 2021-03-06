// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "PipelineLayout.h"
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


        ////////////////////////////////////////////////////////////////////////////////////////////////

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& ps)
    {
		_pipelineLayout = checked_pointer_cast<CompiledPipelineLayout>(pipelineLayout);
		_validationCallback = std::make_shared<::Assets::DependencyValidation>();

		if (vs.GetStage() != ShaderStage::Null) {
			assert(vs.GetStage() == ShaderStage::Vertex);
            _modules[(unsigned)ShaderStage::Vertex] = factory.CreateShaderModule(vs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Vertex] = vs;
			assert(_modules[(unsigned)ShaderStage::Vertex]);
			::Assets::RegisterAssetDependency(_validationCallback, vs.GetDependencyValidation());
		}

		if (ps.GetStage() != ShaderStage::Null) {
			assert(ps.GetStage() == ShaderStage::Pixel);
            _modules[(unsigned)ShaderStage::Pixel] = factory.CreateShaderModule(ps.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Pixel] = ps;
			assert(_modules[(unsigned)ShaderStage::Pixel]);
			::Assets::RegisterAssetDependency(_validationCallback, ps.GetDependencyValidation());
		}

		// auto& globals = Internal::VulkanGlobalsTemp::GetInstance();
		// Assets::RegisterAssetDependency(_validationCallback, globals._graphicsRootSignatureFile->GetDependencyValidation());
		// _pipelineLayoutConfig = globals._mainGraphicsConfig;
    }
    
    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									StreamOutputInitializers so)
    :   ShaderProgram(factory, pipelineLayout, vs, ps)
    {
		if (gs.GetStage() != ShaderStage::Null) {
			assert(gs.GetStage() == ShaderStage::Geometry);
            _modules[(unsigned)ShaderStage::Geometry] = factory.CreateShaderModule(gs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Geometry] = gs;
			assert(_modules[(unsigned)ShaderStage::Geometry]);
			::Assets::RegisterAssetDependency(_validationCallback, gs.GetDependencyValidation());
		}
    }

    ShaderProgram::ShaderProgram(   ObjectFactory& factory,
									const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
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
            _modules[(unsigned)ShaderStage::Hull] = factory.CreateShaderModule(hs.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Hull] = hs;
			assert(_modules[(unsigned)ShaderStage::Hull]);
			::Assets::RegisterAssetDependency(_validationCallback, hs.GetDependencyValidation());
		}

		if (ds.GetStage() != ShaderStage::Null) {
			assert(ds.GetStage() == ShaderStage::Domain);
            _modules[(unsigned)ShaderStage::Domain] = factory.CreateShaderModule(ds.GetByteCode());
			_compiledCode[(unsigned)ShaderStage::Domain] = ds;
			assert(_modules[(unsigned)ShaderStage::Domain]);
			::Assets::RegisterAssetDependency(_validationCallback, ds.GetDependencyValidation());
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
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const CompiledShaderByteCode& compiledShader)
    : _compiledCode(compiledShader)
    {
		_pipelineLayout = checked_pointer_cast<CompiledPipelineLayout>(pipelineLayout);

		if (compiledShader.GetStage() != ShaderStage::Null) {
			assert(compiledShader.GetStage() == ShaderStage::Compute);
            _module = factory.CreateShaderModule(compiledShader.GetByteCode());
		}

        _validationCallback = std::make_shared<::Assets::DependencyValidation>();
        ::Assets::RegisterAssetDependency(_validationCallback, compiledShader.GetDependencyValidation());

		// auto& globals = Internal::VulkanGlobalsTemp::GetInstance();
		// Assets::RegisterAssetDependency(_validationCallback, globals._computeRootSignatureFile->GetDependencyValidation());
		// _pipelineLayoutConfig = globals._mainComputeConfig;
    }

    ComputeShader::ComputeShader() {}
    ComputeShader::~ComputeShader() {}

        ////////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_ptr<::Assets::AssetFuture<CompiledShaderByteCode>> MakeByteCodeFuture(ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
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
		::Assets::DepValPtr& dst,
		const ::Assets::DepValPtr& dependency)
	{
		if (dependency)
			::Assets::RegisterAssetDependency(dst, dependency);
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		::Assets::WhenAll(vsFuture, psFuture).ThenConstructToFuture<ShaderProgram>(
			future,
			[pipelineLayout](const std::shared_ptr<CompiledShaderByteCode>& vsActual, const std::shared_ptr<CompiledShaderByteCode>& psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, *vsActual, *psActual);
			});
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> gsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto gsFuture = MakeByteCodeFuture(ShaderStage::Geometry, gsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		::Assets::WhenAll(vsFuture, gsFuture, psFuture).ThenConstructToFuture<ShaderProgram>(
			future,
			[pipelineLayout](const std::shared_ptr<CompiledShaderByteCode>& vsActual, const std::shared_ptr<CompiledShaderByteCode>& gsActual, const std::shared_ptr<CompiledShaderByteCode>& psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, *vsActual, *gsActual, *psActual);
			});
	}

	void ShaderProgram::ConstructToFuture(
		::Assets::AssetFuture<ShaderProgram>& future,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
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

		::Assets::WhenAll(vsFuture, gsFuture, psFuture, hsFuture, dsFuture).ThenConstructToFuture<ShaderProgram>(
			future,
			[pipelineLayout](	const std::shared_ptr<CompiledShaderByteCode>& vsActual,
				const std::shared_ptr<CompiledShaderByteCode>& gsActual,
				const std::shared_ptr<CompiledShaderByteCode>& psActual,
				const std::shared_ptr<CompiledShaderByteCode>& hsActual,
				const std::shared_ptr<CompiledShaderByteCode>& dsActual) {

				return std::make_shared<ShaderProgram>(GetObjectFactory(), pipelineLayout, *vsActual, *gsActual, *psActual, *hsActual, *dsActual);
			});
	}

	void ComputeShader::ConstructToFuture(
		::Assets::AssetFuture<ComputeShader>& future,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<::Assets::ResChar> codeName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto code = MakeByteCodeFuture(ShaderStage::Compute, codeName, definesTable);

		::Assets::WhenAll(code).ThenConstructToFuture<ComputeShader>(
			future,
			[pipelineLayout](const std::shared_ptr<CompiledShaderByteCode>& csActual) {
				return std::make_shared<ComputeShader>(GetObjectFactory(), pipelineLayout, *csActual);
			});
	}

}}


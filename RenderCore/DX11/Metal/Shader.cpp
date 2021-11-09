// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "DeviceContext.h"
#include "ObjectFactory.h"
#include "InputLayout.h"
#include "../../Format.h"

#include "IncludeDX11.h"
#include <D3D11Shader.h>

#include "../../../Assets/Assets.h"
#include "../../../Assets/AssetFutureContinuation.h"

namespace RenderCore { namespace Metal_DX11
{
    using ::Assets::ResChar;

        ////////////////////////////////////////////////////////////////////////////////////////////////

    static unsigned BuildNativeDeclaration(
        D3D11_SO_DECLARATION_ENTRY nativeDeclaration[], unsigned nativeDeclarationCount,
        const StreamOutputInitializers& soInitializers)
    {
        auto finalCount = std::min(nativeDeclarationCount, (unsigned)soInitializers._outputElements.size());
        for (unsigned c=0; c<finalCount; ++c) {
            auto& ele = soInitializers._outputElements[c];
            nativeDeclaration[c].Stream = 0;
            nativeDeclaration[c].SemanticName = ele._semanticName.c_str();
            nativeDeclaration[c].SemanticIndex = ele._semanticIndex;
            nativeDeclaration[c].StartComponent = 0;
            nativeDeclaration[c].ComponentCount = (BYTE)GetComponentCount(GetComponents(ele._nativeFormat));
                // hack -- treat "R16G16B16A16_FLOAT" as a 3 dimensional vector
            if (ele._nativeFormat == Format::R16G16B16A16_FLOAT)
                nativeDeclaration[c].ComponentCount = 3;
            nativeDeclaration[c].OutputSlot = (BYTE)ele._inputSlot;
            assert(nativeDeclaration[c].OutputSlot < soInitializers._outputBufferStrides.size());
        }
        return finalCount;
    }

    intrusive_ptr<ID3D::GeometryShader> CreateSOGeometryShader(
		ObjectFactory& objFactory,
		const CompiledShaderByteCode& compiledShader, 
		const StreamOutputInitializers& soInitializers,
		ID3D::ClassLinkage* classLinkage)
    {
        assert(compiledShader.GetStage() == ShaderStage::Geometry);
		assert(!soInitializers._outputBufferStrides.empty());
		assert(!soInitializers._outputElements.empty());

        auto byteCode = compiledShader.GetByteCode();

        assert(soInitializers._outputBufferStrides.size() <= D3D11_SO_BUFFER_SLOT_COUNT);
        D3D11_SO_DECLARATION_ENTRY nativeDeclaration[D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT];
        auto delcCount = BuildNativeDeclaration(nativeDeclaration, dimof(nativeDeclaration), soInitializers);

        auto featureLevel = objFactory.GetUnderlying()->GetFeatureLevel();
        auto result = objFactory.CreateGeometryShaderWithStreamOutput( 
			byteCode.begin(), byteCode.size(),
            nativeDeclaration, delcCount,
            soInitializers._outputBufferStrides.begin(), (unsigned)soInitializers._outputBufferStrides.size(),
                //      Note --     "NO_RASTERIZED_STREAM" is only supported on feature level 11. For other feature levels
                //                  we must disable the rasterization step some other way
            (featureLevel>=D3D_FEATURE_LEVEL_11_0)?D3D11_SO_NO_RASTERIZED_STREAM:0,
			classLinkage);

		return result;
    }

        ////////////////////////////////////////////////////////////////////////////////////////////////

    ComputeShader::ComputeShader(ObjectFactory& factory, const CompiledShaderByteCode& compiledShader)
	: _compiledCode(compiledShader)
    {
        if (compiledShader.GetStage() != ShaderStage::Null) {
            assert(compiledShader.GetStage() == ShaderStage::Compute);
            auto byteCode = compiledShader.GetByteCode();
            _underlying = GetObjectFactory().CreateComputeShader(byteCode.begin(), byteCode.size());
        }

        _validationCallback = std::make_shared<Assets::DependencyValidation>();
        Assets::RegisterAssetDependency(_validationCallback, compiledShader.GetDependencyValidation());
    }

    ComputeShader::ComputeShader() {}

    ComputeShader::~ComputeShader() {}

        ////////////////////////////////////////////////////////////////////////////////////////////////

	void ShaderProgram::Apply(DeviceContext& devContext) const
	{
		ID3D::DeviceContext* d3dDevContext = devContext.GetUnderlying();
		d3dDevContext->VSSetShader(_vertexShader.get(), nullptr, 0);
		d3dDevContext->GSSetShader(_geometryShader.get(), nullptr, 0);
		d3dDevContext->PSSetShader(_pixelShader.get(), nullptr, 0);
		d3dDevContext->HSSetShader(_hullShader.get(), nullptr, 0);			// todo -- prevent frequent thrashing of this state
		d3dDevContext->DSSetShader(_domainShader.get(), nullptr, 0);
	}

	void ShaderProgram::Apply(DeviceContext& devContext, const BoundClassInterfaces& dynLinkage) const
	{
		ID3D::DeviceContext* d3dDevContext = devContext.GetUnderlying();
		auto& vsDyn = dynLinkage.GetClassInstances(ShaderStage::Vertex);
		auto& gsDyn = dynLinkage.GetClassInstances(ShaderStage::Geometry);
		auto& psDyn = dynLinkage.GetClassInstances(ShaderStage::Pixel);
		auto& hsDyn = dynLinkage.GetClassInstances(ShaderStage::Hull);
		auto& dsDyn = dynLinkage.GetClassInstances(ShaderStage::Domain);

		d3dDevContext->VSSetShader(_vertexShader.get(), (ID3D::ClassInstance*const*)AsPointer(vsDyn.cbegin()), (unsigned)vsDyn.size());
		d3dDevContext->GSSetShader(_geometryShader.get(), (ID3D::ClassInstance*const*)AsPointer(gsDyn.cbegin()), (unsigned)gsDyn.size());
		d3dDevContext->PSSetShader(_pixelShader.get(), (ID3D::ClassInstance*const*)AsPointer(psDyn.cbegin()), (unsigned)psDyn.size());
		d3dDevContext->HSSetShader(_hullShader.get(), (ID3D::ClassInstance*const*)AsPointer(hsDyn.cbegin()), (unsigned)hsDyn.size());
		d3dDevContext->DSSetShader(_domainShader.get(), (ID3D::ClassInstance*const*)AsPointer(dsDyn.cbegin()), (unsigned)dsDyn.size());
	}

	const CompiledShaderByteCode&       ShaderProgram::GetCompiledCode(ShaderStage stage) const
	{
		assert(stage < ShaderStage::Max);
		return _compiledCode[(unsigned)stage];
	}

	ID3D::ClassLinkage*					ShaderProgram::GetClassLinkage(ShaderStage stage) const
	{
		assert(stage < ShaderStage::Max);
		return _classLinkage[(unsigned)stage].get();
	}

	static uint64_t s_nextShaderProgramGuid = 1;

    ShaderProgram::ShaderProgram(	ObjectFactory& factory, 
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& ps)
	: _guid(s_nextShaderProgramGuid++)
    {
		///////////////// VS ////////////////////
		if (vs.DynamicLinkingEnabled())
			_classLinkage[(unsigned)ShaderStage::Vertex] = factory.CreateClassLinkage();
			
		if (vs.GetStage() != ShaderStage::Null) {
			assert(vs.GetStage() == ShaderStage::Vertex);
			auto byteCode = vs.GetByteCode();
			_vertexShader = factory.CreateVertexShader(byteCode.begin(), byteCode.size(), _classLinkage[(unsigned)ShaderStage::Vertex].get());
			_compiledCode[(unsigned)ShaderStage::Vertex] = vs;
		}

		///////////////// PS ////////////////////
		if (ps.DynamicLinkingEnabled())
			_classLinkage[(unsigned)ShaderStage::Pixel] = factory.CreateClassLinkage();

		if (ps.GetStage() != ShaderStage::Null) {
			assert(ps.GetStage() == ShaderStage::Pixel);
			auto byteCode = ps.GetByteCode();
			_pixelShader = factory.CreatePixelShader(byteCode.begin(), byteCode.size(), _classLinkage[(unsigned)ShaderStage::Pixel].get());
			_compiledCode[(unsigned)ShaderStage::Pixel] = ps;
		}

        _validationCallback = std::make_shared<Assets::DependencyValidation>();
        Assets::RegisterAssetDependency(_validationCallback, vs.GetDependencyValidation());
        Assets::RegisterAssetDependency(_validationCallback, ps.GetDependencyValidation());
    }
    
    ShaderProgram::ShaderProgram(   ObjectFactory& factory, 
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									StreamOutputInitializers so)
	: ShaderProgram(factory, vs, ps)
    {
		///////////////// GS ////////////////////
		if (gs.DynamicLinkingEnabled())
			_classLinkage[(unsigned)ShaderStage::Geometry] = factory.CreateClassLinkage();

		if (gs.GetStage() != ShaderStage::Null) {
			assert(gs.GetStage() == ShaderStage::Geometry);
			if (!so._outputElements.empty()) {
				_geometryShader = CreateSOGeometryShader(
					factory, gs, so, _classLinkage[(unsigned)ShaderStage::Geometry].get());
			} else {
				auto byteCode = gs.GetByteCode();
				_geometryShader = factory.CreateGeometryShader(byteCode.begin(), byteCode.size(), _classLinkage[(unsigned)ShaderStage::Geometry].get());
			}
			_compiledCode[(unsigned)ShaderStage::Geometry] = gs;
		}

		Assets::RegisterAssetDependency(_validationCallback, gs.GetDependencyValidation());
    }

    ShaderProgram::ShaderProgram(	ObjectFactory& factory,
									const CompiledShaderByteCode& vs,
									const CompiledShaderByteCode& gs,
									const CompiledShaderByteCode& ps,
									const CompiledShaderByteCode& hs,
									const CompiledShaderByteCode& ds,
									StreamOutputInitializers so)
	: ShaderProgram(factory, vs, gs, ps, so)
    {
		///////////////// HS ////////////////////
		if (hs.DynamicLinkingEnabled())
			_classLinkage[(unsigned)ShaderStage::Hull] = factory.CreateClassLinkage();

		if (hs.GetStage() != ShaderStage::Null) {
			assert(hs.GetStage() == ShaderStage::Hull);
			auto byteCode = hs.GetByteCode();
			_hullShader = factory.CreateHullShader(byteCode.begin(), byteCode.size(), _classLinkage[(unsigned)ShaderStage::Hull].get());
			_compiledCode[(unsigned)ShaderStage::Hull] = hs;
		}

		///////////////// DS ////////////////////
		if (ds.DynamicLinkingEnabled())
			_classLinkage[(unsigned)ShaderStage::Domain] = factory.CreateClassLinkage();

		if (ds.GetStage() != ShaderStage::Null) {
			assert(ds.GetStage() == ShaderStage::Domain);
			auto byteCode = ds.GetByteCode();
			_domainShader = factory.CreateDomainShader(byteCode.begin(), byteCode.size(), _classLinkage[(unsigned)ShaderStage::Domain].get());
			_compiledCode[(unsigned)ShaderStage::Domain] = ds;
		}

		Assets::RegisterAssetDependency(_validationCallback, hs.GetDependencyValidation());
		Assets::RegisterAssetDependency(_validationCallback, ds.GetDependencyValidation());
    }

	ShaderProgram::ShaderProgram() : _guid(0) {}
    ShaderProgram::~ShaderProgram() {}

    bool ShaderProgram::DynamicLinkingEnabled() const
    {
        for (unsigned c=0; c<(unsigned)ShaderStage::Max; ++c)
			if (_classLinkage[c]) return true;
		return false;
    }

	static std::shared_ptr<::Assets::FuturePtr<CompiledShaderByteCode>> MakeByteCodeFuture(ShaderStage stage, StringSection<> initializer, StringSection<> definesTable)
	{
		char profileStr[] = "?s_";
		switch (stage) {
		case ShaderStage::Vertex: profileStr[0] = 'v'; break;
		case ShaderStage::Geometry: profileStr[0] = 'g'; break;
		case ShaderStage::Pixel: profileStr[0] = 'p'; break;
		case ShaderStage::Domain: profileStr[0] = 'd'; break;
		case ShaderStage::Hull: profileStr[0] = 'h'; break;
		case ShaderStage::Compute: profileStr[0] = 'c'; break;
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

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
		StringSection<::Assets::ResChar> vsName,
		StringSection<::Assets::ResChar> psName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto vsFuture = MakeByteCodeFuture(ShaderStage::Vertex, vsName, definesTable);
		auto psFuture = MakeByteCodeFuture(ShaderStage::Pixel, psName, definesTable);

		::Assets::WhenAll(vsFuture, psFuture).ThenConstructToPromise(
			std::move(promise),
			[](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *psActual);
			});
	}

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
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
			[](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> gsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
				return std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *gsActual, *psActual);
			});
	}

	StreamOutputInitializers g_defaultStreamOutputInitializers = {};

	void ShaderProgram::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
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
			[](	std::shared_ptr<CompiledShaderByteCode> vsActual,
				std::shared_ptr<CompiledShaderByteCode> gsActual,
				std::shared_ptr<CompiledShaderByteCode> psActual,
				std::shared_ptr<CompiledShaderByteCode> hsActual,
				std::shared_ptr<CompiledShaderByteCode> dsActual) {

				return std::make_shared<ShaderProgram>(GetObjectFactory(), *vsActual, *gsActual, *psActual, *hsActual, *dsActual, g_defaultStreamOutputInitializers);
			});
	}

	void ComputeShader::ConstructToPromise(
		std::promise<std::shared_ptr<ShaderProgram>>&& promise,
		StringSection<::Assets::ResChar> codeName,
		StringSection<::Assets::ResChar> definesTable)
	{
		auto code = MakeByteCodeFuture(ShaderStage::Compute, codeName, definesTable);

		::Assets::WhenAll(code).ThenConstructToPromise(
			std::move(promise),
			[](std::shared_ptr<CompiledShaderByteCode> csActual) {
				return std::make_shared<ComputeShader>(GetObjectFactory(), *csActual);
			});
	}

}}


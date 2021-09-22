// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderStep_PrepareShadows.h"
#include "LightingParserContext.h"
#include "LightingParser.h"		// (for ILightingParserPlugin);
#include "SceneEngineUtils.h"
#include "RenderStepUtils.h"
#include "LightDesc.h"
#include "LightInternal.h"
#include "ShadowResources.h"
#include "RayTracedShadows.h"
#include "LightingTargets.h"		// for ShadowGen_DrawDebugging
#include "../RenderCore/Techniques/RenderPass.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderStateResolver.h"
#include "../RenderCore/Techniques/TechniqueDelegates.h"
#include "../RenderCore/Techniques/PipelineAccelerator.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/FrameBufferDesc.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Shader.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/Assets.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/FunctionUtils.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
	using namespace RenderCore;

	class ShadowGenTechniqueDelegateBox
	{
	public:
		std::shared_ptr<RenderCore::Techniques::TechniqueSetFile> _techniqueSetFile;
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _shadowGenDelegate;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _techniqueSetFile->GetDependencyValidation(); }

		class Desc
        {
        public:
            using RSDepthBias = RenderCore::Techniques::RSDepthBias;
            RSDepthBias     _singleSidedBias;
            RSDepthBias     _doubleSidedBias;
            RenderCore::CullMode	_windingCullMode;

            Desc(   const RSDepthBias& singleSidedBias,
                    const RSDepthBias& doubleSidedBias,
                    RenderCore::CullMode windingCullMode) 
            : _singleSidedBias(singleSidedBias)
            , _doubleSidedBias(doubleSidedBias)
            , _windingCullMode(windingCullMode) {}
        };

		ShadowGenTechniqueDelegateBox(const Desc& desc)
		{
			_techniqueSetFile = ::Assets::AutoConstructAsset<RenderCore::Techniques::TechniqueSetFile>(ILLUM_TECH);
			_shadowGenDelegate = RenderCore::Techniques::CreateTechniqueDelegate_ShadowGen(
				_techniqueSetFile,
				desc._singleSidedBias, desc._doubleSidedBias, desc._windingCullMode);
		}
	};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RenderStep_PrepareRTShadows::Execute(
		IThreadContext& threadContext,
		Techniques::ParsingContext& parsingContext,
		LightingParserContext& lightingParserContext,
		RenderStepFragmentInstance& rpi,
		IViewDelegate* viewDelegate)
	{
		auto& shadowDelegate = *checked_cast<ViewDelegate_Shadow*>(viewDelegate);
		assert(shadowDelegate._shadowProj._shadowGeneratorDesc._resolveType == ShadowResolveType::RayTraced);

		auto shadow = PrepareRTShadows(threadContext, parsingContext, lightingParserContext, shadowDelegate);
        if (shadow.IsReady())
            lightingParserContext._preparedRTShadows.push_back(std::make_pair(shadowDelegate._shadowProj._lightId, std::move(shadow)));
	}

	RenderStep_PrepareRTShadows::RenderStep_PrepareRTShadows()
	: _fragment(RenderCore::PipelineType::Graphics)
	{
	}

	RenderStep_PrepareRTShadows::~RenderStep_PrepareRTShadows()
	{
	}

	RenderCore::Techniques::DrawablesPacket* ViewDelegate_Shadow::GetDrawablesPacket(RenderCore::Techniques::BatchFilter batch)
	{
		if (batch == RenderCore::Techniques::BatchFilter::General)
			return &_general;
		return nullptr;
	}

	void ViewDelegate_Shadow::Reset()
	{
		_general.Reset();
	}

	ViewDelegate_Shadow::ViewDelegate_Shadow(StandardShadowProjection shadowProjection)
	: _shadowProj(shadowProjection)
	{
	}

	ViewDelegate_Shadow::~ViewDelegate_Shadow()
	{
	}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			//		draw frustum debugging

	class SFDResources
    {
    public:
        class Desc 
        {
        public:
            unsigned    _cascadeMode;
            bool        _enableNearCascade;

            Desc(unsigned cascadeMode, bool enableNearCascade) 
            : _cascadeMode(cascadeMode), _enableNearCascade(enableNearCascade) {}
        };

        const Metal::ShaderProgram*    _shader;
        Metal::BoundUniforms           _uniforms;
        
        const ::Assets::DependencyValidation& GetDependencyValidation() const   { return _depVal; }
        SFDResources(const Desc&);
        ~SFDResources();
    protected:
        ::Assets::DependencyValidation _depVal;
    };

    SFDResources::SFDResources(const Desc& desc)
    {
        _shader = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
            BASIC2D_VERTEX_HLSL ":fullscreen_viewfrustumvector:vs_*",
            CASCADE_VIS_HLSL ":main:ps_*",
            (const ::Assets::ResChar*)(StringMeld<128, ::Assets::ResChar>() 
                << "SHADOW_CASCADE_MODE=" << desc._cascadeMode 
                << ";SHADOW_ENABLE_NEAR_CASCADE=" << (desc._enableNearCascade?1:0)));

		UniformsStreamInterface uniformsInterf;
		uniformsInterf.BindConstantBuffer(0, { Hash64("ArbitraryShadowProjection") });
		uniformsInterf.BindConstantBuffer(1, { Hash64("OrthogonalShadowProjection") });
		uniformsInterf.BindConstantBuffer(2, { Hash64("ScreenToShadowProjection") });
		uniformsInterf.BindShaderResource(0, { Hash64("DepthTexture") });
		_uniforms = Metal::BoundUniforms(
			*_shader,
			Metal::PipelineLayoutConfig{},
			Techniques::TechniqueContext::GetGlobalUniformsStreamInterface(),
			uniformsInterf);
        
        _depVal = _shader->GetDependencyValidation();
    }

    SFDResources::~SFDResources() {}

    void ShadowGen_DrawShadowFrustums(
        Metal::DeviceContext& devContext, 
		RenderCore::Techniques::ParsingContext& parserContext,
        MainTargets mainTargets,
		const StandardShadowProjection& projectionDesc)
    {
        devContext.Bind(Techniques::CommonResources()._dssDisable);
        devContext.Bind(Techniques::CommonResources()._blendAlphaPremultiplied);

        Metal::ShaderResourceView depthSrv = mainTargets.GetSRV(
			parserContext, 
			Techniques::AttachmentSemantics::MultisampleDepth,
			RenderCore::TextureViewDesc{RenderCore::TextureViewDesc::Aspect::ColorLinear});

        auto& res = ConsoleRig::FindCachedBoxDep2<SFDResources>(
            (projectionDesc._projections._mode == ShadowProjectionMode::Ortho)?2:1,
            projectionDesc._projections._useNearProj);
        devContext.Bind(*res._shader);

        CB_ArbitraryShadowProjection arbitraryCB;
        CB_OrthoShadowProjection orthoCB;
        BuildShadowConstantBuffers(arbitraryCB, orthoCB, projectionDesc._projections);

		auto mainCameraProjDesc = parserContext.GetProjectionDesc();

        ConstantBufferView constantBufferPackets[3];
        constantBufferPackets[0] = RenderCore::MakeSharedPkt(arbitraryCB);
        constantBufferPackets[1] = RenderCore::MakeSharedPkt(orthoCB);
        constantBufferPackets[2] = BuildScreenToShadowConstants(
            projectionDesc._projections._normalProjCount,
            arbitraryCB, orthoCB, 
            mainCameraProjDesc._cameraToWorld,
            mainCameraProjDesc._cameraToProjection);
        const Metal::ShaderResourceView* srv[] = { &depthSrv };

        res._uniforms.Apply(devContext, 0, parserContext.GetGlobalUniformsStream());
		res._uniforms.Apply(devContext, 1,
            UniformsStream{
                MakeIteratorRange(constantBufferPackets),
				UniformsStream::MakeResources(MakeIteratorRange(srv))});

        devContext.Bind(Topology::TriangleStrip);
        devContext.Draw(4);

        // devContext.UnbindPS<Metal::ShaderResourceView>(4, 1);
    }
}


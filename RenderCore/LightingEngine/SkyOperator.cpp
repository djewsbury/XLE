// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkyOperator.h"
#include "LightingEngineIterator.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"
#include "../Techniques/DescriptorSetAccelerator.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/Services.h"
#include "../Techniques/CommonResources.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	void SkyOperator::Execute(Techniques::ParsingContext& parsingContext)
	{
		const IDescriptorSet* descSets[] = { _descSet.get() };
		_shader->Draw(
			parsingContext,
			{},
			MakeIteratorRange(descSets));
	}

	void SkyOperator::Execute(LightingTechniqueIterator& iterator)
	{
		Execute(*iterator._parsingContext);
	}

	void SkyOperator::SetResource(std::shared_ptr<IResourceView> texture)
	{
		auto& pipelineLayout = _shader->GetPredefinedPipelineLayout();
		auto* descSetLayout = pipelineLayout.FindDescriptorSet("SkyDS");
		assert(descSetLayout);
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Sky"));
		auto& commonRes = *Techniques::Services::GetCommonResources();
		if (texture) {
			_descSet = Techniques::ConstructDescriptorSetHelper{_device, &commonRes._samplerPool}
				.ConstructImmediately(
					*descSetLayout, usi, 
					ResourceViewStream{*texture},
					"SkyOperator");
		} else {
			auto dummy = Techniques::Services::GetCommonResources()->_blackCubeSRV;
			_descSet = Techniques::ConstructDescriptorSetHelper{_device, &commonRes._samplerPool}
				.ConstructImmediately(
					*descSetLayout, usi, 
					ResourceViewStream{*dummy},
					"SkyOperator");
		}
	}

	::Assets::DependencyValidation SkyOperator::GetDependencyValidation() const { return _shader->GetDependencyValidation(); }

	SkyOperator::SkyOperator(
		const SkyOperatorDesc& desc,
		std::shared_ptr<Techniques::IShaderOperator> shader,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool)
	: _shader(std::move(shader))
	{
		_device = pipelinePool->GetDevice();
		_pool = std::move(pipelinePool);
		SetResource(nullptr);		// initial blocked out state
	}

	SkyOperator::~SkyOperator()
	{}

	void SkyOperator::ConstructToPromise(
		std::promise<std::shared_ptr<SkyOperator>>&& promise,
		const SkyOperatorDesc& desc,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		UniformsStreamInterface usi;
		usi.BindFixedDescriptorSet(0, Hash64("SkyDS"));

		ParameterBox params;
		params.SetParameter("SKY_PROJECTION", 5);
		Techniques::PixelOutputStates po;
		po.Bind(*fbTarget._fbDesc, fbTarget._subpassIdx);
		po.Bind(Techniques::CommonResourceBox::s_dsReadOnly);
		AttachmentBlendDesc blendDescs[] {Techniques::CommonResourceBox::s_abOpaque};
		po.Bind(MakeIteratorRange(blendDescs));
		auto futureShader = CreateFullViewportOperator(
			pipelinePool,
			Techniques::FullViewportOperatorSubType::MaxDepth,
			SKY_PIXEL_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":Sky",
			po, usi);
		::Assets::WhenAll(futureShader).ThenConstructToPromise(
			std::move(promise),
			[desc, pipelinePool=std::move(pipelinePool)](auto shader) {
				return std::make_shared<SkyOperator>(desc, std::move(shader), pipelinePool);
			});
	}

	uint64_t SkyOperatorDesc::GetHash() const
	{
		return (uint64_t)_textureType;
	}
}}


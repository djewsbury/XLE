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
#include "../Techniques/CommonBindings.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	void SkyOperator::Execute(Techniques::ParsingContext& parsingContext)
	{
		assert(_secondStageConstructionState == 2);
		assert(_shader);
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
		assert(_secondStageConstructionState == 2);
		auto& pipelineLayout = _shader->GetPredefinedPipelineLayout();
		auto* descSetLayout = pipelineLayout.FindDescriptorSet("SkyDS");
		assert(descSetLayout);
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Sky"_h);
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

	::Assets::DependencyValidation SkyOperator::GetDependencyValidation() const { assert(_secondStageConstructionState == 2); return _shader->GetDependencyValidation(); }

	SkyOperator::SkyOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const SkyOperatorDesc& desc)
	: _secondStageConstructionState(0)
	{
		_device = pipelinePool->GetDevice();
		_pool = std::move(pipelinePool);
	}

	SkyOperator::~SkyOperator()
	{}

	void SkyOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<SkyOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		_secondStageConstructionState = 1;

		UniformsStreamInterface usi;
		usi.BindFixedDescriptorSet(0, "SkyDS"_h);

		ParameterBox params;
		params.SetParameter("SKY_PROJECTION", 5);
		Techniques::PixelOutputStates po;
		po.Bind(*fbTarget._fbDesc, fbTarget._subpassIdx);
		po.Bind(Techniques::CommonResourceBox::s_dsReadOnly);
		AttachmentBlendDesc blendDescs[] {Techniques::CommonResourceBox::s_abOpaque};
		po.Bind(MakeIteratorRange(blendDescs));
		auto futureShader = CreateFullViewportOperator(
			_pool,
			Techniques::FullViewportOperatorSubType::MaxDepth,
			SKY_PIXEL_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":Sky",
			po, usi);
		::Assets::WhenAll(futureShader).ThenConstructToPromise(
			std::move(promise),
			[strongThis=shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				strongThis->SetResource(nullptr);		// initial blocked out state
				return strongThis;
			});
	}

	uint64_t SkyOperatorDesc::GetHash(uint64_t seed) const
	{
		return HashCombine((uint64_t)_textureType, seed);
	}
}}


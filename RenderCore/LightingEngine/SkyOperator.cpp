// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SkyOperator.h"
#include "LightingEngineInternal.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Drawables.h"
#include "../Techniques/DescriptorSetAccelerator.h"
#include "../Assets/PredefinedPipelineLayout.h"
#include "../UniformsStream.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine
{
	void SkyOperator::Execute(LightingEngine::LightingTechniqueIterator& iterator)
	{
		if (!_descSet) return;

		// todo -- don't reconstruct the SequencerUniformsHelper every time here!
		const IDescriptorSet* descSets[] = { _descSet.get() };
		Techniques::SequencerUniformsHelper seqUniforms(*iterator._parsingContext);
		_shader->Draw(
			*iterator._threadContext,
			*iterator._parsingContext,
			seqUniforms,
			{},
			MakeIteratorRange(descSets));
	}

	void SkyOperator::SetResource(std::shared_ptr<IResourceView> texture)
	{
		auto& pipelineLayout = _shader->GetPredefinedPipelineLayout();
		auto* descSetLayout = pipelineLayout.FindDescriptorSet("Material");
		assert(descSetLayout);
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("t3"));
		_descSet = Techniques::ConstructDescriptorSet(
			*_device, *descSetLayout, usi, 
			ResourceViewStream{*texture});
	}

	::Assets::DependencyValidation SkyOperator::GetDependencyValidation() const { return _shader->GetDependencyValidation(); }

	SkyOperator::SkyOperator(
		const SkyOperatorDesc& desc,
		std::shared_ptr<Techniques::IShaderOperator> shader,
		std::shared_ptr<IDevice> device)
	: _shader(std::move(shader))
	, _device(std::move(device))
	{
	}

	SkyOperator::~SkyOperator()
	{}

	void SkyOperator::ConstructToFuture(
		::Assets::FuturePtr<SkyOperator>& future,
		const SkyOperatorDesc& desc,
		std::shared_ptr<Techniques::PipelinePool> pipelinePool,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		UniformsStreamInterface usi;
		usi.BindFixedDescriptorSet(0, Hash64("Material"));

		ParameterBox params;
		params.SetParameter("SKY_PROJECTION", 5);
		auto futureShader = CreateFullViewportOperator(
			pipelinePool,
			Techniques::FullViewportOperatorSubType::MaxDepth,
			SKY_PIXEL_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":GraphicsWithMaterial",
			fbTarget, usi);
		::Assets::WhenAll(futureShader).ThenConstructToFuture(
			future,
			[desc, device=pipelinePool->GetDevice()](auto shader) {
				return std::make_shared<SkyOperator>(desc, std::move(shader), std::move(device));
			});
	}

	uint64_t SkyOperatorDesc::GetHash() const
	{
		return (uint64_t)_textureType;
	}
}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderStepFragments.h"

namespace RenderCore { namespace LightingEngine 
{

	RenderCore::Techniques::FrameBufferDescFragment::DefineAttachmentHelper RenderStepFragmentInterface::DefineAttachment(uint64_t semantic)
	{
		return _frameBufferDescFragment.DefineAttachment(semantic);
	}

	RenderCore::Techniques::FrameBufferDescFragment::DefineAttachmentHelper RenderStepFragmentInterface::DefineAttachment(uint64_t semantic, const AttachmentDesc& request)
	{
		return _frameBufferDescFragment.DefineAttachment(semantic, request);
	}

	void RenderStepFragmentInterface::AddSubpass(
		Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
		std::shared_ptr<Techniques::ITechniqueDelegate> techniqueDelegate,
		Techniques::BatchFlags::BitField batchFilter,
		ParameterBox&& sequencerSelectors,
		std::shared_ptr<Techniques::IShaderResourceDelegate> shaderResourceDelegate)
	{
		_frameBufferDescFragment.AddSubpass(std::move(subpass));
		_subpassExtensions.emplace_back(
			SubpassExtension {
				SubpassExtension::Type::ExecuteDrawables,
				std::move(techniqueDelegate), std::move(sequencerSelectors), batchFilter,
				std::move(shaderResourceDelegate)
			});
	}

	void RenderStepFragmentInterface::AddSubpass(
		Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
		std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		_frameBufferDescFragment.AddSubpass(std::move(subpass));
		SubpassExtension ext;
		ext._type = SubpassExtension::Type::CallLightingIteratorFunction;
		ext._lightingIteratorFunction = std::move(fn);
		_subpassExtensions.emplace_back(std::move(ext));
	}

	void RenderStepFragmentInterface::AddSubpasses(
		IteratorRange<const Techniques::FrameBufferDescFragment::SubpassDesc*> subpasses,
		std::function<void(LightingTechniqueIterator&)>&& fn)
	{
		if (subpasses.empty()) return;
		for (const auto& s:subpasses)
			_frameBufferDescFragment.AddSubpass(Techniques::FrameBufferDescFragment::SubpassDesc{s});

		SubpassExtension ext;
		ext._type = SubpassExtension::Type::CallLightingIteratorFunction;
		ext._lightingIteratorFunction = std::move(fn);
		_subpassExtensions.emplace_back(std::move(ext));
		// One function should iterate through all subpasses -- so subpasses after the first need to be marked as handled by that function
		for (unsigned c=1; c<subpasses.size(); ++c)
			_subpassExtensions.emplace_back(SubpassExtension { SubpassExtension::Type::HandledByPrevious });
	}

	void RenderStepFragmentInterface::AddSkySubpass(Techniques::FrameBufferDescFragment::SubpassDesc&& subpass)
	{
		_frameBufferDescFragment.AddSubpass(std::move(subpass));
		SubpassExtension ext;
		ext._type = SubpassExtension::Type::ExecuteSky;
		_subpassExtensions.emplace_back(std::move(ext));
	}

	RenderStepFragmentInterface::RenderStepFragmentInterface(RenderCore::PipelineType pipelineType)
	{
		_frameBufferDescFragment._pipelineType = pipelineType;
	}

	RenderStepFragmentInterface::~RenderStepFragmentInterface() {}


	const RenderCore::Techniques::SequencerConfig* RenderStepFragmentInstance::GetSequencerConfig() const
	{
		if ((_rpi->GetCurrentSubpassIndex()-_firstSubpassIndex) >= _sequencerConfigs.size())
			return nullptr;
		return _sequencerConfigs[_rpi->GetCurrentSubpassIndex()-_firstSubpassIndex].get();
	}

	RenderStepFragmentInstance::RenderStepFragmentInstance(
		RenderCore::Techniques::RenderPassInstance& rpi,
		IteratorRange<const std::shared_ptr<RenderCore::Techniques::SequencerConfig>*> sequencerConfigs)
	: _rpi(&rpi)
	{
		_firstSubpassIndex = _rpi->GetCurrentSubpassIndex();
		_sequencerConfigs = sequencerConfigs;
	}

	RenderStepFragmentInstance::RenderStepFragmentInstance() {}

}}

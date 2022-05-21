// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/RenderPass.h"
#include "../Techniques/Drawables.h"		// for BatchFlags
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques 
{
	class FrameBufferDescFragment;
	class ITechniqueDelegate;
	class SequencerConfig;
	class IShaderResourceDelegate;
}}

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;

	class RenderStepFragmentInterface
	{
	public:
		Techniques::FrameBufferDescFragment::DefineAttachmentHelper DefineAttachment(uint64_t semantic);
		void AddSubpass(
			Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
			std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> techniqueDelegate = nullptr,
			Techniques::BatchFlags::BitField batchFilter = Techniques::BatchFlags::Opaque,
			ParameterBox&& sequencerSelectors = {},
			std::shared_ptr<Techniques::IShaderResourceDelegate> shaderResourceDelegates = {});
		void AddSubpass(
			Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
			std::function<void(LightingTechniqueIterator&)>&& fn);
		void AddSkySubpass(Techniques::FrameBufferDescFragment::SubpassDesc&& subpass);
		void AddSubpasses(
			IteratorRange<const Techniques::FrameBufferDescFragment::SubpassDesc*> subpasses,
			std::function<void(LightingTechniqueIterator&)>&& fn);

		RenderStepFragmentInterface(RenderCore::PipelineType);
		~RenderStepFragmentInterface();

		const RenderCore::Techniques::FrameBufferDescFragment& GetFrameBufferDescFragment() const { return _frameBufferDescFragment; }
		PipelineType GetPipelineType() const { return _frameBufferDescFragment._pipelineType; }

		struct SubpassExtension
		{
			enum Type { ExecuteDrawables, ExecuteSky, CallLightingIteratorFunction, HandledByPrevious };
			Type _type;
			std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _techniqueDelegate;
			ParameterBox _sequencerSelectors;
			Techniques::BatchFlags::BitField _batchFilter;
			std::shared_ptr<Techniques::IShaderResourceDelegate> _shaderResourceDelegate;
			std::function<void(LightingTechniqueIterator&)> _lightingIteratorFunction;
		};
		IteratorRange<const SubpassExtension*> GetSubpassAddendums() const { return MakeIteratorRange(_subpassExtensions); }
	private:
		RenderCore::Techniques::FrameBufferDescFragment _frameBufferDescFragment;
		std::vector<SubpassExtension> _subpassExtensions;
	};

	class RenderStepFragmentInstance
	{
	public:
		const RenderCore::Techniques::SequencerConfig* GetSequencerConfig() const;
		const RenderCore::Techniques::RenderPassInstance& GetRenderPassInstance() const { return *_rpi; }
		RenderCore::Techniques::RenderPassInstance& GetRenderPassInstance() { return *_rpi; }

		RenderStepFragmentInstance(
			RenderCore::Techniques::RenderPassInstance& rpi,
			IteratorRange<const std::shared_ptr<RenderCore::Techniques::SequencerConfig>*> sequencerConfigs);
		RenderStepFragmentInstance();
	private:
		RenderCore::Techniques::RenderPassInstance* _rpi;
		IteratorRange<const std::shared_ptr<RenderCore::Techniques::SequencerConfig>*> _sequencerConfigs;
		unsigned _firstSubpassIndex;
	};

}}


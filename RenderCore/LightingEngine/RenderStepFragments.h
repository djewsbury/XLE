// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/RenderPass.h"
#include "../Techniques/Drawables.h"		// for BatchFilter
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
		AttachmentName DefineAttachment(
            uint64_t semantic,
            LoadStore loadOp = LoadStore::Retain, LoadStore storeOp = LoadStore::Retain);
        AttachmentName DefineAttachmentRelativeDims(
			uint64_t semantic,
            float width, float height,
            const AttachmentDesc& request);
		AttachmentName DefineAttachment(
			uint64_t semantic,
            unsigned width, unsigned height, unsigned arrayLayerCount,
            const AttachmentDesc& request);
		void AddSubpass(
			Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
			const std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate>& techniqueDelegate = nullptr,
			Techniques::BatchFilter batchFilter = Techniques::BatchFilter::Max,
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
			Techniques::BatchFilter _batchFilter;
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


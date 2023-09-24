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
	class SequenceIterator;

	class RenderStepFragmentInterface
	{
	public:
		Techniques::FrameBufferDescFragment::DefineAttachmentHelper DefineAttachment(uint64_t semantic);
		void AddSubpass(
			Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
			std::shared_ptr<Techniques::ITechniqueDelegate> techniqueDelegate = nullptr,
			Techniques::BatchFlags::BitField batchFilter = Techniques::BatchFlags::Opaque,
			ParameterBox&& sequencerSelectors = {},
			std::shared_ptr<Techniques::IShaderResourceDelegate> shaderResourceDelegates = {});
		void AddSubpass(
			Techniques::FrameBufferDescFragment::SubpassDesc&& subpass,
			std::function<void(SequenceIterator&)>&& fn);
		void AddSkySubpass(Techniques::FrameBufferDescFragment::SubpassDesc&& subpass);
		void AddSubpasses(
			IteratorRange<const Techniques::FrameBufferDescFragment::SubpassDesc*> subpasses,
			std::function<void(SequenceIterator&)>&& fn);

		RenderStepFragmentInterface(PipelineType = PipelineType::Graphics);
		~RenderStepFragmentInterface();

		const Techniques::FrameBufferDescFragment& GetFrameBufferDescFragment() const { return _frameBufferDescFragment; }
		PipelineType GetPipelineType() const { return _frameBufferDescFragment._pipelineType; }

		struct SubpassExtension
		{
			enum Type { ExecuteDrawables, ExecuteSky, CallLightingIteratorFunction, HandledByPrevious };
			Type _type;
			std::shared_ptr<Techniques::ITechniqueDelegate> _techniqueDelegate;
			ParameterBox _sequencerSelectors;
			Techniques::BatchFlags::BitField _batchFilter;
			std::shared_ptr<Techniques::IShaderResourceDelegate> _shaderResourceDelegate;
			std::function<void(SequenceIterator&)> _lightingIteratorFunction;
		};
		IteratorRange<const SubpassExtension*> GetSubpassAddendums() const { return MakeIteratorRange(_subpassExtensions); }
	private:
		Techniques::FrameBufferDescFragment _frameBufferDescFragment;
		std::vector<SubpassExtension> _subpassExtensions;
	};

	class RenderStepFragmentInstance
	{
	public:
		const Techniques::SequencerConfig* GetSequencerConfig() const;
		const Techniques::RenderPassInstance& GetRenderPassInstance() const { return *_rpi; }
		Techniques::RenderPassInstance& GetRenderPassInstance() { return *_rpi; }

		RenderStepFragmentInstance(
			Techniques::RenderPassInstance& rpi,
			IteratorRange<const std::shared_ptr<Techniques::SequencerConfig>*> sequencerConfigs);
		RenderStepFragmentInstance();
	private:
		Techniques::RenderPassInstance* _rpi;
		IteratorRange<const std::shared_ptr<Techniques::SequencerConfig>*> _sequencerConfigs;
		unsigned _firstSubpassIndex;
	};

	/////////////////////////////////////////////////////////////////////////
	/// <summary>Utility for setting barriers and binding uniforms for attachments</summary>
	/// Intended for compute pipelines, since graphics pipelines have more features of the 
	/// render pass system available to them.
	class ComputeAttachmentUniformsTracker
	{
	public:
		using AttachmentSemantic = uint64_t;
		using ShaderUniformName = uint64_t;

		struct AttachmentState
		{
			LoadStore _loadStore = LoadStore::Retain;
			std::optional<BindFlag::BitField> _layout;
			ShaderStage _shaderStageForBarriers = ShaderStage::Compute;

			AttachmentState(BindFlag::BitField, ShaderStage);
			AttachmentState(LoadStore, BindFlag::BitField, ShaderStage);
			AttachmentState(LoadStore);
			AttachmentState() = default;
			static AttachmentState NoState();
		};
		void ExpectAttachment(AttachmentSemantic, AttachmentState);
		void Barrier(AttachmentSemantic, AttachmentState);
		void Discard(AttachmentSemantic);

		void Bind(ShaderUniformName, AttachmentSemantic, BindFlag::Enum usage = BindFlag::ShaderResource, TextureViewDesc window = {});
		void BindWithBarrier(ShaderUniformName, AttachmentSemantic, BindFlag::Enum usage = BindFlag::ShaderResource, TextureViewDesc window = {});
		UniformsStreamInterface EndUniformsStream();

		Techniques::FrameBufferDescFragment::SubpassDesc CreateSubpass(RenderStepFragmentInterface& fragmentInterface, const std::string& name);

		struct PassHelper;
		PassHelper BeginPass(IThreadContext&, Techniques::RenderPassInstance&);

		ComputeAttachmentUniformsTracker();
	private:
		struct KnownAttachment
		{
			std::optional<AttachmentState> _initialState;
			std::optional<AttachmentState> _currentState;
			unsigned _firstViewIdx = ~0u;
		};
		std::vector<std::pair<AttachmentSemantic, KnownAttachment>> _knownAttachments;

		struct ViewCfg
		{
			unsigned _attachmentIdx;
			BindFlag::Enum _usage;
			TextureViewDesc _window;
		};
		std::vector<ViewCfg> _views;

		struct WorkingUniformsStream
		{
			std::vector<uint64_t> _usi;
		};
		std::vector<WorkingUniformsStream> _streams;

		struct Cmd
		{
			enum class Type { Barrier, Bind, PrepareUniformsStream };
			Type _type;

			unsigned _attachmentIdx;
			AttachmentState _barrierOldState;
			AttachmentState _barrierNewState;

			unsigned _usiIdx;
			unsigned _viewIdx;
		};
		std::vector<Cmd> _cmdList;
		unsigned _usiCountMax = 0;

		bool _frozen = false;
	};

	struct ComputeAttachmentUniformsTracker::PassHelper
	{
	public:
		UniformsStream GetNextUniformsStream();
		void EndPass();

		~PassHelper();
	private:
		PassHelper(
			ComputeAttachmentUniformsTracker& parent,
			IThreadContext& threadContext,
			Techniques::RenderPassInstance& rpi);
		PassHelper(PassHelper&&) = delete;
		PassHelper& operator=(PassHelper&&) = delete;
		friend class ComputeAttachmentUniformsTracker;

		std::vector<const IResourceView*> _srvs;
		Techniques::RenderPassInstance* _rpi;
		IThreadContext* _threadContext;
		ComputeAttachmentUniformsTracker* _parent;
		std::vector<Cmd>::iterator _cmdListIterator;
		bool _ended = false;

		void AdvanceCommands();
	};

}}


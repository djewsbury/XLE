// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RenderStepFragments.h"
#include "../Techniques/CommonBindings.h"
#include "../Metal/Resource.h"
#include "../UniformsStream.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace LightingEngine 
{

	RenderCore::Techniques::FrameBufferDescFragment::DefineAttachmentHelper RenderStepFragmentInterface::DefineAttachment(uint64_t semantic)
	{
		return _frameBufferDescFragment.DefineAttachment(semantic);
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
		std::function<void(SequenceIterator&)>&& fn)
	{
		_frameBufferDescFragment.AddSubpass(std::move(subpass));
		SubpassExtension ext;
		ext._type = SubpassExtension::Type::CallLightingIteratorFunction;
		ext._lightingIteratorFunction = std::move(fn);
		_subpassExtensions.emplace_back(std::move(ext));
	}

	void RenderStepFragmentInterface::AddSubpasses(
		IteratorRange<const Techniques::FrameBufferDescFragment::SubpassDesc*> subpasses,
		std::function<void(SequenceIterator&)>&& fn)
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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		struct StreamAttachmentSemantic { uint64_t _value = 0; };
		static std::ostream& operator<<(std::ostream& str, StreamAttachmentSemantic semantic)
		{
			auto dehash = Techniques::AttachmentSemantics::TryDehash(semantic._value);
			if (dehash) str << dehash;
			else str << "0x" << std::hex << semantic._value << std::dec;
			return str;
		}
	}

	void ComputeAttachmentUniformsTracker::ExpectAttachment(AttachmentSemantic attachment, AttachmentState expectedState)
	{
		assert(!_frozen);
		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i != _knownAttachments.end())
			Throw(std::runtime_error(StringMeld<256>() << "ExpectAttachment used twice for attachment (" << Internal::StreamAttachmentSemantic{attachment} << ") in ComputeAttachmentUniformsTracker"));

		_knownAttachments.emplace_back(
			attachment,
			KnownAttachment {
				expectedState,
				expectedState
			});
	}

	void ComputeAttachmentUniformsTracker::Barrier(AttachmentSemantic attachment, AttachmentState newState)
	{
		// Barrier from our expected current state to the new state given. We do this even if the state hasn't changed, since a barrier
		// without a state change is still valid and useful
		assert(!_frozen);

		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i == _knownAttachments.end()) {
			_knownAttachments.emplace_back(
				attachment,
				KnownAttachment {});
			i = _knownAttachments.end()-1;
		}

		Cmd cmd;
		cmd._type = Cmd::Type::Barrier;
		cmd._attachmentIdx = (unsigned)std::distance(_knownAttachments.begin(), i);
		cmd._barrierOldState = i->second._currentState.value_or(AttachmentState::NoState());
		cmd._barrierNewState = newState;
		_cmdList.push_back(cmd);

		i->second._currentState = newState;
	}

	void ComputeAttachmentUniformsTracker::Discard(AttachmentSemantic attachment)
	{
		assert(!_frozen);
		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i == _knownAttachments.end())
			return;

		i->second._currentState = AttachmentState::NoState();
	}

	void ComputeAttachmentUniformsTracker::Bind(ShaderUniformName uniform, AttachmentSemantic attachment, BindFlag::Enum usage, TextureViewDesc window)
	{
		assert(!_frozen);
		assert(usage == BindFlag::ShaderResource || usage == BindFlag::UnorderedAccess);

		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i == _knownAttachments.end()) {
			// create a new attachment using implied state
			_knownAttachments.emplace_back(
				attachment,
				KnownAttachment {});
			i = _knownAttachments.end()-1;

			i->second._currentState = i->second._initialState = AttachmentState { LoadStore::Retain, (BindFlag::BitField)usage, ShaderStage::Compute };
		}

		auto& usi = _streams.back()._usi;
		auto i2 = std::find(usi.begin(), usi.end(), uniform);
		if (i2 == usi.end()) {
			usi.push_back(uniform);
			i2 = usi.end()-1;
		}

		auto attachmentIdx = (unsigned)std::distance(_knownAttachments.begin(), i);
		auto i3 = std::find_if(_views.begin(), _views.end(), [usage, &window, attachmentIdx](const auto& q) {
			return q._attachmentIdx == attachmentIdx && q._usage == usage && q._window.GetHash() == window.GetHash();
		});
		if (i3 == _views.end()) {
			_views.emplace_back(ViewCfg{attachmentIdx, usage, window});
			i3 = _views.end()-1;
		}

		// Expecting the attachment to already be barrier'd to the state we're requesting
		assert(i->second._currentState.has_value() && i->second._currentState->_layout.value_or(0) == usage);

		Cmd cmd;
		cmd._type = Cmd::Type::Bind;
		cmd._attachmentIdx = attachmentIdx;
		cmd._usiIdx = (unsigned)std::distance(usi.begin(), i2);
		cmd._viewIdx = (unsigned)std::distance(_views.begin(), i3);

		if (i->second._firstViewIdx == ~0u)
			i->second._firstViewIdx = cmd._viewIdx;
		
		_cmdList.push_back(cmd);
	}

	void ComputeAttachmentUniformsTracker::BindWithBarrier(ShaderUniformName uniform, AttachmentSemantic attachment, BindFlag::Enum usage, TextureViewDesc window)
	{
		Barrier(attachment, {(BindFlag::BitField)usage, ShaderStage::Compute});
		Bind(uniform, attachment, usage, window);
	}

	UniformsStreamInterface ComputeAttachmentUniformsTracker::EndUniformsStream()
	{
		assert(!_frozen);
		// Create a command that will generate a UniformsStream with everything bound as requested
		Cmd cmd;
		cmd._type = Cmd::Type::PrepareUniformsStream;
		cmd._usiIdx = (unsigned)_streams.back()._usi.size();		// repurposed for the size of the USI
		_usiCountMax = std::max(_usiCountMax, cmd._usiIdx);
		_cmdList.push_back(cmd);

		UniformsStreamInterface result;
		for (unsigned c=0; c<_streams.back()._usi.size(); ++c)
			result.BindResourceView(c, _streams.back()._usi[c]);
		_streams.emplace_back();
		return result;
	}

	auto ComputeAttachmentUniformsTracker::CreateSubpass(RenderStepFragmentInterface& fragmentInterface, const std::string& name) -> Techniques::FrameBufferDescFragment::SubpassDesc
	{
		_frozen = true;

		// Define attachments in the RenderStepFragmentInterface
		// todo -- if the attachment is already in the RenderStepFragmentInterface, might be useful to just use it from there
		VLA(AttachmentName, mappedAttachmentNames, _knownAttachments.size());
		unsigned idx = 0;
		for (auto& a:_knownAttachments) {
			auto definer = fragmentInterface.DefineAttachment(a.first);
			if (a.second._initialState)
				if (a.second._initialState->_layout)
					definer.InitialState(a.second._initialState->_loadStore, *a.second._initialState->_layout);
				else
					definer.InitialState(a.second._initialState->_loadStore);
			else
				definer.NoInitialState();
			assert(a.second._currentState.has_value());
			if (a.second._currentState->_layout)
				definer.FinalState(a.second._currentState->_loadStore, *a.second._currentState->_layout);
			else
				definer.FinalState(a.second._currentState->_loadStore);

			mappedAttachmentNames[idx++] = definer;
		}

		// Also create views for a subpass
		Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
		for (auto& v:_views)
			spDesc.AppendNonFrameBufferAttachmentView(mappedAttachmentNames[v._attachmentIdx], v._usage, v._window);
		spDesc.SetName(name);

		// we can now drop anything that's not needed by PassHelper
		_views = {};
		_streams = {};

		return spDesc;
	}

	ComputeAttachmentUniformsTracker::AttachmentState::AttachmentState(BindFlag::BitField layout, ShaderStage shaderStage)
	{
		_loadStore = LoadStore::Retain;
		_layout = layout;
		_shaderStageForBarriers = shaderStage;
	}

	ComputeAttachmentUniformsTracker::AttachmentState::AttachmentState(LoadStore loadStore, BindFlag::BitField layout, ShaderStage shaderStage)
	{
		_loadStore = loadStore;
		_layout = layout;
		_shaderStageForBarriers = shaderStage;
	}

	ComputeAttachmentUniformsTracker::AttachmentState::AttachmentState(LoadStore loadStore)
	{
		_loadStore = loadStore;
	}

	auto ComputeAttachmentUniformsTracker::AttachmentState::NoState() -> AttachmentState
	{
		return LoadStore::DontCare;
	}

	static Metal::BarrierResourceUsage AsBarrierResourceUsage(ComputeAttachmentUniformsTracker::AttachmentState attachmentState)
	{
		if (attachmentState._layout)
			return Metal::BarrierResourceUsage { *attachmentState._layout, attachmentState._shaderStageForBarriers };
		return Metal::BarrierResourceUsage::NoState();
	}

	UniformsStream ComputeAttachmentUniformsTracker::PassHelper::GetNextUniformsStream()
	{
		AdvanceCommands();
		if (_cmdListIterator != _parent->_cmdList.end() && _cmdListIterator->_type == Cmd::Type::PrepareUniformsStream) {
			UniformsStream result { MakeIteratorRange(_srvs.data(), &_srvs.data()[_cmdListIterator->_usiIdx]) };
			++_cmdListIterator;
			return result;
		}

		assert(0);		// requested more uniforms streams than were originally registered
		return {};
	}

	void ComputeAttachmentUniformsTracker::PassHelper::AdvanceCommands()
	{
		Metal::BarrierHelper barrierHelper(*_threadContext);
		bool activeBarrierHelper = false;

		// Advance forward, applying barriers as needed
		while (_cmdListIterator != _parent->_cmdList.end()) {
			if (_cmdListIterator->_type == Cmd::Type::Barrier) {
				activeBarrierHelper = true;
				IResource* res;
				if (auto firstView = _parent->_knownAttachments[_cmdListIterator->_attachmentIdx].second._firstViewIdx; firstView != ~0u) {
					res = _rpi->GetNonFrameBufferAttachmentView(firstView)->GetResource().get();
				} else {
					// we don't have a view, so we have to lookup this attachment by semantic name
					res = _rpi->GetAttachmentReservation().MapSemanticToResource(_parent->_knownAttachments[_cmdListIterator->_attachmentIdx].first).get();
				}
				barrierHelper.Add(
					*res, 
					AsBarrierResourceUsage(_cmdListIterator->_barrierOldState),
					AsBarrierResourceUsage(_cmdListIterator->_barrierNewState));
			} else if (_cmdListIterator->_type == Cmd::Type::PrepareUniformsStream) {
				return;				
			} else {
				assert(_cmdListIterator->_type == Cmd::Type::Bind);
				_srvs[_cmdListIterator->_usiIdx] = _rpi->GetNonFrameBufferAttachmentView(_cmdListIterator->_viewIdx).get();
			}

			++_cmdListIterator;
		}
	}

	void ComputeAttachmentUniformsTracker::PassHelper::EndPass()
	{
		_ended = true;

		// apply find binds and leave the attachments in their final states
		while (_cmdListIterator != _parent->_cmdList.end()) {
			// skip over non-barrier commands (perhaps we ended early as a result of an exception)
			if (_cmdListIterator->_type == Cmd::Type::PrepareUniformsStream)
				++_cmdListIterator;
			AdvanceCommands();
		}
	}

	ComputeAttachmentUniformsTracker::PassHelper::PassHelper(
		ComputeAttachmentUniformsTracker& parent,
		IThreadContext& threadContext,
		Techniques::RenderPassInstance& rpi)
	: _parent(&parent)
	, _threadContext(&threadContext)
	, _rpi(&rpi)
	{
		_srvs.resize(_parent->_usiCountMax);
		_cmdListIterator = _parent->_cmdList.begin();
	}

	ComputeAttachmentUniformsTracker::PassHelper::~PassHelper()
	{
		if (!_ended) EndPass();
	}

	auto ComputeAttachmentUniformsTracker::BeginPass(IThreadContext& threadContext, Techniques::RenderPassInstance& rpi) -> PassHelper
	{
		return PassHelper { *this, threadContext, rpi };
	}

	ComputeAttachmentUniformsTracker::ComputeAttachmentUniformsTracker()
	{
		_streams.emplace_back();		// first uniform stream is begun implicitly
	}

}}

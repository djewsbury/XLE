// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PostProcessOperators.h"
#include "RenderStepFragments.h"
#include "SequenceIterator.h"
#include "LightingEngine.h"				// for ChainedOperatorDesc
#include "LightingDelegateUtil.h"		// for ChainedOperatorCast
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Metal/Resource.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{

	class FragmentAttachmentUniformsHelper
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
		void PrepareUniformsStream();

		UniformsStreamInterface GetUSI();
		Techniques::FrameBufferDescFragment::SubpassDesc CreateSubpass(RenderStepFragmentInterface& fragmentInterface, const std::string& name);

		struct PassHelper;
		PassHelper BeginPass(IThreadContext&, Techniques::RenderPassInstance&);

		FragmentAttachmentUniformsHelper();
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

		std::vector<uint64_t> _workingUSI;

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

		bool _frozen = false;
	};

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

	void FragmentAttachmentUniformsHelper::ExpectAttachment(AttachmentSemantic attachment, AttachmentState expectedState)
	{
		assert(!_frozen);
		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i != _knownAttachments.end())
			Throw(std::runtime_error(StringMeld<256>() << "ExpectAttachment used twice for attachment (" << Internal::StreamAttachmentSemantic{attachment} << ") in FragmentAttachmentUniformsHelper"));

		_knownAttachments.emplace_back(
			attachment,
			KnownAttachment {
				expectedState,
				expectedState
			});
	}

	void FragmentAttachmentUniformsHelper::Barrier(AttachmentSemantic attachment, AttachmentState newState)
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

	void FragmentAttachmentUniformsHelper::Discard(AttachmentSemantic attachment)
	{
		assert(!_frozen);
		auto i = std::find_if(_knownAttachments.begin(), _knownAttachments.end(), [attachment](const auto& q) { return q.first == attachment; });
		if (i == _knownAttachments.end())
			return;

		i->second._currentState = AttachmentState::NoState();
	}

	void FragmentAttachmentUniformsHelper::Bind(ShaderUniformName uniform, AttachmentSemantic attachment, BindFlag::Enum usage, TextureViewDesc window)
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

		auto i2 = std::find(_workingUSI.begin(), _workingUSI.end(), uniform);
		if (i2 == _workingUSI.end()) {
			_workingUSI.push_back(uniform);
			i2 = _workingUSI.end()-1;
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
		cmd._usiIdx = (unsigned)std::distance(_workingUSI.begin(), i2);
		cmd._viewIdx = (unsigned)std::distance(_views.begin(), i3);

		if (i->second._firstViewIdx == ~0u)
			i->second._firstViewIdx = cmd._viewIdx;
		
		_cmdList.push_back(cmd);
	}

	void FragmentAttachmentUniformsHelper::BindWithBarrier(ShaderUniformName uniform, AttachmentSemantic attachment, BindFlag::Enum usage, TextureViewDesc window)
	{
		Barrier(attachment, {(BindFlag::BitField)usage, ShaderStage::Compute});
		Bind(uniform, attachment, usage, window);
	}

	void FragmentAttachmentUniformsHelper::PrepareUniformsStream()
	{
		assert(!_frozen);
		// Create a command that will generate a UniformsStream with everything bound as requested
		Cmd cmd;
		cmd._type = Cmd::Type::PrepareUniformsStream;
		_cmdList.push_back(cmd);
	}

	auto FragmentAttachmentUniformsHelper::CreateSubpass(RenderStepFragmentInterface& fragmentInterface, const std::string& name) -> Techniques::FrameBufferDescFragment::SubpassDesc
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

		return spDesc;
	}

	FragmentAttachmentUniformsHelper::AttachmentState::AttachmentState(BindFlag::BitField layout, ShaderStage shaderStage)
	{
		_loadStore = LoadStore::Retain;
		_layout = layout;
		_shaderStageForBarriers = shaderStage;
	}

	FragmentAttachmentUniformsHelper::AttachmentState::AttachmentState(LoadStore loadStore, BindFlag::BitField layout, ShaderStage shaderStage)
	{
		_loadStore = loadStore;
		_layout = layout;
		_shaderStageForBarriers = shaderStage;
	}

	FragmentAttachmentUniformsHelper::AttachmentState::AttachmentState(LoadStore loadStore)
	{
		_loadStore = loadStore;
	}

	auto FragmentAttachmentUniformsHelper::AttachmentState::NoState() -> AttachmentState
	{
		return LoadStore::DontCare;
	}

	static Metal::BarrierResourceUsage AsBarrierResourceUsage(FragmentAttachmentUniformsHelper::AttachmentState attachmentState)
	{
		if (attachmentState._layout)
			return Metal::BarrierResourceUsage { *attachmentState._layout, attachmentState._shaderStageForBarriers };
		return Metal::BarrierResourceUsage::NoState();
	}

	struct FragmentAttachmentUniformsHelper::PassHelper
	{
	public:
		UniformsStream GetNextUniformsStream();
		void EndPass();

		~PassHelper();
	private:
		PassHelper() = default;
		friend class FragmentAttachmentUniformsHelper;

		std::vector<const IResourceView*> _srvs;
		Techniques::RenderPassInstance* _rpi;
		IThreadContext* _threadContext;
		FragmentAttachmentUniformsHelper* _parent;
		std::vector<Cmd>::iterator _cmdListIterator;
		bool _ended = false;

		void AdvanceCommands();
	};

	UniformsStream FragmentAttachmentUniformsHelper::PassHelper::GetNextUniformsStream()
	{
		AdvanceCommands();
		if (_cmdListIterator != _parent->_cmdList.end() && _cmdListIterator->_type == Cmd::Type::PrepareUniformsStream) {
			++_cmdListIterator;
			return UniformsStream { MakeIteratorRange(_srvs) };
		}

		assert(0);		// requested more uniforms streams than were originally registered
		return {};
	}

	void FragmentAttachmentUniformsHelper::PassHelper::AdvanceCommands()
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

	void FragmentAttachmentUniformsHelper::PassHelper::EndPass()
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

	FragmentAttachmentUniformsHelper::PassHelper::~PassHelper()
	{
		if (!_ended) EndPass();
	}

	auto FragmentAttachmentUniformsHelper::BeginPass(IThreadContext& threadContext, Techniques::RenderPassInstance& rpi) -> PassHelper
	{
		PassHelper result;
		result._srvs.resize(_workingUSI.size());
		result._rpi = &rpi;
		result._threadContext = &threadContext;
		result._parent = this;
		result._cmdListIterator = _cmdList.begin();
		return result;
	}

	UniformsStreamInterface FragmentAttachmentUniformsHelper::GetUSI()
	{
		UniformsStreamInterface result;
		for (unsigned c=0; c<_workingUSI.size(); ++c)
			result.BindResourceView(c, _workingUSI[c]);
		return result;
	}

	FragmentAttachmentUniformsHelper::FragmentAttachmentUniformsHelper()
	{}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void PostProcessOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<PostProcessOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		assert(_secondStageConstructionState == 0);
		assert(_uniformsHelper);		// CreateFragment must have already been called
		_secondStageConstructionState = 1;

		ParameterBox selectors;
		selectors.SetParameter("SHARPEN", _desc._sharpen.has_value());

		auto shader = Techniques::CreateComputeOperator(
			_pool,
			POSTPROCESS_COMPUTE_HLSL ":main",
			std::move(selectors),
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			_uniformsHelper->GetUSI());

		::Assets::WhenAll(shader).ThenConstructToPromise(
			std::move(promise),
			[strongThis = shared_from_this()](auto shader) {
				assert(strongThis->_secondStageConstructionState == 1);
				strongThis->_shader = std::move(shader);
				strongThis->_secondStageConstructionState = 2;
				return strongThis;
			});
	}

	void PostProcessOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		UInt2 fbSize{fbProps._width, fbProps._height};
		stitchingContext.DefineAttachment(
			Techniques::PreregisteredAttachment {
				"PostProcessInput"_h,
				CreateDesc(
					BindFlag::UnorderedAccess | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], AsTypelessFormat(stitchingContext.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::LDRColor)))),
				"post-process-input"
			});
	}

	RenderStepFragmentInterface PostProcessOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		assert(_secondStageConstructionState == 0);
		RenderStepFragmentInterface result{PipelineType::Compute};

		assert(!_uniformsHelper);
		_uniformsHelper = std::make_unique<FragmentAttachmentUniformsHelper>();
		_uniformsHelper->ExpectAttachment("PostProcessInput"_h, {BindFlag::RenderTarget, ShaderStage::Pixel});

		_uniformsHelper->BindWithBarrier("Input"_h, "PostProcessInput"_h);
		_uniformsHelper->BindWithBarrier("Output"_h, Techniques::AttachmentSemantics::ColorLDR, BindFlag::UnorderedAccess, TextureViewDesc{TextureViewDesc::Aspect::ColorLinear});
		_uniformsHelper->PrepareUniformsStream();
		_uniformsHelper->Barrier(Techniques::AttachmentSemantics::ColorLDR, {BindFlag::RenderTarget, ShaderStage::Pixel});
		_uniformsHelper->Discard("PostProcessInput"_h);

		result.AddSubpass(
			_uniformsHelper->CreateSubpass(result, "post-process"),
			[op=shared_from_this()](SequenceIterator& iterator) {
				auto pass = op->_uniformsHelper->BeginPass(iterator._parsingContext->GetThreadContext(), iterator._rpi);

				auto uniforms = pass.GetNextUniformsStream();

				auto& parsingContext = *iterator._parsingContext;
				UInt2 outputDims { parsingContext.GetFrameBufferProperties()._width, parsingContext.GetFrameBufferProperties()._height };
				const unsigned groupSize = 8;
				op->_shader->Dispatch(
					parsingContext,
					(outputDims[0] + groupSize - 1) / groupSize, (outputDims[1] + groupSize - 1) / groupSize, 1,
					uniforms);
			});

		return result;
	}

	::Assets::DependencyValidation PostProcessOperator::GetDependencyValidation() const
	{
		assert(_secondStageConstructionState==2);
		return _shader->GetDependencyValidation();
	}

	auto PostProcessOperator::MakeCombinedDesc(const ChainedOperatorDesc* descChain) -> std::optional<CombinedDesc>
	{
		CombinedDesc result;
		bool foundSomething = false;

		while (descChain) {
			switch(descChain->_structureType) {
			case TypeHashCode<SharpenOperatorDesc>:
				result._sharpen = Internal::ChainedOperatorCast<SharpenOperatorDesc>(*descChain);
				foundSomething = true;
				break;
			}
			descChain = descChain->_next;
		}

		if (foundSomething)
			return result;
		return {};
	}

	PostProcessOperator::PostProcessOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const CombinedDesc& desc)
	: _pool(std::move(pipelinePool))
	, _secondStageConstructionState(0)
	, _desc(desc)
	{}

	PostProcessOperator::~PostProcessOperator()
	{}
}}

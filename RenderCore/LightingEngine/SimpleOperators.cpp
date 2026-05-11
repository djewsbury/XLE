// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SimpleOperators.h"
#include "RenderStepFragments.h"
#include "SequenceIterator.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../../Math/Vector.h"
#include <future>

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine 
{
	static const uint64_t s_refractionBufferSemantic = "refraction-buffer"_h;
	static const uint64_t s_refractionDepthBufferSemantic = "refraction-depth-buffer"_h;

	class RefractionBufferResourceDelegate : public Techniques::IShaderResourceDelegate
	{
	public:
		void WriteResourceViews(Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<IResourceView**> dst) override
		{
			if (bindingFlags & (1ull<<uint64_t(0))) {
				auto name = context.GetAttachmentReservation().MapSemanticToName(s_refractionBufferSemantic);
				assert(name != ~0u);
				dst[0] = context.GetAttachmentReservation().GetSRV(name).get();
			}
			if (bindingFlags & (1ull<<uint64_t(1))) {
				if (_desc._depthFormat == Format::Unknown)
					Throw(std::runtime_error("Refraction depth buffer required by shader, but not prepared by lighting technique"));
				auto name = context.GetAttachmentReservation().MapSemanticToName(s_refractionDepthBufferSemantic);
				assert(name != ~0u);
				dst[1] = context.GetAttachmentReservation().GetSRV(name).get();
			}
		}

		RefractionBufferResourceDelegate(const RefractionBufferOperatorDesc& desc)
		: _desc(desc)
		{
			_interface.BindResourceView(0, "RefractionBuffer"_h);
			_interface.BindResourceView(1, "RefractionDepthBuffer"_h);
		}

		RefractionBufferOperatorDesc _desc;
	};

	void RefractionBufferOperator::SecondStageConstruction(
		std::promise<std::shared_ptr<RefractionBufferOperator>>&& promise,
		const Techniques::FrameBufferTarget& fbTarget)
	{
		promise.set_value(shared_from_this());
	}

	void RefractionBufferOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps)
	{
		UInt2 fbSize{fbProps._width, fbProps._height};
		stitchingContext.DefineAttachment(
			Techniques::PreregisteredAttachment {
				s_refractionBufferSemantic,
				CreateDesc(
					BindFlag::TransferDst | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], _desc._format)),
				"refraction-buffer"
			});

		if (_desc._depthFormat != Format::Unknown)
			stitchingContext.DefineAttachment(
				Techniques::PreregisteredAttachment {
					s_refractionDepthBufferSemantic,
					CreateDesc(
						BindFlag::TransferDst | BindFlag::ShaderResource,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], _desc._depthFormat)),
					"refraction-depth-buffer"
				});
	}

	RenderStepFragmentInterface RefractionBufferOperator::CreateFragment(const FrameBufferProperties& fbProps)
	{
		BindFlag::BitField outputState = BindFlag::ShaderResource;

		RenderStepFragmentInterface fragment { RenderCore::PipelineType::Compute };
		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		subpass.SetName("refraction-buffer-copy");
		
		subpass.AppendNonFrameBufferAttachmentView(
			fragment.DefineAttachment(s_refractionBufferSemantic).NoInitialState().FinalState(outputState),
			BindFlag::TransferDst, {TextureViewDesc::Aspect::ColorLinear});
		subpass.AppendNonFrameBufferAttachmentView(
			fragment.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).FinalState(BindFlag::TransferSrc),
			BindFlag::TransferSrc, {TextureViewDesc::Aspect::ColorLinear});

		if (_desc._depthFormat != Format::Unknown) {
			subpass.AppendNonFrameBufferAttachmentView(
				fragment.DefineAttachment(s_refractionDepthBufferSemantic).NoInitialState().FinalState(outputState),
				BindFlag::TransferDst, {TextureViewDesc::Aspect::ColorLinear});
			subpass.AppendNonFrameBufferAttachmentView(
				fragment.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).FinalState(BindFlag::TransferSrc),
				BindFlag::TransferSrc, {TextureViewDesc::Aspect::ColorLinear});
		}

		fragment.AddSubpass(
			std::move(subpass),
			[op=this, outputState, doDepth=(_desc._depthFormat != Format::Unknown)](SequenceIterator& iterator) {

				iterator._rpi.AutoNonFrameBufferBarrier({
					{0, BindFlag::TransferDst},
					{1, BindFlag::TransferSrc}
				});

				// todo -- multisample will require resolve during these copies

				Metal::DeviceContext::Get(iterator._parsingContext->GetThreadContext())->BeginBlitEncoder().Copy(
					*iterator._rpi.GetNonFrameBufferAttachmentView(0)->GetResource(),
					*iterator._rpi.GetNonFrameBufferAttachmentView(1)->GetResource());

				iterator._rpi.AutoNonFrameBufferBarrier({
					{0, outputState}
				});

				if (doDepth) {
					iterator._rpi.AutoNonFrameBufferBarrier({
						{2, BindFlag::TransferDst},
						{3, BindFlag::TransferSrc}
					});

					Metal::DeviceContext::Get(iterator._parsingContext->GetThreadContext())->BeginBlitEncoder().Copy(
						*iterator._rpi.GetNonFrameBufferAttachmentView(2)->GetResource(),
						*iterator._rpi.GetNonFrameBufferAttachmentView(3)->GetResource());

					iterator._rpi.AutoNonFrameBufferBarrier({
						{2, outputState}
					});
				}
			});

		return fragment;
	}

	std::shared_ptr<Techniques::IShaderResourceDelegate> RefractionBufferOperator::CreateShaderResourceDelegate()
	{
		return std::make_shared<RefractionBufferResourceDelegate>(_desc);
	}

	::Assets::DependencyValidation RefractionBufferOperator::GetDependencyValidation() const { return {}; }

	RefractionBufferOperator::RefractionBufferOperator(
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const RefractionBufferOperatorDesc& desc)
	{}
	RefractionBufferOperator::~RefractionBufferOperator()
	{}


}}
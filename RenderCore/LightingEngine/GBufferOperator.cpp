// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GBufferOperator.h"
#include "RenderStepFragments.h"
#include "LightingEngineApparatus.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/TechniqueDelegates.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/DrawableDelegates.h"
#include "../Techniques/ParsingContext.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/Assets.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace LightingEngine
{
	class GBufferOperator::ResourceDelegate : public RenderCore::Techniques::IShaderResourceDelegate
	{
	public:
		virtual void WriteResourceViews(RenderCore::Techniques::ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<RenderCore::IResourceView**> dst)
		{
			assert(bindingFlags != 0);
			dst[0] = context._rpi->GetNonFrameBufferAttachmentView(0).get();
			dst[1] = context._rpi->GetNonFrameBufferAttachmentView(1).get();
		}

		ResourceDelegate(GBufferDelegateType type)
		{
			// we only need the historical buffers when we need to calculate the history confidence
			if (type == GBufferDelegateType::DepthMotionNormalRoughnessAccumulation) {
				BindResourceView(0, "DepthPrev"_h);
				BindResourceView(1, "GBufferNormalPrev"_h);
			}
		}
	};

	static GBufferDelegateType CalculateGBufferDelegateType(const GBufferOperatorDesc& opDesc)
	{
		if (opDesc._colorType != GBufferOperatorDesc::ColorType::None || opDesc._parametersType == GBufferOperatorDesc::ParametersType::Full) {
			if (opDesc._parametersType == GBufferOperatorDesc::ParametersType::Full)
				return GBufferDelegateType::DepthNormalParameters;
			return GBufferDelegateType::DepthNormal;
		}

		if (opDesc._historyConfidenceType != GBufferOperatorDesc::HistoryConfidenceType::None)
			return GBufferDelegateType::DepthMotionNormalRoughnessAccumulation;

		if (opDesc._parametersType == GBufferOperatorDesc::ParametersType::Roughness)
			return GBufferDelegateType::DepthMotionNormalRoughness;

		if (opDesc._normalType == GBufferOperatorDesc::NormalType::Packed8Bit)
			return GBufferDelegateType::DepthMotionNormal;

		if (opDesc._motionType != GBufferOperatorDesc::MotionType::None)
			return GBufferDelegateType::DepthMotion;

		return GBufferDelegateType::Depth;
	}

	RenderStepFragmentInterface GBufferOperator::CreateFragment()
	{
		RenderStepFragmentInterface frag { PipelineType::Graphics };
		Techniques::FrameBufferDescFragment::SubpassDesc subpass;
		std::vector<Techniques::FrameBufferDescFragment::DefineAttachmentHelper> attachments;

		switch (_gbufferType) {
		case GBufferDelegateType::Depth:
			subpass.SetName("gbuffer-Depth");
			break;

		case GBufferDelegateType::DepthMotion:
			{
				auto motion = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).FixedFormat(Format::R8G8_SINT);
				attachments.push_back(motion);
				subpass.AppendOutput(motion);
				subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepthPrev).InitialState(BindFlag::ShaderResource).Discard(), BindFlag::ShaderResource, {TextureViewDesc::Aspect::Depth});
				subpass.SetName("gbuffer-DepthMotion");
			}
			break;

		case GBufferDelegateType::DepthMotionNormal:
		case GBufferDelegateType::DepthMotionNormalRoughness:
			{
				auto motion = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).FixedFormat(Format::R8G8_SINT);
				auto normal = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).FixedFormat(Format::R8G8B8A8_SNORM);
				attachments.push_back(motion); attachments.push_back(normal);
				subpass.AppendOutput(motion);
				subpass.AppendOutput(normal);
				subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepthPrev).InitialState(BindFlag::ShaderResource).Discard(), BindFlag::ShaderResource, {TextureViewDesc::Aspect::Depth});
				subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormalPrev).FixedFormat(Format::R8G8B8A8_SNORM).InitialState(BindFlag::ShaderResource).Discard());
				if (_gbufferType == GBufferDelegateType::DepthMotionNormal) subpass.SetName("gbuffer-DepthMotionNormal");
				else subpass.SetName("gbuffer-DepthMotionNormalRoughness");
			}
			break;

		case GBufferDelegateType::DepthMotionNormalRoughnessAccumulation:
			{
				auto motion = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferMotion).FixedFormat(Format::R8G8_SINT);
				auto normal = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).FixedFormat(Format::R8G8B8A8_SNORM);
				auto historyConfidence = frag.DefineAttachment(Techniques::AttachmentSemantics::HistoryConfidence).FixedFormat(Format::R8_UNORM);
				attachments.push_back(motion); attachments.push_back(normal); attachments.push_back(historyConfidence);
				subpass.AppendOutput(motion);
				subpass.AppendOutput(normal);
				subpass.AppendOutput(historyConfidence);
				subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepthPrev).InitialState(BindFlag::ShaderResource).Discard(), BindFlag::ShaderResource, {TextureViewDesc::Aspect::Depth});
				subpass.AppendNonFrameBufferAttachmentView(frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormalPrev).FixedFormat(Format::R8G8B8A8_SNORM).InitialState(BindFlag::ShaderResource).Discard());
				subpass.SetName("gbuffer-DepthNormalRoughnessMotionAccumulation");
			}
			break;

		case GBufferDelegateType::DepthNormal:
			{
				auto color = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).FixedFormat(Format::R8G8B8A8_UNORM_SRGB);
				auto normal = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).FixedFormat(Format::R8G8B8A8_SNORM);
				attachments.push_back(color); attachments.push_back(normal);
				subpass.AppendOutput(color);
				subpass.AppendOutput(normal);
				subpass.SetName("gbuffer-DepthDiffuseNormal");
			}
			break;

		case GBufferDelegateType::DepthNormalParameters:
			{
				auto color = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferDiffuse).FixedFormat(Format::R8G8B8A8_UNORM_SRGB);
				auto normal = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferNormal).FixedFormat(Format::R8G8B8A8_SNORM);
				auto parameters = frag.DefineAttachment(Techniques::AttachmentSemantics::GBufferParameter).FixedFormat(Format::R8G8B8A8_UNORM);
				attachments.push_back(color); attachments.push_back(normal); attachments.push_back(parameters);
				subpass.AppendOutput(color);
				subpass.AppendOutput(normal);
				subpass.AppendOutput(parameters);
				subpass.SetName("gbuffer-DepthDiffuseNormalParameters");
			}
			break;

		default:
			assert(0);
			break;
		}

		auto requireBindFlags = BindFlag::ShaderResource;
		for (auto& a:attachments)
			a.RequireBindFlags(requireBindFlags).NoInitialState();

		auto msDepth = frag.DefineAttachment(Techniques::AttachmentSemantics::MultisampleDepth).Clear().RequireBindFlags(requireBindFlags);
		subpass.SetDepthStencil(msDepth);

		std::shared_ptr<Techniques::IShaderResourceDelegate> resDel;
		if (_gbufferType == GBufferDelegateType::DepthMotionNormalRoughnessAccumulation)
			resDel = std::make_shared<GBufferOperator::ResourceDelegate>(_gbufferType);

		frag.AddSubpass(std::move(subpass), _techDel, Techniques::BatchFlags::Opaque, {}, std::move(resDel));
		return frag;
	}

	void GBufferOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitching, const RenderCore::FrameBufferProperties& fbProps)
	{
		// note that we have to fully define the attachments we want to double buffer, in order for DefineDoubleBufferAttachment() to full define the "prev" attachment
		UInt2 fbSize{fbProps._width, fbProps._height};
		stitching.DefineAttachment(
			Techniques::PreregisteredAttachment {
				Techniques::AttachmentSemantics::MultisampleDepth,
				CreateDesc(
					BindFlag::DepthStencil | BindFlag::ShaderResource,
					TextureDesc::Plain2D(fbSize[0], fbSize[1], stitching.GetSystemAttachmentFormat(Techniques::SystemAttachmentFormat::MainDepthStencil))),
				"main-depth"
			});

		bool takesNormal = false, takesMotion = false, takesHistoryConfidence = false, takesColor = false, takesParameters = false;
		switch (_gbufferType) {
		case GBufferDelegateType::Depth:
			break;

		case GBufferDelegateType::DepthMotion:
			takesMotion = true;
			break;

		case GBufferDelegateType::DepthMotionNormal:
		case GBufferDelegateType::DepthMotionNormalRoughness:
			takesMotion = takesNormal = true;
			break;

		case GBufferDelegateType::DepthMotionNormalRoughnessAccumulation:
			takesMotion = takesNormal = takesHistoryConfidence = true;
			break;

		case GBufferDelegateType::DepthNormal:
			takesColor = takesNormal = true;
			break;

		case GBufferDelegateType::DepthNormalParameters:
			takesColor = takesNormal = takesParameters = true;
			break;

		default:
			assert(0);
			break;
		}

		auto requireBindFlags = BindFlag::ShaderResource;

		if (takesNormal && _opDesc._normalType != GBufferOperatorDesc::NormalType::None) {
			Format fmt = Format::Unknown;
			switch (_opDesc._normalType) {
			case GBufferOperatorDesc::NormalType::Packed8Bit: fmt = Format::R8G8B8A8_SNORM; break;
			case GBufferOperatorDesc::NormalType::Float16: fmt = Format::R16G16B16A16_FLOAT; break;
			case GBufferOperatorDesc::NormalType::Float32: fmt = Format::R32G32B32_FLOAT; break;
			default: assert(0); break;
			}
			stitching.DefineAttachment(
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferNormal,
					CreateDesc(
						BindFlag::RenderTarget | requireBindFlags,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], fmt)),
					"gbuffer-normal"
				});
		}

		if (takesMotion && _opDesc._motionType != GBufferOperatorDesc::MotionType::None) {
			stitching.DefineAttachment(
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferMotion,
					CreateDesc(
						BindFlag::RenderTarget | requireBindFlags,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8G8_SINT)),
					"gbuffer-motion"
				});
		}

		if (takesParameters && _opDesc._parametersType == GBufferOperatorDesc::ParametersType::Full) {
			stitching.DefineAttachment(
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferParameter,
					CreateDesc(
						BindFlag::RenderTarget | requireBindFlags,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8G8B8A8_UNORM)),
					"gbuffer-parameters"
				});
		}

		if (takesColor && _opDesc._colorType != GBufferOperatorDesc::ColorType::None) {
			stitching.DefineAttachment(
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::GBufferDiffuse,
					CreateDesc(
						BindFlag::RenderTarget | requireBindFlags,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8G8B8A8_UNORM_SRGB)),
					"gbuffer-color"
				});
		}

		if (takesHistoryConfidence && _opDesc._historyConfidenceType != GBufferOperatorDesc::HistoryConfidenceType::None) {
			stitching.DefineAttachment(
				Techniques::PreregisteredAttachment {
					Techniques::AttachmentSemantics::HistoryConfidence,
					CreateDesc(
						BindFlag::RenderTarget | requireBindFlags,
						TextureDesc::Plain2D(fbSize[0], fbSize[1], Format::R8_UNORM)),
					"gbuffer-history-confidence"
				});
		}

		bool historyType = _gbufferType == GBufferDelegateType::DepthMotion || _gbufferType == GBufferDelegateType::DepthMotionNormal || _gbufferType == GBufferDelegateType::DepthMotionNormalRoughness || _gbufferType == GBufferDelegateType::DepthMotionNormalRoughnessAccumulation;
		if (historyType) {
			stitching.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::MultisampleDepth, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
			if (_opDesc._normalType != GBufferOperatorDesc::NormalType::None && (_gbufferType == GBufferDelegateType::DepthMotionNormal || _gbufferType == GBufferDelegateType::DepthMotionNormalRoughness || _gbufferType == GBufferDelegateType::DepthMotionNormalRoughnessAccumulation))
				stitching.DefineDoubleBufferAttachment(Techniques::AttachmentSemantics::GBufferNormal, MakeClearValue(0,0,0,0), BindFlag::ShaderResource);
		}
	}

	::Assets::DependencyValidation GBufferOperator::GetDependencyValidation() const { return _techDel->GetDependencyValidation(); }

	GBufferOperator::GBufferOperator(
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> techDel,
		const GBufferOperatorDesc& opDesc)
	: _techDel(std::move(techDel))
	, _opDesc(opDesc)
	{
		_gbufferType = CalculateGBufferDelegateType(opDesc);
	}

	GBufferOperator::GBufferOperator() = default;

	void GBufferOperator::ConstructToPromise(
		std::promise<GBufferOperator>&& promise,
		std::shared_ptr<SharedTechniqueDelegateBox> sharedTechDel,
		const GBufferOperatorDesc& opDesc)
	{
		::Assets::WhenAll(sharedTechDel->GetGBufferDelegate(CalculateGBufferDelegateType(opDesc))).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[opDesc](auto techDel) {
				return GBufferOperator { std::move(techDel), opDesc };
			});
	}

	void GBufferOperator::ConstructToPromise(
		std::promise<GBufferOperator>&& promise,
		const GBufferOperatorDesc& opDesc)
	{
		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>> promisedTechDel;
		auto techDelFuture = promisedTechDel.get_future();
		CreateTechniqueDelegate_GBuffer(
			std::move(promisedTechDel),
			::Assets::GetAssetFuturePtr<Techniques::TechniqueSetFile>(ILLUM_TECH),
			CalculateGBufferDelegateType(opDesc));

		::Assets::WhenAll(std::move(techDelFuture)).CheckImmediately().ThenConstructToPromise(
			std::move(promise),
			[opDesc](auto techDel) {
				return GBufferOperator { std::move(techDel), opDesc };
			});
	}

	static const char* s_normalTypeNames[] { "None", "Packed8Bit", "Float16", "Float32" };
	static const char* s_motionTypeNames[] { "None", "Packed8Bit" };
	static const char* s_parametersTypeNames[] { "None", "Roughness", "Full" };
	static const char* s_colorTypeNames[] { "None", "DiffusePacked8Bit" };
	static const char* s_historyConfidenceTypeNames[] { "None", "Enabled" };

	static std::optional<GBufferOperatorDesc::NormalType> AsNormalType(StringSection<> shader)
	{
		for (unsigned c=0; c<dimof(s_normalTypeNames); ++c)
			if (XlEqString(shader, s_normalTypeNames[c]))
				return GBufferOperatorDesc::NormalType(c);
		return {};
	}

	static std::optional<GBufferOperatorDesc::MotionType> AsMotionType(StringSection<> shader)
	{
		for (unsigned c=0; c<dimof(s_motionTypeNames); ++c)
			if (XlEqString(shader, s_motionTypeNames[c]))
				return GBufferOperatorDesc::MotionType(c);
		return {};
	}

	static std::optional<GBufferOperatorDesc::ParametersType> AsParametersType(StringSection<> shader)
	{
		for (unsigned c=0; c<dimof(s_parametersTypeNames); ++c)
			if (XlEqString(shader, s_parametersTypeNames[c]))
				return GBufferOperatorDesc::ParametersType(c);
		return {};
	}

	static std::optional<GBufferOperatorDesc::ColorType> AsColorType(StringSection<> shader)
	{
		for (unsigned c=0; c<dimof(s_colorTypeNames); ++c)
			if (XlEqString(shader, s_colorTypeNames[c]))
				return GBufferOperatorDesc::ColorType(c);
		return {};
	}

	static std::optional<GBufferOperatorDesc::HistoryConfidenceType> AsHistoryConfidenceType(StringSection<> shader)
	{
		for (unsigned c=0; c<dimof(s_historyConfidenceTypeNames); ++c)
			if (XlEqString(shader, s_historyConfidenceTypeNames[c]))
				return GBufferOperatorDesc::HistoryConfidenceType(c);
		return {};
	}

	template<typename Formatter, typename std::enable_if_t<!std::is_same_v<std::decay_t<Formatter>, GBufferOperatorDesc>>*>
		GBufferOperatorDesc::GBufferOperatorDesc(Formatter& fmttr)
	{
		uint64_t keyName;
		while (Formatters::TryKeyedItem(fmttr, keyName)) {
			switch (keyName) {
			case "Normal"_h:
				_normalType = Formatters::RequireEnum<GBufferOperatorDesc::NormalType, AsNormalType>(fmttr);
				break;

			case "Motion"_h:
				_motionType = Formatters::RequireEnum<GBufferOperatorDesc::MotionType, AsMotionType>(fmttr);
				break;

			case "Parameters"_h:
				_parametersType = Formatters::RequireEnum<GBufferOperatorDesc::ParametersType, AsParametersType>(fmttr);
				break;

			case "Color"_h:
				_colorType = Formatters::RequireEnum<GBufferOperatorDesc::ColorType, AsColorType>(fmttr);
				break;

			case "HistoryConfidence"_h:
				_historyConfidenceType = Formatters::RequireEnum<GBufferOperatorDesc::HistoryConfidenceType, AsHistoryConfidenceType>(fmttr);
				break;

			default:
				Formatters::SkipValueOrElement(fmttr);
				break;
			}
		}
	}

	static uint64_t MaskBits(unsigned bitCount) { return (1ull << uint64_t(bitCount)) - 1ull; }

	uint64_t GBufferOperatorDesc::GetHash() const
	{
		assert((uint64_t(_normalType) & MaskBits(2)) == uint64_t(_normalType));
		assert((uint64_t(_motionType) & MaskBits(2)) == uint64_t(_motionType));
		assert((uint64_t(_parametersType) & MaskBits(2)) == uint64_t(_parametersType));
		assert((uint64_t(_colorType) & MaskBits(2)) == uint64_t(_colorType));
		assert((uint64_t(_historyConfidenceType) & MaskBits(2)) == uint64_t(_historyConfidenceType));
		return uint64_t(_normalType)
			| (uint64_t(_motionType) << 2ull)
			| (uint64_t(_parametersType) << 4ull)
			| (uint64_t(_colorType) << 6ull)
			| (uint64_t(_historyConfidenceType) << 8ull)
			;
	}

	template GBufferOperatorDesc::GBufferOperatorDesc(Formatters::TextInputFormatter<>&);
	template GBufferOperatorDesc::GBufferOperatorDesc(Formatters::IDynamicInputFormatter&);

}}


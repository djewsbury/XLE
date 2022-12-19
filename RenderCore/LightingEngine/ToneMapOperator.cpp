// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ToneMapOperator.h"
#include "LightingEngineIterator.h"
#include "RenderStepFragments.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/RenderPass.h"
#include "../Techniques/ParsingContext.h"
#include "../Techniques/CommonBindings.h"
#include "../Techniques/PipelineOperators.h"
#include "../Metal/Resource.h"
#include "../UniformsStream.h"
#include "../../Assets/Continuation.h"
#include "../../xleres/FileList.h"

namespace RenderCore { namespace LightingEngine 
{
	struct CB_Params
	{
		Float3x4 _preToneScale;
		Float3x4 _postToneScale;
	};

	static Float4x4 BuildPreToneScaleTransform();
	static Float4x4 BuildPostToneScaleTransform_SRGB();

	void ToneMapAcesOperator::Execute(Techniques::ParsingContext& parsingContext, IResourceView& ldrOutput, IResourceView& hdrInput)
	{
		Metal::BarrierHelper{parsingContext.GetThreadContext()}.Add(*hdrInput.GetResource(), BindFlag::UnorderedAccess, BindFlag::ShaderResource);
		Metal::BarrierHelper{parsingContext.GetThreadContext()}.Add(*ldrOutput.GetResource(), Metal::BarrierResourceUsage::NoState(), BindFlag::UnorderedAccess);

		_paramsBufferCounter = (_paramsBufferCounter+1)%dimof(_params);
		if (_paramsBufferCopyCountdown) {
			Metal::ResourceMap map { *parsingContext.GetThreadContext().GetDevice(), *_params[0]->GetResource(), Metal::ResourceMap::Mode::WriteDiscardPrevious };
			std::memcpy(
				PtrAdd(map.GetData().begin(), _paramsBufferCounter*_paramsData.size()),
				_paramsData.data(), _paramsData.size());
			_paramsBufferCopyCountdown--;
		}

		auto fbProps = parsingContext._rpi->GetFrameBufferDesc().GetProperties();
		assert(fbProps._width != 0 && fbProps._height != 0);
		const unsigned dispatchGroupWidth = 8;
		const unsigned dispatchGroupHeight = 8;
		ResourceViewStream uniforms {
			hdrInput, ldrOutput,
			*_params[_paramsBufferCounter],
		};
		_shader->Dispatch(
			parsingContext,
			(fbProps._width + dispatchGroupWidth - 1) / dispatchGroupWidth,
			(fbProps._height + dispatchGroupHeight - 1) / dispatchGroupHeight,
			1,
			uniforms);

		Metal::BarrierHelper{parsingContext.GetThreadContext()}.Add(*ldrOutput.GetResource(), {BindFlag::UnorderedAccess, ShaderStage::Compute}, BindFlag::RenderTarget);
	}

	::Assets::DependencyValidation ToneMapAcesOperator::GetDependencyValidation() const { return _shader->GetDependencyValidation(); }

	RenderStepFragmentInterface ToneMapAcesOperator::CreateFragment(const FrameBufferProperties& fbProps)
    {
        RenderStepFragmentInterface result{PipelineType::Compute};

		// todo -- what should we set the final state for ColorLDR to be here? just go directly to PresentationSrc?
        Techniques::FrameBufferDescFragment::SubpassDesc spDesc;
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorLDR).NoInitialState().FinalState(BindFlag::RenderTarget), BindFlag::UnorderedAccess);
        spDesc.AppendNonFrameBufferAttachmentView(result.DefineAttachment(Techniques::AttachmentSemantics::ColorHDR).InitialState(BindFlag::UnorderedAccess).Discard());
        spDesc.SetName("tone-map-aces-operator");

        result.AddSubpass(
            std::move(spDesc),
            [op=shared_from_this()](LightingTechniqueIterator& iterator) {
                op->Execute(
                    *iterator._parsingContext,
                    *iterator._rpi.GetNonFrameBufferAttachmentView(0),
                    *iterator._rpi.GetNonFrameBufferAttachmentView(1));
            });

        return result;
    }

	void ToneMapAcesOperator::PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext)
	{
		// todo -- should we actually define the ColorHDR attachment here?
	}

	ToneMapAcesOperator::ToneMapAcesOperator(
		const ToneMapAcesOperatorDesc& desc,
		std::shared_ptr<Techniques::IComputeShaderOperator> shader,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool)
	: _shader(std::move(shader))
	{
		_device = pipelinePool->GetDevice();
		_pool = std::move(pipelinePool);

		_paramsData.resize(sizeof(CB_Params));
		auto& params = *(CB_Params*)_paramsData.data();
		params._preToneScale = Truncate(BuildPreToneScaleTransform());
		params._postToneScale = Truncate(BuildPostToneScaleTransform_SRGB());

		// we need to multi-buffer the params buffer in order to update it safely
		auto paramsBuffer = _pool->GetDevice()->CreateResource(
			CreateDesc(BindFlag::ConstantBuffer, AllocationRules::HostVisibleSequentialWrite, LinearBufferDesc::Create(unsigned(dimof(_params)*_paramsData.size()))),
			"aces-tonemap-params");
		_params[0] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(0*_paramsData.size()), (unsigned)_paramsData.size());
		_params[1] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(1*_paramsData.size()), (unsigned)_paramsData.size());
		_params[2] = paramsBuffer->CreateBufferView(BindFlag::ConstantBuffer, unsigned(2*_paramsData.size()), (unsigned)_paramsData.size());
		_paramsBufferCopyCountdown = 3;
	}

	ToneMapAcesOperator::~ToneMapAcesOperator()
	{}

	void ToneMapAcesOperator::ConstructToPromise(
		std::promise<std::shared_ptr<ToneMapAcesOperator>>&& promise,
		std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
		const ToneMapAcesOperatorDesc& desc)
	{
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("HDRInput"));
		usi.BindResourceView(1, Hash64("LDROutput"));
		usi.BindResourceView(2, Hash64("Params"));

		ParameterBox params;

		// We could do tonemapping in a pixel shader with an input attachment
		// but it's probanly more practical to just use a compute shader
		auto futureShader = Techniques::CreateComputeOperator(
			pipelinePool,
			TONEMAP_ACES_COMPUTE_HLSL ":main",
			params,
			GENERAL_OPERATOR_PIPELINE ":ComputeMain",
			usi);
		::Assets::WhenAll(futureShader).ThenConstructToPromise(
			std::move(promise),
			[desc, pipelinePool=std::move(pipelinePool)](auto shader) {
				return std::make_shared<ToneMapAcesOperator>(desc, std::move(shader), pipelinePool);
			});
	}

	uint64_t ToneMapAcesOperatorDesc::GetHash() const
	{
		return 0;
	}

	namespace ACES
	{
		static Float3x3 Init3x3(Float3 A, Float3 B, Float3 C)
		{
			return MakeFloat3x3(
				A[0], B[0], C[0],
				A[1], B[1], C[1],
				A[2], B[2], C[2]);
		}

		// note i, j flipped (required because of ordering described in https://github.com/ampas/aces-dev/blob/dev/transforms/ctl/README-MATRIX.md)
		template<typename Matrix> float Element(const Matrix& m, int j, int i) { return m(i, j); }
		template<typename Matrix> float& Element(Matrix& m, int j, int i) { return m(i, j); }
		static Float4x4 mult_f44_f44(const Float4x4& lhs, const Float4x4& rhs) { return lhs * rhs; }
		static float pow10(float x) { return pow(10.f, x); }

		struct Chromaticities { Float2 red, green, blue, white; };

		static Float4x4 RGBtoXYZ(const Chromaticities &chroma, float Y)
		{
			// Reference -- http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
			// See also interesting papers (not sure how relevant they are) https://www.cs.sfu.ca/~mark/ftp/Cic97/cic97.pdf, https://www.researchgate.net/publication/3183222_A_New_Method_for_RGB_to_XYZ_Transformation_Based_on_Pattern_Search_Optimization
			float X = chroma.white[0] * Y / chroma.white[1];
			float Z = (1 - chroma.white[0] - chroma.white[1]) * Y / chroma.white[1];

			float d =
				chroma.red[0]   * (chroma.blue[1]  - chroma.green[1]) +
				chroma.blue[0]  * (chroma.green[1] - chroma.red[1]) +
				chroma.green[0] * (chroma.red[1]   - chroma.blue[1]);

			float Sr = (X * (chroma.blue[1] - chroma.green[1]) -
				chroma.green[0] * (Y * (chroma.blue[1] - 1) +
				chroma.blue[1]  * (X + Z)) +
				chroma.blue[0]  * (Y * (chroma.green[1] - 1) +
				chroma.green[1] * (X + Z))) / d;

			float Sg = (X * (chroma.red[1] - chroma.blue[1]) +
				chroma.red[0]   * (Y * (chroma.blue[1] - 1) +
				chroma.blue[1]  * (X + Z)) -
				chroma.blue[0]  * (Y * (chroma.red[1] - 1) +
				chroma.red[1]   * (X + Z))) / d;

			float Sb = (X * (chroma.green[1] - chroma.red[1]) -
				chroma.red[0]   * (Y * (chroma.green[1] - 1) +
				chroma.green[1] * (X + Z)) +
				chroma.green[0] * (Y * (chroma.red[1] - 1) +
				chroma.red[1]   * (X + Z))) / d;

			Float4x4 M = Identity<Float4x4>();
			Element(M, 0, 0) = Sr * chroma.red[0];
			Element(M, 0, 1) = Sr * chroma.red[1];
			Element(M, 0, 2) = Sr * (1 - chroma.red[0] - chroma.red[1]);

			Element(M, 1, 0) = Sg * chroma.green[0];
			Element(M, 1, 1) = Sg * chroma.green[1];
			Element(M, 1, 2) = Sg * (1 - chroma.green[0] - chroma.green[1]);

			Element(M, 2, 0) = Sb * chroma.blue[0];
			Element(M, 2, 1) = Sb * chroma.blue[1];
			Element(M, 2, 2) = Sb * (1 - chroma.blue[0] - chroma.blue[1]);
			return M;
		}

		static Float4x4 XYZtoRGB (const Chromaticities &chroma, float Y)
		{
			return Inverse(RGBtoXYZ(chroma, Y));
		}

		static Float3x3 calc_sat_adjust_matrix(float sat, Float3 rgb2Y)
		{
			// Following the ACES reference transform, this just causes some percentage
			// of each color channel to be added to the other channels -- thereby decreasing saturation
			Float3x3 M;
			Element(M, 0, 0) = (1.0f - sat) * rgb2Y[0] + sat;
			Element(M, 1, 0) = (1.0f - sat) * rgb2Y[0];
			Element(M, 2, 0) = (1.0f - sat) * rgb2Y[0];
			
			Element(M, 0, 1) = (1.0f - sat) * rgb2Y[1];
			Element(M, 1, 1) = (1.0f - sat) * rgb2Y[1] + sat;
			Element(M, 2, 1) = (1.0f - sat) * rgb2Y[1];
			
			Element(M, 0, 2) = (1.0f - sat) * rgb2Y[2];
			Element(M, 1, 2) = (1.0f - sat) * rgb2Y[2];
			Element(M, 2, 2) = (1.0f - sat) * rgb2Y[2] + sat;
			M = Transpose(M);    
			return M;
		}

		// Reference -- ACESlib.Utilities_Color.ctl
		static const Chromaticities AP0 = // From reference, this is the definition of AP0 color space
		{
			{ 0.73470f,  0.26530f},
			{ 0.00000f,  1.00000f},
			{ 0.00010f, -0.07700f},
			{ 0.32168f,  0.33767f}
		};

		const Chromaticities AP1 = // As above, this is the definition of AP1 color space
		{
			{ 0.71300f,  0.29300f},
			{ 0.16500f,  0.83000f},
			{ 0.12800f,  0.04400f},
			{ 0.32168f,  0.33767f}
		};

		static const Chromaticities REC709_PRI =
		{
			{ 0.64000f,  0.33000f},
			{ 0.30000f,  0.60000f},
			{ 0.15000f,  0.06000f},
			{ 0.31270f,  0.32900f}
		};

		// Reference -- ACESlib.Transform_Common.ctl
		// Using the same names as the ACES reference code here to ensure that following the code is a little clearer
		static const Float4x4 AP0_2_XYZ_MAT = RGBtoXYZ( AP0, 1.0);
		static const Float4x4 XYZ_2_AP0_MAT = XYZtoRGB( AP0, 1.0);

		static const Float4x4 AP1_2_XYZ_MAT = RGBtoXYZ( AP1, 1.0);
		static const Float4x4 XYZ_2_AP1_MAT = XYZtoRGB( AP1, 1.0);

		static const Float4x4 AP0_2_AP1_MAT = mult_f44_f44( AP0_2_XYZ_MAT, XYZ_2_AP1_MAT);
		static const Float4x4 AP1_2_AP0_MAT = mult_f44_f44( AP1_2_XYZ_MAT, XYZ_2_AP0_MAT);

		static const Float3 AP1_RGB2Y = {
			Element(AP1_2_XYZ_MAT, 0, 1),
			Element(AP1_2_XYZ_MAT, 1, 1),
			Element(AP1_2_XYZ_MAT, 2, 1) };

		// Reference -- ACESlib.RRT_Common.ctl
		static const float RRT_SAT_FACTOR = 0.96f;
		static const Float3x3 RRT_SAT_MAT = calc_sat_adjust_matrix(RRT_SAT_FACTOR, AP1_RGB2Y);

		// Reference -- ACESlib.ODT_Common.ctl
		static const float ODT_SAT_FACTOR = 0.93f;
		static const Float3x3 ODT_SAT_MAT = calc_sat_adjust_matrix( ODT_SAT_FACTOR, AP1_RGB2Y);
		static const float CINEMA_WHITE = 48.0f;
		static const float CINEMA_BLACK = pow10(std::log10(0.02f));

		static const Chromaticities DISPLAY_PRI = REC709_PRI;
		static const Float4x4 XYZ_2_DISPLAY_PRI_MAT = XYZtoRGB(DISPLAY_PRI, 1.0f);
	}

	static Float4x4 BuildPreToneScaleTransform()
	{
		auto A = Expand(ACES::Init3x3(			// sRGB to XYZ (D65 white) http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
			{0.4124564, 0.2126729, 0.0193339},		// (transposed from textbook form)
			{0.3575761, 0.7151522, 0.1191920},
			{0.1804375, 0.0721750, 0.9503041}),
			Float3(0,0,0));
		auto XYZtoAP0 = ACES::XYZtoRGB( ACES::AP0, 1.0);		// aces matrix conventions
		auto result = Expand(ACES::RRT_SAT_MAT, {0,0,0}) * ACES::AP0_2_AP1_MAT * XYZtoAP0 * A;
		return result;
	}

	static Float4x4 BuildPostToneScaleTransform_SRGB()
	{
		// Note that the output color uses the SRGB primaries, but it's still linear (in that the reverse monitor curve is not applied)
		float A = 1.0f / (ACES::CINEMA_WHITE - ACES::CINEMA_BLACK);
		Float4x4 {
			  A, 0.f, 0.f, -ACES::CINEMA_BLACK * A,
			0.f,   A, 0.f, -ACES::CINEMA_BLACK * A,
			0.f, 0.f,   A, -ACES::CINEMA_BLACK * A,
			0.f, 0.f, 0.f, 1.f};
		// Aces uses a unique whitepoint (which is commonly called D60, though there are some technicalities there)
		// The reference ODT compensates for this by adjusting the color in XYZ space using the following transform
		const Float3x3 D60_2_D65_CAT {
			1.00744021f, 0.00458632875f, 0.00342495739f,
			0.00197348557f, 0.997794211f, -0.00621009618f,
			0.0135383308f, 0.00393609330f, 1.08976591f
		};
		auto result = ACES::XYZ_2_DISPLAY_PRI_MAT * Expand(D60_2_D65_CAT, {0,0,0}) * ACES::AP1_2_XYZ_MAT * Expand(ACES::ODT_SAT_MAT, {0,0,0}) * A;
		return result;
	}

}}


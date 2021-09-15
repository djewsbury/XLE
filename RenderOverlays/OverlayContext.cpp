// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "OverlayContext.h"
#include "Font.h"
#include "FontRendering.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/StateDesc.h"
#include "../RenderCore/UniformsStream.h"
#include "../Assets/Assets.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"

namespace RenderOverlays
{
	using namespace RenderCore;

	class Vertex_PC     { public: Float3 _position; unsigned _colour;                                                               Vertex_PC(Float3 position, unsigned colour) : _position(position), _colour(colour) {};                                          static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };
	class Vertex_PCR    { public: Float3 _position; unsigned _colour; float _radius;                                                Vertex_PCR(Float3 position, unsigned colour, float radius) : _position(position), _colour(colour), _radius(radius) {};          static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };
	class Vertex_PCT    { public: Float3 _position; unsigned _colour; Float2 _texCoord;                                             Vertex_PCT(Float3 position, unsigned colour, Float2 texCoord) : _position(position), _colour(colour), _texCoord(texCoord) {};   static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };
	class Vertex_PCCTT  { public: Float3 _position; unsigned _colour0; unsigned _colour1; Float2 _texCoord0; Float2 _texCoord1;     Vertex_PCCTT(Float3 position, unsigned colour0, unsigned colour1, Float2 texCoord0, Float2 texCoord1) : _position(position), _colour0(colour0), _colour1(colour1), _texCoord0(texCoord0), _texCoord1(texCoord1) {};   static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };

	static inline unsigned  HardwareColor(ColorB input)
	{
		return (uint32_t(input.a) << 24) | (uint32_t(input.b) << 16) | (uint32_t(input.g) << 8) | uint32_t(input.r);
	}

	MiniInputElementDesc Vertex_PC::inputElements3D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM }
	};

	MiniInputElementDesc Vertex_PCR::inputElements3D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::RADIUS, Format::R32_FLOAT }
	};

	MiniInputElementDesc Vertex_PCT::inputElements3D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD, Format::R32G32_FLOAT }
	};

	MiniInputElementDesc Vertex_PCCTT::inputElements3D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR + 1, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD, Format::R32G32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD + 1, Format::R32G32_FLOAT }
	};

	MiniInputElementDesc Vertex_PC::inputElements2D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::PIXELPOSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM }
	};

	MiniInputElementDesc Vertex_PCR::inputElements2D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::PIXELPOSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::RADIUS, Format::R32_FLOAT }
	};

	MiniInputElementDesc Vertex_PCT::inputElements2D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::PIXELPOSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD, Format::R32G32_FLOAT }
	};

	MiniInputElementDesc Vertex_PCCTT::inputElements2D[] = 
	{
		MiniInputElementDesc{ Techniques::CommonSemantics::PIXELPOSITION, Format::R32G32B32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::COLOR + 1, Format::R8G8B8A8_UNORM },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD, Format::R32G32_FLOAT },
		MiniInputElementDesc{ Techniques::CommonSemantics::TEXCOORD + 1, Format::R32G32_FLOAT }
	};

	class ImmediateOverlayContext::DrawCall
	{
	public:
		unsigned				_vertexCount;
		RenderCore::Topology	_topology;
		IteratorRange<const MiniInputElementDesc*> _inputAssembly;
		std::shared_ptr<RenderCore::IResourceView> _textureResource;
	};
	
	void ImmediateOverlayContext::DrawPoint      (ProjectionMode proj, const Float3& v,     const ColorB& col,      uint8_t size)
	{
		using Vertex = Vertex_PCR;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{1, Topology::PointList, inputElements}).Cast<Vertex*>();
		data[0] = Vertex(v, HardwareColor(col), float(size));
	}

	void ImmediateOverlayContext::DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    uint8_t size)
	{
		using Vertex = Vertex_PCR;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::PointList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col), float(size));
	}

	void ImmediateOverlayContext::DrawPoints     (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   uint8_t size)
	{
		using Vertex = Vertex_PCR;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::PointList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col[c]), float(size));
	}

	void ImmediateOverlayContext::DrawLine       (ProjectionMode proj, const Float3& v0,    const ColorB& colV0,    const Float3& v1,     const ColorB& colV1, float thickness)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{2, Topology::LineList, inputElements}).Cast<Vertex*>();
		data[0] = Vertex(v0, HardwareColor(colV0));
		data[1] = Vertex(v1, HardwareColor(colV1));
	}

	void ImmediateOverlayContext::DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col,    float thickness)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::LineList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col));
	}

	void ImmediateOverlayContext::DrawLines      (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[],   float thickness)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::LineList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col[c]));
	}

	void ImmediateOverlayContext::DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB& col)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::TriangleList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col));
	}

	void ImmediateOverlayContext::DrawTriangles  (ProjectionMode proj, const Float3 v[],    uint32 numPoints,       const ColorB col[])
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{numPoints, Topology::TriangleList, inputElements}).Cast<Vertex*>();
		for (unsigned c=0; c<numPoints; ++c)
			data[c] = Vertex(v[c], HardwareColor(col[c]));
	}

	void ImmediateOverlayContext::DrawTriangle   (  ProjectionMode proj,
													const Float3& v0,    const ColorB& colV0,    const Float3& v1,     
													const ColorB& colV1, const Float3& v2,       const ColorB& colV2)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{3, Topology::TriangleList, inputElements}).Cast<Vertex*>();
		data[0] = Vertex(v0, HardwareColor(colV0));
		data[1] = Vertex(v1, HardwareColor(colV1));
		data[2] = Vertex(v2, HardwareColor(colV2));
	}
	
	void    ImmediateOverlayContext::DrawQuad(
		ProjectionMode proj, 
		const Float3& mins, const Float3& maxs, 
		ColorB color0, ColorB color1,
		const Float2& minTex0, const Float2& maxTex0, 
		const Float2& minTex1, const Float2& maxTex1,
		RenderCore::Techniques::ImmediateDrawableMaterial&& material)
	{
		using Vertex = Vertex_PCCTT;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);

		auto data = _immediateDrawables->QueueDraw(6, inputElements, std::move(material), Topology::TriangleList).Cast<Vertex*>();
		auto col0 = HardwareColor(color0);
		auto col1 = HardwareColor(color1);
		data[0] = Vertex(Float3(mins[0], mins[1], mins[2]), col0, col1, Float2(minTex0[0], minTex0[1]), Float2(minTex1[0], minTex1[1]));
		data[1] = Vertex(Float3(mins[0], maxs[1], mins[2]), col0, col1, Float2(minTex0[0], maxTex0[1]), Float2(minTex1[0], maxTex1[1]));
		data[2] = Vertex(Float3(maxs[0], mins[1], mins[2]), col0, col1, Float2(maxTex0[0], minTex0[1]), Float2(maxTex1[0], minTex1[1]));
		data[3] = Vertex(Float3(maxs[0], mins[1], mins[2]), col0, col1, Float2(maxTex0[0], minTex0[1]), Float2(maxTex1[0], minTex1[1]));
		data[4] = Vertex(Float3(mins[0], maxs[1], mins[2]), col0, col1, Float2(minTex0[0], maxTex0[1]), Float2(minTex1[0], maxTex1[1]));
		data[5] = Vertex(Float3(maxs[0], maxs[1], mins[2]), col0, col1, Float2(maxTex0[0], maxTex0[1]), Float2(maxTex1[0], maxTex1[1]));
	}

	void    ImmediateOverlayContext::DrawQuad(
			ProjectionMode proj, 
			const Float3& mins, const Float3& maxs, 
			ColorB color)
	{
		using Vertex = Vertex_PC;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{6, Topology::TriangleList, inputElements}).Cast<Vertex*>();
		auto col = HardwareColor(color);
		data[0] = Vertex(Float3(mins[0], mins[1], mins[2]), col);
		data[1] = Vertex(Float3(mins[0], maxs[1], mins[2]), col);
		data[2] = Vertex(Float3(maxs[0], mins[1], mins[2]), col);
		data[3] = Vertex(Float3(maxs[0], mins[1], mins[2]), col);
		data[4] = Vertex(Float3(mins[0], maxs[1], mins[2]), col);
		data[5] = Vertex(Float3(maxs[0], maxs[1], mins[2]), col);
	}

	void ImmediateOverlayContext::DrawTexturedQuad(
		ProjectionMode proj, 
		const Float3& mins, const Float3& maxs, 
		std::shared_ptr<RenderCore::IResourceView> textureResource,
		ColorB color, const Float2& minTex0, const Float2& maxTex0)
	{
		using Vertex = Vertex_PCCTT;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{6, Topology::TriangleList, inputElements, std::move(textureResource)}).Cast<Vertex*>();
		auto col = HardwareColor(color);
		data[0] = Vertex(Float3(mins[0], mins[1], mins[2]), col, col, Float2(minTex0[0], minTex0[1]), Float2(0.f, 0.f));
		data[1] = Vertex(Float3(mins[0], maxs[1], mins[2]), col, col, Float2(minTex0[0], maxTex0[1]), Float2(0.f, 0.f));
		data[2] = Vertex(Float3(maxs[0], mins[1], mins[2]), col, col, Float2(maxTex0[0], minTex0[1]), Float2(0.f, 0.f));
		data[3] = Vertex(Float3(maxs[0], mins[1], mins[2]), col, col, Float2(maxTex0[0], minTex0[1]), Float2(0.f, 0.f));
		data[4] = Vertex(Float3(mins[0], maxs[1], mins[2]), col, col, Float2(minTex0[0], maxTex0[1]), Float2(0.f, 0.f));
		data[5] = Vertex(Float3(maxs[0], maxs[1], mins[2]), col, col, Float2(maxTex0[0], maxTex0[1]), Float2(0.f, 0.f));
	}

	float ImmediateOverlayContext::DrawText      (  const std::tuple<Float3, Float3>& quad, 
													const std::shared_ptr<Font>& font, const TextStyle& textStyle, 
													ColorB col, 
													TextAlignment alignment, StringSection<char> text)
	{
		if (!_fontRenderingManager) return 0.f;

		ucs4 unicharBuffer[4096];
		utf8_2_ucs4((const utf8*)text.begin(), text.size(), unicharBuffer, dimof(unicharBuffer));
		StringSection<ucs4> convertedText = unicharBuffer;

		Quad q;
		q.min = Float2(std::get<0>(quad)[0], std::get<0>(quad)[1]);
		q.max = Float2(std::get<1>(quad)[0], std::get<1>(quad)[1]);
		Float2 alignedPosition = AlignText(*_defaultFont, q, alignment, convertedText);
		return Draw(
			*_threadContext,
			*_immediateDrawables, 
			*_fontRenderingManager,
			font ? *font : *_defaultFont, textStyle,
			alignedPosition[0], alignedPosition[1],
			convertedText,
			0.f, 1.f, 0.f, 
			LinearInterpolate(std::get<0>(quad)[2], std::get<1>(quad)[2], 0.5f),
			col.AsUInt32(), false);
	}

	void ImmediateOverlayContext::CaptureState() 
	{
		SetState(OverlayState());
	}

	void ImmediateOverlayContext::ReleaseState() 
	{
	}

	void ImmediateOverlayContext::SetState(const OverlayState& state) 
	{
		_currentState = state;
	}

	BufferUploads::CommandListID ImmediateOverlayContext::GetRequiredBufferUploadsCommandList() const
	{ 
		return _requiredBufferUploadsCommandList;
	}

	void ImmediateOverlayContext::RequireCommandList(BufferUploads::CommandListID cmdList)
	{
		_requiredBufferUploadsCommandList = std::max(_requiredBufferUploadsCommandList, cmdList);
	}

	static RenderCore::Techniques::ImmediateDrawableMaterial AsMaterial(const OverlayState& state)
	{
		RenderCore::Techniques::ImmediateDrawableMaterial result;
		result._stateSet._forwardBlendSrc = Blend::SrcAlpha;
		result._stateSet._forwardBlendDst = Blend::InvSrcAlpha;
		result._stateSet._forwardBlendOp = BlendOp::Add;
		result._stateSet._flag = RenderCore::Assets::RenderStateSet::Flag::ForwardBlend | RenderCore::Assets::RenderStateSet::Flag::WriteMask;
		result._stateSet._writeMask = 1<<1;		// repurposed -- bit 0 is depth write; bit 1 is depth read
		return result;
	}

	IteratorRange<void*> ImmediateOverlayContext::BeginDrawCall(const DrawCall& drawCall)
	{
		if (!drawCall._vertexCount) return {}; // (skip draw calls with zero vertices)

		auto mat = AsMaterial(_currentState);
		if (drawCall._textureResource) {
			mat._uniformStreamInterface = _texturedUSI;
			mat._uniforms._resourceViews.push_back(drawCall._textureResource);
		}

		return _immediateDrawables->QueueDraw(
			drawCall._vertexCount,
			drawCall._inputAssembly,
			std::move(mat),
			drawCall._topology);
	}

#if 0
	class ImmediateOverlayContext::ShaderBox
	{
	public:
		class Desc
		{
		public:
			Topology _topology;
			VertexFormat _format;
			ProjectionMode _projMode;
			std::string _pixelShaderName;

			Desc(Topology topology, VertexFormat format, ProjectionMode projMode, const std::string& pixelShaderName)
				: _topology(topology), _format(format), _projMode(projMode), _pixelShaderName(pixelShaderName) {}
		};

		const Metal::ShaderProgram* _shaderProgram;
		Metal::BoundInputLayout _boundInputLayout;
		Metal::BoundUniforms _boundUniforms;
		Metal::BoundClassInterfaces _boundClassInterfaces;

		const ::Assets::DependencyValidation& GetDependencyValidation() const     
			{ return _validationCallback; }
		
		ShaderBox(const Desc&);

	private:
		std::shared_ptr<::Assets::DependencyValidation>   _validationCallback;
	};

	ImmediateOverlayContext::ShaderBox::ShaderBox(const Desc& desc)
	{
		auto validationCallback = std::make_shared<::Assets::DependencyValidation>();
		InputLayout inputLayout;
		_shaderProgram = nullptr;

		const char* vertexShaderSource = nullptr;
		const char* geometryShaderSource = nullptr;
		const char* pixelShaderDefault = nullptr;

		if (desc._topology == Topology::PointList) {

			if (desc._format == PCR) {
				vertexShaderSource = (desc._projMode==ProjectionMode::P2D)? BASIC2D_VERTEX_HLSL ":P2CR:vs_*" : BASIC3D_VERTEX_HLSL ":PCR:vs_*";
				geometryShaderSource = BASIC_GEO_HLSL ":PCR:gs_*";
				pixelShaderDefault = BASIC_PIXEL_HLSL ":PC:ps_*";
				inputLayout = MakeIteratorRange(Vertex_PCR::inputElements);
			}

		} else {

			if (desc._format == PC) {
				vertexShaderSource = (desc._projMode==ProjectionMode::P2D)? BASIC2D_VERTEX_HLSL ":P2C:vs_*" : BASIC3D_VERTEX_HLSL ":PC:vs_*";
				pixelShaderDefault = BASIC_PIXEL_HLSL ":PC:ps_*";
				inputLayout = MakeIteratorRange(Vertex_PC::inputElements);
			} else if (desc._format == PCT) {
				vertexShaderSource = (desc._projMode==ProjectionMode::P2D)? BASIC2D_VERTEX_HLSL ":P2CT:vs_*" : BASIC3D_VERTEX_HLSL ":PCT:vs_*";
				pixelShaderDefault = BASIC_PIXEL_HLSL ":PCT:ps_*";
				inputLayout = MakeIteratorRange(Vertex_PCT::inputElements);
			} else if (desc._format == PCCTT) {
				vertexShaderSource = (desc._projMode==ProjectionMode::P2D)? BASIC2D_VERTEX_HLSL ":P2CCTT:vs_*" : BASIC3D_VERTEX_HLSL ":PCCTT:vs_*";
				pixelShaderDefault = BASIC_PIXEL_HLSL ":PCT:ps_*";
				inputLayout = MakeIteratorRange(Vertex_PCCTT::inputElements);
			}

		}

		if (desc._pixelShaderName.empty()) {
			if (geometryShaderSource) {
				_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, geometryShaderSource, pixelShaderDefault, "");
			} else 
				_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, pixelShaderDefault, "");
		} else {
			StringMeld<MaxPath, ::Assets::ResChar> assetName;
			auto paramStart = desc._pixelShaderName.find_first_of(':');
			auto comma = desc._pixelShaderName.find_first_of(',', paramStart);
			if (paramStart != std::string::npos && comma != std::string::npos) {

					// this shader name has extra parameters (in the form:
					//   <file>:<entry point>,<interface>=<implementation>
					//
					// Build a version of this shader with dynamic linking enabled,
					// and create a binding for the class interfaces we want.
				assetName << desc._pixelShaderName.substr(0, comma) << ":!ps_*";

				if (geometryShaderSource) {
					_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, geometryShaderSource, assetName.get(), "");
				} else
					_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, assetName.get(), "");
				_boundClassInterfaces = Metal::BoundClassInterfaces(*_shaderProgram);

				auto i = desc._pixelShaderName.cbegin() + comma + 1;
				for (;;) {
					while (i < desc._pixelShaderName.cend() && *i == ',') ++i;
					auto start = i;
					while (i < desc._pixelShaderName.cend() && *i != ',' && *i != '=') ++i;
					if (i == start) break;

					if (i < desc._pixelShaderName.cend() && *i == '=') {
						auto classEnd = i;
						auto instanceNameStart = i+1;
						while (i < desc._pixelShaderName.cend() && *i != ',') ++i;
						auto bindingHash = Hash64(AsPointer(start), AsPointer(classEnd));
						_boundClassInterfaces.Bind(bindingHash, 0, 
							desc._pixelShaderName.substr(
								std::distance(desc._pixelShaderName.cbegin(), instanceNameStart), i-instanceNameStart).c_str());
					} else {
						Log(Warning) << "Malformed shader name in OverlayContext: " << desc._pixelShaderName << std::endl;
					}
				}
			} else {
				assetName << desc._pixelShaderName << ":ps_*";
				if (geometryShaderSource) {
					_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, geometryShaderSource, assetName.get(), "");
				} else
					_shaderProgram = &::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(vertexShaderSource, assetName.get(), "");
			}
		}

		if (_shaderProgram) {
			Metal::BoundInputLayout boundInputLayout(inputLayout, *_shaderProgram);
			UniformsStreamInterface uniformsInterf;
			uniformsInterf.BindImmediateData(0, Hash64("ReciprocalViewportDimensionsCB"), MakeIteratorRange(ReciprocalViewportDimensions_Elements));
			Metal::BoundUniforms boundUniforms(
				*_shaderProgram,
				Metal::PipelineLayoutConfig(),
				Techniques::TechniqueContext::GetGlobalUniformsStreamInterface(),
				uniformsInterf);

			validationCallback.RegisterDependency(_shaderProgram->GetDependencyValidation());

			_boundInputLayout = std::move(boundInputLayout);
			_boundUniforms = std::move(boundUniforms);
		}

		_validationCallback = std::move(validationCallback);
	}
#endif

	class DefaultFontBox
	{
	public:
		std::shared_ptr<Font> _font;
		DefaultFontBox()
		{
			_font = GetX2Font("Petra", 16);
		}
	};

	ImmediateOverlayContext::ImmediateOverlayContext(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables)
	: _immediateDrawables(&immediateDrawables)
	, _threadContext(&threadContext)
	, _fontRenderingManager(nullptr)
	{
		_texturedUSI = std::make_shared<RenderCore::UniformsStreamInterface>();
		_texturedUSI->BindResourceView(0, Hash64("InputTexture"));
		_requiredBufferUploadsCommandList = 0;
	}

	ImmediateOverlayContext::ImmediateOverlayContext(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		FontRenderingManager* fontRenderingManager)
	: ImmediateOverlayContext(threadContext, immediateDrawables)
	{
		_fontRenderingManager = fontRenderingManager;
		if (_fontRenderingManager)
			_defaultFont = ConsoleRig::FindCachedBox<DefaultFontBox>()._font;
	}

	ImmediateOverlayContext::~ImmediateOverlayContext()
	{
	}

	std::unique_ptr<ImmediateOverlayContext>
		MakeImmediateOverlayContext(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			FontRenderingManager* fontRenderingManager)
	{
		return std::make_unique<ImmediateOverlayContext>(threadContext, immediateDrawables, fontRenderingManager);
	}

	std::unique_ptr<ImmediateOverlayContext>
		MakeImmediateOverlayContext(
            RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::ImmediateDrawingApparatus& apparatus)
	{
		return MakeImmediateOverlayContext(threadContext, *apparatus._immediateDrawables, apparatus._fontRenderingManager.get());
	}

	IOverlayContext::~IOverlayContext() {}
}

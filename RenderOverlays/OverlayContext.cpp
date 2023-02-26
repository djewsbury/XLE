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
#include "../RenderOverlays/OverlayApparatus.h"
#include "../Assets/Assets.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/StringFormat.h"
#include "../Utility/StringUtils.h"

using namespace Utility::Literals;

namespace RenderOverlays
{
	using namespace RenderCore;

	class Vertex_PC     { public: Float3 _position; unsigned _colour;                                                               Vertex_PC(Float3 position, unsigned colour) : _position(position), _colour(colour) {};                                          static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };
	class Vertex_PCR    { public: Float3 _position; unsigned _colour; float _radius;                                                Vertex_PCR(Float3 position, unsigned colour, float radius) : _position(position), _colour(colour), _radius(radius) {};          static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };
	class Vertex_PCT    { public: Float3 _position; unsigned _colour; Float2 _texCoord;                                             Vertex_PCT(Float3 position, unsigned colour, Float2 texCoord) : _position(position), _colour(colour), _texCoord(texCoord) {};   static MiniInputElementDesc inputElements2D[]; static MiniInputElementDesc inputElements3D[]; };

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
	
	IteratorRange<void*> ImmediateOverlayContext::DrawGeometry(
		unsigned vertexCount,
		IteratorRange<const RenderCore::MiniInputElementDesc*> inputLayout,
		RenderCore::Techniques::ImmediateDrawableMaterial&& material)
	{
		return _immediateDrawables->QueueDraw(vertexCount, inputLayout, std::move(material), Topology::TriangleList);
	}

	void ImmediateOverlayContext::DrawTexturedQuad(
		ProjectionMode proj, 
		const Float3& mins, const Float3& maxs, 
		std::shared_ptr<RenderCore::IResourceView> textureResource,
		ColorB color, const Float2& minTex0, const Float2& maxTex0)
	{
		using Vertex = Vertex_PCT;
		auto inputElements = (proj == ProjectionMode::P2D) ? MakeIteratorRange(Vertex::inputElements2D) : MakeIteratorRange(Vertex::inputElements3D);
		auto data = BeginDrawCall(DrawCall{6, Topology::TriangleList, inputElements, std::move(textureResource)}).Cast<Vertex*>();
		auto col = HardwareColor(color);
		data[0] = Vertex(Float3(mins[0], mins[1], mins[2]), col, Float2(minTex0[0], minTex0[1]));
		data[1] = Vertex(Float3(mins[0], maxs[1], mins[2]), col, Float2(minTex0[0], maxTex0[1]));
		data[2] = Vertex(Float3(maxs[0], mins[1], mins[2]), col, Float2(maxTex0[0], minTex0[1]));
		data[3] = Vertex(Float3(maxs[0], mins[1], mins[2]), col, Float2(maxTex0[0], minTex0[1]));
		data[4] = Vertex(Float3(mins[0], maxs[1], mins[2]), col, Float2(minTex0[0], maxTex0[1]));
		data[5] = Vertex(Float3(maxs[0], maxs[1], mins[2]), col, Float2(maxTex0[0], maxTex0[1]));
	}

	Float2 ImmediateOverlayContext::DrawText	 (  const std::tuple<Float3, Float3>& quad, 
													const Font& font, DrawTextFlags::BitField flags, 
													ColorB col, 
													TextAlignment alignment, StringSection<char> text)
	{
		if (!_fontRenderingManager) return Float2{0, 0};

		Quad q;
		q.min = Float2(std::get<0>(quad)[0], std::get<0>(quad)[1]);
		q.max = Float2(std::get<1>(quad)[0], std::get<1>(quad)[1]);
		Float2 alignedPosition = AlignText(font, q, alignment, text);
		return Draw(
			*_threadContext,
			*_immediateDrawables, 
			*_fontRenderingManager,
			font, flags,
			alignedPosition[0], alignedPosition[1],
			0.f, 0.f, // q.max[0], q.max[1],
			text,
			1.f, LinearInterpolate(std::get<0>(quad)[2], std::get<1>(quad)[2], 0.5f),
			col);
	}

	Float2  ImmediateOverlayContext::DrawTextWithTable(
			const std::tuple<Float3, Float3>& quad,
			FontPtrAndFlags fontTable[256],
			TextAlignment alignment,
			StringSection<> text,
			IteratorRange<const uint32_t*> colors,
			IteratorRange<const uint8_t*> fontSelectors,
			ColorB shadowColor)
	{
		if (!_fontRenderingManager) return Float2{0, 0};

		Quad q;
		q.min = Float2(std::get<0>(quad)[0], std::get<0>(quad)[1]);
		q.max = Float2(std::get<1>(quad)[0], std::get<1>(quad)[1]);
		Float2 alignedPosition = q.min;
		if (fontTable[0].first)
			alignedPosition = AlignText(*fontTable[0].first, q, alignment, text);
		return DrawWithTable(
			*_threadContext,
			*_immediateDrawables, 
			*_fontRenderingManager,
			fontTable,
			alignedPosition[0], alignedPosition[1],
			0.f, 0.f, // q.max[0], q.max[1],
			text, colors, fontSelectors,
			1.f, LinearInterpolate(std::get<0>(quad)[2], std::get<1>(quad)[2], 0.5f),
			shadowColor);
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
			mat._uniformStreamInterface = _texturedUSI.get();
			mat._uniforms._resourceViews.push_back(drawCall._textureResource);
		}

		return _immediateDrawables->QueueDraw(
			drawCall._vertexCount,
			drawCall._inputAssembly,
			std::move(mat),
			drawCall._topology);
	}

	void* ImmediateOverlayContext::GetService(uint64_t id)
	{
		auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id)
			return i->second;
		return nullptr;
	}

	void ImmediateOverlayContext::AttachService(uint64_t id, void* ptr)
	{
		auto i = LowerBound(_services, id);
		if (i != _services.end() && i->first == id) {
			i->second = ptr;
		} else {
			_services.emplace_back(id, ptr);
		}
	}

	ImmediateOverlayContext::ImmediateOverlayContext(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables)
	: _immediateDrawables(&immediateDrawables)
	, _threadContext(&threadContext)
	, _fontRenderingManager(nullptr)
	{
		_texturedUSI = std::make_shared<RenderCore::UniformsStreamInterface>();
		_texturedUSI->BindResourceView(0, "InputTexture"_h);
		_requiredBufferUploadsCommandList = 0;
	}

	ImmediateOverlayContext::ImmediateOverlayContext(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		FontRenderingManager* fontRenderingManager)
	: ImmediateOverlayContext(threadContext, immediateDrawables)
	{
		_fontRenderingManager = fontRenderingManager;
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
			RenderOverlays::OverlayApparatus& apparatus)
	{
		return MakeImmediateOverlayContext(threadContext, *apparatus._immediateDrawables, apparatus._fontRenderingManager.get());
	}

	IOverlayContext::~IOverlayContext() {}
}

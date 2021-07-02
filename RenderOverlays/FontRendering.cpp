// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FontRendering.h"
#include "FontRectanglePacking.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Resource.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/Format.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetFutureContinuation.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Math/Vector.h"
#include <assert.h>
#include <algorithm>

namespace RenderOverlays
{
	class FontTexture2D
	{
	public:
		void UpdateToTexture(RenderCore::IThreadContext& threadContext, IteratorRange<const uint8_t*> data, const RenderCore::Box2D& destBox);
		const std::shared_ptr<RenderCore::IResource>& GetUnderlying() const { return _resource; }
		const std::shared_ptr<RenderCore::IResourceView>& GetSRV() const { return _srv; }

		FontTexture2D(
			RenderCore::IDevice& dev,
			unsigned width, unsigned height, RenderCore::Format pixelFormat);
		~FontTexture2D();

		FontTexture2D(FontTexture2D&&) = default;
		FontTexture2D& operator=(FontTexture2D&&) = default;

	private:
		std::shared_ptr<RenderCore::IResource>			_resource;
		std::shared_ptr<RenderCore::IResourceView>		_srv;
		RenderCore::Format _format;
	};

	class WorkingVertexSetPCT
	{
	public:
		struct Vertex
		{
			Float3      p;
			unsigned    c;
			Float2      t;

			Vertex() {}
			Vertex(Float3 ip, unsigned ic, Float2 it) : p(ip), c(ic), t(it) {}
		};
		static const int VertexSize = sizeof(Vertex);

		void PushQuad(const Quad& positions, unsigned color, const Quad& textureCoords, float depth, bool snap=true);
		void Complete();

		WorkingVertexSetPCT(
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			std::shared_ptr<RenderCore::IResourceView> textureView,
			unsigned reservedQuads);

	private:
		RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;
		RenderCore::Techniques::ImmediateDrawableMaterial _material;
		IteratorRange<Vertex*> 	_currentAllocation;
		Vertex*            		_currentIterator;
	};

	static RenderCore::MiniInputElementDesc s_inputElements[] = 
	{
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
	};

	void WorkingVertexSetPCT::PushQuad(const Quad& positions, unsigned color, const Quad& textureCoords, float depth, bool snap)
	{
		if (__builtin_expect((_currentIterator + 6) > _currentAllocation.end(), false)) {
			auto reserveVertexCount = _currentAllocation.size() + 6 + (_currentAllocation.size() + 6)/2;
			auto iteratorPosition = _currentIterator - _currentAllocation.begin();
			_currentAllocation = _immediateDrawables->UpdateLastDrawCallVertexCount(reserveVertexCount).Cast<Vertex*>();
			_currentIterator = _currentAllocation.begin() + iteratorPosition;
			assert((_currentIterator + 6) <= _currentAllocation.end());
		}

		float x0 = positions.min[0];
		float x1 = positions.max[0];
		float y0 = positions.min[1];
		float y1 = positions.max[1];

		Float3 p0(x0, y0, depth);
		Float3 p1(x1, y0, depth);
		Float3 p2(x0, y1, depth);
		Float3 p3(x1, y1, depth);

		if (snap) {
			p0[0] = (float)(int)(0.5f + p0[0]);
			p1[0] = (float)(int)(0.5f + p1[0]);
			p2[0] = (float)(int)(0.5f + p2[0]);
			p3[0] = (float)(int)(0.5f + p3[0]);

			p0[1] = (float)(int)(0.5f + p0[1]);
			p1[1] = (float)(int)(0.5f + p1[1]);
			p2[1] = (float)(int)(0.5f + p2[1]);
			p3[1] = (float)(int)(0.5f + p3[1]);
		}

			//
			//      Following behaviour from DrawQuad in Archeage display_list.cpp
			//
		*_currentIterator++ = Vertex(p0, color, Float2( textureCoords.min[0], textureCoords.min[1] ));
		*_currentIterator++ = Vertex(p2, color, Float2( textureCoords.min[0], textureCoords.max[1] ));
		*_currentIterator++ = Vertex(p1, color, Float2( textureCoords.max[0], textureCoords.min[1] ));
		*_currentIterator++ = Vertex(p1, color, Float2( textureCoords.max[0], textureCoords.min[1] ));
		*_currentIterator++ = Vertex(p2, color, Float2( textureCoords.min[0], textureCoords.max[1] ));
		*_currentIterator++ = Vertex(p3, color, Float2( textureCoords.max[0], textureCoords.max[1] ));
	}

	void WorkingVertexSetPCT::Complete()
	{
		// Update the vertex count to be where we ended up
		_immediateDrawables->UpdateLastDrawCallVertexCount(_currentIterator - _currentAllocation.begin());
	}

	WorkingVertexSetPCT::WorkingVertexSetPCT(
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		std::shared_ptr<RenderCore::IResourceView> textureView,
		unsigned reservedQuads)
	: _immediateDrawables(&immediateDrawables)
	{
		static std::shared_ptr<RenderCore::UniformsStreamInterface> usi;
		if (!usi) {
			usi = std::make_shared<RenderCore::UniformsStreamInterface>();
			usi->BindResourceView(0, Hash64("InputTexture"));
		}
		assert(reservedQuads != 0);
		_material._uniformStreamInterface = usi;
		_material._uniforms._resourceViews.push_back(std::move(textureView));
		_material._stateSet = RenderCore::Assets::RenderStateSet{};
		_material._shaderSelectors.SetParameter("FONT_RENDERER", 1);
		_currentAllocation = _immediateDrawables->QueueDraw(
			reservedQuads * 6,
			MakeIteratorRange(s_inputElements), 
			_material).Cast<Vertex*>();
		_currentIterator = _currentAllocation.begin();
	}

	static unsigned ToDigitValue(ucs4 chr, unsigned base)
	{
		if (chr >= '0' && chr <= '9')                   { return       chr - '0'; }
		else if (chr >= 'a' && chr < ('a'+base-10))     { return 0xa + chr - 'a'; }
		else if (chr >= 'A' && chr < ('a'+base-10))     { return 0xa + chr - 'A'; }
		return 0xff;
	}

	static unsigned ParseColorValue(const ucs4 text[], unsigned* colorOverride)
	{
		assert(text && colorOverride);

		unsigned digitCharacters = 0;
		while (     (text[digitCharacters] >= '0' && text[digitCharacters] <= '9')
				||  (text[digitCharacters] >= 'A' && text[digitCharacters] <= 'F')
				||  (text[digitCharacters] >= 'a' && text[digitCharacters] <= 'f')) {
			++digitCharacters;
		}

		if (digitCharacters == 6 || digitCharacters == 8) {
			unsigned result = (digitCharacters == 6)?0xff000000:0x0;
			for (unsigned c=0; c<digitCharacters; ++c) {
				result |= ToDigitValue(text[c], 16) << ((digitCharacters-c-1)*4);
			}
			*colorOverride = result;
			return digitCharacters;
		}
		return 0;
	}
	float Draw(
		RenderCore::IThreadContext& threadContext,
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		FontRenderingManager& textureMan,
		const Font& font, const TextStyle& style,
		float x, float y, StringSection<ucs4> text,
		float spaceExtra, float scale, float mx, float depth,
		unsigned colorARGB, bool applyDescender, Quad* q)
	{
		using namespace RenderCore;
		if (text.IsEmpty()) return 0.f;

		int prevGlyph = 0;
		float xScale = scale;
		float yScale = scale;

		if (style._options.snap) {
			x = xScale * (int)(0.5f + x / xScale);
			y = yScale * (int)(0.5f + y / yScale);
		}

		auto* res = textureMan.GetFontTexture().GetUnderlying().get();
		Metal::CompleteInitialization(
			*Metal::DeviceContext::Get(threadContext),
			{&res, &res+1});
			
		auto texDims = textureMan.GetTextureDimensions();
		auto estimatedQuadCount = text.size();
		if (style._options.shadow)
			estimatedQuadCount += text.size();
		if (style._options.outline)
			estimatedQuadCount += 8 * text.size();
		WorkingVertexSetPCT workingVertices(immediateDrawables, textureMan.GetFontTexture().GetSRV(), estimatedQuadCount);

		float descent = 0.0f;
		if (applyDescender) {
			descent = font.GetFontProperties()._descender;
		}
		float opacity = (colorARGB >> 24) / float(0xff);
		unsigned colorOverride = 0x0;
		unsigned prev_rsb_delta = 0;

		for (auto i=text.begin(); i!=text.end(); ++i) {
			auto ch = *i;
			if (__builtin_expect(mx > 0.0f && x > mx, false)) {
				break;
			}

			if (__builtin_expect(!XlComparePrefixI((ucs4*)"{\0\0\0C\0\0\0o\0\0\0l\0\0\0o\0\0\0r\0\0\0:\0\0\0", i, 7), false)) {
				unsigned newColorOverride = 0;
				unsigned parseLength = ParseColorValue(i+7, &newColorOverride);
				if (parseLength) {
					colorOverride = newColorOverride;
					i += 7 + parseLength;
					while (i<text.end() && *i != '}') ++i;
					continue;
				}
			}

			int curGlyph;
			Float2 v = font.GetKerning(prevGlyph, ch, &curGlyph);
			x += xScale * v[0];
			y += yScale * v[1];
			prevGlyph = curGlyph;

			auto bitmap = textureMan.GetBitmap(threadContext, font, ch);

			/*
			The freetype library suggests 2 different ways to use the lsb & rsb delta values. This method is
			sounds like it is intended when for maintaining pixel alignment is needed
			if (prev_rsb_delta - bitmap._lsbDelta > 32)
				x -= 1.0f;
			else if (prev_rsb_delta - bitmap._lsbDelta < -31)
				x += 1.0f;
			prev_rsb_delta = bitmap._rsbDelta;
			*/

			float baseX = x + bitmap._bitmapOffsetX * xScale;
			float baseY = y + (bitmap._bitmapOffsetY - descent) * yScale;
			if (style._options.snap) {
				baseX = xScale * (int)(0.5f + baseX / xScale);
				baseY = yScale * (int)(0.5f + baseY / yScale);
			}

			Quad pos = Quad::MinMax(
				baseX, baseY, 
				baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);
			Quad tc = Quad::MinMax(
				bitmap._tcTopLeft[0], bitmap._tcTopLeft[1], 
				bitmap._tcBottomRight[0], bitmap._tcBottomRight[1]);

			if (style._options.outline) {
				Quad shadowPos;
				unsigned shadowColor = ColorB::FromNormalized(0, 0, 0, opacity).AsUInt32();

				shadowPos = pos;
				shadowPos.min[0] -= xScale;
				shadowPos.max[0] -= xScale;
				shadowPos.min[1] -= yScale;
				shadowPos.max[1] -= yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[1] -= yScale;
				shadowPos.max[1] -= yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[0] += xScale;
				shadowPos.max[0] += xScale;
				shadowPos.min[1] -= yScale;
				shadowPos.max[1] -= yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[0] -= xScale;
				shadowPos.max[0] -= xScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[0] += xScale;
				shadowPos.max[0] += xScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[0] -= xScale;
				shadowPos.max[0] -= xScale;
				shadowPos.min[1] += yScale;
				shadowPos.max[1] += yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[1] += yScale;
				shadowPos.max[1] += yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);

				shadowPos = pos;
				shadowPos.min[0] += xScale;
				shadowPos.max[0] += xScale;
				shadowPos.min[1] += yScale;
				shadowPos.max[1] += yScale;
				workingVertices.PushQuad(shadowPos, shadowColor, tc, depth);
			}

			if (style._options.shadow) {
				Quad shadowPos = pos;
				shadowPos.min[0] += xScale;
				shadowPos.max[0] += xScale;
				shadowPos.min[1] += yScale;
				shadowPos.max[1] += yScale;
				workingVertices.PushQuad(shadowPos, ColorB::FromNormalized(0,0,0,opacity).AsUInt32(), tc, depth);
			}

			workingVertices.PushQuad(pos, RenderCore::ARGBtoABGR(colorOverride?colorOverride:colorARGB), tc, depth);

			x += bitmap._xAdvance * xScale;
			x += float(bitmap._lsbDelta - bitmap._rsbDelta) / 64.f;
			if (style._options.outline) {
				x += 2 * xScale;
			}
			if (ch == ' ') {
				x += spaceExtra;
			}

			if (q) {
				if (i == 0) {
					*q = pos;
				} else {
					if (q->min[0] > pos.min[0]) {
						q->min[0] = pos.min[0];
					}
					if (q->min[1] > pos.min[1]) {
						q->min[1] = pos.min[1];
					}

					if (q->max[0] < pos.max[0]) {
						q->max[0] = pos.max[0];
					}
					if (q->max[1] < pos.max[1]) {
						q->max[1] = pos.max[1];
					}
				}
			}
		}

		workingVertices.Complete();
		return x;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////

	class FontRenderingManager::Pimpl
	{
	public:
		RectanglePacker_FontCharArray	_rectanglePacker;
		std::unique_ptr<FontTexture2D>  _texture;

		unsigned _texWidth, _texHeight;

		Pimpl(RenderCore::IDevice& device, unsigned texWidth, unsigned texHeight)
		: _rectanglePacker({texWidth, texHeight})
		, _texWidth(texWidth), _texHeight(texHeight) 
		{
			_texture = std::make_unique<FontTexture2D>(device, texWidth, texHeight, RenderCore::Format::R8_UNORM);
		}
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////

	FontRenderingManager::FontRenderingManager(RenderCore::IDevice& device) { _pimpl = std::make_unique<Pimpl>(device, 512, 2048); }
	FontRenderingManager::~FontRenderingManager() {}

	FontTexture2D::FontTexture2D(
		RenderCore::IDevice& dev,
		unsigned width, unsigned height, RenderCore::Format pixelFormat)
	: _format(pixelFormat)
	{
		using namespace RenderCore;
		ResourceDesc desc;
		desc._type = ResourceDesc::Type::Texture;
		desc._bindFlags = BindFlag::ShaderResource | BindFlag::TransferDst;
		desc._cpuAccess = CPUAccess::Write;
		desc._gpuAccess = GPUAccess::Read;
		desc._allocationRules = 0;
		desc._textureDesc = TextureDesc::Plain2D(width, height, pixelFormat, 1);
		XlCopyString(desc._name, "Font");
		_resource = dev.CreateResource(desc);
		_srv = _resource->CreateTextureView();
	}

	FontTexture2D::~FontTexture2D()
	{
	}

	void FontTexture2D::UpdateToTexture(
		RenderCore::IThreadContext& threadContext,
		IteratorRange<const uint8_t*> data, const RenderCore::Box2D& destBox)
	{
		using namespace RenderCore;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto blitEncoder = metalContext.BeginBlitEncoder();
		blitEncoder.Write(
			Metal::BlitEncoder::CopyPartial_Dest {
				_resource.get(), {}, VectorPattern<unsigned, 3>{ unsigned(destBox._left), unsigned(destBox._top), 0u }
			},
			SubResourceInitData { data },
			_format,
			VectorPattern<unsigned, 3>{ unsigned(destBox._right - destBox._left), unsigned(destBox._bottom - destBox._top), 1u });
	}

	static std::vector<uint8_t> GlyphAsDataPacket(
		unsigned srcWidth, unsigned srcHeight,
		IteratorRange<const void*> srcData,
		int offX, int offY, int width, int height)
	{
		std::vector<uint8_t> packet(width*height);

		int j = 0;
		for (; j < std::min(height, (int)srcHeight); ++j) {
			int i = 0;
			for (; i < std::min(width, (int)srcWidth); ++i)
				packet[i + j*width] = ((const uint8_t*)srcData.begin())[i + srcWidth * j];
			for (; i < width; ++i)
				packet[i + j*width] = 0;
		}
		for (; j < height; ++j)
			for (int i=0; i < width; ++i)
				packet[i + j*width] = 0;

		return packet;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////

	auto FontRenderingManager::InitializeNewGlyph(
		RenderCore::IThreadContext& threadContext,
		const Font& font,
		ucs4 ch,
		std::vector<std::pair<uint64_t, Bitmap>>::iterator insertPoint,
		uint64_t code) -> Bitmap
	{
		auto newData = font.GetBitmap(ch);
		if ((newData._width * newData._height) == 0) {
			Bitmap result = {};
			result._xAdvance = newData._xAdvance;		// still need xAdvance here for characters that aren't drawn (ie, whitespace)
			_glyphs.insert(insertPoint, std::make_pair(code, result));
			return result;
		}

		auto rect = _pimpl->_rectanglePacker.Allocate({newData._width, newData._height});
		if (rect.second[0] <= rect.first[0] || rect.second[1] <= rect.first[1])
			return {};

		assert((rect.second[0]-rect.first[0]) >= newData._width);
		assert((rect.second[1]-rect.first[1]) >= newData._height);
		assert(rect.second[0] > rect.first[0]);
		assert(rect.second[1] > rect.first[1]);

		if (_pimpl->_texture) {
			auto pkt = GlyphAsDataPacket(newData._width, newData._height, newData._data, rect.first[0], rect.first[1], rect.second[0]-rect.first[0], rect.second[1]-rect.first[1]);
			_pimpl->_texture->UpdateToTexture(
				threadContext,
				pkt, 
				RenderCore::Box2D{
					(int)rect.first[0], (int)rect.first[1], 
					(int)rect.second[0], (int)rect.second[1]});
		}

		Bitmap result;
		result._xAdvance = newData._xAdvance;
		result._bitmapOffsetX = newData._bitmapOffsetX;
		result._bitmapOffsetY = newData._bitmapOffsetY;
		result._width = newData._width;
		result._height = newData._height;
		result._tcTopLeft[0] = rect.first[0] / float(_pimpl->_texWidth);
		result._tcTopLeft[1] = rect.first[1] / float(_pimpl->_texHeight);
		result._tcBottomRight[0] = (rect.first[0] + newData._width) / float(_pimpl->_texWidth);
		result._tcBottomRight[1] = (rect.first[1] + newData._height) / float(_pimpl->_texHeight);
		result._lsbDelta = newData._lsbDelta;
		result._rsbDelta = newData._rsbDelta;

		_glyphs.insert(insertPoint, std::make_pair(code, result));
		return result;
	}

	const FontTexture2D& FontRenderingManager::GetFontTexture()
	{
		return *_pimpl->_texture;
	}

	UInt2 FontRenderingManager::GetTextureDimensions()
	{
		return UInt2{_pimpl->_texWidth, _pimpl->_texHeight};
	}

}

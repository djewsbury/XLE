// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FontRendering.h"
#include "ShapesInternal.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/BufferUploads/IBufferUploads.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/Resource.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/ResourceUtils.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/Format.h"
#include "../Assets/Assets.h"
#include "../Assets/Continuation.h"
#include "../Math/RectanglePacking.h"
#include "../Math/Transformations.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/BitUtils.h"
#include "../Math/Vector.h"
#include <assert.h>
#include <algorithm>

using namespace Utility::Literals;

namespace RenderOverlays
{
	class FontTexture2D
	{
	public:
		void UpdateToTexture(RenderCore::IThreadContext& threadContext, IteratorRange<const void*> data, const RenderCore::Box2D& destBox);
		void UpdateToTexture(RenderCore::IThreadContext& threadContext, IteratorRange<const void*> data, unsigned offset);
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

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class WorkingVertexSetFontResource
	{
	public:
		struct Vertex
		{
			Float3      p;
			unsigned    c;
			uint8_t     u, v;
			uint16_t 	width, height, spacer;
			uint32_t	offset;
		};
		static const int VertexSize = sizeof(Vertex);
		static RenderCore::MiniInputElementDesc s_inputElements[];

		void PushQuad(const Quad& positions, ColorB color, const FontRenderingManager::Bitmap& bitmap);
		void Complete();
		void ReserveQuads(unsigned reservedQuads);

		WorkingVertexSetFontResource(
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			const RenderCore::Techniques::ImmediateDrawableMaterial& material,
			const RenderCore::Techniques::RetainedUniformsStream& uniforms,
			float depth, bool snap, Float2 offset = Float2{0,0});
		WorkingVertexSetFontResource();

	protected:
		RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;
		const RenderCore::Techniques::ImmediateDrawableMaterial* _material;
		const RenderCore::Techniques::RetainedUniformsStream* _uniforms;
		IteratorRange<Vertex*> 	_currentAllocation;
		Vertex*            		_currentIterator;
		float _depth;
		bool _snap;
		Float2 _offset;
	};

	RenderCore::MiniInputElementDesc WorkingVertexSetFontResource::s_inputElements[] = 
	{
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::FONTTABLE, RenderCore::Format::R16G16B16A16_UINT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::FONTTABLE+1, RenderCore::Format::R32_UINT }
	};

	void WorkingVertexSetFontResource::PushQuad(const Quad& positions, ColorB color, const FontRenderingManager::Bitmap& bitmap)
	{
		if (expect_evaluation((_currentIterator + 6) > _currentAllocation.end(), false)) {
			auto reserveVertexCount = _currentAllocation.size() + 6 + (_currentAllocation.size() + 6)/2;
			auto iteratorPosition = _currentIterator - _currentAllocation.begin();
			_currentAllocation = _immediateDrawables->UpdateLastDrawCallVertexCount(reserveVertexCount).Cast<Vertex*>();
			_currentIterator = _currentAllocation.begin() + iteratorPosition;
			assert((_currentIterator + 6) <= _currentAllocation.end());
		}

		float x0 = positions.min[0] + _offset[0];
		float x1 = positions.max[0] + _offset[0];
		float y0 = positions.min[1] + _offset[1];
		float y1 = positions.max[1] + _offset[1];

		Float3 p0(x0, y0, _depth);
		Float3 p1(x1, y0, _depth);
		Float3 p2(x0, y1, _depth);
		Float3 p3(x1, y1, _depth);

		if (_snap) {
			p0[0] = (float)(int)(0.5f + p0[0]);
			p1[0] = (float)(int)(0.5f + p1[0]);
			p2[0] = (float)(int)(0.5f + p2[0]);
			p3[0] = (float)(int)(0.5f + p3[0]);

			p0[1] = (float)(int)(0.5f + p0[1]);
			p1[1] = (float)(int)(0.5f + p1[1]);
			p2[1] = (float)(int)(0.5f + p2[1]);
			p3[1] = (float)(int)(0.5f + p3[1]);
		}

		auto col = HardwareColor(color);
		*_currentIterator++ = Vertex{p0, col, 0x00, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		*_currentIterator++ = Vertex{p2, col, 0x00, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		*_currentIterator++ = Vertex{p1, col, 0xff, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		*_currentIterator++ = Vertex{p1, col, 0xff, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		*_currentIterator++ = Vertex{p2, col, 0x00, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		*_currentIterator++ = Vertex{p3, col, 0xff, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
	}

	void WorkingVertexSetFontResource::Complete()
	{
		// Update the vertex count to be where we ended up
		if (!_currentIterator) return;		// (never started)
		assert(_currentIterator != _currentAllocation.begin());
		_immediateDrawables->UpdateLastDrawCallVertexCount(_currentIterator - _currentAllocation.begin());
	}

	namespace Internal
	{
		static RenderCore::UniformsStreamInterface CreateFontResourceUSI()
		{
			RenderCore::UniformsStreamInterface result;
			result.BindResourceView(0, "FontResource"_h);
			return result;
		}
		static ParameterBox CreateFontRendererSelectorBoxFontResource()
		{
			ParameterBox result;
			result.SetParameter("FONT_RENDERER", 1);
			return result;
		}

		static RenderCore::UniformsStreamInterface s_fontResourceUSI = Internal::CreateFontResourceUSI();
		static ParameterBox s_fontRendererSelectorBoxFontResource = Internal::CreateFontRendererSelectorBoxFontResource();

		static RenderCore::Techniques::ImmediateDrawableMaterial CreateMaterialForFontResource()
		{
			RenderCore::Techniques::ImmediateDrawableMaterial material;
			material._uniformStreamInterface = &s_fontResourceUSI;
			material._stateSet = RenderCore::Assets::RenderStateSet{};
			material._shaderSelectors = &s_fontRendererSelectorBoxFontResource;
			material._hash = HashCombine(s_fontResourceUSI.GetHash(), s_fontRendererSelectorBoxFontResource.GetHash());
			return material;
		}
	}

	void WorkingVertexSetFontResource::ReserveQuads(unsigned reservedQuads)
	{
		assert(!_currentIterator);
		assert(reservedQuads != 0);
		_currentAllocation = _immediateDrawables->QueueDraw(
			reservedQuads * 6,
			MakeIteratorRange(s_inputElements), 
			*_material, RenderCore::Techniques::RetainedUniformsStream{*_uniforms}).Cast<Vertex*>();
		_currentIterator = _currentAllocation.begin();
	}

	WorkingVertexSetFontResource::WorkingVertexSetFontResource(
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		const RenderCore::Techniques::ImmediateDrawableMaterial& material,
		const RenderCore::Techniques::RetainedUniformsStream& uniforms,
		float depth, bool snap, Float2 offset)
	: _immediateDrawables(&immediateDrawables)
	, _material(&material), _uniforms(&uniforms)
	, _currentIterator{nullptr}
	, _depth(depth), _snap(snap), _offset(offset)
	{}

	WorkingVertexSetFontResource::WorkingVertexSetFontResource()
	: _immediateDrawables{nullptr}, _material(nullptr), _currentIterator{nullptr}, _depth(0.f), _snap(false) {}

	static RenderCore::MiniInputElementDesc s_inputElements3D[] = 
	{
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::POSITION, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::FONTTABLE, RenderCore::Format::R16G16B16A16_UINT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::FONTTABLE+1, RenderCore::Format::R32_UINT }
	};

	class WorkingVertexSetFontResource3D : public WorkingVertexSetFontResource
	{
	public:
		void PushQuad(const Quad& positions, ColorB color, const FontRenderingManager::Bitmap& bitmap)
		{
			if (expect_evaluation((_currentIterator + 6) > _currentAllocation.end(), false)) {
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
			auto p0 = TransformPoint(_localToWorld, Float3{ x0, y0, 0.f });
			auto p1 = TransformPoint(_localToWorld, Float3{ x1, y0, 0.f });
			auto p2 = TransformPoint(_localToWorld, Float3{ x0, y1, 0.f });
			auto p3 = TransformPoint(_localToWorld, Float3{ x1, y1, 0.f });

			auto col = HardwareColor(color);
			*_currentIterator++ = Vertex{p0, col, 0x00, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
			*_currentIterator++ = Vertex{p2, col, 0x00, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
			*_currentIterator++ = Vertex{p1, col, 0xff, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
			*_currentIterator++ = Vertex{p1, col, 0xff, 0x00, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
			*_currentIterator++ = Vertex{p2, col, 0x00, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
			*_currentIterator++ = Vertex{p3, col, 0xff, 0xff, (uint16_t)bitmap._width, (uint16_t)bitmap._height, 0, bitmap._encodingOffset};
		}

		void ReserveQuads(unsigned reservedQuads)
		{
			assert(!_currentIterator);
			assert(reservedQuads != 0);
			_currentAllocation = _immediateDrawables->QueueDraw(
				reservedQuads * 6,
				MakeIteratorRange(s_inputElements3D), 
				_modifiedMaterial, RenderCore::Techniques::RetainedUniformsStream{*_uniforms}).Cast<Vertex*>();
			_currentIterator = _currentAllocation.begin();
		}

		WorkingVertexSetFontResource3D(
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			const RenderCore::Techniques::ImmediateDrawableMaterial& baseMaterial,
			const RenderCore::Techniques::RetainedUniformsStream& uniforms,
			const Float3x4& localToWorld, RenderCore::Assets::RenderStateSet stateSet)
		: WorkingVertexSetFontResource(immediateDrawables, baseMaterial, uniforms, 0.f, false)
		, _localToWorld(localToWorld), _modifiedMaterial(baseMaterial)
		{
			_modifiedMaterial._stateSet = stateSet;
			_modifiedMaterial._hash = HashCombine(stateSet.GetHash(), "WorkingVertexSetFontResource3D"_h);
		}
		WorkingVertexSetFontResource3D() {}
	protected:
		Float3x4 _localToWorld;
		RenderCore::Techniques::ImmediateDrawableMaterial _modifiedMaterial;
	};


	class WorkingVertexSetPCT
	{
	public:
		struct Vertex
		{
			Float3      p;
			unsigned    c;
			Float2		t;
		};
		static const int VertexSize = sizeof(Vertex);
		static RenderCore::MiniInputElementDesc s_inputElements[];

		void PushQuad(const Quad& positions, ColorB color, const FontRenderingManager::Bitmap& bitmap);
		void Complete();
		void ReserveQuads(unsigned reservedQuads);

		WorkingVertexSetPCT(
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			const RenderCore::Techniques::ImmediateDrawableMaterial& material,
			const RenderCore::Techniques::RetainedUniformsStream& uniforms,
			float depth, bool snap, Float2 offset = Float2{0,0});
		WorkingVertexSetPCT();

	protected:
		RenderCore::Techniques::IImmediateDrawables* _immediateDrawables;
		const RenderCore::Techniques::ImmediateDrawableMaterial* _material;
		const RenderCore::Techniques::RetainedUniformsStream* _uniforms;
		IteratorRange<Vertex*> 	_currentAllocation;
		Vertex*            		_currentIterator;
		float _depth;
		bool _snap;
		Float2 _offset;
	};

	RenderCore::MiniInputElementDesc WorkingVertexSetPCT::s_inputElements[] = 
	{
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::PIXELPOSITION, RenderCore::Format::R32G32B32_FLOAT },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::COLOR, RenderCore::Format::R8G8B8A8_UNORM },
		RenderCore::MiniInputElementDesc{ RenderCore::Techniques::CommonSemantics::TEXCOORD, RenderCore::Format::R32G32_FLOAT }
	};

	void WorkingVertexSetPCT::PushQuad(const Quad& positions, ColorB color, const FontRenderingManager::Bitmap& bitmap)
	{
		if (expect_evaluation((_currentIterator + 6) > _currentAllocation.end(), false)) {
			auto reserveVertexCount = _currentAllocation.size() + 6 + (_currentAllocation.size() + 6)/2;
			auto iteratorPosition = _currentIterator - _currentAllocation.begin();
			_currentAllocation = _immediateDrawables->UpdateLastDrawCallVertexCount(reserveVertexCount).Cast<Vertex*>();
			_currentIterator = _currentAllocation.begin() + iteratorPosition;
			assert((_currentIterator + 6) <= _currentAllocation.end());
		}

		float x0 = positions.min[0] + _offset[0];
		float x1 = positions.max[0] + _offset[0];
		float y0 = positions.min[1] + _offset[1];
		float y1 = positions.max[1] + _offset[1];

		Float3 p0(x0, y0, _depth);
		Float3 p1(x1, y0, _depth);
		Float3 p2(x0, y1, _depth);
		Float3 p3(x1, y1, _depth);

		if (_snap) {
			p0[0] = (float)(int)(0.5f + p0[0]);
			p1[0] = (float)(int)(0.5f + p1[0]);
			p2[0] = (float)(int)(0.5f + p2[0]);
			p3[0] = (float)(int)(0.5f + p3[0]);

			p0[1] = (float)(int)(0.5f + p0[1]);
			p1[1] = (float)(int)(0.5f + p1[1]);
			p2[1] = (float)(int)(0.5f + p2[1]);
			p3[1] = (float)(int)(0.5f + p3[1]);
		}

		auto col = HardwareColor(color);
		*_currentIterator++ = Vertex{p0, col, {bitmap._tcTopLeft[0], bitmap._tcTopLeft[1]}};
		*_currentIterator++ = Vertex{p2, col, {bitmap._tcTopLeft[0], bitmap._tcBottomRight[1]}};
		*_currentIterator++ = Vertex{p1, col, {bitmap._tcBottomRight[0], bitmap._tcTopLeft[1]}};
		*_currentIterator++ = Vertex{p1, col, {bitmap._tcBottomRight[0], bitmap._tcTopLeft[1]}};
		*_currentIterator++ = Vertex{p2, col, {bitmap._tcTopLeft[0], bitmap._tcBottomRight[1]}};
		*_currentIterator++ = Vertex{p3, col, {bitmap._tcBottomRight[0], bitmap._tcBottomRight[1]}};
	}

	void WorkingVertexSetPCT::Complete()
	{
		// Update the vertex count to be where we ended up
		if (!_currentIterator) return;		// (never started)
		assert(_currentIterator != _currentAllocation.begin());
		_immediateDrawables->UpdateLastDrawCallVertexCount(_currentIterator - _currentAllocation.begin());
	}

	namespace Internal
	{
		static RenderCore::UniformsStreamInterface CreateInputTextureUSI()
		{
			RenderCore::UniformsStreamInterface result;
			result.BindResourceView(0, "InputTexture"_h);
			return result;
		}
		static ParameterBox CreateFontRendererSelectorBox()
		{
			ParameterBox result;
			result.SetParameter("FONT_RENDERER", 1);
			return result;
		}

		static RenderCore::UniformsStreamInterface s_inputTextureUSI = Internal::CreateInputTextureUSI();
		static ParameterBox s_fontRendererSelectorBox = Internal::CreateFontRendererSelectorBox();

		static RenderCore::Techniques::ImmediateDrawableMaterial CreateMaterialForPCT()
		{
			RenderCore::Techniques::ImmediateDrawableMaterial material;
			material._uniformStreamInterface = &s_inputTextureUSI;
			material._stateSet = RenderCore::Assets::RenderStateSet{};
			material._shaderSelectors = &s_fontRendererSelectorBox;
			material._hash = HashCombine(s_fontResourceUSI.GetHash(), s_fontRendererSelectorBoxFontResource.GetHash());
			return material;
		}
	}

	void WorkingVertexSetPCT::ReserveQuads(unsigned reservedQuads)
	{
		assert(!_currentIterator);
		assert(reservedQuads != 0);
		_currentAllocation = _immediateDrawables->QueueDraw(
			reservedQuads * 6,
			MakeIteratorRange(s_inputElements), 
			*_material, RenderCore::Techniques::RetainedUniformsStream{*_uniforms}).Cast<Vertex*>();
		_currentIterator = _currentAllocation.begin();
	}

	WorkingVertexSetPCT::WorkingVertexSetPCT(
		RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
		const RenderCore::Techniques::ImmediateDrawableMaterial& material,
		const RenderCore::Techniques::RetainedUniformsStream& uniforms,
		float depth, bool snap, Float2 offset)
	: _immediateDrawables(&immediateDrawables)
	, _material(&material), _uniforms(&uniforms)
	, _currentIterator{nullptr}
	, _depth(depth), _snap(snap), _offset(offset)
	{}

	WorkingVertexSetPCT::WorkingVertexSetPCT()
	: _immediateDrawables{nullptr}, _material(nullptr), _currentIterator{nullptr}, _depth(0.f), _snap(false) {}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename CharType>
		static unsigned ToDigitValue(CharType chr, unsigned base)
	{
		if (chr >= '0' && chr <= '9')                   { return       chr - '0'; }
		else if (chr >= 'a' && chr < ('a'+base-10))     { return 0xa + chr - 'a'; }
		else if (chr >= 'A' && chr < ('a'+base-10))     { return 0xa + chr - 'A'; }
		return 0xff;
	}

	template<typename CharType>
		static unsigned ParseColorValue(const CharType text[], unsigned* colorOverride)
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

	static ucs4 GetNext(StringSection<ucs4>& text)
	{
		assert(!text.IsEmpty());
		++text._start;
		return *(text._start-1);
	}

	static ucs4 GetNext(StringSection<char>& text)
	{
		assert(!text.IsEmpty());
		return utf8_nextchar(text._start, text._end);
	}

	template<typename CharType> struct DrawingTags { static const StringSection<CharType> s_changeColor; };
	template<> const StringSection<ucs4> DrawingTags<ucs4>::s_changeColor = (const ucs4*)"C\0\0\0o\0\0\0l\0\0\0o\0\0\0r\0\0\0:\0\0\0";
	template<> const StringSection<ucs2> DrawingTags<ucs2>::s_changeColor = (const ucs2*)"C\0o\0l\0o\0r\0:\0";
	template<> const StringSection<utf8> DrawingTags<utf8>::s_changeColor = "Color:";

	template<typename CharType>
		StringSection<CharType> FontRenderingControlStatement::TryParse(StringSection<CharType> input)
	{
		assert(!input.IsEmpty());
		if (*input.begin() != '{') return input;

		auto text = input;
		++text._start;
		_type = Type::None;
		if (text.size() > 6 && XlEqStringI({text.begin(), text.begin()+6}, DrawingTags<CharType>::s_changeColor)) {
			if (text._start+6 != text.end() && *(text._start+6) == '}') {
				_newColorOverride = 0;
				text._start += 7;
				_type = Type::ColorOverride;
				return text;
			} else {
				unsigned parseLength = ParseColorValue(text._start+6, &_newColorOverride);
				if (parseLength) {
					text._start += 6 + parseLength;
					while (text._start!=text.end() && *text._start != '}') ++text._start;
					if (text._start!=text.end()) ++text._start;
					_type = Type::ColorOverride;
					return text;
				}
			}
		}
		return input;	// no match
	}

	template<bool SnapCoords, bool CheckMaxXY, typename Instance>
		static unsigned FindFirstRenderInstance(Instance** sortedInstances, unsigned instanceCount, const FontRenderingManager::Bitmap** bitmaps, float xScale, float yScale, float maxX)
	{
		unsigned firstRenderInstance = 0;
		for (; firstRenderInstance<instanceCount; ++firstRenderInstance) {
			auto* inst = sortedInstances[firstRenderInstance];
			auto& bitmap = *bitmaps[inst->_glyphIdx];
			if (!bitmap._width || !bitmap._height) continue;

			float baseX = inst->_xy[0] + bitmap._bitmapOffsetX * xScale;
			float baseY = inst->_xy[1] + bitmap._bitmapOffsetY * yScale;
			if constexpr (SnapCoords) {
				baseX = xScale * (int)(0.5f + baseX / xScale);
				baseY = yScale * (int)(0.5f + baseY / yScale);
			}

			Quad pos = Quad::MinMax(
				baseX, baseY, 
				baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);

			if (expect_evaluation(!CheckMaxXY || (pos.max[0] <= maxX), true))
				break;		// this one will render
		}
		return firstRenderInstance;
	}

	template<typename CharType, typename WorkingSetType, bool CheckMaxXY, bool SnapCoords>
		static bool DrawTemplate_Section(
			RenderCore::IThreadContext& threadContext,
			WorkingSetType& workingVertices,
			FontRenderingManager& textureMan,
			const Font& font, DrawTextFlags::BitField flags,
			Float2& iterator, float xAtLineStart, float maxX, float maxY,
			StringSection<CharType>& text,
			float scale,
			ColorB color, ColorB& colorOverride)
	{
		using namespace RenderCore;
		assert(!text.IsEmpty());

		struct Instance
		{
			ucs4 _chr;
			Float2 _xy;
			ColorB _color;
			unsigned _lineIdx = 0;
			unsigned _glyphIdx = ~0u;
		};
		// Split very long strings into smaller ones. Since we're using stack based allocations frequently
		// we might blow out the stack otherwise
		const size_t maxInstancePerCall = 1024;
		VLA_UNSAFE_FORCE(Instance, instances, std::min(maxInstancePerCall, text.size()));
		unsigned instanceCount = 0;

		float x = iterator[0], y = iterator[1];
		float xScale = scale, yScale = scale;
		if (!CheckMaxXY || (y + yScale * font.GetFontProperties()._lineHeight) <= maxY) {
			int prevGlyph = 0;
			float yAtLineStart = y;
			unsigned lineIdx = 0;

			if constexpr (SnapCoords) {
				x = xScale * (int)(0.5f + x / xScale);
				y = yScale * (int)(0.5f + y / yScale);
			}
			while (instanceCount < maxInstancePerCall) {
				if (!text.IsEmpty() && expect_evaluation(*text._start == '{', false)) {
					FontRenderingControlStatement ctrl;
					text = ctrl.TryParse(text);
					if (ctrl._type == FontRenderingControlStatement::Type::ColorOverride) {
						colorOverride = ctrl._newColorOverride;
						continue;
					}
				}
				if (text.IsEmpty()) break;

				auto ch = GetNext(text);

				// \n, \r\n, \r all considered new lines
				if (ch == '\n' || ch == '\r') {
					if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
					x = xAtLineStart;
					prevGlyph = 0;
					y = yAtLineStart = yAtLineStart + yScale * font.GetFontProperties()._lineHeight;
					if constexpr (SnapCoords) {
						x = xScale * (int)(0.5f + x / xScale);
						y = yScale * (int)(0.5f + y / yScale);
					}
					++lineIdx;

					if (CheckMaxXY && (y + yScale * font.GetFontProperties()._lineHeight) > maxY) {
						text._start = text._end;		// end iteration
						break;
					}

					continue;
				}

				int curGlyph;
				Float2 v = font.GetKerning(prevGlyph, ch, &curGlyph);
				x += xScale * v[0];
				y += yScale * v[1];
				prevGlyph = curGlyph;

				instances[instanceCount++] = { ch, Float2{x, y}, colorOverride.a?colorOverride:color, lineIdx };
			}
		} else {
			text._start = text._end;		// end iteration
		}

		if (!instanceCount) {
			iterator = {x, y};
			return true;
		}

		VLA(Instance*, sortedInstances, instanceCount);
		for (unsigned c=0; c<instanceCount; ++c) sortedInstances[c] = &instances[c];
		std::sort(sortedInstances, &sortedInstances[instanceCount], [](auto* lhs, auto* rhs) { return lhs->_chr < rhs->_chr; });

		VLA(ucs4, chrsToLookup, instanceCount);
		unsigned chrsToLookupCount = 0;
		ucs4 lastChar = ~ucs4(0);
		for (auto* i=sortedInstances; i!=&sortedInstances[instanceCount]; ++i) {
			if ((*i)->_chr != lastChar)
				chrsToLookup[chrsToLookupCount++] = lastChar = (*i)->_chr;		// get unique chars
			(*i)->_glyphIdx = chrsToLookupCount-1;
		}

		assert(chrsToLookupCount);
		VLA(const FontRenderingManager::Bitmap*, bitmaps, chrsToLookupCount);
		bool queryResult = textureMan.GetBitmaps(bitmaps, threadContext, font, MakeIteratorRange(chrsToLookup, &chrsToLookup[chrsToLookupCount]));
		if (!queryResult)
			return false;

		// update the x values for each instance, now we know the set of bitmaps
		float xIterator = 0;
		{
			// unsigned prev_rsb_delta = 0;
			unsigned lineIdx = 0;
			for (auto& inst:MakeIteratorRange(instances, &instances[instanceCount])) {
				auto& bitmap = *bitmaps[inst._glyphIdx];

				/*
				The freetype library suggests 2 different ways to use the lsb & rsb delta values. This method is
				sounds like it is intended when for maintaining pixel alignment is needed
				if (prev_rsb_delta - bitmap._lsbDelta > 32)
					x -= 1.0f;
				else if (prev_rsb_delta - bitmap._lsbDelta < -31)
					x += 1.0f;
				prev_rsb_delta = bitmap._rsbDelta;
				*/

				if (inst._lineIdx != lineIdx) {
					lineIdx = inst._lineIdx;
					xIterator = 0;		// reset because we just had a line break
				}

				inst._xy[0] += xIterator;

				xIterator += bitmap._xAdvance * xScale;
				#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
					xIterator += float(bitmap._lsbDelta - bitmap._rsbDelta) / 64.f;
				#endif
				if (flags & DrawTextFlags::Outline) {
					xIterator += 2 * xScale;
				}
			}
		}

		// Advance until we find the first character that is actually going to render
		// this is important because we don't want to start the WorkingSetType if absolutely nothing renders (eg, all whitespace)
		unsigned firstRenderInstance = FindFirstRenderInstance<SnapCoords, CheckMaxXY>(sortedInstances, instanceCount, bitmaps, xScale, yScale, maxX);
		if (firstRenderInstance == instanceCount) {
			iterator = { x + xIterator, y };
			return true;
		}

		auto estimatedQuadCount = instanceCount - firstRenderInstance;
		if (flags & DrawTextFlags::Shadow) estimatedQuadCount += text.size();
		if (flags & DrawTextFlags::Outline) estimatedQuadCount += 8 * text.size();

		workingVertices.ReserveQuads((unsigned)estimatedQuadCount);
		
		auto shadowColor = ColorB{0, 0, 0, color.a};
		if (flags & DrawTextFlags::Outline) {
			for (auto* inst:MakeIteratorRange(&sortedInstances[firstRenderInstance], &sortedInstances[instanceCount])) {
				auto& bitmap = *bitmaps[inst->_glyphIdx];
				if (!bitmap._width || !bitmap._height) continue;

				float baseX = inst->_xy[0] + bitmap._bitmapOffsetX * xScale;
				float baseY = inst->_xy[1] + bitmap._bitmapOffsetY * yScale;
				if constexpr (SnapCoords) {
					baseX = xScale * (int)(0.5f + baseX / xScale);
					baseY = yScale * (int)(0.5f + baseY / yScale);
				}

				Quad pos = Quad::MinMax(
					baseX, baseY,
					baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);

				if (expect_evaluation(!CheckMaxXY || (pos.max[0] <= maxX), true)) {
					Quad shadowPos;
					shadowPos = pos;
					shadowPos.min[0] -= xScale;
					shadowPos.max[0] -= xScale;
					shadowPos.min[1] -= yScale;
					shadowPos.max[1] -= yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[1] -= yScale;
					shadowPos.max[1] -= yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[0] += xScale;
					shadowPos.max[0] += xScale;
					shadowPos.min[1] -= yScale;
					shadowPos.max[1] -= yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[0] -= xScale;
					shadowPos.max[0] -= xScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[0] += xScale;
					shadowPos.max[0] += xScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[0] -= xScale;
					shadowPos.max[0] -= xScale;
					shadowPos.min[1] += yScale;
					shadowPos.max[1] += yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[1] += yScale;
					shadowPos.max[1] += yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);

					shadowPos = pos;
					shadowPos.min[0] += xScale;
					shadowPos.max[0] += xScale;
					shadowPos.min[1] += yScale;
					shadowPos.max[1] += yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);
				}
			}
		}

		if (flags & DrawTextFlags::Shadow) {
			for (auto* inst:MakeIteratorRange(&sortedInstances[firstRenderInstance], &sortedInstances[instanceCount])) {
				auto& bitmap = *bitmaps[inst->_glyphIdx];
				if (!bitmap._width || !bitmap._height) continue;

				float baseX = inst->_xy[0] + bitmap._bitmapOffsetX * xScale;
				float baseY = inst->_xy[1] + bitmap._bitmapOffsetY * yScale;
				if constexpr (SnapCoords) {
					baseX = xScale * (int)(0.5f + baseX / xScale);
					baseY = yScale * (int)(0.5f + baseY / yScale);
				}

				Quad pos = Quad::MinMax(
					baseX, baseY, 
					baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);

				if (expect_evaluation(!CheckMaxXY || (pos.max[0] <= maxX), true)) {
					Quad shadowPos = pos;
					shadowPos.min[0] += xScale;
					shadowPos.max[0] += xScale;
					shadowPos.min[1] += yScale;
					shadowPos.max[1] += yScale;
					workingVertices.PushQuad(shadowPos, shadowColor, bitmap);
				}
			}
		}

		for (auto* inst:MakeIteratorRange(&sortedInstances[firstRenderInstance], &sortedInstances[instanceCount])) {
			auto& bitmap = *bitmaps[inst->_glyphIdx];
			if (!bitmap._width || !bitmap._height) continue;

			float baseX = inst->_xy[0] + bitmap._bitmapOffsetX * xScale;
			float baseY = inst->_xy[1] + bitmap._bitmapOffsetY * yScale;
			if constexpr (SnapCoords) {
				baseX = xScale * (int)(0.5f + baseX / xScale);
				baseY = yScale * (int)(0.5f + baseY / yScale);
			}

			Quad pos = Quad::MinMax(
				baseX, baseY, 
				baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);

			if (expect_evaluation(!CheckMaxXY || (pos.max[0] <= maxX), true))
				workingVertices.PushQuad(pos, inst->_color, bitmap);
		}

		iterator = { x + xIterator, y };		// y is at the baseline here
		return true;
	}

	template<typename CharType, typename WorkingSetType, bool CheckMaxXY, bool SnapCoords>
		static Float2 DrawTemplate(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			FontRenderingManager& textureMan,
			const Font& font, DrawTextFlags::BitField flags,
			float x, float y, float xAtLineStart, float maxX, float maxY,
			StringSection<CharType> text,
			float scale, float depth,
			ColorB color)
	{
		// Draw the string in sections -- split up so we don't blow out the stack for very long strings
		// color override complicates implementation a little bit
		Float2 iterator { x, y };
		ColorB colorOverride = 0x0;
		while (!text.IsEmpty()) {
			WorkingSetType workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true };
			bool success = DrawTemplate_Section<CharType, WorkingSetType, CheckMaxXY, SnapCoords>(
				threadContext, workingSet,
				textureMan, font, flags,
				iterator, xAtLineStart, maxX, maxY,
				text, scale, color, colorOverride);
			if (!success) return {0,0};
			workingSet.Complete();
		}
		return iterator;
	}

	template<typename CharType, typename WorkingSetType>
		static Float2 DrawTemplate(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			FontRenderingManager& textureMan,
			const Font& font, DrawTextFlags::BitField flags,
			StringSection<CharType> text,
			const Float3x4& localToWorld, RenderCore::Assets::RenderStateSet stateSet,
			ColorB color)
	{
		// Draw the string in sections -- split up so we don't blow out the stack for very long strings
		// color override complicates implementation a little bit
		Float2 iterator { 0.f, 0.f };
		ColorB colorOverride = 0x0;
		while (!text.IsEmpty()) {
			WorkingSetType workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), localToWorld, stateSet };
			bool success = DrawTemplate_Section<CharType, WorkingSetType, false, false>(
				threadContext, workingSet,
				textureMan, font, flags,
				iterator, 0.f, 0.f, 0.f,
				text, 1.f, color, colorOverride);
			if (!success) return {0,0};
			workingSet.Complete();
		}
		return iterator;
	}

	Float2		Draw(   RenderCore::IThreadContext& threadContext,
						RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
						FontRenderingManager& textureMan,
						const Font& font, DrawTextFlags::BitField flags,
						float x, float y, float maxX, float maxY,
						StringSection<> text,
						float scale, float depth,
						ColorB col)
	{
		assert(!(flags & DrawTextFlags::Snap));		// we could support this by using the SnapCoords template parameter to DrawTemplate<>
		if (maxX || maxY) {
			// checking maximum extents
			if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
				return DrawTemplate<utf8, WorkingVertexSetFontResource, true, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			} else {
				return DrawTemplate<utf8, WorkingVertexSetPCT, true, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			}
		} else {
			if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
				return DrawTemplate<utf8, WorkingVertexSetFontResource, false, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			} else {
				return DrawTemplate<utf8, WorkingVertexSetPCT, false, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			}
		}
	}

	Float2		Draw(   RenderCore::IThreadContext& threadContext,
						RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
						FontRenderingManager& textureMan,
						const Font& font, DrawTextFlags::BitField flags,
						float x, float y, float maxX, float maxY,
						StringSection<ucs4> text,
						float scale, float depth,
						ColorB col)
	{
		assert(!(flags & DrawTextFlags::Snap));
		if (maxX || maxY) {
			// checking maximum extents
			if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
				return DrawTemplate<ucs4, WorkingVertexSetFontResource, true, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			} else {
				return DrawTemplate<ucs4, WorkingVertexSetPCT, true, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			}
		} else {
			if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
				return DrawTemplate<ucs4, WorkingVertexSetFontResource, false, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			} else {
				return DrawTemplate<ucs4, WorkingVertexSetPCT, false, false>(threadContext, immediateDrawables, textureMan, font, flags, x, y, x, maxX, maxY, text, scale, depth, col);
			}
		}
	}

	void 		Draw(	RenderCore::IThreadContext& threadContext,
						RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
						FontRenderingManager& textureMan,
						const Font& font, DrawTextFlags::BitField flags,
						StringSection<> text,
						const Float3x4& localToWorld, RenderCore::Assets::RenderStateSet stateSet,
						ColorB col)
	{
		assert(!(flags & DrawTextFlags::Snap));
		if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
			DrawTemplate<utf8, WorkingVertexSetFontResource3D>(threadContext, immediateDrawables, textureMan, font, flags, text, localToWorld, stateSet, col);
		} else {
			assert(0);		// not implemented
		}
	}

	template<typename CharType>
		static void DrawWithTableTemplate(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			FontRenderingManager& textureMan,
			FontPtrAndFlags fontTable[256],
			float x, float y, float maxX, float maxY,
			StringSection<CharType> text,
			IteratorRange<const uint32_t*> colors,
			IteratorRange<const uint8_t*> fontSelectors,
			float scale, float depth,
			ColorB shadowColor)
	{
		using namespace RenderCore;
		if (text.IsEmpty()) return;

		////////
		using ChrAndFont = std::pair<ucs4, const Font*>;
		struct Instance
		{
			ChrAndFont _chr;
			Float2 _xy;
			ColorB _color;
			unsigned _lineIdx = 0;
			unsigned _glyphIdx = ~0u;
		};
		const size_t maxInstancePerCall = 1024;
		VLA_UNSAFE_FORCE(Instance, instances, std::min(maxInstancePerCall, text.size()));
		////////

		float xScale = scale, yScale = scale;
		WorkingVertexSetFontResource workingVertices;
		bool begun = false;
		float xAtLineStart = x, yAtLineStart = y;
		unsigned lineIdx = 0;

		while (!text.IsEmpty()) {

			unsigned instanceCount = 0;

			{
				int prevGlyph = 0;
				Font* prevFont = nullptr;

				auto fontSelectorI = fontSelectors.begin();
				auto colorI = colors.begin();
				auto shadowC = shadowColor.AsUInt32();

				while (instanceCount < maxInstancePerCall && !text.IsEmpty()) {

					auto ch = GetNext(text);
					auto fontSelector = (fontSelectorI < fontSelectors.end()) ? *fontSelectorI : 0;
					++fontSelectorI;
					auto color = (colorI < colors.end()) ? *colorI : 0xffffffff;
					++colorI;

					// \n, \r\n, \r all considered new lines
					if (ch == '\n' || ch == '\r') {
						if (ch == '\r' && text._start!=text.end() && *text._start=='\n') ++text._start;
						x = xAtLineStart;
						if (fontTable[0].first)
							y = yAtLineStart = yAtLineStart + fontTable[0].first->GetFontProperties()._lineHeight;
						++lineIdx;
						continue;
					}

					auto* font = fontTable[fontSelector].first;
					auto flags = fontTable[fontSelector].second;
					assert(font);
					if (!font) continue;

					int curGlyph;
					if (prevFont != font) prevGlyph = 0;		// we could just attempt to use the kerning as if the prev font was the same font?
					Float2 v = font->GetKerning(prevGlyph, ch, &curGlyph);
					x += xScale * v[0];
					y += yScale * v[1];
					prevGlyph = curGlyph; prevFont = font;

					instances[instanceCount++] = { {ch, font}, Float2{x, y}, color, lineIdx };
				}
			}

			if (!instanceCount) continue;

			VLA(Instance*, sortedInstances, instanceCount);
			for (unsigned c=0; c<instanceCount; ++c) sortedInstances[c] = &instances[c];
			std::sort(sortedInstances, &sortedInstances[instanceCount], [](auto* lhs, auto* rhs) { return lhs->_chr < rhs->_chr; });

			VLA(ucs4, chrsToLookup, instanceCount);
			VLA(const Font*, chrsFontsToLookup, instanceCount);
			unsigned chrsToLookupCount = 0;
			ChrAndFont lastChar { ~ucs4(0), nullptr };
			for (auto* i=sortedInstances; i!=&sortedInstances[instanceCount]; ++i) {
				if ((*i)->_chr != lastChar) {
					chrsToLookup[chrsToLookupCount] = lastChar.first = (*i)->_chr.first;
					chrsFontsToLookup[chrsToLookupCount] = lastChar.second = (*i)->_chr.second;
					++chrsToLookupCount;
				}
				(*i)->_glyphIdx = chrsToLookupCount-1;
			}

			assert(chrsToLookupCount);
			VLA(const FontRenderingManager::Bitmap*, bitmaps, chrsToLookupCount);
			{
				bool allBitmapsSucceeded = true;
				unsigned invalidationStart;
				for (unsigned pass=2; pass; --pass) {
					invalidationStart = textureMan._getBitmapsInvalidationIdx;
					unsigned i = 0;
					while (i != chrsToLookupCount) {
						auto starti = i;
						++i;
						while (i != chrsToLookupCount && chrsFontsToLookup[i] == chrsFontsToLookup[starti]) ++i;

						// Note that GetBitmaps() can invalidate the bitmaps we retrieved from a previous call (if new glyphs are initialized)
						// We use the _getBitmapsInvalidationIdx to try to handle this case
						allBitmapsSucceeded &= textureMan.GetBitmaps(bitmaps+starti, threadContext, *chrsFontsToLookup[starti], MakeIteratorRange(chrsToLookup+starti, chrsToLookup+i));
					}
					if (textureMan._getBitmapsInvalidationIdx == invalidationStart) break;
				}
				if (!allBitmapsSucceeded || textureMan._getBitmapsInvalidationIdx != invalidationStart) break;
			}

			// update the x values for each instance, now we know the set of bitmaps
			{
				float xIterator = 0;
				unsigned lineIdx = 0;
				for (auto& inst:MakeIteratorRange(instances, &instances[instanceCount])) {
					assert(bitmaps[inst._glyphIdx]);
					auto& bitmap = *bitmaps[inst._glyphIdx];
					assert(bitmap._xAdvance > 0);

					if (inst._lineIdx != lineIdx) {
						lineIdx = inst._lineIdx;
						xIterator = 0;		// reset because we just had a line break
					}

					inst._xy[0] += xIterator;
					xIterator += bitmap._xAdvance * xScale;
					#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
						xIterator += float(bitmap._lsbDelta - bitmap._rsbDelta) / 64.f;
					#endif
				}
			}

			unsigned firstRenderInstance = FindFirstRenderInstance<false, false>(sortedInstances, instanceCount, bitmaps, xScale, yScale, std::numeric_limits<float>::max());
			if (firstRenderInstance == instanceCount)
				break;

			auto estimatedQuadCount = instanceCount - firstRenderInstance;
			if (!begun)
				workingVertices = WorkingVertexSetFontResource{immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true};
			workingVertices.ReserveQuads((unsigned)estimatedQuadCount);

			for (auto* inst:MakeIteratorRange(&sortedInstances[firstRenderInstance], &sortedInstances[instanceCount])) {
				auto& bitmap = *bitmaps[inst->_glyphIdx];
				if (!bitmap._width || !bitmap._height) continue;

				float baseX = inst->_xy[0] + bitmap._bitmapOffsetX * xScale;
				float baseY = inst->_xy[1] + bitmap._bitmapOffsetY * yScale;

				Quad pos = Quad::MinMax(
					baseX, baseY, 
					baseX + bitmap._width * xScale, baseY + bitmap._height * yScale);
				workingVertices.PushQuad(pos, inst->_color, bitmap);
			}
		}

		if (begun)
			workingVertices.Complete();
	}

	void DrawWithTable(
			RenderCore::IThreadContext& threadContext,
			RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
			FontRenderingManager& textureMan,
			FontPtrAndFlags fontTable[256],
			float x, float y, float maxX, float maxY,
			StringSection<> text,
			IteratorRange<const uint32_t*> colors,
			IteratorRange<const uint8_t*> fontSelectors,
			float scale, float depth,
			ColorB shadowColor)
	{
		DrawWithTableTemplate<utf8>(
			threadContext, immediateDrawables, textureMan, fontTable,
			x, y, maxX, maxY,
			text, colors, fontSelectors, scale, depth, shadowColor);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename WorkingSetType>
		static bool DrawSpanInternal(
			RenderCore::IThreadContext& threadContext,
			WorkingSetType& workingVertices,
			FontRenderingManager& textureMan,
			const Font& font, const FontSpan& span,
			ColorB color)
	{
		if (!span._glyphCount)
			return true;

		VLA(const FontRenderingManager::Bitmap*, bitmaps, span._glyphCount);
		bool queryResult = textureMan.GetBitmaps(bitmaps, threadContext, font, MakeIteratorRange(span._glyphs, &span._glyphs[span._glyphCount]));
		if (!queryResult)
			return false;

		assert(bitmaps[0]->_width * bitmaps[0]->_height);	// empty bitmap glyph shouldn't show up

		auto estimatedQuadCount = span._totalInstanceCount;
		workingVertices.ReserveQuads((unsigned)estimatedQuadCount);

		auto i = span._instances;
		for (unsigned c=0; c<span._glyphCount; ++c) {
			auto& bitmap = *bitmaps[c];
			assert(bitmap._width && bitmap._height);
			// if (!bitmap._width || !bitmap._height) continue;

			auto endi = i+span._glyphsInstanceCounts[c];
			for (;i!=endi; ++i) {
				float baseX = i->_xy[0] + bitmap._bitmapOffsetX;
				float baseY = i->_xy[1] + bitmap._bitmapOffsetY;

				Quad pos = Quad::MinMax(
					baseX, baseY, 
					baseX + bitmap._width, baseY + bitmap._height);

				workingVertices.PushQuad(pos, i->_colorOverride.a?i->_colorOverride:color, bitmap);
			}
		}

		return true;
	}

	bool 		Draw(		RenderCore::IThreadContext& threadContext,
							RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
							FontRenderingManager& textureMan,
							const Font& font, const FontSpan& span,
							ColorB color, Float2 offset)
	{
		const float depth = 0.f;

		if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
			WorkingVertexSetFontResource workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true, offset };
			bool success = DrawSpanInternal(
				threadContext, workingSet, textureMan,
				font, span, color);
			if (!success) return false;
			workingSet.Complete();
		} else {
			WorkingVertexSetPCT workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true, offset };
			bool success = DrawSpanInternal(
				threadContext, workingSet, textureMan,
				font, span, color);
			if (!success) return false;
			workingSet.Complete();
		}
		return true;
	}

	template<typename WorkingSetType>
		static bool DrawSpanOutlineInternal(
			RenderCore::IThreadContext& threadContext,
			WorkingSetType& workingVertices,
			FontRenderingManager& textureMan,
			const Font& font, const FontSpan& span,
			ColorB color, float outlineWidth)
	{
		if (!span._glyphCount)
			return true;

		VLA(const FontRenderingManager::Bitmap*, bitmaps, span._glyphCount);
		bool queryResult = textureMan.GetBitmaps(bitmaps, threadContext, font, MakeIteratorRange(span._glyphs, &span._glyphs[span._glyphCount]));
		if (!queryResult)
			return false;

		assert(bitmaps[0]->_width * bitmaps[0]->_height);	// empty bitmap glyph shouldn't show up

		auto estimatedQuadCount = span._totalInstanceCount;
		workingVertices.ReserveQuads((unsigned)estimatedQuadCount * 8);

		const float xShift = outlineWidth, yShift = outlineWidth;

		auto i = span._instances;
		for (unsigned c=0; c<span._glyphCount; ++c) {
			auto& bitmap = *bitmaps[c];
			assert(bitmap._width && bitmap._height);
			// if (!bitmap._width || !bitmap._height) continue;

			auto endi = i+span._glyphsInstanceCounts[c];
			for (;i!=endi; ++i) {
				float baseX = i->_xy[0] + bitmap._bitmapOffsetX;
				float baseY = i->_xy[1] + bitmap._bitmapOffsetY;

				Quad pos = Quad::MinMax(
					baseX, baseY, 
					baseX + bitmap._width, baseY + bitmap._height);

				Quad shadowPos;
				shadowPos = pos;
				shadowPos.min[0] -= xShift;
				shadowPos.max[0] -= xShift;
				shadowPos.min[1] -= yShift;
				shadowPos.max[1] -= yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[1] -= yShift;
				shadowPos.max[1] -= yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[0] += xShift;
				shadowPos.max[0] += xShift;
				shadowPos.min[1] -= yShift;
				shadowPos.max[1] -= yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[0] -= xShift;
				shadowPos.max[0] -= xShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[0] += xShift;
				shadowPos.max[0] += xShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[0] -= xShift;
				shadowPos.max[0] -= xShift;
				shadowPos.min[1] += yShift;
				shadowPos.max[1] += yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[1] += yShift;
				shadowPos.max[1] += yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);

				shadowPos = pos;
				shadowPos.min[0] += xShift;
				shadowPos.max[0] += xShift;
				shadowPos.min[1] += yShift;
				shadowPos.max[1] += yShift;
				workingVertices.PushQuad(shadowPos, color, bitmap);
			}
		}

		return true;
	}

	bool 		DrawOutline(	RenderCore::IThreadContext& threadContext,
								RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
								FontRenderingManager& textureMan,
								const Font& font, const FontSpan& span,
								ColorB color, Float2 offset, float outlineWidth)
	{
		const float depth = 0.f;

		if (expect_evaluation(textureMan.GetMode() == FontRenderingManager::Mode::LinearBuffer, true)) {
			WorkingVertexSetFontResource workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true, offset };
			bool success = DrawSpanOutlineInternal(
				threadContext, workingSet, textureMan,
				font, span, color, outlineWidth);
			if (!success) return false;
			workingSet.Complete();
		} else {
			WorkingVertexSetPCT workingSet { immediateDrawables, textureMan.GetImmediateDrawableMaterial(), textureMan.GetImmediateDrawableUniforms(), depth, true, offset };
			bool success = DrawSpanOutlineInternal(
				threadContext, workingSet, textureMan,
				font, span, color, outlineWidth);
			if (!success) return false;
			workingSet.Complete();
		}
		return true;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////

	class FontRenderingManager::Pimpl
	{
	public:
		std::unique_ptr<FontTexture2D>  _texture;

		struct Page
		{
			Rect _spaceInTexture;
			RectanglePacker_MaxRects _packer;
			SpanningHeap<unsigned> _spanningHeap;
			int _texelsAllocated = 0;
		};
		std::vector<Page> _activePages;
		Page _reservedPage;
		unsigned _texWidth, _texHeight;
		unsigned _pageWidth, _pageHeight;
		FontRenderingManager::Mode _mode;
		RenderCore::Techniques::ImmediateDrawableMaterial _immediateDrawableMaterial;
		RenderCore::Techniques::RetainedUniformsStream _immediateDrawableUniforms;

		Pimpl(RenderCore::IDevice& device, Mode mode, unsigned pageWidth, unsigned pageHeight, unsigned pageCount)
		: _pageWidth(pageWidth), _pageHeight(pageHeight)
		, _mode(mode)
		{
			if (_mode == Mode::Texture2D) {
				assert(IsPowerOfTwo(pageCount));
				auto pagesAcross = (unsigned)std::sqrt(pageCount);
				auto pagesDown = pageCount / pagesAcross;
				assert((pagesAcross*pagesDown) == pageCount);
				_texture = std::make_unique<FontTexture2D>(device, pageWidth*pagesAcross, pageHeight*pagesDown, RenderCore::Format::R8_UNORM);
				_texWidth = pageWidth*pagesAcross; _texHeight = pageHeight*pagesDown;
				_activePages.reserve(pageCount-1);
				for (unsigned y=0; y<pagesDown; ++y)
					for (unsigned x=0; x<pagesAcross; ++x) {
						Page newPage;
						newPage._spaceInTexture = {Coord2{x*pageWidth, y*pageHeight}, Coord2{(x+1)*pageWidth, (y+1)*pageHeight}};
						newPage._packer = UInt2{pageWidth, pageHeight};
						newPage._texelsAllocated = 0;
						if ((x+y)==0) {
							_reservedPage = std::move(newPage);
						} else
							_activePages.emplace_back(std::move(newPage));
					}

				_immediateDrawableMaterial = Internal::CreateMaterialForPCT();
				_immediateDrawableUniforms._resourceViews.push_back(_texture->GetSRV());

			} else {
				auto linearPageSize = _pageWidth * _pageHeight;
				_pageWidth = linearPageSize;
				_pageHeight = 1u;

				_texture = std::make_unique<FontTexture2D>(device, linearPageSize*pageCount, 1u, RenderCore::Format::R8_UNORM);
				_texWidth = linearPageSize*pageCount; _texHeight = 1u;
				_activePages.reserve(pageCount-1);
				for (unsigned p=0; p<pageCount; ++p) {
					Page newPage;
					newPage._spaceInTexture = {Coord2{p*linearPageSize, 0}, Coord2{(p+1)*linearPageSize, 1u}};
					newPage._spanningHeap = SpanningHeap<unsigned> { linearPageSize };
					newPage._texelsAllocated = 0;
					if (p==(pageCount-1)) {
						_reservedPage = std::move(newPage);
					} else
						_activePages.emplace_back(std::move(newPage));
				}

				_immediateDrawableMaterial = Internal::CreateMaterialForFontResource();
				_immediateDrawableUniforms._resourceViews.push_back(_texture->GetSRV());
			}
		}
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////

	FontRenderingManager::FontRenderingManager(RenderCore::IDevice& device, Mode mode) { _pimpl = std::make_unique<Pimpl>(device, mode, 128, 256, 16); }
	FontRenderingManager::~FontRenderingManager() {}

	FontRenderingManager::Mode FontRenderingManager::GetMode() const { return _pimpl->_mode; }

	FontTexture2D::FontTexture2D(
		RenderCore::IDevice& dev,
		unsigned width, unsigned height, RenderCore::Format pixelFormat)
	: _format(pixelFormat)
	{
		using namespace RenderCore;
		if (height != 1) {
			_resource = dev.CreateResource(CreateDesc(BindFlag::ShaderResource | BindFlag::TransferDst | BindFlag::TransferSrc, TextureDesc::Plain2D(width, height, pixelFormat, 1)), "Font");
			_srv = _resource->CreateTextureView();
		} else {
			assert(BitsPerPixel(pixelFormat) == 8);
			_resource = dev.CreateResource(CreateDesc(BindFlag::ShaderResource | BindFlag::TexelBuffer | BindFlag::TransferDst | BindFlag::TransferSrc, LinearBufferDesc::Create(width*height)), "Font");
			_srv = _resource->CreateTextureView(BindFlag::ShaderResource, TextureViewDesc{TextureViewDesc::FormatFilter{pixelFormat}});
		}
	}

	FontTexture2D::~FontTexture2D() {}

	void FontTexture2D::UpdateToTexture(
		RenderCore::IThreadContext& threadContext,
		IteratorRange<const void*> data, const RenderCore::Box2D& destBox)
	{
		using namespace RenderCore;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto* res = _resource.get();
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), {&res, &res+1});
		auto blitEncoder = metalContext.BeginBlitEncoder();
		TexturePitches pitches { (destBox._right - destBox._left) * BitsPerPixel(_format) / 8, (destBox._right - destBox._left) * (destBox._bottom - destBox._top) * BitsPerPixel(_format) / 8, (destBox._right - destBox._left) * (destBox._bottom - destBox._top) * BitsPerPixel(_format) / 8 };
		blitEncoder.Write(
			CopyPartial_Dest {
				*_resource, {}, VectorPattern<unsigned, 3>{ unsigned(destBox._left), unsigned(destBox._top), 0u }
			},
			SubResourceInitData { data },
			_format,
			VectorPattern<unsigned, 3>{ unsigned(destBox._right - destBox._left), unsigned(destBox._bottom - destBox._top), 1u },
			pitches);
	}

	void FontTexture2D::UpdateToTexture(RenderCore::IThreadContext& threadContext, IteratorRange<const void*> data, unsigned offset)
	{
		using namespace RenderCore;
		auto& metalContext = *Metal::DeviceContext::Get(threadContext);
		auto* res = _resource.get();
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(threadContext), {&res, &res+1});
		auto blitEncoder = metalContext.BeginBlitEncoder();
		blitEncoder.Write(CopyPartial_Dest { *_resource, offset }, data);
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

	static const FontRenderingManager::Bitmap s_emptyBitmap;

	auto FontRenderingManager::InitializeNewGlyph(
		RenderCore::IThreadContext& threadContext,
		const Font& font,
		ucs4 ch,
		std::vector<std::pair<uint64_t, Bitmap>>::iterator insertPoint,
		uint64_t code, bool alreadyAttemptedFree) -> const Bitmap&
	{
		assert(_pimpl->_mode == Mode::Texture2D);

		auto newData = font.GetBitmap(ch);
		if ((newData._width * newData._height) == 0) {
			Bitmap result = {};
			result._xAdvance = newData._xAdvance;		// still need xAdvance here for characters that aren't drawn (ie, whitespace)
			result._lastAccessFrame = _currentFrameIdx;
			auto i = _glyphs.insert(insertPoint, std::make_pair(code, result));
			return i->second;
		}

		if (newData._width > _pimpl->_pageWidth || newData._height > _pimpl->_pageHeight)
			return s_emptyBitmap;		// can't fit this glyph, even when using an entire page

		unsigned bestPage = ~0u;
		RectanglePacker_MaxRects::PreviewedAllocation bestAllocation;
		bestAllocation._score = std::numeric_limits<int>::max();
		for (unsigned c=0; c<_pimpl->_activePages.size(); ++c) {
			auto allocation = _pimpl->_activePages[c]._packer.PreviewAllocation({newData._width, newData._height});
			if (allocation._score < bestAllocation._score) {
				bestPage = c;
				bestAllocation = allocation;
			}
		}
		if (bestPage == ~0u) {
			// could not fit it in -- we need to release some space and try to do a defrag
			if (alreadyAttemptedFree) return s_emptyBitmap;		// maybe too big to fit on a page?
			FreeUpHeapSpace_2D({newData._width, newData._height});
			SynchronousDefrag_2D(threadContext);
			auto code = HashCombine(ch, font.GetHash());
			insertPoint = LowerBound(_glyphs, code);		// FreeUpHeapSpace invalidates this vector
			return InitializeNewGlyph(threadContext, font, ch, insertPoint, code, true);
		}

		_pimpl->_activePages[bestPage]._packer.Allocate(bestAllocation);
		auto rect = bestAllocation._rectangle;
		rect.first += _pimpl->_activePages[bestPage]._spaceInTexture._topLeft;
		rect.second += _pimpl->_activePages[bestPage]._spaceInTexture._topLeft;
		_pimpl->_activePages[bestPage]._texelsAllocated += (rect.second[0] - rect.first[0]) * (rect.second[1] - rect.first[1]);
		assert(_pimpl->_activePages[bestPage]._texelsAllocated >= 0);

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
		#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
			result._lsbDelta = newData._lsbDelta;
			result._rsbDelta = newData._rsbDelta;
		#endif
		result._lastAccessFrame = _currentFrameIdx;

		auto i = _glyphs.insert(insertPoint, std::make_pair(code, result));
		++_getBitmapsInvalidationIdx;
		return i->second;
	}

	bool FontRenderingManager::InitializeNewGlyphs(
		RenderCore::IThreadContext& threadContext,
		const Font& font,
		IteratorRange<const ucs4*> chrs, bool alreadyAttemptedFree)
	{
		assert(_pimpl->_mode == Mode::LinearBuffer);
		assert(!chrs.empty());

		// Initialize multiple new glyphs at once. We'll allocate all of the space for the new glyphs in one go
		// only works with linear buffer resources

		VLA_UNSAFE_FORCE(RenderOverlays::Font::Bitmap, bitmaps, chrs.size());
		std::vector<uint8_t> storageBuffer;
		std::vector<ucs4> overflowChrs;
		unsigned maxPageSize = _pimpl->_pageWidth;
		storageBuffer.reserve(maxPageSize);
		unsigned cnt=0;
		for (auto chr:chrs) {
			bitmaps[cnt] = font.GetBitmap(chr);
			auto start = storageBuffer.size();
			assert(!bitmaps[cnt]._data.empty() || (bitmaps[cnt]._width == 0 && bitmaps[cnt]._height == 0));
			if (start+bitmaps[cnt]._data.size() > maxPageSize) {
				bitmaps[cnt]._data = MakeIteratorRange((const void*)1, (const void*)0);
				++cnt;
				if (bitmaps[cnt]._data.size() <= maxPageSize) overflowChrs.emplace_back(chr);
				continue;
			}
			storageBuffer.insert(storageBuffer.end(), (const uint8_t*)bitmaps[cnt]._data.begin(), (const uint8_t*)bitmaps[cnt]._data.end());
			bitmaps[cnt]._data = MakeIteratorRange((const void*)start, (const void*)(start+bitmaps[cnt]._data.size()));
			++cnt;
		}

		uint32_t encodingOffsetBase = 0;
		if (!storageBuffer.empty()) {		// (if all chars are whitespace, nothing to upload)
			unsigned bestPage = ~0u;
			unsigned allocationSize = (unsigned)storageBuffer.size(), bestFreeBlock = ~0u;
			for (unsigned c=0; c<_pimpl->_activePages.size(); ++c) {
				auto largestBlock = _pimpl->_activePages[c]._spanningHeap.CalculateLargestFreeBlock();
				if (largestBlock >= allocationSize && largestBlock < bestFreeBlock) {
					bestPage = c;
					bestFreeBlock = largestBlock;
				}
			}

			if (bestPage == ~0u) {
				// could not fit it in -- we need to release some space and try to do a defrag
				if (alreadyAttemptedFree)
					return false;		// maybe too big to fit on a page?
				FreeUpHeapSpace_Linear(storageBuffer.size());
				SynchronousDefrag_Linear(threadContext);
				return InitializeNewGlyphs(threadContext, font, chrs, true);
			}

			auto allocation = _pimpl->_activePages[bestPage]._spanningHeap.Allocate(allocationSize);
			assert(allocation != ~0u);
			_pimpl->_activePages[bestPage]._texelsAllocated += allocationSize;

			// no strong guarantee on exception from here, because allocation already completed

			if (_pimpl->_texture)
				_pimpl->_texture->UpdateToTexture(threadContext, storageBuffer, _pimpl->_activePages[bestPage]._spaceInTexture._topLeft[0] + allocation);

			encodingOffsetBase = _pimpl->_activePages[bestPage]._spaceInTexture._topLeft[0] + allocation;
		}

		uint64_t fontHash = (font.GetHash() & 0xffffffffull) << 32ull;
		auto i = _glyphs.begin();
		for (unsigned c=0; c<chrs.size(); ++c) {
			if (bitmaps[c]._data.begin() > bitmaps[c]._data.end()) continue;		// skipped chr

			Bitmap result;
			result._xAdvance = bitmaps[c]._xAdvance;
			result._bitmapOffsetX = bitmaps[c]._bitmapOffsetX;
			result._bitmapOffsetY = bitmaps[c]._bitmapOffsetY;
			#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
				result._lsbDelta = bitmaps[c]._lsbDelta;
				result._rsbDelta = bitmaps[c]._rsbDelta;
			#endif
			result._lastAccessFrame = _currentFrameIdx;
			result._encodingOffset = encodingOffsetBase + (size_t)bitmaps[c]._data.begin();
			result._width = bitmaps[c]._width;
			result._height = bitmaps[c]._height;

			// expecting 'chrs' in sorted order, so we always move 'i' forward
			i = LowerBound2(MakeIteratorRange(i, _glyphs.end()), fontHash | chrs[c]);
			i = _glyphs.insert(i, std::make_pair(fontHash | chrs[c], result));
		}

		++_getBitmapsInvalidationIdx;

		// If the allocation exceeded a page size, we might need to come back for some new characters
		if (!overflowChrs.empty())
			return InitializeNewGlyphs(threadContext,font, overflowChrs, alreadyAttemptedFree);

		return true;
	}

	void FontRenderingManager::FreeUpHeapSpace_2D(UInt2 requestedSpace)
	{
		assert(_pimpl->_mode == Mode::Texture2D);
		// Attempt to free up some space in the heap...
		// This is optimized for infrequent calls. We will erase many of the oldest glyphs, and prepare the heap for
		// defrag operations
		auto glyphsToErase = _glyphs.size() / _pimpl->_activePages.size();
		if (glyphsToErase == 0) return;
		
		using GlyphPair = std::pair<unsigned, unsigned>;
		VLA_UNSAFE_FORCE(GlyphPair, glyphsByAge, _glyphs.size());
		for (unsigned c=0; c<_glyphs.size(); ++c)
			glyphsByAge[c] = {c, _glyphs[c].second._lastAccessFrame};
		std::sort(glyphsByAge, &glyphsByAge[_glyphs.size()], [](const auto&lhs, const auto& rhs) { return lhs.second < rhs.second; });
		std::sort(glyphsByAge, &glyphsByAge[glyphsToErase], [](const auto&lhs, const auto& rhs) { return lhs.first > rhs.first; });

		bool foundBigEnoughGap = false;
		for (unsigned c=0; c<glyphsToErase; ++c) {
			auto& glyph = _glyphs[glyphsByAge[c].first];
			Rect rectangle {
				{
					unsigned(glyph.second._tcTopLeft[0] * _pimpl->_texWidth + 0.5f),
					unsigned(glyph.second._tcTopLeft[1] * _pimpl->_texHeight + 0.5f)
				},
				{
					unsigned(glyph.second._tcBottomRight[0] * _pimpl->_texWidth + 0.5f),
					unsigned(glyph.second._tcBottomRight[1] * _pimpl->_texHeight + 0.5f)
				}
			};
			if (!rectangle.Width()) continue;		// glyph with no bitmap content
			// find the page and erase this from the rectangle packer
			bool foundPage = false;
			for (auto& p:_pimpl->_activePages)
				if (Contains(p._spaceInTexture, rectangle)) {
					p._packer.Deallocate({
						rectangle._topLeft -= p._spaceInTexture._topLeft,
						rectangle._bottomRight -= p._spaceInTexture._topLeft});
					p._texelsAllocated -= (rectangle._bottomRight[0] - rectangle._topLeft[0]) * (rectangle._bottomRight[1] - rectangle._topLeft[1]);
					foundPage = true;
					break;
				}
			assert(foundPage);
			_glyphs.erase(_glyphs.begin()+glyphsByAge[c].first);

			foundBigEnoughGap |= (rectangle.Width() >= requestedSpace[0]) && (rectangle.Height() >= requestedSpace[1]);
		}

		if (!foundBigEnoughGap) {
			// As a safety measure, let's try to release at least one glyph that is equal or larger than the requested
			// one. This might not work (obviously the requested glyph might be the largest one ever requested), but 
			// if it does, at least we know we'll find some space for it
			// The issue here is it might start causing thrashing if there are only a few very large glyphs
			// This is going to be a little expensive, because we have to do another sort & search
			for (unsigned c=0; c<_glyphs.size(); ++c)
				glyphsByAge[c] = {c, _glyphs[c].second._lastAccessFrame};
			std::sort(glyphsByAge, &glyphsByAge[_glyphs.size()], [](const auto&lhs, const auto& rhs) { return lhs.second < rhs.second; });
			for (unsigned c=0; c<_glyphs.size(); ++c) {
				auto& glyph = _glyphs[glyphsByAge[c].first];
				Rect rectangle {
					{
						unsigned(glyph.second._tcTopLeft[0] * _pimpl->_texWidth + 0.5f),
						unsigned(glyph.second._tcTopLeft[1] * _pimpl->_texHeight + 0.5f)
					},
					{
						unsigned(glyph.second._tcBottomRight[0] * _pimpl->_texWidth + 0.5f),
						unsigned(glyph.second._tcBottomRight[1] * _pimpl->_texHeight + 0.5f)
					}
				};
				if (rectangle.Width() >= requestedSpace[0] && rectangle.Height() >= requestedSpace[1]) {
					bool foundPage = false;
					for (auto& p:_pimpl->_activePages)
						if (Contains(p._spaceInTexture, rectangle)) {
							p._packer.Deallocate({
								rectangle._topLeft -= p._spaceInTexture._topLeft,
								rectangle._bottomRight -= p._spaceInTexture._topLeft});
							p._texelsAllocated -= (rectangle._bottomRight[0] - rectangle._topLeft[0]) * (rectangle._bottomRight[1] - rectangle._topLeft[1]);
							foundPage = true;
							break;
						}
					assert(foundPage);
					_glyphs.erase(_glyphs.begin()+glyphsByAge[c].first);
					foundBigEnoughGap = true;
					break;
				}
			}
		}
		// caller should generally call SynchronousDefrag after this
		// when we return, we should have space for a lot more glyphs
	}

	void FontRenderingManager::SynchronousDefrag_2D(RenderCore::IThreadContext& threadContext)
	{
		assert(_pimpl->_mode == Mode::Texture2D);
		// find the most fragmented page, and do a synchronous defragment
		unsigned worstPage = ~0;
		int worstPageScore = 0;
		for (unsigned c=0; c<_pimpl->_activePages.size(); ++c) {
			auto& page = _pimpl->_activePages[c];
			auto freeBlock = page._packer.LargestFreeBlock();
			auto freeSpace = (page._spaceInTexture._bottomRight[0] - page._spaceInTexture._topLeft[0]) * (page._spaceInTexture._bottomRight[1] - page._spaceInTexture._topLeft[1]) - page._texelsAllocated;
			auto score = freeSpace - (freeBlock.second[0] - freeBlock.first[0]) * (freeBlock.second[1] - freeBlock.first[1]);
			if (score > worstPageScore) {
				worstPageScore = score;
				worstPage = c;
			}
		}

		if (worstPage == ~0u) return;

		auto& srcPage = _pimpl->_activePages[worstPage];

		// Find all of the glyphs & all of the rectangles that are on this page. We will reallocate them and try to get an optimal packing
		std::vector<std::pair<unsigned, Rect>> associatedRectangles;
		associatedRectangles.reserve(_glyphs.size() / (_pimpl->_activePages.size()) * 2);

		for (unsigned g=0; g<_glyphs.size(); ++g) {
			auto& glyph = _glyphs[g];
			Rect rectangle {
				{
					unsigned(glyph.second._tcTopLeft[0] * _pimpl->_texWidth + 0.5f),
					unsigned(glyph.second._tcTopLeft[1] * _pimpl->_texHeight + 0.5f)
				},
				{
					unsigned(glyph.second._tcBottomRight[0] * _pimpl->_texWidth + 0.5f),
					unsigned(glyph.second._tcBottomRight[1] * _pimpl->_texHeight + 0.5f)
				}
			};
			if (!rectangle.Width() || !Contains(srcPage._spaceInTexture, rectangle)) continue;
			associatedRectangles.emplace_back(g, rectangle);
		}

		// repack optimally
		std::sort(
			associatedRectangles.begin(), associatedRectangles.end(),
			[](const auto& lhs, const auto& rhs)
			{
				auto lhsDims = lhs.second._bottomRight - lhs.second._topLeft;
				auto rhsDims = rhs.second._bottomRight - rhs.second._topLeft;
				return std::max(lhsDims[0], lhsDims[1]) > std::max(rhsDims[0], rhsDims[1]);
			});
		
		std::vector<Rect> newPacking;
		std::vector<unsigned> glyphsToDelete;
		newPacking.reserve(associatedRectangles.size());
		RectanglePacker_MaxRects packer{UInt2{_pimpl->_pageWidth, _pimpl->_pageHeight}};
		unsigned allocatedTexels = 0;
		for (const auto& r:associatedRectangles) {
			auto dims = r.second._bottomRight - r.second._topLeft;
			auto rect = packer.Allocate(dims);
			// In rare cases the Allocate() can fail -- we've effectively ended up with a less well packed result. We will just delete those glyphs
			if (rect.second[0] > rect.first[0]) {
				assert(rect.second[0] > rect.first[0] && rect.second[1] > rect.first[1]);
				allocatedTexels += dims[0]*dims[1];
				rect.first += _pimpl->_reservedPage._spaceInTexture._topLeft;
				rect.second += _pimpl->_reservedPage._spaceInTexture._topLeft;
			} else {
				glyphsToDelete.push_back(r.first);
			}
			newPacking.emplace_back(rect.first, rect.second);
		}

		// copy from the old locations into the new destination positions
		{
			using namespace RenderCore;
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto blitEncoder = metalContext.BeginBlitEncoder();
			// Vulkan can do all of this copying with a single cmd -- would we better off with an interface
			// that allows for multiple copies?
			auto* res = _pimpl->_texture->GetUnderlying().get();
			for (unsigned c=0; c<associatedRectangles.size(); ++c) {
				auto srcRectangle = associatedRectangles[c].second;
				auto dstRectangle = newPacking[c];
				if (!dstRectangle.Width()) continue;
				assert(srcRectangle.Width() == dstRectangle.Width() && srcRectangle.Height() == dstRectangle.Height());
				blitEncoder.Copy(
					CopyPartial_Dest {
						*res, {}, VectorPattern<unsigned, 3>{ (unsigned)dstRectangle._topLeft[0], (unsigned)dstRectangle._topLeft[1], 0u }
					},
					CopyPartial_Src { *res }.PartialSubresource(
						VectorPattern<unsigned, 3>{ (unsigned)srcRectangle._topLeft[0], (unsigned)srcRectangle._topLeft[1], 0u },
						VectorPattern<unsigned, 3>{ (unsigned)srcRectangle._bottomRight[0], (unsigned)srcRectangle._bottomRight[1], 1u },
						MakeTexturePitches(res->GetDesc()._textureDesc)
					));
			}
		}

		// reassign glyphs table and make the new page active
		for (unsigned c=0; c<associatedRectangles.size(); ++c) {
			auto& glyph = _glyphs[associatedRectangles[c].first].second;
			auto rect = newPacking[c];
			glyph._tcTopLeft[0] = rect._topLeft[0] / float(_pimpl->_texWidth);
			glyph._tcTopLeft[1] = rect._topLeft[1] / float(_pimpl->_texHeight);
			glyph._tcBottomRight[0] = rect._bottomRight[0] / float(_pimpl->_texWidth);
			glyph._tcBottomRight[1] = rect._bottomRight[1] / float(_pimpl->_texHeight);
		}

		// delete any glyphs that didn't be successfully packed into the new texture
		std::sort(glyphsToDelete.begin(), glyphsToDelete.end(), [](auto lhs, auto rhs) { return rhs > lhs; });
		for (auto g:glyphsToDelete) _glyphs.erase(_glyphs.begin()+g);

		_pimpl->_reservedPage._packer = std::move(packer);
		_pimpl->_reservedPage._texelsAllocated = allocatedTexels;
		auto srcSpaceInTexture = srcPage._spaceInTexture;
		srcPage = std::move(_pimpl->_reservedPage);
		_pimpl->_reservedPage._packer = {};
		_pimpl->_reservedPage._texelsAllocated = 0;	
		_pimpl->_reservedPage._spaceInTexture = srcSpaceInTexture;
	}

	void FontRenderingManager::FreeUpHeapSpace_Linear(size_t requestedSpace)
	{
		assert(_pimpl->_mode == Mode::LinearBuffer);

		// Attempt to free up some space in the heap...
		// This is optimized for infrequent calls. We will erase many of the oldest glyphs, and prepare the heap for
		// defrag operations
		unsigned desiredGlyphsToErase = unsigned(_glyphs.size() / _pimpl->_activePages.size());
		if (desiredGlyphsToErase == 0) return;

		unsigned glyphsErased = 0;
		for (;;) {
			auto oldestFrame = _currentFrameIdx;
			for (auto& glyph:_glyphs) oldestFrame = std::min(oldestFrame, glyph.second._lastAccessFrame);

			const unsigned gracePeriod = 4;
			if (_currentFrameIdx < gracePeriod || oldestFrame > _currentFrameIdx-gracePeriod) return;

			for (unsigned g=0; g<_glyphs.size();) {
				auto idx = _glyphs.size() - g - 1;
				if (_glyphs[idx].second._lastAccessFrame == oldestFrame) {

					auto start = _glyphs[idx].second._encodingOffset;
					auto end = start+_glyphs[idx].second._width*_glyphs[idx].second._height;
					bool foundPage = false;
					for (auto& p:_pimpl->_activePages)
						if (start >= p._spaceInTexture._topLeft[0] && end <= p._spaceInTexture._bottomRight[0]) {
							p._spanningHeap.Deallocate(start-p._spaceInTexture._topLeft[0], end-start);
							p._texelsAllocated -= end-start;
							foundPage = true;
							break;
						}
					assert(foundPage);

					_glyphs.erase(_glyphs.begin()+idx);
					++glyphsErased;
				} else
					++g;
			}

			if (glyphsErased >= desiredGlyphsToErase) {
				unsigned largestEmptyBlock = 0;
				for (auto& p:_pimpl->_activePages)
					largestEmptyBlock = std::min(largestEmptyBlock, p._spanningHeap.CalculateLargestFreeBlock());
				if (largestEmptyBlock >= requestedSpace) break;		// got enough space for the request
			}
		}
	}

	void FontRenderingManager::SynchronousDefrag_Linear(RenderCore::IThreadContext& threadContext)
	{
		assert(_pimpl->_mode == Mode::LinearBuffer);

		// find the most fragmented page, and do a synchronous defragment
		unsigned worstPage = ~0;
		int worstPageScore = 0;
		for (unsigned c=0; c<_pimpl->_activePages.size(); ++c) {
			auto& page = _pimpl->_activePages[c];
			auto freeBlock = page._spanningHeap.CalculateLargestFreeBlock();
			auto freeSpace = page._spanningHeap.CalculateAvailableSpace();
			auto score = freeSpace - freeBlock;
			if (score > worstPageScore) {
				worstPageScore = score;
				worstPage = c;
			}
		}

		if (worstPage == ~0u) return;

		auto& srcPage = _pimpl->_activePages[worstPage];
		auto srcPageStart = srcPage._spaceInTexture._topLeft[0];
		auto srcPageEnd = srcPage._spaceInTexture._bottomRight[0];
		auto dstPageStart = _pimpl->_reservedPage._spaceInTexture._topLeft[0];

		// Find all of the glyphs & all of the rectangles that are on this page. We will reallocate them and try to get an optimal packing
		auto compression = srcPage._spanningHeap.CalculateHeapCompression();

		// copy from the old locations into the new destination positions
		if (_pimpl->_texture) {
			using namespace RenderCore;
			auto& metalContext = *Metal::DeviceContext::Get(threadContext);
			auto blitEncoder = metalContext.BeginBlitEncoder();
			// Vulkan can do all of this copying with a single cmd -- would we better off with an interface
			// that allows for multiple copies?
			auto* res = _pimpl->_texture->GetUnderlying().get();
			for (auto reposition:compression)
				blitEncoder.Copy(
					CopyPartial_Dest { *res, dstPageStart + reposition._destination },
					CopyPartial_Src { *res, srcPageStart + reposition._sourceStart, srcPageStart + reposition._sourceEnd });
		}

		// reassign glyphs table and make the new page active
		for (unsigned g=0; g<_glyphs.size(); ++g) {
			auto& glyph = _glyphs[g].second;
			if (glyph._encodingOffset >= srcPageStart && glyph._encodingOffset < srcPageEnd) {
				auto startInSrcHeap = glyph._encodingOffset - srcPageStart;
				auto endInSrcHeap = startInSrcHeap + glyph._width*glyph._height;
				bool foundReposition = false;
				for (auto& c:compression) {
					if (startInSrcHeap >= c._sourceStart && startInSrcHeap < c._sourceEnd) {
						assert(endInSrcHeap <= c._sourceEnd);	// if you hit this it means that the compression has split a block so this character is no longer contiguous
						glyph._encodingOffset = dstPageStart + c._destination + startInSrcHeap - c._sourceStart;
						foundReposition = true;
					}
				}
				assert(foundReposition);
			}
		}

		srcPage._spanningHeap.PerformReposition(compression);
		std::swap(srcPage._spaceInTexture, _pimpl->_reservedPage._spaceInTexture);
	}

	bool FontRenderingManager::GetBitmaps(
		const Bitmap* bitmaps[],
		RenderCore::IThreadContext& threadContext,
		const Font& font,
		IteratorRange<const ucs4*> chrs)
	{
		// first - check if all of the characters are already in the glyphs list
		// expecting "chrs" to be in sorted order already
		uint64_t fontHash = (font.GetHash() & 0xffffffffull) << 32ull;	// we only use the lower 32 bits of the font hash (because we're going to use the fact that the input chrs are in sorted order)
		auto chrIterator = chrs.begin();
		VLA(ucs4, missingGlyphs, chrs.size());
		unsigned missingGlyphCount = 0;
		const Bitmap** bitmapIterator = bitmaps;
		auto i = LowerBound(_glyphs, fontHash|uint64_t(*chrIterator));
		auto endi = LowerBound2(MakeIteratorRange(i, _glyphs.end()), fontHash|0xffffffffull);
		while (chrIterator != chrs.end()) {
			auto code = fontHash | *chrIterator;
			i = LowerBound2(MakeIteratorRange(i, endi), code);
			if (i == endi) break;
			if (i->first != code) {
				missingGlyphs[missingGlyphCount++] = *chrIterator;
			} else {
				*bitmapIterator++ = &i->second;
				i->second._lastAccessFrame = _currentFrameIdx;	// update _lastAccessFrame before we call InitializeNewGlyphs below
			}
			++chrIterator;
		}
		for (; chrIterator != chrs.end(); ++chrIterator) missingGlyphs[missingGlyphCount++] = *chrIterator;

		// Note that InitializeNewGlyphs changes the _glyphs vector, thereby invalidating everything returned from GetBitmaps()
		// from this call, and all previous calls.
		if (missingGlyphCount) {
			if (_pimpl->_mode == Mode::LinearBuffer) {
				if (!InitializeNewGlyphs(threadContext, font, MakeIteratorRange(missingGlyphs, &missingGlyphs[missingGlyphCount]), false))
					return false;
			} else {
				// in the (older) Texture2D mode, we must initialize each glyph separately
				for(auto ch:MakeIteratorRange(missingGlyphs, &missingGlyphs[missingGlyphCount])) {
					auto code = fontHash|uint64_t(ch);
					auto i = LowerBound(_glyphs, code);
					assert(i == _glyphs.end() || i->first != code);
					InitializeNewGlyph(threadContext, font, ch, i, code, false);
				}
			}

			// We have to redo everything from scratch after calling InitializeNewGlyphs
			return GetBitmaps(bitmaps, threadContext, font, chrs);
		}
		return true;
	}

	void FontRenderingManager::AddUploadBarrier(RenderCore::IThreadContext& threadContext)
	{
		RenderCore::Metal::BarrierHelper(threadContext).Add(*_pimpl->_texture->GetUnderlying(), RenderCore::BindFlag::TransferDst, RenderCore::BindFlag::ShaderResource);
	}

	const FontTexture2D& FontRenderingManager::GetFontTexture()
	{
		return *_pimpl->_texture;
	}

	const std::shared_ptr<RenderCore::IResourceView>& FontRenderingManager::GetSRV() const
	{
		return _pimpl->_texture->GetSRV();
	}

	UInt2 FontRenderingManager::GetTextureDimensions()
	{
		return UInt2{_pimpl->_texWidth, _pimpl->_texHeight};
	}

	void FontRenderingManager::OnFrameBarrier()
	{
		++_currentFrameIdx;
	}

	const RenderCore::Techniques::ImmediateDrawableMaterial& FontRenderingManager::GetImmediateDrawableMaterial()
	{
		return _pimpl->_immediateDrawableMaterial;
	}

	const RenderCore::Techniques::RetainedUniformsStream& FontRenderingManager::GetImmediateDrawableUniforms()
	{
		return _pimpl->_immediateDrawableUniforms;
	}

	const std::shared_ptr<RenderCore::IResource>& FontRenderingManager::GetUnderlyingTextureResource()
	{
		return _pimpl->_texture->GetUnderlying();
	}

	template StringSection<char> FontRenderingControlStatement::TryParse(StringSection<char>);
	template StringSection<utf8> FontRenderingControlStatement::TryParse(StringSection<utf8>);
	template StringSection<ucs2> FontRenderingControlStatement::TryParse(StringSection<ucs2>);
	template StringSection<ucs4> FontRenderingControlStatement::TryParse(StringSection<ucs4>);

}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Font.h"
#include "OverlayPrimitives.h"
#include "../RenderCore/Format.h"
#include "../Math/Vector.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/IntrusivePtr.h"
#include <vector>
#include <memory>

namespace RenderCore { class IResource; class IResourceView; class IThreadContext; class IDevice; }
namespace RenderCore { namespace Techniques { class IImmediateDrawables; }}

namespace RenderOverlays
{
	class Font;
	class FontTexture2D;
	class FontRenderingManager;
        
    float       Draw(   RenderCore::IThreadContext& threadContext,
						RenderCore::Techniques::IImmediateDrawables& immediateDrawables,
						FontRenderingManager& textureMan,
						const Font& font, const TextStyle& style,
                        float x, float y, StringSection<ucs4> text,
                        float spaceExtra, float scale, float mx, float depth,
                        unsigned colorARGB, bool applyDescender, Quad* q);

	class FontRenderingManager
	{
	public:
		struct Bitmap
		{
			float _xAdvance = 0.f;
			signed _bitmapOffsetX = 0, _bitmapOffsetY = 0;
			unsigned _width, _height;
			Float2 _tcTopLeft = Float2{0.f, 0.f};
			Float2 _tcBottomRight = Float2{0.f, 0.f};
			unsigned _lsbDelta = 0, _rsbDelta = 0;
		};

		Bitmap GetBitmap(
			RenderCore::IThreadContext& threadContext,
			const Font& font,
			ucs4 ch);

		const FontTexture2D& GetFontTexture();
		UInt2 GetTextureDimensions();
    
		FontRenderingManager(RenderCore::IDevice& device);
		~FontRenderingManager();

	private:
		std::vector<std::pair<uint64_t, Bitmap>> _glyphs;
		
		class Pimpl;
		std::shared_ptr<Pimpl> _pimpl;

		Bitmap InitializeNewGlyph(
			RenderCore::IThreadContext& threadContext,
			const Font& font,
			ucs4 ch,
			std::vector<std::pair<uint64_t, Bitmap>>::iterator insertPoint,
			uint64_t code);
	};

	inline auto FontRenderingManager::GetBitmap(
		RenderCore::IThreadContext& threadContext,
		const Font& font,
		ucs4 ch) -> Bitmap
	{
		auto code = HashCombine(ch, font.GetHash());
		auto i = LowerBound(_glyphs, code);
		if (__builtin_expect(i != _glyphs.end() && i->first == code, true))
			return i->second;

		return InitializeNewGlyph(threadContext, font, ch, i, code);
	}

}


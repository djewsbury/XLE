// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Font.h"
#include "../Assets/AssetsCore.h"
#include "../Math/Vector.h"
#include <memory>

typedef struct FT_FaceRec_ FT_FaceRec;
typedef struct FT_FaceRec_* FT_Face;

namespace RenderCore { class IDevice; }

namespace RenderOverlays
{
	class FTFontResources;

	class FTFont : public Font 
	{
	public:
		virtual FontProperties GetFontProperties() const;
		virtual Bitmap GetBitmap(ucs4 ch) const;
		virtual GlyphProperties GetGlyphProperties(ucs4 ch) const;

		virtual Float2 GetKerning(int prevGlyph, ucs4 ch, int* curGlyph) const;
		virtual Float2 GetKerningReverse(int prevGlyph, ucs4 ch, int* curGlyph) const;
		virtual float GetKerning(ucs4 prev, ucs4 ch) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		FTFont(StringSection<::Assets::ResChar> faceName, int faceSize);
		virtual ~FTFont();
	protected:
		FTFontResources* _resources;
		int _ascend;
		std::shared_ptr<FT_FaceRec_> _face;
		::Assets::Blob _pBuffer;
		::Assets::DependencyValidation _depVal;

		mutable std::vector<std::pair<ucs4, GlyphProperties>> _cachedGlyphProperties;
		FontProperties _fontProperties;
	};

	std::shared_ptr<FTFontResources> CreateFTFontResources();
}


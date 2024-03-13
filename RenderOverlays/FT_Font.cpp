// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FT_Font.h"
#include "Font.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetsCore.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../OSServices/RawFS.h"
#include "../Math/Vector.h"
#include "../Formatters/TextFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Conversion.h"
#include "../Utility/FastParseValue.h"
#include "../xleres/FileList.h"
#include <set>
#include <algorithm>
#include <assert.h>
#include <locale>

#include "ft2build.h"
#include FT_FREETYPE_H

namespace RenderOverlays
{
	class FTFontResources
	{
	public:
		FT_Library _ftLib;
		std::unordered_map<std::string, std::string> _nameMap;
		::Assets::DependencyValidation _nameMapDepVal;

		FTFontResources();
		~FTFontResources();
	};

	Threading::Mutex s_mainFontResourcesInstanceLock;
	ConsoleRig::AttachablePtr<FTFontResources> s_mainFontResourcesInstance;

	class FTFont : public Font 
	{
	public:
		virtual FontProperties GetFontProperties() const;
		virtual Bitmap GetBitmap(ucs4 ch) const;
		virtual GlyphProperties GetGlyphProperties(ucs4 ch) const;
		virtual void GetGlyphPropertiesSorted(
			IteratorRange<GlyphProperties*> result,
			IteratorRange<const ucs4*> glyphs) const;

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

		struct LoadedChar;
		mutable std::vector<std::pair<ucs4, LoadedChar>> _cachedLoadedChars;
		FontProperties _fontProperties;
	};

	constexpr unsigned loadFlags = FT_LOAD_TARGET_LIGHT/* | FT_LOAD_NO_AUTOHINT*/;

	FTFont::FTFont(StringSection<::Assets::ResChar> faceName, int faceSize)
	{
		{
			ScopedLock(s_mainFontResourcesInstanceLock);
			_resources = s_mainFontResourcesInstance.get();
			if (!_resources) {
				s_mainFontResourcesInstance = std::make_shared<FTFontResources>();
				_resources = s_mainFontResourcesInstance.get();
			}
		}

		std::string finalPath = faceName.AsString();
		auto i = _resources->_nameMap.find(finalPath);
		if (i != _resources->_nameMap.end())
			finalPath = i->second;

		_hashCode = Hash64(finalPath, DefaultSeed64 + faceSize);
		_pBuffer = ::Assets::MainFileSystem::TryLoadFileAsBlob(finalPath);

		auto& depValSys = ::Assets::GetDepValSys();
		_depVal = depValSys.Make();
		_depVal.RegisterDependency(depValSys.GetDependentFileState(finalPath));
		_depVal.RegisterDependency(_resources->_nameMapDepVal);

		if (!_pBuffer)
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::MissingFile,
				_depVal,
				StringMeld<256>() << "Failed to load font (" << finalPath << ")"));

		FT_Face face;
		FT_New_Memory_Face(_resources->_ftLib, _pBuffer->data(), (FT_Long)_pBuffer->size(), 0, &face);
		_face = std::shared_ptr<FT_FaceRec_>{
			face,
			[](FT_Face f) { FT_Done_Face(f); } };

		FT_Error error = FT_Set_Pixel_Sizes(_face.get(), 0, faceSize);
		if (error)
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::FormatNotUnderstood,
				_depVal,
				StringMeld<256>() << "Failed to set pixel size while initializing font (" << finalPath << ")"));

		_fontProperties._descender = _face->size->metrics.descender / 64.0f;
		_fontProperties._ascender = _face->size->metrics.ascender / 64.0f;
		_fontProperties._lineHeight = _face->size->metrics.height / 64.0f;
		_fontProperties._maxAdvance = _face->size->metrics.max_advance / 64.0f;
		_fontProperties._ascenderExcludingAccent = _fontProperties._ascender;
		_fontProperties._fixedWidthAdvance = 0.f;

		error = FT_Load_Char(_face.get(), 'X', loadFlags);
		if (!error) {
			_fontProperties._ascenderExcludingAccent = (float)_face->glyph->bitmap_top;
			if (FT_IS_FIXED_WIDTH(_face.get()))
				_fontProperties._fixedWidthAdvance = _face->glyph->advance.x / 64.f;
		}
	}

	FTFont::~FTFont()
	{
	}

	auto FTFont::GetFontProperties() const -> FontProperties { return _fontProperties; }

	Float2 FTFont::GetKerning(int prevGlyph, ucs4 ch, int* curGlyph) const
	{
		int currentGlyph = FT_Get_Char_Index(_face.get(), ch); 
		if(*curGlyph)
			*curGlyph = currentGlyph;

		if (prevGlyph) {
			FT_Vector kerning;
			FT_Get_Kerning(_face.get(), prevGlyph, currentGlyph, FT_KERNING_DEFAULT, &kerning);

			return Float2((float)kerning.x / 64, (float)kerning.y / 64);
		}

		return Float2(0.0f, 0.0f);
	}

	Float2 FTFont::GetKerningReverse(int prevGlyph, ucs4 ch, int* curGlyph) const
	{
		int currentGlyph = FT_Get_Char_Index(_face.get(), ch); 
		if(*curGlyph)
			*curGlyph = currentGlyph;

		if (prevGlyph) {
			FT_Vector kerning;
			FT_Get_Kerning(_face.get(), currentGlyph, prevGlyph, FT_KERNING_DEFAULT, &kerning);

			return Float2((float)kerning.x / 64, (float)kerning.y / 64);
		}

		return Float2(0.0f, 0.0f);
	}

	float FTFont::GetKerning(ucs4 prev, ucs4 ch) const
	{
		if (prev) {
			int prevGlyph = FT_Get_Char_Index(_face.get(), prev);
			int curGlyph = FT_Get_Char_Index(_face.get(), ch); 

			FT_Vector kerning;
			FT_Get_Kerning(_face.get(), prevGlyph, curGlyph, FT_KERNING_DEFAULT, &kerning);
			return (float)kerning.x / 64;
		}

		return 0.0f;
	}

	struct FTFont::LoadedChar
	{
		FT_GlyphSlot _glyph = nullptr;
		GlyphProperties _glyphProps;
		bool _hasBeenRendered = false;
	};

	auto FTFont::GetGlyphProperties(ucs4 ch) const -> GlyphProperties
	{
		auto i = LowerBound(_cachedLoadedChars, ch);
		if (i == _cachedLoadedChars.end() || i->first != ch) {
			LoadedChar loadedChar;
			FT_Error error = FT_Load_Char(_face.get(), ch, loadFlags);
			if (!error) {
				loadedChar._glyph = _face->glyph;
				loadedChar._glyphProps._xAdvance = (float)loadedChar._glyph->advance.x / 64.0f;
				loadedChar._glyphProps._lsbDelta = loadedChar._glyph->lsb_delta;
				loadedChar._glyphProps._rsbDelta = loadedChar._glyph->rsb_delta;
				loadedChar._glyphProps._bitmapOffsetX = loadedChar._glyph->bitmap_left;
				loadedChar._glyphProps._bitmapOffsetY = -loadedChar._glyph->bitmap_top;
				loadedChar._glyphProps._width = loadedChar._glyph->bitmap.width;
				loadedChar._glyphProps._height = loadedChar._glyph->bitmap.rows;
			}
			i = _cachedLoadedChars.insert(i, std::make_pair(ch, loadedChar));
		}
		return i->second._glyphProps;
	}

	void FTFont::GetGlyphPropertiesSorted(
		IteratorRange<GlyphProperties*> result,
		IteratorRange<const ucs4*> glyphs) const
	{
		// Load a number of glyphs at once; looking through our _cachedLoadedChars efficiently because the input
		// is in sorted order
		auto i = _cachedLoadedChars.begin();
		while (!glyphs.empty()) {
			i = LowerBound2(MakeIteratorRange(i, _cachedLoadedChars.end()), glyphs.front());
			if (i == _cachedLoadedChars.end() || i->first != glyphs.front()) {
				LoadedChar loadedChar;
				FT_Error error = FT_Load_Char(_face.get(), glyphs.front(), loadFlags);
				if (!error) {
					loadedChar._glyph = _face->glyph;
					loadedChar._glyphProps._xAdvance = (float)loadedChar._glyph->advance.x / 64.0f;
					loadedChar._glyphProps._lsbDelta = loadedChar._glyph->lsb_delta;
					loadedChar._glyphProps._rsbDelta = loadedChar._glyph->rsb_delta;
					loadedChar._glyphProps._bitmapOffsetX = loadedChar._glyph->bitmap_left;
					loadedChar._glyphProps._bitmapOffsetY = -loadedChar._glyph->bitmap_top;
					loadedChar._glyphProps._width = loadedChar._glyph->bitmap.width;
					loadedChar._glyphProps._height = loadedChar._glyph->bitmap.rows;
				}
				i = _cachedLoadedChars.insert(i, std::make_pair(glyphs.front(), loadedChar));
			}

			result.front() = i->second._glyphProps;

			++glyphs.first;
			++result.first;
		}	
	}

	auto FTFont::GetBitmap(ucs4 ch) const -> Bitmap
	{
		auto i = LowerBound(_cachedLoadedChars, ch);
		if (i == _cachedLoadedChars.end() || i->first != ch) {
			LoadedChar loadedChar;
			FT_Error error = FT_Load_Char(_face.get(), ch, FT_LOAD_RENDER | loadFlags);
			if (!error) {
				loadedChar._glyph = _face->glyph;
				loadedChar._glyphProps._xAdvance = (float)loadedChar._glyph->advance.x / 64.0f;
				loadedChar._glyphProps._lsbDelta = loadedChar._glyph->lsb_delta;
				loadedChar._glyphProps._rsbDelta = loadedChar._glyph->rsb_delta;
				loadedChar._glyphProps._bitmapOffsetX = loadedChar._glyph->bitmap_left;
				loadedChar._glyphProps._bitmapOffsetY = -loadedChar._glyph->bitmap_top;
				loadedChar._glyphProps._width = loadedChar._glyph->bitmap.width;
				loadedChar._glyphProps._height = loadedChar._glyph->bitmap.rows;
				loadedChar._hasBeenRendered = true;
			}
			i = _cachedLoadedChars.insert(i, std::make_pair(ch, loadedChar));
			if (error)
				return Bitmap {};
		} else if (!i->second._hasBeenRendered) {
			// We must load the character again to render, because it seems like only the most recently loaded character
			// can be rendered
			FT_Error error = FT_Load_Char(_face.get(), ch, FT_LOAD_RENDER | loadFlags);
			if (error)
				return Bitmap {};		// i->second._hasBeenRendered not set; will always attempt to re-render
			i->second._hasBeenRendered = true;
			assert(i->second._glyph == _face->glyph);
			i->second._glyph = _face->glyph;
		}

		FT_GlyphSlot glyph = i->second._glyph;

		Bitmap result;
		result._xAdvance = (float)glyph->advance.x / 64.0f;
		result._bitmapOffsetX = glyph->bitmap_left;
		result._bitmapOffsetY = -glyph->bitmap_top;
		result._width = glyph->bitmap.width;
		result._height = glyph->bitmap.rows;
		result._data = MakeIteratorRange(glyph->bitmap.buffer, PtrAdd(glyph->bitmap.buffer, glyph->bitmap.width*glyph->bitmap.rows));
		result._lsbDelta = glyph->lsb_delta;
		result._rsbDelta = glyph->rsb_delta;
		return result;
	}

	struct FontDef { std::string path; int size; };
	struct FontDefLessPred
	{
		bool operator() (const FontDef& x, const FontDef& y) const
		{
			if (x.size < y.size) {
				return true;
			}

			if (x.size > y.size) {
				return false;
			}

			if (x.path < y.path) {
				return true;
			}

			return false;
		}
	};

	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> path, int size)
	{
		return std::reinterpret_pointer_cast<::Assets::MarkerPtr<Font>>(::Assets::GetAssetMarkerPtr<FTFont>(path, size));
	}

	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> pathAndSize)
	{
		auto colon = pathAndSize.end();
		while (colon != pathAndSize.begin() && *(colon-1) != ':') --colon;
		if (colon != pathAndSize.begin()) {
			uint32_t fontSize = 0;
			auto* parseEnd = FastParseValue(MakeStringSection(colon, pathAndSize.end()), fontSize);
			if (parseEnd != pathAndSize.end())
				Throw(std::runtime_error(StringMeld<128>() << "Could not interpret font name (" << pathAndSize << ")"));
			return MakeFont({pathAndSize.begin(), colon-1}, fontSize);
		} else {
			return MakeFont(pathAndSize, 16);
		}
	}

	////////////////////////////////////////////////////////////////////////////////////

	static void LoadFontNameMapping(Formatters::TextInputFormatter<utf8>& formatter, std::unordered_map<std::string, std::string>& result)
	{
		StringSection<> name;
		while (formatter.TryKeyedItem(name)) {
            switch (formatter.PeekNext()) {
            case Formatters::FormatterBlob::Value:
				result.insert({name.AsString(), RequireStringValue(formatter).AsString()});
				break;

			case Formatters::FormatterBlob::BeginElement:
				RequireBeginElement(formatter);
				SkipElement(formatter);
				RequireEndElement(formatter);
				break;

			default:
                Throw(Formatters::FormatException("Unexpected blob", formatter.GetLocation()));
            }
        }
	}

	static std::unordered_map<std::string, std::string> LoadFontConfigFile(StringSection<> cfgFile)
	{
		std::unordered_map<std::string, std::string> result;

		size_t blobSize = 0;
		auto blob = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(cfgFile, &blobSize);

		Formatters::TextInputFormatter<utf8> formatter(MakeStringSection((const char*)blob.get(), (const char*)PtrAdd(blob.get(), blobSize)));

		auto locale = std::locale("").name();

		StringSection<> name;
		while (formatter.TryKeyedItem(name)) {
			RequireBeginElement(formatter);
			if (XlEqStringI(name, "*")) {
				LoadFontNameMapping(formatter, result);
			} else if (XlEqStringI(name, locale)) {
				LoadFontNameMapping(formatter, result);
			} else {
				SkipElement(formatter);
			}
			RequireEndElement(formatter);
        }

		return result;
	}

	FTFontResources::FTFontResources()
	{
		FT_Error error = FT_Init_FreeType(&_ftLib);
		if (error)
			Throw(::Exceptions::BasicLabel("Freetype font library failed to initialize (error code: %i)", error));

		_nameMapDepVal = ::Assets::GetDepValSys().Make(FONTS_DAT);
		_nameMap = LoadFontConfigFile(FONTS_DAT);
	}

	FTFontResources::~FTFontResources()
	{
		FT_Done_FreeType(_ftLib);
	}

	std::shared_ptr<Font> CreateFTFont(StringSection<::Assets::ResChar> faceName, int faceSize)
	{
		return std::make_shared<FTFont>(faceName, faceSize);
	}

}


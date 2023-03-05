// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FT_Font.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../ConsoleRig/AttachablePtr.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringUtils.h"
#include "../OSServices/RawFS.h"
#include "../Formatters/TextFormatter.h"
#include "../Formatters/FormatterUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Conversion.h"
#include "../Utility/FastParseValue.h"
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

		error = FT_Load_Char(_face.get(), 'X', FT_LOAD_RENDER);
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

	auto FTFont::GetGlyphProperties(ucs4 ch) const -> GlyphProperties
	{
		auto i = LowerBound(_cachedGlyphProperties, ch);
		if (i == _cachedGlyphProperties.end() || i->first != ch) {
			GlyphProperties props;
			FT_Error error = FT_Load_Char(_face.get(), ch, 0/*FT_LOAD_NO_AUTOHINT*/);
			if (!error) {
				FT_GlyphSlot glyph = _face->glyph;
				props._xAdvance = (float)glyph->advance.x / 64.0f;
			}
			i = _cachedGlyphProperties.insert(i, std::make_pair(ch, props));
		}
		return i->second;
	}

	auto FTFont::GetBitmap(ucs4 ch) const -> Bitmap
	{
		FT_Error error = FT_Load_Char(_face.get(), ch, FT_LOAD_RENDER /*| FT_LOAD_NO_AUTOHINT*/);
		if (error)
			return Bitmap {};

		FT_GlyphSlot glyph = _face->glyph;

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
		return std::reinterpret_pointer_cast<::Assets::MarkerPtr<Font>>(::Assets::MakeAssetMarkerPtr<FTFont>(path, size));
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

		const char* fontCfg = "xleres/DefaultResources/fonts/fonts.dat";
		_nameMap = LoadFontConfigFile(fontCfg);
		_nameMapDepVal = ::Assets::GetDepValSys().Make(fontCfg);
	}

	FTFontResources::~FTFontResources()
	{
		FT_Done_FreeType(_ftLib);
	}

}


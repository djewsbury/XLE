// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Font.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/Assets.h"
#include "../Assets/AssetsCore.h"
#include "../Assets/AssetMixins.h"
#include "../Assets/Marker.h"
#include "../Assets/Continuation.h"
#include "../Assets/ContinuationUtil.h"
#include "../Assets/AssetMixins.h"
#include "../Assets/ConfigFileContainer.h"
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
#include "../Utility/Threading/Mutex.h"
#include <set>
#include <algorithm>
#include <unordered_map>
#include <assert.h>
#include <locale>

#include "ft2build.h"
#include FT_FREETYPE_H

namespace RenderOverlays
{
	struct FontLibraryFile
	{
		std::unordered_map<std::string, std::string> _nameMap;

		FontLibraryFile(Formatters::TextInputFormatter<char>& fmttr, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal);
		FontLibraryFile() = default;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
	private:
		::Assets::DependencyValidation _depVal;
	};

	class FTFontResources
	{
	public:
		FT_Library _ftLib;

		struct FontLibraryCollection
		{
			std::unordered_map<std::string, std::string> _nameMap;
			::Assets::DependencyValidation _depVal;
			const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }
		};
		::Assets::PtrToMarkerPtr<FontLibraryCollection> _fontLibraryCollection;
		Threading::Mutex _mutex;

		std::vector<std::string> _sourceFontLibraries;

		FTFontResources();
		~FTFontResources();

		void RebuildFontLibraryCollectionAlreadyLocked();
	};

	static ConsoleRig::WeakAttachablePtr<FTFontResources> s_mainFontResourcesInstance;
	static FTFontResources* GetFontResources() { return s_mainFontResourcesInstance.lock().get(); }

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

		FTFont(StringSection<::Assets::ResChar> faceName, int faceSize, FT_Library& library, const FTFontResources::FontLibraryCollection& fontLibraryCollection);
		virtual ~FTFont();
	protected:
		int _ascend;
		std::shared_ptr<FT_FaceRec_> _face;
		::Assets::Blob _pBuffer;
		::Assets::DependencyValidation _depVal;

		struct LoadedChar;
		mutable std::vector<std::pair<ucs4, LoadedChar>> _cachedLoadedChars;
		FontProperties _fontProperties;
	};

	// NOTE -- AUTOHINT creates problems with fixed width fonts
	//			It can give values in the lsbDelta & rsbDelta members for fractional gylph width
	//			-- however, that may be calculated differently for each glyph
	constexpr unsigned loadFlags = FT_LOAD_TARGET_LIGHT | ((XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS) ? 0 : (FT_LOAD_NO_AUTOHINT));

	FTFont::FTFont(StringSection<::Assets::ResChar> faceName, int faceSize, FT_Library& library, const FTFontResources::FontLibraryCollection& fontLibraryCollection)
	{
		std::string finalPath = faceName.AsString();
		auto i = fontLibraryCollection._nameMap.find(finalPath);
		if (i != fontLibraryCollection._nameMap.end())
			finalPath = i->second;

		_hashCode = Hash64(finalPath, DefaultSeed64 + faceSize);
		_pBuffer = ::Assets::MainFileSystem::TryLoadFileAsBlob(finalPath);

		auto& depValSys = ::Assets::GetDepValSys();
		_depVal = depValSys.Make();
		depValSys.RegisterFileDependency(_depVal, depValSys.GetDependentFileState(finalPath));
		depValSys.RegisterAssetDependency(_depVal, fontLibraryCollection._depVal);

		if (!_pBuffer)
			Throw(::Assets::Exceptions::ConstructionError(
				::Assets::Exceptions::ConstructionError::Reason::MissingFile,
				_depVal,
				StringMeld<256>() << "Failed to load font (" << finalPath << ")"));

		FT_Face face;
		FT_New_Memory_Face(library, _pBuffer->data(), (FT_Long)_pBuffer->size(), 0, &face);
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
		GlyphProperties _glyphProps;
		std::vector<uint8_t> _renderedBits;
		bool _hasBeenRendered = false;
	};

	auto FTFont::GetGlyphProperties(ucs4 ch) const -> GlyphProperties
	{
		auto i = LowerBound(_cachedLoadedChars, ch);
		if (i == _cachedLoadedChars.end() || i->first != ch) {
			LoadedChar loadedChar;
			FT_Error error = FT_Load_Char(_face.get(), ch, loadFlags);
			if (!error) {
				auto glyph = _face->glyph;
				loadedChar._glyphProps._xAdvance = (float)glyph->advance.x / 64.0f;
				#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
					loadedChar._glyphProps._lsbDelta = glyph->lsb_delta;
					loadedChar._glyphProps._rsbDelta = glyph->rsb_delta;
				#endif
				loadedChar._glyphProps._bitmapOffsetX = glyph->bitmap_left;
				loadedChar._glyphProps._bitmapOffsetY = -glyph->bitmap_top;
				loadedChar._glyphProps._width = glyph->bitmap.width;
				loadedChar._glyphProps._height = glyph->bitmap.rows;
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
					auto glyph = _face->glyph;
					loadedChar._glyphProps._xAdvance = (float)glyph->advance.x / 64.0f;
					#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
						loadedChar._glyphProps._lsbDelta = glyph->lsb_delta;
						loadedChar._glyphProps._rsbDelta = glyph->rsb_delta;
					#endif
					loadedChar._glyphProps._bitmapOffsetX = glyph->bitmap_left;
					loadedChar._glyphProps._bitmapOffsetY = -glyph->bitmap_top;
					loadedChar._glyphProps._width = glyph->bitmap.width;
					loadedChar._glyphProps._height = glyph->bitmap.rows;
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
				auto glyph = _face->glyph;
				loadedChar._glyphProps._xAdvance = (float)glyph->advance.x / 64.0f;
				#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
					loadedChar._glyphProps._lsbDelta = glyph->lsb_delta;
					loadedChar._glyphProps._rsbDelta = glyph->rsb_delta;
				#endif
				loadedChar._glyphProps._bitmapOffsetX = glyph->bitmap_left;
				loadedChar._glyphProps._bitmapOffsetY = -glyph->bitmap_top;
				loadedChar._glyphProps._width = glyph->bitmap.width;
				loadedChar._glyphProps._height = glyph->bitmap.rows;

				auto src = MakeIteratorRange(glyph->bitmap.buffer, PtrAdd(glyph->bitmap.buffer, glyph->bitmap.width*glyph->bitmap.rows));
				loadedChar._renderedBits = std::vector<uint8_t>{src.begin(), src.end()};
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

			auto glyph = _face->glyph;
			auto src = MakeIteratorRange(glyph->bitmap.buffer, PtrAdd(glyph->bitmap.buffer, glyph->bitmap.width*glyph->bitmap.rows));
			i->second._renderedBits = std::vector<uint8_t>{src.begin(), src.end()};
			i->second._hasBeenRendered = true;
		}

		Bitmap result;
		result._xAdvance = i->second._glyphProps._xAdvance;
		result._bitmapOffsetX = i->second._glyphProps._bitmapOffsetX;
		result._bitmapOffsetY = i->second._glyphProps._bitmapOffsetY;
		result._width = i->second._glyphProps._width;
		result._height = i->second._glyphProps._height;
		result._data = MakeIteratorRange(i->second._renderedBits);
		#if XLE_FONT_AUTOHINT_FRACTIONAL_WIDTHS
			result._lsbDelta = i->second._glyphProps._lsbDelta;
			result._rsbDelta = i->second._glyphProps._rsbDelta;
		#endif
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

	namespace Internal
	{
		static void FTFont_ConstructToPromise(
			std::promise<std::shared_ptr<Font>>&& promise,
			StringSection<> path, int size)
		{
			auto res = s_mainFontResourcesInstance.lock();

			::Assets::PtrToMarkerPtr<FTFontResources::FontLibraryCollection> futureFontLibraryCollection;
			{
				ScopedLock(res->_mutex);
				assert(res->_fontLibraryCollection);
				if (res->_fontLibraryCollection->GetDependencyValidation().GetValidationIndex() > 0)
					res->RebuildFontLibraryCollectionAlreadyLocked();
				futureFontLibraryCollection = res->_fontLibraryCollection;
			}

			::Assets::WhenAll(std::move(futureFontLibraryCollection)).ThenConstructToPromise(
				std::move(promise),
				[p=path.AsString(), size, res](auto libCollection) {
					return std::make_shared<FTFont>(p, size, res->_ftLib, *libCollection);
				});
		}
	}

	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> path, int size)
	{
		return ::Assets::GetAssetMarkerFn<Internal::FTFont_ConstructToPromise>(path, size);
	}

	::Assets::PtrToMarkerPtr<Font> MakeFont(StringSection<> pathAndSize)
	{
		auto colon = pathAndSize.end();
		while (colon != pathAndSize.begin() && *(colon-1) != ':' && *(colon-1) != '-') --colon;
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

	static void LoadFontNameMapping(Formatters::TextInputFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules, std::unordered_map<std::string, std::string>& result)
	{
		char buffer[MaxPath];
		StringSection<> name;
		while (formatter.TryKeyedItem(name)) {
			switch (formatter.PeekNext()) {
			case Formatters::FormatterBlob::Value:
				searchRules.ResolveFile(buffer, RequireStringValue(formatter));
				result.insert({name.AsString(), std::string(buffer)});
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

	FontLibraryFile::FontLibraryFile(Formatters::TextInputFormatter<char>& formatter, const ::Assets::DirectorySearchRules& searchRules, const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		auto locale = std::locale("").name();

		StringSection<> name;
		while (formatter.TryKeyedItem(name)) {
			RequireBeginElement(formatter);
			if (XlEqStringI(name, "*")) {
				LoadFontNameMapping(formatter, searchRules, _nameMap);
			} else if (XlEqStringI(name, locale)) {
				LoadFontNameMapping(formatter, searchRules, _nameMap);
			} else {
				SkipElement(formatter);
			}
			RequireEndElement(formatter);
		}
	}

	FTFontResources::FTFontResources()
	{
		FT_Error error = FT_Init_FreeType(&_ftLib);
		if (error)
			Throw(::Exceptions::BasicLabel("Freetype font library failed to initialize (error code: %i)", error));
	}

	FTFontResources::~FTFontResources()
	{
		FT_Done_FreeType(_ftLib);
	}

	void FTFontResources::RebuildFontLibraryCollectionAlreadyLocked()
	{
		std::vector<std::shared_ptr<::Assets::Marker<FontLibraryFile>>> futureFontLibraries;
		futureFontLibraries.reserve(_sourceFontLibraries.size());
		for (auto s:_sourceFontLibraries) futureFontLibraries.emplace_back(::Assets::GetAssetMarker<FontLibraryFile>(s));

		_fontLibraryCollection = std::make_shared<::Assets::MarkerPtr<FontLibraryCollection>>();

		::Assets::PollToPromise(
			_fontLibraryCollection->AdoptPromise(),
			[futureFontLibraries](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				for (auto& f:futureFontLibraries) {
					auto remainingTime = timeoutTime - std::chrono::steady_clock::now();
					if (remainingTime.count() <= 0) return ::Assets::PollStatus::Continue;
					auto t = f->StallWhilePending(std::chrono::duration_cast<std::chrono::microseconds>(remainingTime));
					if (t.value_or(::Assets::AssetState::Pending) == ::Assets::AssetState::Pending)
						return ::Assets::PollStatus::Continue;
				}
				return ::Assets::PollStatus::Finish;
			},
			[futureFontLibraries]() mutable {
				auto result = std::make_shared<FontLibraryCollection>();
				std::vector<::Assets::DependencyValidationMarker> depVals;
				depVals.reserve(futureFontLibraries.size());
				for (auto& m:futureFontLibraries) {
					auto q = m->Actualize()._nameMap;
					result->_nameMap.insert(q.begin(), q.end());
					depVals.emplace_back(m->GetDependencyValidation());
				}
				result->_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);
				return result;
			});
	}

	std::shared_ptr<FTFontResources> CreateFTFontResources() { return std::make_shared<FTFontResources>(); }

	void RegisterFontLibraryFile(StringSection<> path)
	{
		auto resources = s_mainFontResourcesInstance.lock();
		assert(resources);

		ScopedLock(resources->_mutex);
		for (auto i:resources->_sourceFontLibraries) if (XlEqString(path, i)) return;	// already here
		resources->_sourceFontLibraries.emplace_back(path.AsString());

		// recreate the font library collection entirely
		resources->RebuildFontLibraryCollectionAlreadyLocked();
	}

}


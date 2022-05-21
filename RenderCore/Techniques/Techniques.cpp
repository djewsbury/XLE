// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Techniques.h"
#include "Services.h"
#include "../UniformsStream.h"
#include "../IDevice.h"
#include "../Metal/Metal.h"
#include "../../Assets/Assets.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/IFileSystem.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/PtrUtils.h"
#include <algorithm>

namespace RenderCore { namespace Techniques
{
	using Formatter = InputStreamFormatter<utf8>;

	static void LoadInheritedParameterBoxes(
		TechniqueEntry& dst, Formatter& formatter, 
		IteratorRange<const std::pair<uint64_t, TechniqueEntry>*> localSettings,
		const ::Assets::DirectorySearchRules& searchRules,
		std::vector<::Assets::DependencyValidation>& inherited)
	{
			//  We will serialize in a list of 
			//  shareable settings that we can inherit from
			//  Inherit lists should take the form "FileName:Setting"
			//  The "setting" should be a top-level item in the file

		while (formatter.PeekNext() == FormatterBlob::Value) {

			auto value = RequireStringValue(formatter);
		
			auto colon = std::find(value._start, value._end, ':');
			if (colon != value._end) {
				::Assets::ResChar resolvedFile[MaxPath];
				XlCopyNString(resolvedFile, (const ::Assets::ResChar*)value._start, colon-value._start);
				searchRules.ResolveFile(resolvedFile, dimof(resolvedFile), resolvedFile);

				auto& settingsTable = ::Assets::Legacy::GetAssetDep<TechniqueSetFile>(resolvedFile);
				auto settingHash = Hash64(colon+1, value._end);
					
				auto s = LowerBound(settingsTable._settings, settingHash);
				if (s != settingsTable._settings.end() && s->first == settingHash) {
					dst.MergeIn(s->second);
				} else 
					Throw(FormatException("Inheritted object not found", formatter.GetLocation()));

				if (std::find(inherited.begin(), inherited.end(), settingsTable.GetDependencyValidation()) == inherited.end()) {
					inherited.push_back(settingsTable.GetDependencyValidation());
				}
			} else {
				// this setting is in the same file
				auto settingHash = Hash64(value._start, value._end);
				auto s = std::lower_bound(localSettings.begin(), localSettings.end(), settingHash, CompareFirst<uint64_t, TechniqueEntry>());
				if (s != localSettings.end() && s->first == settingHash) {
					dst.MergeIn(s->second);
				} else
					Throw(FormatException("Inheritted object not found", formatter.GetLocation()));
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected blob when deserializing inheritted list", formatter.GetLocation()));
	}

	static void LoadSelectorFiltering(Formatter& formatter, ShaderSourceParser::ManualSelectorFiltering& dst)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem || formatter.PeekNext() == FormatterBlob::Value) {

			if (formatter.PeekNext() == FormatterBlob::Value) {

				// a selector name alone becomes a whitelist setting
				auto selectorName = RequireStringValue(formatter);
				dst._relevanceMap[selectorName.AsString()] = "1";

			} else {
				auto selectorName = RequireKeyedItem(formatter);
				if (formatter.PeekNext() == FormatterBlob::BeginElement) {
					RequireBeginElement(formatter);

					for (;;) {
						if (formatter.PeekNext() == FormatterBlob::EndElement) break;

						auto filterType = RequireKeyedItem(formatter);
						auto value = RequireStringValue(formatter);
						if (XlEqStringI(filterType, "relevance")) {
							dst._relevanceMap[selectorName.AsString()] = value.AsString();
						} else if (XlEqStringI(filterType, "set")) {
							dst._setValues.SetParameter(selectorName, value);
						} else {
							Throw(FormatException("Expecting \"whitelist\", \"blacklist\" or \"set\"", formatter.GetLocation()));
						}
					}

					RequireEndElement(formatter);
				} else {
					auto value = RequireStringValue(formatter);
					dst._setValues.SetParameter(selectorName, value);
				}
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected blob when deserializing selector filtering", formatter.GetLocation()));

		dst.GenerateHash();
	}
	
	static TechniqueEntry ParseTechniqueEntry(
		Formatter& formatter, 
		IteratorRange<const std::pair<uint64_t, TechniqueEntry>*> localSettings,
		const ::Assets::DirectorySearchRules& searchRules, 
		std::vector<::Assets::DependencyValidation>& inherited)
	{
		using ParsingString = std::basic_string<Formatter::value_type>;

		TechniqueEntry result;
		Formatter::InteriorSection name;
		while (formatter.TryKeyedItem(name)) {
			if (XlEqString(name, "Inherit")) {
				RequireBeginElement(formatter);
				LoadInheritedParameterBoxes(result, formatter, localSettings, searchRules, inherited);
				RequireEndElement(formatter);
			} else if (XlEqString(name, "Selectors")) {
				RequireBeginElement(formatter);
				LoadSelectorFiltering(formatter, result._selectorFiltering);
				RequireEndElement(formatter);
			}  else if (XlEqString(name, "VertexShader")) {
				result._vertexShaderName = RequireStringValue(formatter).AsString();
			} else if (XlEqString(name, "PixelShader")) {
				result._pixelShaderName = RequireStringValue(formatter).AsString();
			} else if (XlEqString(name, "GeometryShader")) {
				result._geometryShaderName = RequireStringValue(formatter).AsString();
			} else if (XlEqString(name, "Preconfiguration")) {
				result._preconfigurationFileName = RequireStringValue(formatter).AsString();
			} else {
				Throw(FormatException("Unknown mapped item while reading technique", formatter.GetLocation()));
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected blob when deserializing technique entry", formatter.GetLocation()));

		return result;
	}

	TechniqueSetFile::TechniqueSetFile(
		Utility::InputStreamFormatter<utf8>& formatter, 
		const ::Assets::DirectorySearchRules& searchRules, 
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		std::vector<::Assets::DependencyValidation> inherited;
			
			//  each top-level entry is a "Setting", which can contain parameter
			//  boxes (and possibly inherit statements and shaders)

		Formatter::InteriorSection name;
		while (formatter.TryKeyedItem(name)) {
			RequireBeginElement(formatter);
			if (XlEqString(name, "Inherit") || XlEqString(name, "Technique")) {
				SkipElement(formatter);
			} else {
				auto hash = Hash64(name._start, name._end);
				auto i = LowerBound(_settings, hash);
				_settings.insert(i, std::make_pair(hash, ParseTechniqueEntry(formatter, MakeIteratorRange(_settings), searchRules, inherited)));
			}
			RequireEndElement(formatter);
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected blob while reading stream", formatter.GetLocation()));

		for (auto i=inherited.begin(); i!=inherited.end(); ++i) {
			_depVal.RegisterDependency(*i);
		}
	}

	TechniqueSetFile::~TechniqueSetFile() {}

	const TechniqueEntry* TechniqueSetFile::FindEntry(uint64_t hashName) const
	{
		auto i = LowerBound(_settings, hashName);
		if (i != _settings.end() && i->first == hashName)
			return &i->second;
		return nullptr;
	}

	void TechniqueEntry::MergeIn(const TechniqueEntry& source)
	{
		if (!source._vertexShaderName.empty()) _vertexShaderName = source._vertexShaderName;
		if (!source._pixelShaderName.empty()) _pixelShaderName = source._pixelShaderName;
		if (!source._geometryShaderName.empty()) _geometryShaderName = source._geometryShaderName;
		if (!source._preconfigurationFileName.empty()) _preconfigurationFileName = source._preconfigurationFileName;

		_selectorFiltering._setValues.MergeIn(source._selectorFiltering._setValues);
		for (const auto&i:source._selectorFiltering._relevanceMap)
			_selectorFiltering._relevanceMap[i.first] = i.second;
		_selectorFiltering.GenerateHash();

		GenerateHash();
	}

	void TechniqueEntry::GenerateHash()
	{
		_shaderNamesHash = DefaultSeed64;
		if (!_vertexShaderName.empty())
			_shaderNamesHash = Hash64(_vertexShaderName);
		if (!_pixelShaderName.empty())
			_shaderNamesHash = Hash64(_pixelShaderName, _shaderNamesHash);
		if (!_geometryShaderName.empty())
			_shaderNamesHash = Hash64(_geometryShaderName, _shaderNamesHash);
		if (!_preconfigurationFileName.empty())
			_shaderNamesHash = Hash64(_preconfigurationFileName, _shaderNamesHash);
	}

	template<typename Char>
		static void ReplaceInString(
			std::basic_string<Char>& str, 
			StringSection<Char> oldText, 
			StringSection<Char> newText)
		{
			size_t i = 0;
			for (;;) {
				i = str.find(oldText.begin(), i, oldText.Length());
				if (i == std::basic_string<Char>::npos) return;
				str.replace(i, oldText.Length(), newText.begin());
				i += newText.Length(); // prevent infinite loops if newText contains oldText
			}
		}

	static void ReplaceSelfReference(TechniqueEntry& entry, StringSection<::Assets::ResChar> filename)
	{
		auto selfRef = MakeStringSection("<.>");
		ReplaceInString(entry._vertexShaderName, selfRef, filename);
		ReplaceInString(entry._pixelShaderName, selfRef, filename);
		ReplaceInString(entry._geometryShaderName, selfRef, filename);
		ReplaceInString(entry._preconfigurationFileName, selfRef, filename);
		entry.GenerateHash();
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	T1(Pair) class CompareFirstString
	{
	public:
		bool operator()(const Pair& lhs, const Pair& rhs) const { return XlCompareString(lhs.first, rhs.first) < 0; }
		using Section = StringSection<
			typename std::remove_const<typename std::remove_reference<decltype(*std::declval<typename Pair::first_type>())>::type>::type
			>;
		bool operator()(const Pair& lhs, const Section& rhs) const { return XlCompareString(lhs.first, rhs) < 0; }
		bool operator()(const Section& lhs, const Pair& rhs) const { return XlCompareString(lhs, rhs.first) < 0; }
	};

	static unsigned AsTechniqueIndex(StringSection<utf8> name)
	{
		using Pair = std::pair<const utf8*, unsigned>;
		const Pair bindingNames[] = 
		{
				// note -- lexographically sorted!
			{ "Deferred",                       unsigned(TechniqueIndex::Deferred) },
			{ "DepthOnly",                      unsigned(TechniqueIndex::DepthOnly) },
			{ "DepthWeightedTransparency",      unsigned(TechniqueIndex::DepthWeightedTransparency) },
			{ "Forward",                        unsigned(TechniqueIndex::Forward) },
			{ "OrderIndependentTransparency",   unsigned(TechniqueIndex::OrderIndependentTransparency) },
			{ "PrepareVegetationSpawn",         unsigned(TechniqueIndex::PrepareVegetationSpawn) },
			{ "RayTest",                        unsigned(TechniqueIndex::RayTest) },
			{ "ShadowGen",                      unsigned(TechniqueIndex::ShadowGen) },
			{ "StochasticTransparency",         unsigned(TechniqueIndex::StochasticTransparency) },
			{ "VisNormals",                     unsigned(TechniqueIndex::VisNormals) },
			{ "VisWireframe",                   unsigned(TechniqueIndex::VisWireframe) },
			{ "WriteTriangleIndex",             unsigned(TechniqueIndex::WriteTriangleIndex) }
		};

		auto i = std::lower_bound(bindingNames, ArrayEnd(bindingNames), name, CompareFirstString<Pair>());
		if (XlEqString(name, i->first))
			return i->second;
		return ~0u;
	}

	Technique::Technique(StringSection<::Assets::ResChar> resourceName)
	{
		_validationCallback = ::Assets::GetDepValSys().Make(resourceName);

		TRY {
			size_t sourceFileSize = 0;
			auto sourceFile = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(resourceName, &sourceFileSize);
		
			if (sourceFile && sourceFileSize) {
				auto searchRules = ::Assets::DefaultDirectorySearchRules(resourceName);
				std::vector<::Assets::DependencyValidation> inheritedAssets;

				StringSection<char> configSection(
					(const char*)sourceFile.get(), 
					(const char*)PtrAdd(sourceFile.get(), sourceFileSize));

				Formatter formatter(configSection);
				ParseConfigFile(formatter, resourceName, searchRules, inheritedAssets);

					// Do some patch-up after parsing...
					// we want to replace <.> with the name of the asset
					// This allows the asset to reference itself (without complications
					// for related to directories, etc)
				for (unsigned c=0; c<dimof(_entries); ++c)
					ReplaceSelfReference(_entries[c], resourceName);

				for (auto i=inheritedAssets.begin(); i!=inheritedAssets.end(); ++i)
					_validationCallback.RegisterDependency(*i);
			}
		} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _validationCallback));
		} CATCH(const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _validationCallback));
		} CATCH_END
	}

	Technique::~Technique()
	{}

	void Technique::ParseConfigFile(
		Formatter& formatter, 
		StringSection<::Assets::ResChar> containingFileName,
		const ::Assets::DirectorySearchRules& searchRules,
		std::vector<::Assets::DependencyValidation>& inheritedAssets)
	{
		Formatter::InteriorSection name;
		while (formatter.TryKeyedItem(name)) {
			if (XlEqString(name, "Inherit")) {
				RequireBeginElement(formatter);
				
				// we should find a list of other technique configuation files to inherit from
				while (formatter.PeekNext() == FormatterBlob::Value) {
					auto inheritSrc = RequireStringValue(formatter);
					::Assets::ResChar resolvedFile[MaxPath];
					XlCopyNString(resolvedFile, (const ::Assets::ResChar*)inheritSrc._start, inheritSrc._end-inheritSrc._start);
					searchRules.ResolveFile(resolvedFile, resolvedFile);

					// exceptions thrown by from the inheritted asset will not be suppressed
					const auto& inheritFrom = ::Assets::Legacy::GetAssetDep<Technique>(resolvedFile);
					inheritedAssets.push_back(inheritFrom.GetDependencyValidation());

					// we should merge in the content from all the inheritted's assets
					for (unsigned c=0; c<dimof(_entries); ++c)
						_entries[c].MergeIn(inheritFrom._entries[c]);
					_cbLayout = inheritFrom._cbLayout;
				}

				RequireEndElement(formatter);
			} else if (XlEqString(name, "Technique")) {
				RequireBeginElement(formatter);

				// We should find a list of the actual techniques to use, as attributes
				// The attribute name defines the how to apply the technique, and the attribute value is
				// the name of the technique itself
				while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
					auto attribName = RequireKeyedItem(formatter);
					auto value = RequireStringValue(formatter);

					if (XlEqString(attribName, "CBLayout")) {
						_cbLayout = RenderCore::Assets::PredefinedCBLayout(value, searchRules, _validationCallback);
					} else {
						auto index = AsTechniqueIndex(attribName);
						if (index != ~0) {		// (silent failure if the technique name is unknown)
							StringSection<::Assets::ResChar> containerName, settingName;
							const auto* colon = XlFindChar(value, ':');
							if (colon) { 
								containerName = MakeStringSection(value.begin(), colon);
								settingName = MakeStringSection(colon+1, value.end());
							} else {
								containerName = containingFileName;
								settingName = value;
							}

							const auto& setFile = ::Assets::Legacy::GetAssetDep<TechniqueSetFile>(containerName);
							auto hash = Hash64(settingName);
							auto i = LowerBound(setFile._settings, hash);
							if (i != setFile._settings.end() && i->first == hash) {
								_entries[index] = i->second;		// (don't merge in; this a replace)
							} else 
								Throw(FormatException("Could not resolve requested technique setting", formatter.GetLocation()));

							if (std::find(inheritedAssets.begin(), inheritedAssets.end(), setFile.GetDependencyValidation()) == inheritedAssets.end()) {
								inheritedAssets.push_back(setFile.GetDependencyValidation());
							}
						}
					}
				}

				RequireEndElement(formatter);
			} else if (XlEqString(name, "*")) {
				RequireBeginElement(formatter);

				// This is an override that applies to all techniques in this file
				auto overrideTechnique = ParseTechniqueEntry(formatter, {}, searchRules, inheritedAssets);
				for (unsigned c=0; c<dimof(_entries); ++c) 
					_entries[c].MergeIn(overrideTechnique);

				RequireEndElement(formatter);
			} else {
				SkipValueOrElement(formatter);
			}
		}

		if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected blob while reading stream", formatter.GetLocation()));
	}

	TechniqueEntry& Technique::GetEntry(unsigned idx)
	{
		assert(idx < dimof(_entries));
		return _entries[idx];
	}

	const TechniqueEntry& Technique::GetEntry(unsigned idx) const
	{
		assert(idx < dimof(_entries));
		return _entries[idx];
	}

				//////////////////////-------//////////////////////

	UnderlyingAPI GetTargetAPI()
	{
		#if GFXAPI_TARGET == GFXAPI_VULKAN
			return RenderCore::UnderlyingAPI::Vulkan;
		#elif GFXAPI_TARGET == GFXAPI_OPENGLES
			return RenderCore::UnderlyingAPI::OpenGLES;
		#elif GFXAPI_TARGET == GFXAPI_APPLEMETAL
			return RenderCore::UnderlyingAPI::AppleMetal;
		#else
			return RenderCore::UnderlyingAPI::DX11;
		#endif
	}

	static thread_local std::weak_ptr<IThreadContext> s_mainThreadContext;
	std::shared_ptr<IThreadContext> GetThreadContext()
	{
		auto threadContext = s_mainThreadContext.lock();
		if (!threadContext) {
			threadContext = Services::GetInstance().GetDevice().CreateDeferredContext();
			s_mainThreadContext = threadContext;
		}
		return threadContext;
	}

	void SetThreadContext(const std::shared_ptr<IThreadContext>& threadContext)
	{
		s_mainThreadContext = threadContext;
	}

}}


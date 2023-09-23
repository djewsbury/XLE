// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Utility/Meta/ClassAccessorsImpl.h"
#include <vector>
#include <optional>

namespace SceneEngine
{
	template<typename Type>
		class ObjectTable
	{
	public:
		std::vector<std::pair<uint64_t, Type>> _objects;

		template<typename Formatter>
			std::optional<uint64_t> DeserializeObject(Formatter& fmttr);

		struct PendingProperty { StringSection<> _name; IteratorRange<const void*> _data; ImpliedTyping::TypeDesc _typeDesc; };
		std::optional<uint64_t> DeserializeObject(StringSection<> name, IteratorRange<const PendingProperty*> properties);

	private:
		uint64_t _nextUnnamed = 1; 
	};

	struct LightOperatorResolveContext
	{
		ObjectTable<RenderCore::LightingEngine::LightSourceOperatorDesc> _lightSourceOperators;
		ObjectTable<RenderCore::LightingEngine::ShadowOperatorDesc> _shadowOperators;
		ObjectTable<RenderCore::LightingEngine::AmbientLightOperatorDesc> _ambientOperators;
		ObjectTable<RenderCore::LightingEngine::ForwardLightingTechniqueDesc> _forwardLightingOperators;
		ObjectTable<RenderCore::LightingEngine::DeferredLightingTechniqueDesc> _deferredLightingOperators;
		ObjectTable<RenderCore::LightingEngine::UtilityLightingTechniqueDesc> _utilityLightingOperator;
		ObjectTable<RenderCore::LightingEngine::ToneMapAcesOperatorDesc> _toneMapAcesOperators;
		ObjectTable<RenderCore::LightingEngine::MultiSampleOperatorDesc> _multiSampleOperators;
		ObjectTable<RenderCore::LightingEngine::SkyOperatorDesc> _skyOperators;
		ObjectTable<RenderCore::LightingEngine::SkyTextureProcessorDesc> _skyTextureProcessors;
		ObjectTable<RenderCore::LightingEngine::ScreenSpaceReflectionsOperatorDesc> _ssr;
		ObjectTable<RenderCore::LightingEngine::AmbientOcclusionOperatorDesc> _ssao;
		ObjectTable<RenderCore::LightingEngine::TAAOperatorDesc> _taaOperator;

		template<typename Formatter>
			void Deserialize(Formatter& fmttr) 
		{ 
			uint64_t name;
			while (TryKeyedItem(fmttr, name)) {
				switch (name) {
				case "LightSource"_h:
					RequireBeginElement(fmttr);
					_lightSourceOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "Shadow"_h:
					RequireBeginElement(fmttr);
					_shadowOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "Ambient"_h:
					RequireBeginElement(fmttr);
					_ambientOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "ForwardLighting"_h:
					RequireBeginElement(fmttr);
					_forwardLightingOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "DeferredLighting"_h:
					RequireBeginElement(fmttr);
					_deferredLightingOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "UtilityLighting"_h:
					RequireBeginElement(fmttr);
					_utilityLightingOperator.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "ToneMapAces"_h:
					RequireBeginElement(fmttr);
					_toneMapAcesOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "MultiSample"_h:
					RequireBeginElement(fmttr);
					_multiSampleOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "TAA"_h:
					RequireBeginElement(fmttr);
					_taaOperator.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "Sky"_h:
					RequireBeginElement(fmttr);
					_skyOperators.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "SkyTextureProcessor"_h:
					RequireBeginElement(fmttr);
					_skyTextureProcessors.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "ScreenSpaceReflections"_h:
					RequireBeginElement(fmttr);
					_ssr.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				case "ScreenSpaceAmbientOcclusion"_h:
					RequireBeginElement(fmttr);
					_ssao.DeserializeObject(fmttr);
					RequireEndElement(fmttr);
					break;
				default:
					SkipValueOrElement(fmttr);
					break;
				}
			}
		}
	};

	template<typename Type>
		template<typename Formatter>
			std::optional<uint64_t> ObjectTable<Type>::DeserializeObject(Formatter& fmttr)
	{
		StringSection<> keyname;
		std::vector<PendingProperty> properties; 
		StringSection<> objectName;
		while (fmttr.TryKeyedItem(keyname)) {
			if (XlEqString(keyname, "Name")) {
				objectName = RequireStringValue(fmttr);
			} else {
				if constexpr (Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue) {
					ImpliedTyping::TypeDesc typeDesc;
					IteratorRange<const void*> data;
					if (fmttr.TryRawValue(data, typeDesc)) {
						properties.push_back({keyname, data, typeDesc});
					} else
						SkipValueOrElement(fmttr);
				} else {
					StringSection<> strValue;
					if (fmttr.TryStringValue(strValue)) {
						properties.push_back({keyname, strValue, ImpliedTyping::TypeDesc{ImpliedTyping::TypeOf<const char*>()._type, (uint16_t)strValue.size(), ImpliedTyping::TypeHint::String}});
					} else
						SkipValueOrElement(fmttr);
				}
			}
		}
		return DeserializeObject(objectName, MakeIteratorRange(properties));
	}

	template<typename Type>
		std::optional<uint64_t> ObjectTable<Type>::DeserializeObject(StringSection<> name, IteratorRange<const PendingProperty*> properties)
	{
		uint64_t objectNameHash = 0;
		if (!name.IsEmpty()) objectNameHash = Hash64(name);
		else objectNameHash = _nextUnnamed++;

		auto existing = LowerBound(_objects, objectNameHash);
		if (existing == _objects.end() || existing->first != objectNameHash)
			existing = _objects.insert(existing, std::make_pair(objectNameHash, Type{}));

		for (const auto& p:properties) {
			if (expect_evaluation(XlEqString(p._name, "ObjectTableCmd") && p._typeDesc._typeHint == ImpliedTyping::TypeHint::String && (p._typeDesc._type == ImpliedTyping::TypeCat::UInt8 || p._typeDesc._type == ImpliedTyping::TypeCat::Int8), false)) {
				auto value = MakeStringSection((const char*)p._data.begin(), (const char*)p._data.end());
				if (XlEqString(value, "Delete")) {
					_objects.erase(existing);
					return {};
				}
				continue;
			}
			SetProperty(existing->second, Hash64(p._name), p._data, p._typeDesc);
		}
		return objectNameHash;
	}

	template<typename Type, typename Formatter, typename std::enable_if<!Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue>::type* =nullptr>
		static void DeserializeViaAccessors(Formatter& fmttr, Type& obj)
	{
		auto& accessors = Legacy_GetAccessors<Type>();
		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname)) {
			StringSection<> keyvalue;
			if (fmttr.TryStringValue(keyvalue)) {
				accessors.SetFromString(&obj, keyname, keyvalue);
			} else
				SkipValueOrElement(fmttr);
		}
	}

	template<typename Type, typename Formatter, typename std::enable_if<Formatters::Internal::FormatterTraits<Formatter>::HasTryRawValue>::type* =nullptr>
		static void DeserializeViaAccessors(Formatter& fmttr, Type& obj)
	{
		auto& accessors = Legacy_GetAccessors<Type>();
		StringSection<> keyname;
		while (fmttr.TryKeyedItem(keyname)) {
			ImpliedTyping::TypeDesc typeDesc;
			IteratorRange<const void*> data;
			if (fmttr.TryRawValue(data, typeDesc)) {
				accessors.Set(&obj, keyname, data, typeDesc);
			} else
				SkipValueOrElement(fmttr);
		}
	}

}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../RenderCore/LightingEngine/StandardLightOperators.h"
#include "../../RenderCore/LightingEngine/ShadowPreparer.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Meta/ClassAccessorsImpl.h"
#include <vector>

namespace SceneEngine
{
	template<typename Type>
		class ObjectTable
	{
	public:
		std::vector<std::pair<uint64_t, Type>> _objects;
		template<typename Formatter>
			uint64_t DeserializeObject(Formatter& fmttr);
	private:
		uint64_t _nextUnnamed = 1; 
	};

	struct LightOperatorResolveContext
	{
		ObjectTable<RenderCore::LightingEngine::LightSourceOperatorDesc> _lightSourceOperators;
		ObjectTable<RenderCore::LightingEngine::ShadowOperatorDesc> _shadowOperators;
		ObjectTable<RenderCore::LightingEngine::AmbientLightOperatorDesc> _ambientOperators;

		template<typename Formatter>
			void DeserializeLightSourceOperator(Formatter& fmttr) { _lightSourceOperators.DeserializeObject(fmttr); }
		template<typename Formatter>
			void DeserializeShadowOperator(Formatter& fmttr) { _shadowOperators.DeserializeObject(fmttr); }
		template<typename Formatter>
			void DeserializeAmbientOperator(Formatter& fmttr) { _ambientOperators.DeserializeObject(fmttr); }
	};

	template<typename Type>
		template<typename Formatter>
			uint64_t ObjectTable<Type>::DeserializeObject(Formatter& fmttr)
	{
		auto& accessors = Legacy_GetAccessors<Type>();
		StringSection<> keyname;
		std::vector<std::pair<StringSection<>, StringSection<>>> properties; 
		Type object;
		uint64_t objectName = 0;
		while (fmttr.TryKeyedItem(keyname)) {
			if (XlEqString(keyname, "Name")) {
				auto name = RequireStringValue(fmttr);
				objectName = Hash64(name);
			} else {
				StringSection<> keyvalue;
				if (fmttr.TryStringValue(keyvalue)) {
					properties.emplace_back(keyname, keyvalue);
				} else
					SkipValueOrElement(fmttr);
			}
		}

		if (!objectName)
			objectName = _nextUnnamed++;;

		auto existing = LowerBound(_objects, objectName);
		if (existing == _objects.end() || existing->first != objectName)
			existing = _objects.insert(existing, std::make_pair(objectName, Type{}));

		for (const auto& p:properties)
			accessors.SetFromString(&existing->second, p.first, p.second);

		return objectName;
	}

	template<typename Type, typename Formatter>
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

}

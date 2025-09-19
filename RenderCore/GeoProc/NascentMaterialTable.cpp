#include "NascentMaterialTable.h"
#include "Formatters/TextOutputFormatter.h"

namespace RenderCore { namespace Assets { namespace GeoProc
{

	void SerializationOperator(Formatters::TextOutputFormatter& fmttr, const NascentMaterialTable& table)
	{
		std::vector<std::string> names;
		names.reserve(table._rawMaterials.size());
		for (const auto& t:table._rawMaterials) names.emplace_back(t.first);
		std::sort(b2e(names));
		names.erase(std::unique(b2e(names)), names.end());
		for (auto n:names) fmttr.WriteKeyedValue("Entity", n);

		for (auto n:names) {
			if (auto rm = table._rawMaterials.find(n); rm!=table._rawMaterials.end()) {
				auto e = fmttr.BeginKeyedElement("RawMaterial", n);
				fmttr << rm->second;
				fmttr.EndElement(e);
			}
		}
	}

}}}

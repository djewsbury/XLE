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
			if (auto rm = std::find_if(b2e(table._rawMaterials), [n](const auto& q) { return q.first == n; }); rm!=table._rawMaterials.end()) {
				auto& inheritList = std::get<1>(rm->second);
				if (!inheritList.empty()) {
					auto e = fmttr.BeginKeyedElement("Inherit", n);
					for (const auto& i:inheritList) fmttr.WriteSequencedValue(i);
					fmttr.EndElement(e);
				}

				auto e = fmttr.BeginKeyedElement("RawMaterial", n);
				fmttr << std::get<0>(rm->second);
				fmttr.EndElement(e);
			}
		}
	}

}}}

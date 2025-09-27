// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/RawMaterial.h"
#include <vector>
#include <string>

namespace Formatters { class TextOutputFormatter; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
	class NascentMaterialTable
	{
	public:
		using Entry = std::tuple<RawMaterial, ::Assets::InheritList>;
		std::vector<std::pair<std::string, Entry>> _rawMaterials;

		void AddMaterial(std::string name, const RawMaterial& mat, const ::Assets::InheritList& ih)
		{
			auto i = std::find_if(b2e(_rawMaterials), [name](const auto& q) { return name == q.first; });
			assert(i == _rawMaterials.end());
			_rawMaterials.emplace_back(std::move(name), std::make_tuple(mat, ih));
		}

		void AddMaterial(std::string name, const RawMaterial& mat)
		{
			auto i = std::find_if(b2e(_rawMaterials), [name](const auto& q) { return name == q.first; });
			assert(i == _rawMaterials.end());
			_rawMaterials.emplace_back(std::move(name), std::make_tuple(mat, ::Assets::InheritList{}));
		}

		friend void SerializationOperator(Formatters::TextOutputFormatter&, const NascentMaterialTable&);
	};
}}}
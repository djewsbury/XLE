// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/RawMaterial.h"
#include <map>
#include <string>

namespace Formatters { class TextOutputFormatter; }

namespace RenderCore { namespace Assets { namespace GeoProc
{
	class NascentMaterialTable
	{
	public:
		std::map<std::string, RenderCore::Assets::RawMaterial> _rawMaterials;

		friend void SerializationOperator(Formatters::TextOutputFormatter&, const NascentMaterialTable&);
	};
}}}
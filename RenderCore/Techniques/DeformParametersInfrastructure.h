// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DeformAccelerator.h"

namespace RenderCore { namespace Assets { class ModelScaffold; }}

namespace RenderCore { namespace Techniques
{
	std::shared_ptr<IDeformParametersAttachment> CreateDeformParametersAttachment(
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName = {});
}}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "Drawables.h"
#include "../Metal/Forward.h"

namespace RenderCore { namespace Techniques
{
    void Draw(
		RenderCore::Metal::DeviceContext& metalContext,
		RenderCore::Metal::GraphicsEncoder_Optimized& encoder,
        ParsingContext& parserContext,
		const IPipelineAcceleratorPool& pipelineAccelerators,
		const SequencerConfig& sequencerConfig,
		SequencerUniformsHelper& uniformsHelper,
		const DrawablesPacket& drawablePkt);

	class sequencerUniformsHelper;
	void ApplyLooseUniforms(
		SequencerUniformsHelper& sequencerUniformsHelper,
		Metal::DeviceContext& metalContext,
		Metal::SharedEncoder& encoder,
		ParsingContext& parsingContext,
		Metal::BoundUniforms&,
		unsigned groupIndex);
}}

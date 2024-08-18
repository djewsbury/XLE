#pragma once

#include "../Utility/IteratorUtils.h"
#include <string>

namespace ShaderSourceParser
{
	class InstantiatedShader;

	// If the given patches are part of a sprite pipeline, generate the structure
	// that should go around it
	//
	// We track attributes backwards through the pipeline -- from the inputs of the pixel
	// shader back through GS, VS and IA.
	//
	// Patches of the same shader type (VS, GS, etc) are allowed to modify the same attribute
	// -- in these cases, the patches are applied in the order they appear in "patches"
	// 
	InstantiatedShader BuildSpritePipeline(
		const InstantiatedShader& patches,
		IteratorRange<const std::string*> iaAttributes);
}


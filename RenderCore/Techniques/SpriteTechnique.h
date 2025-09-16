#pragma once

#include "ShaderPatchInstantiationUtil.h"
#include "../Types.h"
#include "../../Utility/IteratorUtils.h"
#include <string>
#include <memory>

namespace GraphLanguage { class NodeGraphSignature; }

namespace RenderCore { namespace Techniques
{
	struct PatchDelegateInput
	{
		std::string _name;										// name of the function to call
		const GraphLanguage::NodeGraphSignature* _signature;	// signature of the patch
		uint64_t _implementsHash = ~0ull;
	};

	struct PatchDelegateOutput
	{
		ShaderStage _stage;
		std::unique_ptr<GraphLanguage::NodeGraphSignature> _entryPointSignature;
		ShaderCompilePatchResource _resource;
	};

	// If the given patches are part of a sprite pipeline, generate the structure
	// that should go around it
	//
	// We track attributes backwards through the pipeline -- from the inputs of the pixel
	// shader back through GS, VS and IA.
	//
	// Patches of the same shader type (VS, GS, etc) are allowed to modify the same attribute
	// -- in these cases, the patches are applied in the order they appear in "patches"
	// 
	std::vector<PatchDelegateOutput> BuildSpritePipeline(
		IteratorRange<const PatchDelegateInput*> patches,
		IteratorRange<const uint64_t*> iaAttributes);

	std::vector<PatchDelegateOutput> BuildAutoPipeline(
		IteratorRange<const PatchDelegateInput*> patches,
		IteratorRange<const uint64_t*> iaAttributes);
}}

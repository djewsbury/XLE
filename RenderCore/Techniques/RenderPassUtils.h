// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "RenderPass.h"
#include "../FrameBufferDesc.h"		// (for LoadStore)
#include <memory>

namespace RenderCore { class IThreadContext; class IResource; }

namespace RenderCore { namespace Techniques
{
	class RenderPassInstance;
	class ParsingContext;

	RenderPassInstance RenderPassToPresentationTarget(
        ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);

	RenderPassInstance RenderPassToPresentationTarget(
		const std::shared_ptr<RenderCore::IResource>& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);

	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
        ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);
	
	RenderPassInstance RenderPassToPresentationTargetWithDepthStencil(
		const std::shared_ptr<RenderCore::IResource>& presentationTarget,
        ParsingContext& parserContext,
		LoadStore loadOperation = LoadStore::Retain,
		unsigned clearColor = 0xff000000);
}}

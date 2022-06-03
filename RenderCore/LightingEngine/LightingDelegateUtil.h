// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "LightingEngineInternal.h"
#include "Assets/Marker.h"
#include <memory>

namespace RenderCore { namespace Techniques
{
	class FrameBufferPool;
	class AttachmentPool;
	class IShaderResourceDelegate;
}}

namespace RenderCore { namespace LightingEngine
{
	class IPreparedShadowResult;
	class IPreparable;
	class ShadowProbes;
}}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase;
	std::shared_ptr<IPreparedShadowResult> SetupShadowPrepare(
		LightingTechniqueIterator& iterator,
		LightingTechniqueSequence& sequence,
		ILightBase& proj,
		ILightScene& lightScene, ILightScene::LightSourceId associatedLightId,
		PipelineType descSetPipelineType,
		Techniques::FrameBufferPool& shadowGenFrameBufferPool,
		Techniques::AttachmentPool& shadowGenAttachmentPool);

	::Assets::MarkerPtr<Techniques::IShaderResourceDelegate> CreateBuildGBufferResourceDelegate();

	std::shared_ptr<IPreparable> CreateShadowProbePrepareDelegate(std::shared_ptr<ShadowProbes> shadowProbes, IteratorRange<const ILightScene::LightSourceId*> associatedLights, ILightScene* lightScene);
}}}


// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../DeviceInitialization.h"
#include "../../Utility/ParameterBox.h"
#include <vector>

namespace RenderCore { class UniformsStreamInterface; class IThreadContext; }

namespace RenderCore { namespace Techniques
{
	struct SelectorStages { enum Enum { Geometry, GlobalEnvironment, Runtime, Material, Max }; };


		//////////////////////////////////////////////////////////////////
			//      C O N T E X T                                   //
		//////////////////////////////////////////////////////////////////
	
	class IUniformDelegateManager;
	class IAttachmentPool;
	class IFrameBufferPool;
	class CommonResourceBox;
	class IDrawablesPool;
	class PipelineCollection;
	class IPipelineAcceleratorPool;
	class IDeformAcceleratorPool;
	class SemiConstantDescriptorSet;
	class SystemUniformsDelegate;

	class TechniqueContext
	{
	public:
		ParameterBox _globalEnvironmentState;

		std::shared_ptr<IAttachmentPool> _attachmentPool;
		std::shared_ptr<IFrameBufferPool> _frameBufferPool;
		std::shared_ptr<CommonResourceBox> _commonResources;
		std::shared_ptr<IDrawablesPool> _drawablesPool;
		std::shared_ptr<IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<IDeformAcceleratorPool> _deformAccelerators;
		std::shared_ptr<PipelineCollection> _graphicsPipelinePool;

		std::shared_ptr<SemiConstantDescriptorSet> _graphicsSequencerDS;
		std::shared_ptr<SemiConstantDescriptorSet> _computeSequencerDS;
		std::shared_ptr<SystemUniformsDelegate> _systemUniformsDelegate;

		std::vector<Format> _systemAttachmentFormats;
	};

	UnderlyingAPI GetTargetAPI();

	std::shared_ptr<IThreadContext> GetThreadContext();
	std::weak_ptr<IThreadContext> SetThreadContext(std::weak_ptr<IThreadContext>);

}}


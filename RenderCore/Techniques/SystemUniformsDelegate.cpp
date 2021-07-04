// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SystemUniformsDelegate.h"
#include "CommonResources.h"
#include "ParsingContext.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
	void SystemUniformsDelegate::WriteImmediateData(ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst)
	{
		switch (idx) {
		case 0:
			if (context.GetEnablePrevProjectionDesc())
				*(GlobalTransformConstants*)dst.begin() = BuildGlobalTransformConstants(context.GetProjectionDesc(), context.GetPrevProjectionDesc());
			else
				*(GlobalTransformConstants*)dst.begin() = BuildGlobalTransformConstants(context.GetProjectionDesc());
			break;
		case 1:
			*(LocalTransformConstants*)dst.begin() = _localTransformFallback;
			break;
		case 2:
			*(ViewportConstants*)dst.begin() = BuildViewportConstants(context.GetViewport());
			break;
		}
	}

	size_t SystemUniformsDelegate::GetImmediateDataSize(ParsingContext& context, const void* objectContext, unsigned idx)
	{
		switch (idx) {
		case 0:
			return sizeof(GlobalTransformConstants);
		case 1:
			return sizeof(LocalTransformConstants);
		case 2:
			return sizeof(ViewportConstants);
		default:
			return 0;
		}
	}

	const UniformsStreamInterface& SystemUniformsDelegate::GetInterface() { return _interface; }

	void SystemUniformsDelegate::WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst)
	{
		assert(dst.size() == 4);
		for (unsigned c=0; c<dimof(_samplers); ++c)
			dst[c] = _samplers[c].get();
	}

	SystemUniformsDelegate::SystemUniformsDelegate(IDevice& device, CommonResourceBox& commonResources)
	{
		_interface.BindImmediateData(0, Hash64("GlobalTransform"));
		_interface.BindImmediateData(1, Hash64("LocalTransform"));
		_interface.BindImmediateData(2, Hash64("ReciprocalViewportDimensionsCB"));

		XlZeroMemory(_localTransformFallback);
		_localTransformFallback._localToWorld = Identity<Float3x4>();
		_localTransformFallback._localSpaceView = Float3(0.f, 0.f, 0.f);

		_interface.BindSampler(0, Hash64("DefaultSampler"));
		_samplers[0] = commonResources._defaultSampler;
		_interface.BindSampler(1, Hash64("ClampingSampler"));
		_samplers[1] = commonResources._linearClampSampler;
		_interface.BindSampler(2, Hash64("AnisotropicSampler"));
		_samplers[2] = commonResources._defaultSampler;
		_interface.BindSampler(3, Hash64("PointClampSampler"));
		_samplers[3] = commonResources._pointClampSampler;
	}

	SystemUniformsDelegate::~SystemUniformsDelegate()
	{
	}
}}

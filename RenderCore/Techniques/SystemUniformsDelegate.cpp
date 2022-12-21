// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SystemUniformsDelegate.h"
#include "CommonResources.h"
#include "ParsingContext.h"
#include "../../Utility/MemoryUtils.h"

using namespace Utility::Literals;

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

	void SystemUniformsDelegate::WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst)
	{
		for (unsigned c=0; c<dimof(_samplers); ++c)
			if (bindingFlags & (1ull<<uint64_t(c))) {
				assert(c < dst.size());
				dst[c] = _samplers[c].get();
			}
	}

	SystemUniformsDelegate::SystemUniformsDelegate(IDevice& device)
	{
		BindImmediateData(0, "GlobalTransform"_h);
		BindImmediateData(1, "LocalTransform"_h);
		BindImmediateData(2, "ReciprocalViewportDimensionsCB"_h);

		XlZeroMemory(_localTransformFallback);
		_localTransformFallback._localToWorld = Identity<Float3x4>();
		_localTransformFallback._localSpaceView = Float3(0.f, 0.f, 0.f);
	}

	SystemUniformsDelegate::~SystemUniformsDelegate()
	{
	}
}}

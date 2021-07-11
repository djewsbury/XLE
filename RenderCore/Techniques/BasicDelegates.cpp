// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if 0

#include "BasicDelegates.h"
#include "ShaderVariationSet.h"
#include "ParsingContext.h"
#include "DeferredShaderResource.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Metal/InputLayout.h"
#include "../BufferView.h"
#include "../../Assets/Assets.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { namespace Techniques
{

    void GlobalCBDelegate::WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst)
	{
		// return context.GetGlobalCB(_cbIndex);
		assert(0);
	}

	size_t GlobalCBDelegate::GetSize()
	{
		return 0;
	}

    IteratorRange<const ConstantBufferElementDesc*> GlobalCBDelegate::GetLayout()
	{
		return {};
	}

}}

#endif
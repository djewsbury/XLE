// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if 0

#pragma once

#include "DrawableDelegates.h"

namespace RenderCore { namespace Assets { class PredefinedCBLayout; }}

namespace RenderCore { namespace Techniques
{
	class GlobalCBDelegate : public IUniformBufferDelegate
    {
    public:
        void WriteImmediateData(ParsingContext& context, const void* objectContext, IteratorRange<void*> dst) override;
        size_t GetSize() override;
        IteratorRange<const ConstantBufferElementDesc*> GetLayout() override;
		GlobalCBDelegate(unsigned cbIndex = 0) : _cbIndex(cbIndex) {}
	private:
		unsigned _cbIndex = 0;
    };
}}

#endif
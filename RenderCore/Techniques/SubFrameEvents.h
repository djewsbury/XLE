// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/FunctionUtils.h"

namespace RenderCore { class IThreadContext; }
namespace RenderCore { namespace Techniques
{
	class SubFrameEvents
	{
	public:
		Signal<IThreadContext&> _onPrePresent;
		Signal<IThreadContext&> _onPostPresent;
		Signal<> _onFrameBarrier;

		// _onCheckCompleteInitialization is invoked very infrequently,
		// but once after early startup before rendering the first frame
		// The parameter will always be the main foreground thread context,
		// and it can be used to invoke Metal::CompleteInitialization
		Signal<IThreadContext&> _onCheckCompleteInitialization;
	};
}}

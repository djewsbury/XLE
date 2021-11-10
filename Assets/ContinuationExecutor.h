// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "thousandeyes/futures/Executor.h"
#include "thousandeyes/futures/PollingExecutor.h"
#include "thousandeyes/futures/detail/InvokerWithNewThread.h"
#include "thousandeyes/futures/detail/InvokerWithSingleThread.h"

namespace Assets
{
	struct InvokerToThreadPool
	{
		template<typename Fn>
			void operator()(Fn&& f)
		{
			_threadPool->Enqueue(std::move(f));
		}

		InvokerToThreadPool(ThreadPool& threadPool) : _threadPool(&threadPool) {}
	private:
		ThreadPool* _threadPool;
	};

	using ContinuationExecutor = thousandeyes::futures::PollingExecutor<
		thousandeyes::futures::detail::InvokerWithNewThread,
		InvokerToThreadPool>;
}

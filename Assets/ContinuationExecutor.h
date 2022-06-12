// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/Threading/LockFree.h"
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

	/// <summary>Polling executor with a finite worst case service time</summary>
	/// Drop in replacement to thousandeyes::futures::PollingExecutor that will spawn up threads proportional to the
	/// number of continuations in the system. Continuations are evenly divided between the threads in such a way
	/// that worst case service delay will be less than s_waitablesPerPage * check time.
	template<class TPollFunctor, class TDispatchFunctor>
		class BalancingPollingExecutor : public thousandeyes::futures::Executor, public std::enable_shared_from_this<BalancingPollingExecutor<TPollFunctor, TDispatchFunctor>>
	{
	public:
		static constexpr unsigned s_waitablesPerPage = 256;
		static constexpr unsigned s_leaveToDrainThreshold = 16;

		void watch(std::unique_ptr<thousandeyes::futures::Waitable> w) override final
		{
			if (!active_) {
				cancel_(std::move(w), "Executor inactive");
				return;
			}

			auto* page = _nextPage.load();

			{
				while (!page->waitables_.push(std::move(w))) {
					std::unique_lock<std::mutex> l2{pageManagementMutex_};
					if (_nextPage == page) {	// another thread may have done this already
						// find a page that is draining, or create a new one
						for (auto& p:_pages) {
							if (p.get() == page) continue;
							if (p->waitables_.size() <= s_leaveToDrainThreshold)
								page = p.get();
						}
						if (page == _nextPage) {    // ie, didn't find a draining page
							_pages.emplace_back(std::make_unique<Page>());
							page = (_pages.end()-1)->get();
						}
						_nextPage = page;
					} else {
						page = _nextPage;
					}
				}

				bool startPoller = !page->isPollerRunning_.exchange(true);

				auto queueSize = page->waitables_.size();
				if (queueSize >= (2 * s_leaveToDrainThreshold) && (queueSize % 32) == 31) {
					// Update smallest page again (requires some locks, so don't do this every time)
					// However, don't check if we're near the s_leaveToDrainThreshold threshold, because this
					// may result in us only building up pages to this threshold and then jumping to a new one
					auto* bestPage = page;
					auto bestPageCount = queueSize;
					std::unique_lock<std::mutex> l2{pageManagementMutex_};
					for (auto& p:_pages) {
						if (p.get() == page) continue;
						auto q = p->waitables_.size();
						// At the s_leaveToDrainThreshold threshold, we just let this queue drain out and go inactive
						if (q > s_leaveToDrainThreshold && q < bestPageCount) {
							bestPageCount = q;
							bestPage = p.get();
						}
					}
					_nextPage.store(bestPage);
				}

				if (!startPoller) return;
			}

			(*pollFunc_)([this, page, keep=this->shared_from_this()]() {
				while (true) {
					std::unique_ptr<thousandeyes::futures::Waitable> w;
					{
						std::unique_ptr<thousandeyes::futures::Waitable>* ptr;
						if (!active_ || !page->waitables_.try_front(ptr)) {
							page->isPollerRunning_.store(false);
							break;
						}

						w = std::move(*ptr);
						page->waitables_.pop();	// pop now, because we push onto the end
					}

					TRY {
						if (!w->wait(q_)) {
							if (!page->waitables_.push(std::move(w)))
								this->watch(std::move(w));		// can't fit it back in the same queue, try to put it anywhere
							continue;
						}

						dispatch_(std::move(w), nullptr);
					}
					CATCH (...) {
						dispatch_(std::move(w), std::current_exception());
					} CATCH_END
				}
			});
		}

		void stop() override final
		{
			active_ = false;
			// note -- we must *never* lock pageManagementMutex_ after active_is has done false, otherwise
			// we can end up with a deadlock on shutdown here
			{
				std::unique_lock<std::mutex> l2{pageManagementMutex_};
				for (const auto& page:_pages) {
					// wait until poller thread is finished, then cancel everything left in the queue
					while (page->isPollerRunning_.load())
						std::this_thread::sleep_for(q_);

					std::unique_ptr<thousandeyes::futures::Waitable>* ptr;
					while (page->waitables_.try_front(ptr)) {
						cancel_(std::move(*ptr), "Executor stoped");
						page->waitables_.pop();
					}
				}
			}
		}

		BalancingPollingExecutor(std::chrono::microseconds q)
		: q_(std::move(q))
		, pollFunc_(std::make_unique<TPollFunctor>())
		, dispatchFunc_(std::make_unique<TDispatchFunctor>())
		{}

		BalancingPollingExecutor(std::chrono::microseconds q,
						TPollFunctor&& pollFunc,
						TDispatchFunctor&& dispatchFunc)
		: q_(std::move(q))
		, pollFunc_(std::make_unique<TPollFunctor>(std::forward<TPollFunctor>(pollFunc)))
		, dispatchFunc_(std::make_unique<TDispatchFunctor>(std::forward<TDispatchFunctor>(dispatchFunc)))
		{
			_pages.reserve(16);
			_pages.emplace_back(std::make_unique<Page>());
			_nextPage.store(_pages.begin()->get());
		}

		~BalancingPollingExecutor()
		{
			stop();
			pollFunc_.reset();
			dispatchFunc_.reset();
		}

		BalancingPollingExecutor(const BalancingPollingExecutor& o) = delete;
		BalancingPollingExecutor& operator=(const BalancingPollingExecutor& o) = delete;

	private:
		inline void dispatch_(std::unique_ptr<thousandeyes::futures::Waitable> w, std::exception_ptr error)
		{
			(*dispatchFunc_)([w=std::move(w), error=std::move(error)]() { w->dispatch(error); });
		}

		inline void cancel_(std::unique_ptr<thousandeyes::futures::Waitable> w, const std::string& message)
		{
			auto error = std::make_exception_ptr(thousandeyes::futures::WaitableWaitException(message));
			dispatch_(std::move(w), std::move(error));
		}

		const std::chrono::microseconds q_;

		struct Page
		{
			LockFreeFixedSizeQueue<std::unique_ptr<thousandeyes::futures::Waitable>, s_waitablesPerPage> waitables_;
			std::atomic<bool> isPollerRunning_{ false };
		};

		std::mutex pageManagementMutex_;
		std::vector<std::unique_ptr<Page>> _pages;
		std::atomic<Page*> _nextPage = nullptr;

		volatile bool active_{ true };

		std::unique_ptr<TPollFunctor> pollFunc_;
		std::unique_ptr<TDispatchFunctor> dispatchFunc_;
	};

	using ContinuationExecutor = BalancingPollingExecutor<
		thousandeyes::futures::detail::InvokerWithNewThread,
		InvokerToThreadPool>;
}

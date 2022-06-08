// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IDeviceVulkan.h"
#include "../IAnnotator.h"
#include "../Utility/Threading/Mutex.h"
#include "../Core/SelectConfiguration.h"
#include "../Core/Types.h"
#include <vector>
#include <deque>
#include <assert.h>

#include "Metal/QueryPool.h"
#include "Metal/DeviceContext.h"
#include "Metal/ObjectFactory.h"

#include "../../Foreign/RenderDoc/renderdoc_app.h"

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
	#include "../../OSServices/WinAPI/IncludeWindows.h"
#endif

namespace RenderCore { namespace ImplVulkan
{
	namespace Metal = RenderCore::Metal_Vulkan;

	class AnnotatorImpl : public IAnnotator
	{
	public:
		void    Event(const char name[], EventTypes::BitField types) override;
		void    Frame_Begin(unsigned frameId) override;
		void    Frame_End() override;
		void	FlushFinishedQueries(Metal::DeviceContext& context);

		unsigned	AddEventListener(const EventListener& callback) override;
		void		RemoveEventListener(unsigned id) override;

		bool		IsCaptureToolAttached() override;
		void		BeginFrameCapture() override;
		void		EndFrameCapture() override;

		AnnotatorImpl(Metal::ObjectFactory&, std::weak_ptr<IThreadContext>);
		~AnnotatorImpl();

	protected:
		struct EventInFlight
		{
			const char* _name;
			Metal::TimeStampQueryPool::QueryId _queryIndex;
			EventTypes::Flags _type;
			unsigned _queryFrameId;
		};

		struct QueryFrame
		{
			Metal::TimeStampQueryPool::FrameId _queryFrameId;
			unsigned _renderFrameId;
			Metal_Vulkan::IAsyncTracker::Marker _commandListMarker;
		};

		std::deque<EventInFlight> _eventsInFlight;
		std::deque<QueryFrame> _framesInFlight;

		Metal::TimeStampQueryPool _queryPool;
		Metal::TimeStampQueryPool::FrameId _currentQueryFrameId;

		unsigned _currentRenderFrameId;
		signed _frameRecursionDepth;

		Threading::Mutex _listeners_Mutex;
		std::vector<std::pair<unsigned, EventListener>> _listeners;
		unsigned _nextListenerId;

		std::weak_ptr<IThreadContext> _threadContext;
		std::shared_ptr<Metal_Vulkan::IAsyncTracker> _asyncTracker;
	};

	//////////////////////////////////////////////////////////////////

	void    AnnotatorImpl::Event(const char name[], EventTypes::BitField types)
	{
		auto context = _threadContext.lock();
		if (!context) return;

		auto& metalContext = *Metal::DeviceContext::Get(*context);
		if (types & EventTypes::MarkerBegin) {
			Metal::GPUAnnotation::Begin(metalContext, name);
		} else if (types & EventTypes::MarkerEnd) {
			Metal::GPUAnnotation::End(metalContext);
		}

		if (!(types & (EventTypes::ProfileBegin|EventTypes::ProfileEnd)))
			return;

        if (_currentQueryFrameId == Metal::TimeStampQueryPool::FrameId_Invalid)
            return;

		EventInFlight newEvent;
		newEvent._name = name;
		newEvent._type = (EventTypes::Flags)types;
		newEvent._queryIndex = _queryPool.SetTimeStampQuery(metalContext);
		newEvent._queryFrameId = _currentQueryFrameId;
		_eventsInFlight.push_back(newEvent);
	}

	void    AnnotatorImpl::Frame_Begin(unsigned frameId)
	{
		auto context = _threadContext.lock();
		if (!context) return;

		auto& metalContext = *Metal::DeviceContext::Get(*context);
		FlushFinishedQueries(metalContext);

		++_frameRecursionDepth;
		if (_currentQueryFrameId != Metal::TimeStampQueryPool::FrameId_Invalid || (_frameRecursionDepth>1)) {
			assert(_currentQueryFrameId != Metal::TimeStampQueryPool::FrameId_Invalid && (_frameRecursionDepth>1));
			return;
		}

		_currentQueryFrameId = _queryPool.BeginFrame(metalContext);
		_currentRenderFrameId = frameId;
	}

	void    AnnotatorImpl::Frame_End()
	{
		auto context = _threadContext.lock();
		if (!context) return;
		auto& metalContext = *Metal::DeviceContext::Get(*context);

		--_frameRecursionDepth;
		if (_frameRecursionDepth == 0) {
			if (_currentQueryFrameId != Metal::TimeStampQueryPool::FrameId_Invalid) {
				QueryFrame frameInFlight;
				frameInFlight._queryFrameId = _currentQueryFrameId;
				frameInFlight._renderFrameId = _currentRenderFrameId;
				frameInFlight._commandListMarker = _asyncTracker->GetProducerMarker();
				_framesInFlight.push_back(frameInFlight);
				_queryPool.EndFrame(metalContext, _currentQueryFrameId);

				_currentQueryFrameId = Metal::TimeStampQueryPool::FrameId_Invalid;
				_currentRenderFrameId = ~unsigned(0);
			}
		}
	}

	static size_t AsListenerType(IAnnotator::EventTypes::BitField types)
	{
		if (types & IAnnotator::EventTypes::ProfileEnd) return 1;
		return 0;
	}

	void AnnotatorImpl::FlushFinishedQueries(Metal::DeviceContext& context)
	{
		//
		//      Look for finished queries, and remove them from the
		//      "in-flight" list
		//

		auto asyncConsumerMarker = _asyncTracker->GetConsumerMarker();
		while (!_framesInFlight.empty()) {
			QueryFrame& frameInFlight = *_framesInFlight.begin();
			// Avoid calling GetFrameResults() until we know the command list has been queued and executed. We won't get
			// valid results back from the queries, anyway, and we don't want to test the query before it's even been set
			// by the cmd list
			// In other words, GetFrameResults operates on the device, while setting/resetting queries operators on the cmdlist
			if (frameInFlight._commandListMarker > asyncConsumerMarker) break;
			auto results = _queryPool.GetFrameResults(context, frameInFlight._queryFrameId);
			if (!results._resultsReady) return;

			uint64 evntBuffer[2048 / sizeof(uint64)];
			byte* eventBufferPtr = (byte*)evntBuffer;
			const byte* eventBufferEnd = (const byte*)&evntBuffer[dimof(evntBuffer)];

			{
				ScopedLock(_listeners_Mutex);
				//      Write an event to set the frequency. We should expect the frequency should be constant
				//      in a single play through, but it doesn't hurt to keep recording it...
				const size_t entrySize = sizeof(size_t) * 2 + sizeof(uint64);
				if (size_t(eventBufferPtr) + entrySize > size_t(eventBufferEnd)) {
					for (auto i = _listeners.begin(); i != _listeners.end(); ++i) {
						(i->second)(evntBuffer, eventBufferPtr);
					}
					eventBufferPtr = (byte*)evntBuffer;
				}

				*((size_t*)eventBufferPtr) = ~size_t(0x0);                  eventBufferPtr += sizeof(size_t);
				*((size_t*)eventBufferPtr) = frameInFlight._renderFrameId;  eventBufferPtr += sizeof(size_t);
				*((uint64*)eventBufferPtr) = results._frequency;			eventBufferPtr += sizeof(uint64);
			}

			//
			//      We've sucessfully completed this "disjoint" query.
			//      The other queries related to this frame should be finished now.
			//      Let's get their data (though, if the disjoint flag is set, we'll ignore the data)
			//
			unsigned thisFrameId = frameInFlight._queryFrameId;
			while (!_eventsInFlight.empty() && _eventsInFlight.begin()->_queryFrameId == thisFrameId) {
				const auto& evnt = *_eventsInFlight.begin();
				auto timeResult = results._resultsStart[evnt._queryIndex];
				if (!results._isDisjoint) {
					ScopedLock(_listeners_Mutex);
					//
					//      Write an event into out buffer to represent this
					//      occurrence. If we can't fit it in; we need to flush it out 
					//      and continue on.
					//
					const size_t entrySize = sizeof(size_t) * 2 + sizeof(uint64);
					if (size_t(eventBufferPtr) + entrySize > size_t(eventBufferEnd)) {
						for (auto i = _listeners.begin(); i != _listeners.end(); ++i) {
							(i->second)(evntBuffer, eventBufferPtr);
						}
						eventBufferPtr = (byte*)evntBuffer;
					}
					*((size_t*)eventBufferPtr) = AsListenerType(evnt._type); eventBufferPtr += sizeof(size_t);
					*((size_t*)eventBufferPtr) = size_t(evnt._name); eventBufferPtr += sizeof(size_t);
					// assert(size_t(eventBufferPtr)%sizeof(uint64)==0);
					*((uint64*)eventBufferPtr) = uint64(timeResult); eventBufferPtr += sizeof(uint64);
				}

				_eventsInFlight.pop_front();
			}

			_framesInFlight.pop_front();

			//  Flush out any remaining entries in the event buffer...
			//  Note, this will insure that event if 2 frames worth of events
			//  complete in the single FlushFinishedQueries() call, we will never
			//  fill the event listener with a mixture of events from multiple frames.
			if (eventBufferPtr != (byte*)evntBuffer) {
				ScopedLock(_listeners_Mutex);
				for (auto i = _listeners.begin(); i != _listeners.end(); ++i) {
					(i->second)(evntBuffer, eventBufferPtr);
				}
			}
		}
	}

	unsigned AnnotatorImpl::AddEventListener(const EventListener& callback)
	{
		ScopedLock(_listeners_Mutex);
		auto id = _nextListenerId++;
		_listeners.push_back(std::make_pair(id, callback));
		return id;
	}

	void AnnotatorImpl::RemoveEventListener(unsigned id)
	{
		ScopedLock(_listeners_Mutex);
		auto i = std::find_if(_listeners.begin(), _listeners.end(), 
			[id](const std::pair<unsigned, EventListener>& p) { return p.first == id; });
		if (i != _listeners.end())
			_listeners.erase(i);
	}

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
	static RENDERDOC_API_1_1_2 *s_rdoc_api = nullptr;
	static bool s_attemptedRenderDocAttach = false;

	static void RenderDoc_Attach()
	{
		if (s_attemptedRenderDocAttach) return;
		if(HMODULE mod = GetModuleHandleA("renderdoc.dll"))
		{
			s_rdoc_api = nullptr;
			pRENDERDOC_GetAPI RENDERDOC_GetAPI =
				(pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
			int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&s_rdoc_api);
			assert(ret == 1);
		}
		s_attemptedRenderDocAttach = true;
	}

	bool AnnotatorImpl::IsCaptureToolAttached()
	{
		RenderDoc_Attach();
		return s_rdoc_api != nullptr;
	}
	void AnnotatorImpl::BeginFrameCapture()
	{
		RenderDoc_Attach();
		if(s_rdoc_api) {
			auto tc = _threadContext.lock();
			if (tc)
				tc->CommitCommands(0);
			s_rdoc_api->StartFrameCapture(nullptr, nullptr);
		}
	}
	void AnnotatorImpl::EndFrameCapture()
	{
		RenderDoc_Attach();
		if(s_rdoc_api) {
			auto tc = _threadContext.lock();
			if (tc)
				tc->CommitCommands(0);
			s_rdoc_api->EndFrameCapture(nullptr, nullptr);
		}
	}
#else
	// todo -- we could support renderdoc for other OSs as well (particularlly as it's so useful on Android)
	bool AnnotatorImpl::IsCaptureToolAttached() { return false; }
	void AnnotatorImpl::BeginFrameCapture() {}
	void AnnotatorImpl::EndFrameCapture() {}
#endif

	AnnotatorImpl::AnnotatorImpl(Metal::ObjectFactory& factory, std::weak_ptr<IThreadContext> threadContext)
	: _queryPool(factory)
	, _threadContext(std::move(threadContext))
	{
		auto tc = _threadContext.lock();
		assert(tc);
		auto vulkanDevice = (IDeviceVulkan*)tc->GetDevice()->QueryInterface(typeid(IDeviceVulkan).hash_code());
		assert(vulkanDevice);
		_asyncTracker = vulkanDevice->GetAsyncTracker();

		_currentRenderFrameId = ~unsigned(0);
		_frameRecursionDepth = 0;
		_currentQueryFrameId = Metal::TimeStampQueryPool::FrameId_Invalid;
		_nextListenerId = 0;
	}

	AnnotatorImpl::~AnnotatorImpl()
	{
	}

	std::unique_ptr<IAnnotator> CreateAnnotator(IDevice& device, std::weak_ptr<IThreadContext> threadContext)
	{
		return std::make_unique<AnnotatorImpl>(Metal::GetObjectFactory(device), threadContext);
	}

}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SILENCE_CXX17_RESULT_OF_DEPRECATION_WARNING

#include "../../OSServices/PollingThread.h"
#include "../../OSServices/FileSystemMonitor.h"
#include "../../OSServices/RawFS.h"
#include "../../OSServices/TimeUtils.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/Threading/CompletionThreadPool.h"
#include "catch2/catch_test_macros.hpp"
#include "catch2/catch_approx.hpp"
#include "thousandeyes/futures/then.h"
#include "thousandeyes/futures/DefaultExecutor.h"
#include <stdexcept>
#include <iostream>
#include <random>
#include <filesystem>
#include <functional>

#if PLATFORMOS_TARGET == PLATFORMOS_LINUX
    // linux specific...
    #include "../../OSServices/Linux/System_Linux.h"
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <unistd.h>
#endif

using namespace Catch::literals;
namespace UnitTests
{
    TEST_CASE( "PollingThread-UnderlyingInterface", "[osservices]" )
    {
        OSServices::PollingThread pollingThread;

        auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
        thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

        SECTION("RespondOnce with stall")
        {
            auto testEvent = CreateUserEvent(OSServices::UserEvent::Type::Binary);
            
            // Here, the event trigger is going to happen before we call RespondOnce
            testEvent->IncreaseCounter();
            
            auto resString = thousandeyes::futures::then(
                pollingThread.RespondOnce(testEvent),
                [](auto) {
                    return std::string{"String returned from future"};
                }).get();

            REQUIRE(resString == "String returned from future");
        }
        
        SECTION("RespondOnce with continuation")
        {
            auto testEvent = CreateUserEvent(OSServices::UserEvent::Type::Binary);
            volatile bool trigger = false;
            auto future = thousandeyes::futures::then(
                pollingThread.RespondOnce(testEvent),
                [&trigger](auto) {
                    trigger = true;
                    return std::string{"String returned from future"};
                });

            Threading::Sleep(1000);
            testEvent->IncreaseCounter();

            auto resString = future.get();
            REQUIRE(resString == "String returned from future");
            REQUIRE(trigger == true);
        }

        SECTION("Event reset")
        {
            // Ensuring that events are getting reset after usage correctly.
            // If events are correctly getting reset to their unsignaled state
            // after they have been signalled; then this should take around 5500
            // milliseconds to complete. However, if they don't get reset, it will
            // be much faster -- perhaps around 500 milliseconds (or we could even
            // get a crash, because we aren't waiting for the background threads to finish)
            auto startTime = std::chrono::steady_clock::now();

            auto event = OSServices::CreateUserEvent(OSServices::UserEvent::Type::Binary);
            std::thread{
                [&]() {
                    Threading::Sleep(500); 
                    event->IncreaseCounter(); }}.detach();

            unsigned iterations = 10;
            for (unsigned i=0; i<iterations; ++i) {
                pollingThread.RespondOnce(event).wait();
                std::thread{
                    [&]() {
                        Threading::Sleep(500); 
                        event->IncreaseCounter(); }}.detach();
            }

            // wait for the last one -- 
            pollingThread.RespondOnce(event).wait();

            auto elapsed = std::chrono::steady_clock::now() - startTime;
            REQUIRE(OSServices::AsMilliseconds(elapsed) > 5000);
            std::cout << "Event reset test took " << OSServices::AsMilliseconds(elapsed) << " milliseconds" << std::endl;
        }

        SECTION("Thrash RespondOnce")
        {
            // This is a horrible nightmare of beginning and ending RespondOnce. It should really
            // give the PollingThread implementation a good test

            const unsigned iterations = 1000;

            std::atomic<signed> eventsInFlight(0);
            Threading::Mutex eventsLock;
            std::vector<std::shared_ptr<OSServices::UserEvent>> events;
            std::vector<std::shared_ptr<OSServices::UserEvent>> pendingTriggerEvents;
            std::deque<std::shared_ptr<OSServices::UserEvent>> eventPool;
            std::vector<std::future<unsigned>> futures;

            auto onTrigger = [&](OSServices::UserEvent* triggeredHandle, const std::future<std::any>&) {
                --eventsInFlight;
                ScopedLock(eventsLock);
                auto i = std::find_if(
                    pendingTriggerEvents.begin(), pendingTriggerEvents.end(), 
                    [triggeredHandle](auto& e) { return e.get() == triggeredHandle; });
                assert(i != pendingTriggerEvents.end());
                eventPool.push_back(std::move(*i));
                pendingTriggerEvents.erase(i);
                return 0u;
            };
            using namespace std::placeholders;

            // Windows has a very low number of events that can be waited on from a single thread
            // (only 64). We have to start spawning new threads to wait on more events than that.
            // However; this doesn't appear to apply to completion routines...?
            events.reserve(60);
            for (unsigned c=0; c<60; ++c) {
                auto event = OSServices::CreateUserEvent(OSServices::UserEvent::Type::Binary);
                ++eventsInFlight;
                auto future = thousandeyes::futures::then(
                    pollingThread.RespondOnce(event),
                    std::bind(onTrigger, event.get(), _1));
                futures.push_back(std::move(future));
                events.push_back(std::move(event));
            }

            std::mt19937 rng;
            for (unsigned c=0; c<iterations; ++c) {
                Threading::Sleep(1);

                ScopedLock(eventsLock);
                auto eventsToEnd = std::uniform_int_distribution<unsigned>(0, 5)(rng);
                auto eventsToBegin = std::uniform_int_distribution<unsigned>(0, 5)(rng);

                for (auto e=0; e<eventsToEnd && !events.empty(); ++e) {
                    auto i = events.begin() + std::uniform_int_distribution<unsigned>(0, events.size()-1)(rng);
                    pendingTriggerEvents.push_back(std::move(*i));
                    events.erase(i);
                    (*(pendingTriggerEvents.end()-1))->IncreaseCounter();
                }

                for (auto e=0; e<eventsToBegin; ++e) {
                    int* front = nullptr;
                    if (eventPool.empty())
                        break;

                    auto reusableEvent = std::move(*eventPool.begin());
                    eventPool.erase(eventPool.begin());

                    ++eventsInFlight;
                    auto future = thousandeyes::futures::then(
                        pollingThread.RespondOnce(reusableEvent),
                        std::bind(onTrigger, reusableEvent.get(), _1));
                    futures.push_back(std::move(future));
                    events.push_back(std::move(reusableEvent));
                }
            }

            // finish remaining events
            {
                ScopedLock(eventsLock);
                while (!events.empty()) {
                    pendingTriggerEvents.push_back(std::move(*(events.end()-1)));
                    events.erase(events.end()-1);
                    (*(pendingTriggerEvents.end()-1))->IncreaseCounter();
                }
            }

            for (auto&f:futures)
                f.get();

            REQUIRE(eventsInFlight.load() == 0);
        }

#if PLATFORMOS_TARGET == PLATFORMOS_LINUX
        SECTION("Conduit for eventfd")
        {
            class EventFDConduit : public OSServices::IConduitProducer, public OSServices::IConduitProducer_PlatformHandle
            {
            public:
                int _platformHandle = 0;

                OSServices::IOPlatformHandle GetPlatformHandle() const override { return _platformHandle; }
                OSServices::PollingEventType::BitField GetListenTypes() const override { return OSServices::PollingEventType::Input; }
		        std::any GeneratePayload(OSServices::PollingEventType::BitField) override 
                { 
                    uint64_t eventFdCounter=0;
                    auto ret = read(_platformHandle, &eventFdCounter, sizeof(eventFdCounter));
                    assert(ret > 0);
                    return eventFdCounter;
                }

                EventFDConduit()
                {
                    _platformHandle = eventfd(0, EFD_NONBLOCK);
                }

                ~EventFDConduit()
                {
                    close(_platformHandle);
                }
            };

            class ConduitConsumer : public OSServices::IConduitConsumer
            {
            public:
                int _eventCount = 0;
                int _exceptionCount = 0;

                void OnEvent(std::any&& payload) override
                {                    
                    _eventCount += std::any_cast<uint64_t>(payload);
                }

		        void OnException(const std::exception_ptr& exception) override
                {
                    ++_exceptionCount;
                }
            };

            auto conduit = std::make_shared<EventFDConduit>();
            auto consumer = std::make_shared<ConduitConsumer>();
            auto connectionFuture = pollingThread.Connect(conduit, consumer);
            connectionFuture.get();            

            const unsigned writeCount = 15;
            for (unsigned c=0; c<writeCount; ++c) {
                uint64_t t = 1;
                write(conduit->_platformHandle, &t, sizeof(t));
            }

            auto disconnectionFuture = pollingThread.Disconnect(conduit);
            disconnectionFuture.get();

            REQUIRE(consumer->_exceptionCount == 0);
            REQUIRE(consumer->_eventCount == writeCount);
        }
#endif
    }

    TEST_CASE( "PollingThread-FileChangeNotification", "[osservices]" )
    {
        auto executor = std::make_shared<thousandeyes::futures::DefaultExecutor>(std::chrono::milliseconds(2));
        thousandeyes::futures::Default<thousandeyes::futures::Executor>::Setter execSetter(executor);

        auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";
        std::filesystem::create_directories(tempDirPath);

        {
            auto pollingThread = std::make_shared<OSServices::PollingThread>();
            class CountChanges : public OSServices::OnChangeCallback
            {
            public:
                signed _changes = 0;
                void OnChange() override { ++_changes; }
            };

            OSServices::RawFSMonitor monitor(pollingThread);
            auto changesToOne = std::make_shared<CountChanges>();
            monitor.Attach(MakeStringSection(tempDirPath.string() + "/one.txt"), changesToOne);

            auto changesToTwo = std::make_shared<CountChanges>();
            monitor.Attach(MakeStringSection(tempDirPath.string() + "/two.txt"), changesToTwo);

            auto changesToThree = std::make_shared<CountChanges>();
            monitor.Attach(MakeStringSection(tempDirPath.string() + "/three.txt"), changesToThree);

            SECTION("Detect file writes")
            {
                char strToWrite[] = "This is a string written by XLE unit tests";
                OSServices::BasicFile{(tempDirPath.string() + "/one.txt").c_str(), "wb", 0}.Write(strToWrite, 1, sizeof(strToWrite));
                OSServices::BasicFile{(tempDirPath.string() + "/three.txt").c_str(), "wb", 0}.Write(strToWrite, 1, sizeof(strToWrite));

                // give a little bit of time incase the background thread needs to catchup to all of the writes
                Threading::Sleep(1000);
                REQUIRE(changesToOne->_changes > 0);
                REQUIRE(changesToTwo->_changes == 0);
                REQUIRE(changesToThree->_changes > 0);
                auto midwayChangesToThree = changesToThree->_changes;

                OSServices::BasicFile{(tempDirPath.string() + "/two.txt").c_str(), "wb", 0}.Write(strToWrite, 1, sizeof(strToWrite));
                OSServices::BasicFile{(tempDirPath.string() + "/three.txt").c_str(), "wb", 0}.Write(strToWrite, 1, sizeof(strToWrite));
                Threading::Sleep(1000);
                REQUIRE(changesToTwo->_changes > 0);
                REQUIRE(changesToThree->_changes > midwayChangesToThree);
            }
        }

        // note that we don't want the RawFSMonitor to still be alive when we do this (because it will end up triggering everything again!)
        std::filesystem::remove_all(tempDirPath);
    }

    class InstanceCountingObject
    {
    public:
        static std::atomic<int> s_instanceCount;
        bool _openInstance = true;
        InstanceCountingObject() { ++s_instanceCount; }
        ~InstanceCountingObject() { if (_openInstance) --s_instanceCount; }

        InstanceCountingObject(const InstanceCountingObject&) { ++s_instanceCount; }
        InstanceCountingObject(InstanceCountingObject&& moveFrom) { _openInstance = moveFrom._openInstance; moveFrom._openInstance = false; }

        InstanceCountingObject& operator=(const InstanceCountingObject& copyFrom) 
        { 
            if (_openInstance) --s_instanceCount;
            _openInstance = copyFrom._openInstance;
            if (_openInstance) ++s_instanceCount; 
            return *this;
        }

        InstanceCountingObject& operator=(InstanceCountingObject&& moveFrom) 
        { 
            if (_openInstance) --s_instanceCount;
            _openInstance = moveFrom._openInstance; 
            moveFrom._openInstance = false; 
            return *this;
        }
    };

    std::atomic<int> InstanceCountingObject::s_instanceCount{0};

    TEST_CASE( "ThreadPool-DestructionRules", "[osservices]" )
    {
        // Ensure that functions queued in the thread pool are getting destructors called correctly
        ThreadPool threadPool(4);

        SECTION("captured smart ptr") 
        {
            for (unsigned c=0; c<1024; ++c) {
                auto ptr = std::make_shared<InstanceCountingObject>();
                threadPool.Enqueue(
                    [ptr]() {
                        REQUIRE(ptr->_openInstance);
                    });
            }

            for (unsigned c=0; c<2048; ++c) {
                auto ptr = std::make_shared<InstanceCountingObject>();
                threadPool.Enqueue(
                    [](auto p) { 
                        REQUIRE(p->_openInstance);
                    }, 
                    ptr);
            }

            threadPool.StallAndDrainQueue();

            REQUIRE(InstanceCountingObject::s_instanceCount.load() == 0);
        }

        SECTION("captured by value") 
        {
            for (unsigned c=0; c<1024; ++c) {
                InstanceCountingObject obj;
                threadPool.Enqueue(
                    [obj=std::move(obj)]() {
                        REQUIRE(obj._openInstance);
                    });
            }

            for (unsigned c=0; c<2048; ++c) {
                InstanceCountingObject obj;
                threadPool.Enqueue(
                    [](auto obj) {
                        REQUIRE(obj._openInstance);
                    }, 
                    obj);
            }

            threadPool.StallAndDrainQueue();

            REQUIRE(InstanceCountingObject::s_instanceCount.load() == 0);
        }
    }
}

// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CompileAndAsyncManager.h"
#include "IntermediatesStore.h"
#include "IntermediateCompilers.h"
#include "../OSServices/Log.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Core/SelectConfiguration.h"
#include "../Core/Exceptions.h"
#include <set>
#include <string>
#include <sstream>
#include <filesystem>

namespace Assets
{

        ////////////////////////////////////////////////////////////

    IPollingAsyncProcess::IPollingAsyncProcess() {}
    IPollingAsyncProcess::~IPollingAsyncProcess() {}

        ////////////////////////////////////////////////////////////

	class CompileAndAsyncManager::Pimpl
	{
	public:
		std::shared_ptr<IntermediatesStore> _intStore;
		std::shared_ptr<IntermediatesStore> _shadowingStore;
        std::unique_ptr<IntermediateCompilers> _intMan;
		std::vector<std::shared_ptr<IPollingAsyncProcess>> _pollingProcesses;

		Utility::Threading::Mutex _pollingProcessesLock;
	};

    void CompileAndAsyncManager::Update()
    {
        if (_pimpl->_pollingProcessesLock.try_lock()) {
            TRY
            {
                    //  Normally the polling processes will be waiting for the thread
                    //  pump to complete something. So do this after the thread pump update
		        for (auto i = _pimpl->_pollingProcesses.begin(); i != _pimpl->_pollingProcesses.end();) {
                    bool remove = false;
                    TRY {
                        auto result = (*i)->Update();
                        remove = result == IPollingAsyncProcess::Result::Finish;
                    } CATCH(const std::exception& e) {
                        Log(Warning) << "Got exception during polling process update: " << e.what() << std::endl;
                        remove = true;
						(void)e;
                    } CATCH_END

                            // remove if necessary...
			        if (remove) { i = _pimpl->_pollingProcesses.erase(i); }
                    else { ++i; }
                }
            } CATCH (...) {
                _pimpl->_pollingProcessesLock.unlock();
                throw;
            } CATCH_END
            _pimpl->_pollingProcessesLock.unlock();
        }
    }

    void CompileAndAsyncManager::Add(const std::shared_ptr<IPollingAsyncProcess>& pollingProcess)
    {
		ScopedLock(_pimpl->_pollingProcessesLock);
		_pimpl->_pollingProcesses.push_back(pollingProcess);
    }

    IntermediateCompilers& CompileAndAsyncManager::GetIntermediateCompilers() 
    { 
		return *_pimpl->_intMan;
    }

	const std::shared_ptr<IntermediatesStore>&	CompileAndAsyncManager::GetIntermediateStore() 
    { 
		return _pimpl->_intStore;
    }

	const std::shared_ptr<IntermediatesStore>&	CompileAndAsyncManager::GetShadowingStore()
	{
		return _pimpl->_shadowingStore;
	}

    CompileAndAsyncManager::CompileAndAsyncManager()
    {
            // todo --  this version string can be used to differentiate different
            //          versions of the compiling tools. This is important when we
            //          need to switch back and forth between different versions
            //          quickly. This can happen if we have different format 
            //          resources for debug and release. Or, while building the
            //          some new format, it's often useful to be able to compare
            //          to previous version. Also, there are times when we want to
            //          run an old version of the engine, and we don't want it to
            //          conflict with more recent work, even if there have been
            //          major changes in the meantime.
        const char storeVersionString[] = "0.0.0";
        #if defined(_DEBUG)
            #if TARGET_64BIT
                const char storeConfigString[] = "d64";
            #else
                const char storeConfigString[] = "d";
            #endif
        #else
            #if TARGET_64BIT
                const char storeConfigString[] = "r64";
            #else
                const char storeConfigString[] = "r";
            #endif
        #endif

        auto tempDirPath = std::filesystem::temp_directory_path() / "xle-unit-tests";

		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_intStore = std::make_shared<IntermediatesStore>(tempDirPath.string().c_str(), storeVersionString, storeConfigString);
		_pimpl->_shadowingStore = std::make_shared<IntermediatesStore>(tempDirPath.string().c_str(), storeVersionString, storeConfigString, true);

		_pimpl->_intMan = std::make_unique<IntermediateCompilers>(_pimpl->_intStore);
    }

    CompileAndAsyncManager::~CompileAndAsyncManager()
    {
            // note -- this order is important. The compiler set
            // can make use of the IntermediatesStore during
            // it's destructor (eg, when flushing an archive cache to disk). 
        _pimpl->_intMan.reset();
        _pimpl->_intStore.reset();
    }

}


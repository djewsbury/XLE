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

	class CompileAndAsyncManager::Pimpl
	{
	public:
		std::shared_ptr<IntermediatesStore> _intStore;
		std::shared_ptr<IntermediatesStore> _shadowingStore;
        std::shared_ptr<IIntermediateCompilers> _intMan;
	};

    IIntermediateCompilers& CompileAndAsyncManager::GetIntermediateCompilers() 
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

    CompileAndAsyncManager::CompileAndAsyncManager(std::shared_ptr<IFileSystem> intermediatesFilesystem)
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
        if (intermediatesFilesystem) {
            _pimpl->_intStore = std::make_shared<IntermediatesStore>(intermediatesFilesystem, tempDirPath.string(), storeVersionString, storeConfigString);
            _pimpl->_shadowingStore = std::make_shared<IntermediatesStore>(intermediatesFilesystem, tempDirPath.string(), storeVersionString, storeConfigString, true);
        } else {
            _pimpl->_intStore = std::make_shared<IntermediatesStore>();
        }

		_pimpl->_intMan = CreateIntermediateCompilers(_pimpl->_intStore);
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


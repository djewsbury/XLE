// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../AttachableLibrary.h"
#include "../../ConsoleRig/GlobalServices.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../RawFS.h"
#include <string>
#include <sstream>
#include <assert.h>

#include "IncludeWindows.h"
#include "System_WinAPI.h"
#include "WinAPIWrapper.h"

namespace OSServices
{
    typedef HMODULE LibraryHandle;
    static const LibraryHandle LibraryHandle_Invalid = (LibraryHandle)INVALID_HANDLE_VALUE;

    class AttachableLibrary::Pimpl
    {
    public:
        signed _attachCount;
        std::basic_string<CharType> _filename;
        LibraryHandle _library;
        LibVersionDesc _dllVersion;
        bool _versionInfoValid;
    };

    bool AttachableLibrary::TryAttach(std::string& errorMsg)
    {
        if (!_pimpl->_attachCount) {
            assert(_pimpl->_library == LibraryHandle_Invalid);
            _pimpl->_library = (*OSServices::Windows::Fn_LoadLibrary)(_pimpl->_filename.c_str());

            if (!_pimpl->_library) _pimpl->_library = LibraryHandle_Invalid;

                // if LoadLibrary failed, the attach must also fail
                // this is most often caused by a missing dll file
            if (_pimpl->_library == LibraryHandle_Invalid) {

				auto errorCode = GetLastError();
				std::stringstream errorMsgStream;
				switch (errorCode) {
				case 126:
					errorMsgStream << "Could not attach library (" << _pimpl->_filename << ") because of error code 126, which usually means a subdependency of the DLL couldn't be found or loaded";
					break;
				case 193:
					errorMsgStream << "Could not attach library (" << _pimpl->_filename << ") because of error code 192, which usually means that the platform or instruction set for the DLL doesn't match this executable (for example, 64 bit app loading 32 bit dll)";
					break;
				default:
					errorMsgStream << "Could not attach library (" << _pimpl->_filename << ") because of error code " << errorCode << ", which is unknown but translates to (" << OSServices::SystemErrorCodeAsString(errorCode) << ")";
					break;
				}

				errorMsg = errorMsgStream.str();
                return false;
			}

                // Look for an "AttachLibrary" function, and call it.
                // Also, call the "GetVersionInformation" function.
                // If either is missing, we still succeed. The AttachLibrary
                // function is only required for dlls that want to use our
                // global services (like logging, console, etc)
            auto attachFn = (void (*)(ConsoleRig::CrossModule&))(*OSServices::Windows::Fn_GetProcAddress)(_pimpl->_library, "AttachLibrary");
            auto getVersionInfoFn = (LibVersionDesc (*)())(*OSServices::Windows::Fn_GetProcAddress)(_pimpl->_library, "GetVersionInformation");
            if (attachFn) {
				(*attachFn)(ConsoleRig::CrossModule::GetInstance());
			}

            if (getVersionInfoFn) {
                _pimpl->_dllVersion = (*getVersionInfoFn)();
                _pimpl->_versionInfoValid = true;
            }
        }

        ++_pimpl->_attachCount;
        return true;
    }

    void AttachableLibrary::Detach()
    {
        assert(_pimpl->_attachCount > 0);
        --_pimpl->_attachCount;

        if (_pimpl->_attachCount==0) {
            assert(_pimpl->_library != LibraryHandle_Invalid);

                // If there is a "DetachLibrary" function, we should
                // call it now.
			auto detachFn = (void (*)())(*OSServices::Windows::Fn_GetProcAddress)(_pimpl->_library, "DetachLibrary");
			if (detachFn) {
				(*detachFn)();
			}

			(*OSServices::Windows::FreeLibrary)(_pimpl->_library);
            _pimpl->_library = LibraryHandle_Invalid;
        }
    }

    bool AttachableLibrary::TryGetVersion(LibVersionDesc& output)
    {
        output = _pimpl->_dllVersion;
        return _pimpl->_versionInfoValid;
    }

    void* AttachableLibrary::GetFunctionAddress(const char name[])
    {
        if (_pimpl->_attachCount <= 0) return nullptr;
        return (void*)(*OSServices::Windows::Fn_GetProcAddress)(_pimpl->_library, name);
    }

    AttachableLibrary::AttachableLibrary(StringSection<CharType> filename)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_attachCount = 0;
        _pimpl->_filename = filename.AsString();
        _pimpl->_library = LibraryHandle_Invalid;
        _pimpl->_dllVersion = LibVersionDesc { "Unknown", "Unknown" };
        _pimpl->_versionInfoValid = false;
    }

    AttachableLibrary::~AttachableLibrary()
    {
        if (_pimpl && _pimpl->_attachCount > 0) {
                // force detach
            _pimpl->_attachCount = 1;
            Detach();
        }
    }

	AttachableLibrary::AttachableLibrary(AttachableLibrary&& moveFrom) never_throws
	: _pimpl(std::move(moveFrom._pimpl))
	{}

	AttachableLibrary& AttachableLibrary::operator=(AttachableLibrary&& moveFrom) never_throws
	{
		_pimpl = std::move(moveFrom._pimpl);
		return *this;
	}
}


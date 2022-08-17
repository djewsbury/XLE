// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AsyncLoadOperation.h"
#include "IFileSystem.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/StringUtils.h"
#include "../Core/SelectConfiguration.h"

#if PLATFORMOS_ACTIVE != PLATFORMOS_WINDOWS
    #error AsyncLoadOperation.cpp only implemented for Windows (or Microsoft API targets)
#endif

#include "../OSServices/WinAPI/IncludeWindows.h"

namespace Assets
{
    class AsyncLoadOperation::SpecialOverlapped : public OVERLAPPED
    {
    public:
        std::shared_ptr<AsyncLoadOperation> _returnPointer;
        HANDLE _fileHandle;

        static void CALLBACK CompletionRoutine(
            DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
            LPOVERLAPPED lpOverlapped);
    };

    void CALLBACK AsyncLoadOperation::SpecialOverlapped::CompletionRoutine(
        DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
        LPOVERLAPPED lpOverlapped)
    {
        auto* o = (SpecialOverlapped*)lpOverlapped;
        assert(o && o->_returnPointer);

        if (o->_fileHandle != INVALID_HANDLE_VALUE)
            CloseHandle(o->_fileHandle);

        std::weak_ptr<AsyncLoadOperation> weakToThis = o->_returnPointer;

            // We can reset the "_returnPointer", which will also decrease the reference
            // count on the marker object (and so could destroy this).
            // If o->_returnPointer is the only reference on this object, then we can
            // consider it a cancel, and we can skip calling Complete().
            // To check, we hold a weak ptr, release o->_returnPointer, and then check
            // the status of the weak ptr.
        o->_returnPointer.reset();

        auto obj = weakToThis.lock();
        if (!obj) return;   // this operation was cancelled. No clients held their references

            // Someone is still waiting on our results...
            // Call the Compete() method to finish all processing
        obj->Complete(obj->GetBuffer(), obj->GetBufferSize());
    }

    void AsyncLoadOperation::Enqueue(const std::shared_ptr<AsyncLoadOperation>& op, StringSection<ResChar> filename, CompletionThreadPool& pool)
    {
        assert(!op->_hasBeenQueued);
        op->_hasBeenQueued = true;
        XlCopyString(op->_filename, filename);

            // We will hold an extra reference to this object
            // during the queueing processing and while the background
            // load is occurring.
            //
            // Before the file open is started, it will be just a 
            // weak reference. If all client references are destroyed
            // before we open the file, then we will cancel.
            // However, once the file has been opened we need to
            // hold a strong reference at least until the read has
            // completed

        std::weak_ptr<AsyncLoadOperation> weakToThis = op;
        pool.Enqueue(
            [weakToThis]()
            {
                auto thisOp = weakToThis.lock();
                    // if all other references to this object have been released
                    // then the weak_ptr::lock() will fail, and we can consider
                    // it a cancel
                if (!thisOp) return;

				auto translated = MainFileSystem::TryGetDesc(thisOp->_filename);
				if (translated._state != FileSnapshot::State::Normal || translated._naturalName.empty()) {
					thisOp->OnFailure();
					return;
				}

                    // if we got to this point, we cannot cancel until the load
                    // level read operation has been completed
                auto h = CreateFileA(
					(const char*)translated._naturalName.c_str(), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                    nullptr);

                if (h == INVALID_HANDLE_VALUE) {
                        // failed to load the file -- probably because it's missing
                    thisOp->OnFailure();
                    return;
                }
                    
                auto fileSize = GetFileSize(h, nullptr);
                if (!fileSize || fileSize == INVALID_FILE_SIZE) {
                    CloseHandle(h);
					thisOp->OnFailure();
                    return;
                }

                thisOp->_buffer.reset((uint8*)XlMemAlign(fileSize, 16));
                thisOp->_bufferLength = fileSize;

                thisOp->_overlapped = std::make_unique<SpecialOverlapped>();
                XlSetMemory(thisOp->_overlapped.get(), 0, sizeof(OVERLAPPED));
                thisOp->_overlapped->_fileHandle = INVALID_HANDLE_VALUE;
                thisOp->_overlapped->_returnPointer = thisOp;

                auto readResult = ReadFileEx(
                    h, thisOp->_buffer.get(), fileSize, 
                    thisOp->_overlapped.get(), &SpecialOverlapped::CompletionRoutine);
                if (!readResult) {
                    CloseHandle(h);
                    thisOp->_overlapped->_returnPointer.reset();
					thisOp->OnFailure();
                    return;
                }

                thisOp->_overlapped->_fileHandle = h;

                // execution will pass to AsyncLoadOperation::CompletionRoutine, which
                // will complete the load operation
            });
    }

    const uint8* AsyncLoadOperation::GetBuffer() const { return  AsPointer(_buffer.get()); }
    size_t AsyncLoadOperation::GetBufferSize() const { return _bufferLength; }

    AsyncLoadOperation::AsyncLoadOperation()
    {
        _filename[0] = '\0';
        _bufferLength = 0;
        _hasBeenQueued = false;
    }

    AsyncLoadOperation::~AsyncLoadOperation() {}

}


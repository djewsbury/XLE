// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetUtils.h"
#include "ICompileOperation.h"
#include "AssetFuture.h"
#include "DepVal.h"
#include "IFileSystem.h"
#include "NascentChunk.h"		// (for AsBlob)
#include "AssetServices.h"
#include "CompileAndAsyncManager.h"
#include "IntermediateCompilers.h"
#include "IArtifact.h"
#include "../OSServices/Log.h"
#include "../Utility/StringUtils.h"
#include "../Utility/StringFormat.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/Threading/Mutex.h"
#include "../Utility/Threading/ThreadingUtils.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/Streams/PathUtils.h"
#include "../OSServices/RawFS.h"
#include <vector>
#include <algorithm>
#include <sstream>

namespace Assets
{

    void DirectorySearchRules::AddSearchDirectory(StringSection<ResChar> dir)
    {
            //  Attempt to fit this directory into our buffer.
            //  note that we have limited space in the buffer, but we can't really
            //  rely on all directory names remaining small and convenient... If we 
            //  overflow our fixed size buffer, we can use the dynamically 
            //  allocated "_bufferOverflow"
        assert((_startPointCount+1) <= dimof(_startOffsets));
        if ((_startPointCount+1) > dimof(_startOffsets)) {
                //  limited number of directories that can be pushed into a single "search rules"
                //  this allows us to avoid a little bit of awkward dynamic memory allocation
            return; 
        }

            // Check for duplicates
            //  Duplicates are bad because they will increase the number of search operations
        if (HasDirectory(dir)) return;

        unsigned allocationLength = (unsigned)(dir.Length() + 1);
        if (_bufferOverflow.empty() && (_bufferUsed + allocationLength <= dimof(_buffer))) {
                // just append this new string to our buffer, and add a new start offset
            XlCopyMemory(&_buffer[_bufferUsed], dir.begin(), (allocationLength-1) * sizeof(ResChar));
            _buffer[_bufferUsed+allocationLength-1] = '\0';
        } else {
            if (_bufferOverflow.empty()) {
                _bufferOverflow.resize(_bufferUsed + allocationLength);
                XlCopyMemory(AsPointer(_bufferOverflow.begin()), _buffer, _bufferUsed * sizeof(ResChar));
                XlCopyMemory(PtrAdd(AsPointer(_bufferOverflow.begin()), _bufferUsed * sizeof(ResChar)), dir.begin(), (allocationLength-1) * sizeof(ResChar));
                _bufferOverflow[_bufferUsed+allocationLength-1] = '\0';
            } else {
                assert(_bufferOverflow.size() == allocationLength);
                auto i = _bufferOverflow.insert(_bufferOverflow.end(), dir.begin(), dir.end());
                _bufferOverflow.insert(i + dir.Length(), 0);
            }
        }

        _startOffsets[_startPointCount++] = _bufferUsed;
        _bufferUsed += allocationLength;
    }

    void DirectorySearchRules::AddSearchDirectoryFromFilename(StringSection<ResChar> filename)
    {
        AddSearchDirectory(MakeFileNameSplitter(filename).DriveAndPath());
    }

	void DirectorySearchRules::SetBaseFile(StringSection<ResChar> file)
	{
		XlCopyString(_baseFile, file);
	}

    std::string DirectorySearchRules::AnySearchDirectory() const
    {
        assert(_startPointCount > 0);
        return std::string(&_buffer[_startOffsets[0]]);
    }
    
    bool DirectorySearchRules::HasDirectory(StringSection<ResChar> dir)
    {
        const ResChar* b = _buffer;
        if (!_bufferOverflow.empty()) {
            b = AsPointer(_bufferOverflow.begin());
        }

            // note --  just doing a string insensitive compare here...
            //          we should really do a more sophisticated path compare
            //          to get a more accurate result
            //          Actually, it might be better to store the paths in some
            //          format that is more convenient for comparisons and combining
            //          paths.
        for (unsigned c=0; c<_startPointCount; ++c)
            if (XlEqStringI(dir, &b[_startOffsets[c]]))
                return true;

        return false;
    }

	static bool DoesFileExist(StringSection<ResChar> fn)
	{
		return MainFileSystem::TryGetDesc(fn)._state == FileDesc::State::Normal;
	}

	static std::vector<std::basic_string<ResChar>> FindFiles(
		StringSection<ResChar> searchPath, 
		OSServices::FindFilesFilter::BitField filter)
	{
		return OSServices::FindFiles(searchPath.AsString(), filter);
	}

    void DirectorySearchRules::ResolveFile(ResChar destination[], unsigned destinationCount, StringSection<ResChar> baseName) const
    {
		if (XlEqString(baseName, "<.>")) {
			if (_baseFile[0]) {
				XlCopyString(destination, destinationCount, _baseFile);
			} else {
				XlCopyString(destination, destinationCount, baseName);
			}
			return;
		}

        ResChar tempBuffer[MaxPath];

        auto splitter = MakeFileNameSplitter(baseName);
        bool baseFileExist = false;
        if (!splitter.ParametersWithDivider().IsEmpty()) {
            XlCopyString(tempBuffer, splitter.AllExceptParameters());
            baseFileExist = DoesFileExist(tempBuffer);
        } else {
            baseFileExist = DoesFileExist(baseName);
        }

            // by definition, we always check the unmodified file name first
        if (!baseFileExist) {
            const ResChar* b = _buffer;
            if (!_bufferOverflow.empty()) {
                b = AsPointer(_bufferOverflow.begin());
            }

                // We want to support the case were destination == baseName
                // But that cases requires another temporary buffer, because we
                // don't want to trash "baseName" while searching for matches
            bool baseNameOverlapsDestination = !(baseName.end() <= destination || baseName.begin() >= &destination[destinationCount]);
            ResChar* workingBuffer = (!baseNameOverlapsDestination) ? destination : tempBuffer;
            unsigned workingBufferSize = (!baseNameOverlapsDestination) ? destinationCount : unsigned(dimof(tempBuffer));

            for (unsigned c=0; c<_startPointCount; ++c) {
                Legacy::XlConcatPath(workingBuffer, workingBufferSize, &b[_startOffsets[c]], 
                    splitter.AllExceptParameters().begin(), splitter.AllExceptParameters().end());
                if (DoesFileExist(workingBuffer)) {
                    SplitPath<ResChar>(workingBuffer).Simplify().Rebuild(workingBuffer, workingBufferSize);
                    if (workingBuffer != destination) {
                        auto workingBufferLen = std::min((ptrdiff_t)XlStringSize(workingBuffer), ptrdiff_t(destinationCount) - 1);
                        auto colonLen = (ptrdiff_t)splitter.ParametersWithDivider().Length();
                        auto colonCopy = std::min(ptrdiff_t(destinationCount) - workingBufferLen - 1, colonLen);
                        assert((workingBufferLen + colonCopy) < ptrdiff_t(destinationCount));
                        if (colonCopy > 0)
                            XlMoveMemory(&destination[workingBufferLen], splitter.ParametersWithDivider().begin(), colonCopy);
                        destination[workingBufferLen + colonCopy] = '\0';
                        assert(workingBufferLen < (ptrdiff_t(destinationCount)-1));
                        XlCopyMemory(destination, workingBuffer, workingBufferLen);
                    } else {
                        XlCatString(destination, destinationCount, splitter.ParametersWithDivider());
                    }
                    return;
                }
            }
        }

        if (baseName.begin() != destination)
            XlCopyString(destination, destinationCount, baseName);
        SplitPath<ResChar>(destination).Simplify().Rebuild(destination, destinationCount);
    }

    void DirectorySearchRules::ResolveDirectory(
            ResChar destination[], unsigned destinationCount, 
            StringSection<ResChar> baseName) const
    {
            //  We have a problem with basic paths (like '../')
            //  These will match for most directories -- which means that
            //  there is some ambiguity. Let's prefer to use the first
            //  registered path for simple relative paths like this.
        bool useBaseName = 
            (baseName[0] != '.' && OSServices::DoesDirectoryExist(baseName));

        if (!useBaseName) {
            const ResChar* b = _buffer;
            if (!_bufferOverflow.empty()) {
                b = AsPointer(_bufferOverflow.begin());
            }

            ResChar tempBuffer[MaxPath];
            bool baseNameOverlapsDestination = !(baseName.end() <= destination || baseName.begin() >= &destination[destinationCount]);
            ResChar* workingBuffer = (!baseNameOverlapsDestination) ? destination : tempBuffer;
            unsigned workingBufferSize = (!baseNameOverlapsDestination) ? destinationCount : unsigned(dimof(tempBuffer));

            for (unsigned c=0; c<_startPointCount; ++c) {
                Legacy::XlConcatPath(workingBuffer, workingBufferSize, &b[_startOffsets[c]], baseName.begin(), baseName.end());
                if (OSServices::DoesDirectoryExist(workingBuffer)) {
                    if (workingBuffer != destination)
                        XlCopyString(destination, destinationCount, workingBuffer);
                    return;
                }
            }
        }

        if (baseName.begin() != destination)
            XlCopyString(destination, destinationCount, baseName);
    }

    void DirectorySearchRules::Merge(const DirectorySearchRules& mergeFrom)
    {
            // Merge in the settings from the given search rules (if the directories
            // don't already exist here)
            // We should really do a better job of comparing directories. Where strings
            // resolve to the same directory, we should consider them identical
        const ResChar* b = mergeFrom._buffer;
        if (!mergeFrom._bufferOverflow.empty())
            b = AsPointer(mergeFrom._bufferOverflow.begin());

        for (unsigned c=0; c<mergeFrom._startPointCount; ++c)
            AddSearchDirectory(&b[mergeFrom._startOffsets[c]]);
    }

	std::vector<std::basic_string<ResChar>> DirectorySearchRules::FindFiles(StringSection<char> wildcardSearch) const
	{
		const ResChar* b = _buffer;
        if (!_bufferOverflow.empty()) {
            b = AsPointer(_bufferOverflow.begin());
        }

		ResChar workingBuffer[MaxPath];
		std::vector<std::basic_string<ResChar>> result;

		for (unsigned c=0; c<_startPointCount; ++c) {
			Legacy::XlConcatPath(workingBuffer, dimof(workingBuffer), &b[_startOffsets[c]], wildcardSearch.begin(), wildcardSearch.end());
			auto partialRes = Assets::FindFiles(workingBuffer, OSServices::FindFilesFilter::File);
			result.insert(result.end(), partialRes.begin(), partialRes.end());
		}
		
		return result;
	}

    DirectorySearchRules::DirectorySearchRules()
    {
        _buffer[0] = '\0';
		_baseFile[0] = '\0';
        _startPointCount = 0;
        _bufferUsed = 0;
        std::fill(_startOffsets, &_startOffsets[dimof(_startOffsets)], 0);
    }

    DirectorySearchRules::DirectorySearchRules(const DirectorySearchRules& copyFrom)
    : _bufferOverflow(copyFrom._bufferOverflow)
    {
        std::copy(copyFrom._buffer, &copyFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(copyFrom._startOffsets, &copyFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferUsed = copyFrom._bufferUsed;
        _startPointCount = copyFrom._startPointCount;
		XlCopyString(_baseFile, copyFrom._baseFile);
    }

    DirectorySearchRules& DirectorySearchRules::operator=(const DirectorySearchRules& copyFrom)
    {
        std::copy(copyFrom._buffer, &copyFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(copyFrom._startOffsets, &copyFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferOverflow = copyFrom._bufferOverflow;
        _bufferUsed = copyFrom._bufferUsed;
        _startPointCount = copyFrom._startPointCount;
		XlCopyString(_baseFile, copyFrom._baseFile);
        return *this;
    }

	DirectorySearchRules::DirectorySearchRules(DirectorySearchRules&& moveFrom) never_throws
	: _bufferOverflow(std::move(moveFrom._bufferOverflow))
    {
        std::copy(moveFrom._buffer, &moveFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferUsed = moveFrom._bufferUsed;
        _startPointCount = moveFrom._startPointCount;
		XlCopyString(_baseFile, moveFrom._baseFile);

		moveFrom._buffer[0] = '\0';
		moveFrom._baseFile[0] = '\0';
        moveFrom._startPointCount = 0;
        moveFrom._bufferUsed = 0;
        std::fill(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(moveFrom._startOffsets)], 0);
    }
        
	DirectorySearchRules& DirectorySearchRules::operator=(DirectorySearchRules&& moveFrom) never_throws
	{
		std::copy(moveFrom._buffer, &moveFrom._buffer[dimof(_buffer)], _buffer);
        std::copy(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(_startOffsets)], _startOffsets);
        _bufferOverflow = std::move(moveFrom._bufferOverflow);
        _bufferUsed = moveFrom._bufferUsed;
        _startPointCount = moveFrom._startPointCount;
		XlCopyString(_baseFile, moveFrom._baseFile);

		moveFrom._buffer[0] = '\0';
		moveFrom._baseFile[0] = '\0';
        moveFrom._startPointCount = 0;
        moveFrom._bufferUsed = 0;
        std::fill(moveFrom._startOffsets, &moveFrom._startOffsets[dimof(moveFrom._startOffsets)], 0);
        return *this;
	}

    DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar> baseFile)
    {
        Assets::DirectorySearchRules searchRules;
        searchRules.AddSearchDirectoryFromFilename(baseFile);
		searchRules.SetBaseFile(baseFile);
        return searchRules;
    }

    namespace Exceptions
    {
		RetrievalError::RetrievalError(StringSection<ResChar> initializer) never_throws
        {
            XlCopyString(_initializer, dimof(_initializer), initializer); 
        }

        RetrievalError::RetrievalError(const RetrievalError& copyFrom)
        {
            XlCopyString(_initializer, dimof(_initializer), copyFrom._initializer); 
        }
        
        RetrievalError& RetrievalError::operator=(const RetrievalError& copyFrom)
        {
            XlCopyString(_initializer, dimof(_initializer), copyFrom._initializer); 
            return *this;
        }

        InvalidAsset::InvalidAsset(StringSection<ResChar> initializer, const DependencyValidation& depVal, const Blob& actualizationLog) never_throws
        : RetrievalError(initializer)
		, _depVal(depVal)
		, _actualizationLog(actualizationLog)
        {
			std::stringstream str;
			if (_actualizationLog) {
				str << "Invalid asset (" << Initializer() << "):" << MakeStringSection((const char*)AsPointer(_actualizationLog->begin()), (const char*)AsPointer(_actualizationLog->end()));
			} else {
				str << "Invalid asset (" << Initializer() << ")";
			}
			_whatString = str.str();
		}

        bool InvalidAsset::CustomReport() const
        {
			Log(Error) << _whatString << std::endl;
            return true;
        }

		const char* InvalidAsset::what() const noexcept
		{
			return _whatString.c_str();
		}

        auto InvalidAsset::State() const -> AssetState { return AssetState::Invalid; }

        InvalidAsset::InvalidAsset(const InvalidAsset& copyFrom)
        : RetrievalError(copyFrom)
        , _depVal(DependencyValidation::SafeCopy(copyFrom._depVal))
        , _actualizationLog(copyFrom._actualizationLog)
        , _whatString(copyFrom._whatString)
        {}

        InvalidAsset& InvalidAsset::operator=(const InvalidAsset& copyFrom)
        {
            RetrievalError::operator=(copyFrom);
            _depVal = DependencyValidation::SafeCopy(copyFrom._depVal);
            _actualizationLog = copyFrom._actualizationLog;
            _whatString = copyFrom._whatString;
            return *this;
        }

        PendingAsset::PendingAsset(StringSection<ResChar> initializer) never_throws
        : RetrievalError(initializer)
        {}

        bool PendingAsset::CustomReport() const
        {
            // "pending asset" exceptions occur very frequently. It may be better to suppress
            // any logging information.
            // LogAlwaysWarning << "Pending asset: " << Initializer();
            return true;
        }

        auto PendingAsset::State() const -> AssetState { return AssetState::Pending; }

		const char* PendingAsset::what() const noexcept
		{
			return Initializer();
		}

		bool ConstructionError::CustomReport() const
		{
			if (_actualizationLog) {
				Log(Error) << "Error during asset construction: " << MakeStringSection((const char*)AsPointer(_actualizationLog->begin()), (const char*)AsPointer(_actualizationLog->end())) << std::endl;
			}
			else {
				Log(Error) << "Error during asset construction (unspecified)" << std::endl;
			}
			return true;
		}

        const char* ConstructionError::what() const noexcept
        {
            static char buffer[4096];
            if (_actualizationLog) {
                XlCopyString(buffer, "Error during asset construction: ");
                auto* d = buffer;
                while (*d) ++d;
                auto* end = &buffer[dimof(buffer)-2];
                for (auto s:*_actualizationLog) {
                    if (d == end) break;
                    *d++ = s;
                }
                *d = '\0';
            } else {
                XlCopyString(buffer, "Error during asset construction (unspecified)");
            }
            return buffer;
        }

		ConstructionError::ConstructionError(Reason reason, const DependencyValidation& depVal, const Blob& actualizationLog) never_throws
		: _reason(reason), _depVal(depVal)
		, _actualizationLog(actualizationLog)
		{
		}

		ConstructionError::ConstructionError(Reason reason, const DependencyValidation& depVal, const char format[], ...) never_throws
		: _reason(reason), _depVal(depVal)
		{
			char buffer[512];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, dimof(buffer), format, args);
			va_end(args);

			_actualizationLog = AsBlob(MakeIteratorRange(buffer, XlStringEnd(buffer)));
		}

		ConstructionError::ConstructionError(const std::exception& e, const DependencyValidation& depVal) never_throws
		: _reason(Reason::Unknown)
		, _depVal(depVal)
		, _actualizationLog(AsBlob(e))
		{}

		ConstructionError::ConstructionError(const ConstructionError& copyFrom, const DependencyValidation& depVal) never_throws
		: _reason(copyFrom._reason)
		, _depVal(copyFrom._depVal)
		, _actualizationLog(copyFrom._actualizationLog)
		{
			// merge the depvals by creating a tree
			if (_depVal && depVal && _depVal != depVal) {
				auto parentDepVal = GetDepValSys().Make();
				parentDepVal.RegisterDependency(_depVal);
				parentDepVal.RegisterDependency(depVal);
				_depVal = std::move(parentDepVal);
			} else if (depVal) {
				_depVal = depVal;
			}
		}

        ConstructionError::ConstructionError(const ConstructionError& copyFrom)
        : _reason(copyFrom._reason)
        , _depVal(DependencyValidation::SafeCopy(copyFrom._depVal))
        , _actualizationLog(copyFrom._actualizationLog)
        {}

        ConstructionError& ConstructionError::operator=(const ConstructionError& copyFrom)
        {
            _reason = copyFrom._reason;
            _depVal = DependencyValidation::SafeCopy(copyFrom._depVal);
            _actualizationLog = copyFrom._actualizationLog;
            return *this;
        }

    }

	Blob AsBlob(const std::exception& e)
	{
		const char* w = e.what();
		return AsBlob(MakeIteratorRange(w, XlStringEnd(w)));
	}


    GenericFuture::GenericFuture(AssetState state) 
    : _state(state)
    {
        DEBUG_ONLY(_initializer[0] = '\0');
    }

    GenericFuture::~GenericFuture() {}

    const char* GenericFuture::GetDebugLabel() const
    {
        #if defined(_DEBUG)
            return _initializer;
        #else
            return "";
        #endif
    }

    void GenericFuture::SetDebugLabel(StringSection<char> initializer)
    {
        DEBUG_ONLY(XlCopyString(_initializer, initializer));
    }

    void GenericFuture::SetState(AssetState newState)
    {
        _state = newState;
    }

    std::optional<AssetState>   GenericFuture::StallWhilePending(std::chrono::milliseconds timeout) const
    {
        auto timeToCancel = std::chrono::steady_clock::now() + timeout;

        // Stall until the _state variable changes in another thread.
        // there is no semaphore, so we must poll
        //      however, that does have a potential advantage in that we can use
        //      YieldToPool rather than stalling the thread entirely, if we're
        //      careful about all of the extra complexity YieldToPool can bring along
        volatile AssetState* state = const_cast<AssetState*>(&_state);
		uint32 waitCount = 0u;
        while (*state == AssetState::Pending) {
			float sleepValue = float(waitCount);
			sleepValue -= 64.f;
			sleepValue /= 16.f;
            ++waitCount;
            if (timeout.count() != 0 && std::chrono::steady_clock::now() >= timeToCancel) return {};
            auto timeoutMS = uint32(std::min(100.0f, std::max(0.0f, sleepValue)));
            YieldToPoolFor(std::chrono::milliseconds(timeoutMS));
		}

        return (AssetState)*state;
    }

	IAsyncMarker::~IAsyncMarker() {}


///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		std::shared_ptr<IIntermediateCompileMarker> BeginCompileOperation(
			CompileRequestCode targetCode, InitializerPack&& initializers)
		{
			auto& compilers = Services::GetAsyncMan().GetIntermediateCompilers();
			return compilers.Prepare(targetCode, std::move(initializers));
		}

        struct ActiveFutureResolutionMoment
        {
            void* _future;
            FutureResolution_CheckStatusFn _checkStatusFn;
        };
        static thread_local std::unique_ptr<std::vector<ActiveFutureResolutionMoment>> s_activeFutureResolutionMoments;
        void FutureResolution_BeginMoment(void* future, FutureResolution_CheckStatusFn checkStatusFn)
        {
            assert((*checkStatusFn)(future) == AssetState::Pending);
            if (!s_activeFutureResolutionMoments)
                s_activeFutureResolutionMoments = std::make_unique<std::vector<ActiveFutureResolutionMoment>>();
            s_activeFutureResolutionMoments->push_back({future, checkStatusFn});
        }

		void FutureResolution_EndMoment(void* future)
        {
            assert(s_activeFutureResolutionMoments);
            for (auto i=s_activeFutureResolutionMoments->rbegin(); i!=s_activeFutureResolutionMoments->rend(); ++i) {
                if (i->_future != future)
                    continue;

                // _checkStatusFn could potentially modify the s_activeFutureResolutionMoments array. So to be safe we should erase
                // before we call it
                auto checkStatusFn = std::move(i->_checkStatusFn);
                s_activeFutureResolutionMoments->erase((i+1).base());

                auto newState = checkStatusFn(future);
                assert(newState == AssetState::Ready || newState == AssetState::Invalid);
                return;
            }
            assert(0);      // didn't find this resolution item
        }

		bool FutureResolution_DeadlockDetection(void* future)
        {
            if (!s_activeFutureResolutionMoments) return false;
            for (auto i=s_activeFutureResolutionMoments->rbegin(); i!=s_activeFutureResolutionMoments->rend(); ++i)
                if (i->_future == future)
                    return true;
            return false;
        }
	}


    auto FileOutputStream::Tell() -> size_type { return _file->TellP(); }
    void FileOutputStream::Write(const void* p, size_type len) { _file->Write(p, 1, len); }
    void FileOutputStream::WriteChar(char ch) { _file->Write(&ch, 1); }
    void FileOutputStream::Write(StringSection<utf8> s) { _file->Write(s.begin(), sizeof(utf8), s.size()); }
    void FileOutputStream::Flush() {}
    FileOutputStream::FileOutputStream(const std::shared_ptr<IFileInterface>& file) : _file(file) {}
    FileOutputStream::FileOutputStream(std::unique_ptr<IFileInterface>&& file) : _file(std::move(file)) {}

}


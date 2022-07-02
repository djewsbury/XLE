// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IBufferUploads.h"
#include <vector>
#include <iosfwd>

namespace BufferUploads
{
    using TimeMarker = int64_t;

    enum class UploadDataType
    {
        Texture, GeometryBuffer, UniformBuffer,
        Max
    };

    struct StagingPageMetrics
    {
        unsigned _bytesAllocated = 0, _maxNextBlockBytes = 0, _bytesAwaitingDevice = 0, _bytesLockedDueToOrdering = 0;
    };

    struct AssemblyLineMetrics
    {
        unsigned _transactionCount, _temporaryTransactionsAllocated;
        unsigned _queuedPrepareStaging, _queuedTransferStagingToFinal, _queuedCreateFromDataPacket;
        unsigned _peakPrepareStaging, _peakTransferStagingToFinal, _peakCreateFromDataPacket;
        size_t _queuedBytes[(unsigned)UploadDataType::Max];
        StagingPageMetrics _stagingPageMetrics;
        AssemblyLineMetrics();
    };

    struct AssemblyLineRetirement
    {
        ResourceDesc _desc;
        TimeMarker _requestTime, _retirementTime;
    };

    struct CommandListMetrics
    {
        size_t _bytesUploaded[(unsigned)UploadDataType::Max];
        size_t _bytesCreated[(unsigned)UploadDataType::Max];
        unsigned _bytesUploadTotal;

        size_t _stagingBytesAllocated[(unsigned)UploadDataType::Max];

        unsigned _countCreations[(unsigned)UploadDataType::Max];
        unsigned _countDeviceCreations[(unsigned)UploadDataType::Max];
        unsigned _countUploaded[(unsigned)UploadDataType::Max];

        unsigned _contextOperations, _deviceCreateOperations;
        AssemblyLineMetrics _assemblyLineMetrics;
        AssemblyLineRetirement _retirements[16];
        unsigned _retirementCount;
        std::vector<AssemblyLineRetirement> _retirementsOverflow;
        TimeMarker _resolveTime, _commitTime;
        TimeMarker _waitTime, _processingStart, _processingEnd;
        TimeMarker _framePriorityStallTime;
        size_t _batchedUploadBytes;
        unsigned _batchedUploadCount;
        unsigned _wakeCount, _frameId;

        CommandListMetrics();
        CommandListMetrics(const CommandListMetrics& cloneFrom);
        const CommandListMetrics& operator=(const CommandListMetrics& cloneFrom);

        unsigned RetirementCount() const                                { return unsigned(_retirementCount + _retirementsOverflow.size()); }
        const AssemblyLineRetirement& Retirement(unsigned index) const  { if (index<_retirementCount) {return _retirements[index];} return _retirementsOverflow[index-_retirementCount]; }
    };

    std::ostream& operator<<(std::ostream& str, const CommandListMetrics&);

}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Metrics.h"
#include "IBufferUploads.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Core/Prefix.h"  // for dimof
#include <algorithm>

namespace RenderCore { namespace BufferUploads
{
	CommandListMetrics::CommandListMetrics()
	{
		XlZeroMemory(_bytesUploaded);
		XlZeroMemory(_bytesCreated);
		XlZeroMemory(_stagingBytesAllocated);
		XlZeroMemory(_countCreations);
		XlZeroMemory(_countDeviceCreations);
		XlZeroMemory(_countUploaded);
		_bytesUploadTotal = _contextOperations = _deviceCreateOperations = 0;
		_resolveTime = _commitTime = 0;
		_waitTime = _processingStart = _processingEnd = 0;
		_framePriorityStallTime = 0;
		_batchedUploadBytes = _batchedUploadCount = 0;
		_wakeCount = 0;
		_frameId = 0;
		_retirementCount = 0;
	}

	CommandListMetrics::CommandListMetrics(CommandListMetrics&& moveFrom)
	{
		this->operator=(std::move(moveFrom));
	}

	const CommandListMetrics& CommandListMetrics::operator=(CommandListMetrics&& moveFrom)
	{
		std::copy(moveFrom._bytesUploaded, &moveFrom._bytesUploaded[dimof(moveFrom._bytesUploaded)], _bytesUploaded);
		std::copy(moveFrom._bytesCreated, &moveFrom._bytesCreated[dimof(moveFrom._bytesCreated)], _bytesCreated);
		std::copy(moveFrom._stagingBytesAllocated, &moveFrom._stagingBytesAllocated[dimof(moveFrom._stagingBytesAllocated)], _stagingBytesAllocated);
		_bytesUploadTotal = moveFrom._bytesUploadTotal;
		std::copy(moveFrom._countCreations, &moveFrom._countCreations[dimof(moveFrom._countCreations)], _countCreations);
		std::copy(moveFrom._countDeviceCreations, &moveFrom._countDeviceCreations[dimof(moveFrom._countDeviceCreations)], _countDeviceCreations);
		std::copy(moveFrom._countUploaded, &moveFrom._countUploaded[dimof(moveFrom._bytesUploaded)], _countUploaded);
		_contextOperations = moveFrom._contextOperations;
		_deviceCreateOperations = moveFrom._deviceCreateOperations;
		_assemblyLineMetrics = moveFrom._assemblyLineMetrics;
		_retirementCount = moveFrom._retirementCount;
		std::copy(moveFrom._retirements, &moveFrom._retirements[std::min(unsigned(dimof(moveFrom._retirements)), moveFrom._retirementCount)], _retirements);
		_retirementsOverflow = moveFrom._retirementsOverflow;
		_resolveTime = moveFrom._resolveTime;
		_commitTime = moveFrom._commitTime;
		_waitTime = moveFrom._waitTime; _processingStart = moveFrom._processingStart; _processingEnd = moveFrom._processingEnd;
		_framePriorityStallTime = moveFrom._framePriorityStallTime;
		_batchedUploadBytes = moveFrom._batchedUploadBytes; _batchedUploadCount = moveFrom._batchedUploadCount;
		_wakeCount = moveFrom._wakeCount; _frameId = moveFrom._frameId;
		_exceptionMsg = std::move(moveFrom._exceptionMsg);
		return *this;
	}

	AssemblyLineMetrics::AssemblyLineMetrics()
	{
		_transactionCount = _temporaryTransactionsAllocated = _queuedPrepareStaging = _queuedTransferStagingToFinal = _queuedCreateFromDataPacket = 0;
		_peakPrepareStaging = _peakTransferStagingToFinal = _peakCreateFromDataPacket = 0;
		XlZeroMemory(_queuedBytes);
	}

	std::ostream& operator<<(std::ostream& str, const CommandListMetrics& metrics)
	{
		str << " Metric               | Texture              | Vertex               | Index" << std::endl;

		str << " "; str.width(20); str << "Bytes Uploaded";
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesUploaded[(unsigned)UploadDataType::Texture]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesUploaded[(unsigned)UploadDataType::GeometryBuffer]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesUploaded[(unsigned)UploadDataType::UniformBuffer]};
		str << std::endl;

		str << " "; str.width(20); str << "Bytes Created";
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesCreated[(unsigned)UploadDataType::Texture]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesCreated[(unsigned)UploadDataType::GeometryBuffer]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._bytesCreated[(unsigned)UploadDataType::UniformBuffer]};
		str << std::endl;

		str << " "; str.width(20); str << "Staging Bytes";
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._stagingBytesAllocated[(unsigned)UploadDataType::Texture]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._stagingBytesAllocated[(unsigned)UploadDataType::GeometryBuffer]};
		str << " | "; str.width(20); str << Utility::ByteCount{metrics._stagingBytesAllocated[(unsigned)UploadDataType::UniformBuffer]};
		str << std::endl;

		str << " "; str.width(20); str << "Creations";
		str << " | "; str.width(20); str << metrics._countCreations[(unsigned)UploadDataType::Texture];
		str << " | "; str.width(20); str << metrics._countCreations[(unsigned)UploadDataType::GeometryBuffer];
		str << " | "; str.width(20); str << metrics._countCreations[(unsigned)UploadDataType::UniformBuffer];
		str << std::endl;

		str << " "; str.width(20); str << "Dev Creations";
		str << " | "; str.width(20); str << metrics._countDeviceCreations[(unsigned)UploadDataType::Texture];
		str << " | "; str.width(20); str << metrics._countDeviceCreations[(unsigned)UploadDataType::GeometryBuffer];
		str << " | "; str.width(20); str << metrics._countDeviceCreations[(unsigned)UploadDataType::UniformBuffer];
		str << std::endl;

		str << " "; str.width(20); str << "Uploaded";
		str << " | "; str.width(20); str << metrics._countUploaded[(unsigned)UploadDataType::Texture];
		str << " | "; str.width(20); str << metrics._countUploaded[(unsigned)UploadDataType::GeometryBuffer];
		str << " | "; str.width(20); str << metrics._countUploaded[(unsigned)UploadDataType::UniformBuffer];
		str << std::endl;

		str << "Batched Bytes Uploaded: " << Utility::ByteCount{metrics._batchedUploadBytes} << " in " << metrics._batchedUploadCount << " steps " << std::endl;
		str << "Total Bytes Uploaded: " << Utility::ByteCount{metrics._bytesUploadTotal} << std::endl;
		str << "Context Operations: " << metrics._contextOperations << std::endl;
		str << "Dev create operations: " << metrics._deviceCreateOperations << std::endl;
		str << "Wake count: " << metrics._wakeCount << std::endl;

		return str;
	}
}}


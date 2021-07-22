// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "QueryPool.h"
#include "IncludeVulkan.h"
#include "ObjectFactory.h"
#include "DeviceContext.h"
#include "../IDeviceVulkan.h"
#include "../../../OSServices/Log.h"

namespace RenderCore { namespace Metal_Vulkan
{
	auto TimeStampQueryPool::SetTimeStampQuery(DeviceContext& context) -> QueryId
	{
		// Attempt to allocate a query from the bit heap
		// Note that if we run out of queries, there is a way to reuse hardware queries
		//		.. we just copy the results using vkCmdCopyQueryPoolResults and then
		//		reset the bit heap. Later on we can lock and read from the buffer to
		//		get the results.
		if (_nextAllocation == _nextFree && _allocatedCount!=0)
			return QueryId_Invalid;		// (we could also look for any buffers that are pending free)
		auto query = _nextAllocation;
		context.GetActiveCommandList().WriteTimestamp(VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, _timeStamps.get(), query);
		_nextAllocation = (_nextAllocation + 1) % _queryCount;
		_allocatedCount = std::min(_allocatedCount+1, _queryCount);
		return query;
	}

	auto TimeStampQueryPool::BeginFrame(DeviceContext& context) -> FrameId
	{
		for (unsigned q=0; q<dimof(_buffers); ++q) {
			auto& b = _buffers[(_activeBuffer+q)%dimof(_buffers)];
			if (b._pendingReset) {
				auto& cmdList = context.GetActiveCommandList();
				if ((b._queryStart + b._queryCount) > _queryCount) {
					auto firstPartCount = _queryCount - b._queryStart;
					cmdList.ResetQueryPool(_timeStamps.get(), b._queryStart, firstPartCount);
					cmdList.ResetQueryPool(_timeStamps.get(), 0, b._queryCount-firstPartCount);
					_allocatedCount -= b._queryCount;
				} else if (b._queryCount) {
					cmdList.ResetQueryPool(_timeStamps.get(), b._queryStart, b._queryCount);
					_allocatedCount -= b._queryCount;
				}
				assert(_nextFree == b._queryStart);
				assert(_allocatedCount >= 0 && _allocatedCount <= _queryCount);
				_nextFree = (b._queryStart + b._queryCount)%_queryCount;
				if (_nextFree == _queryCount) _nextFree = 0;
				b._frameId = FrameId_Invalid;
				b._pendingReset = false;
				b._queryStart = b._queryCount = 0;
			}
		}

		auto& b = _buffers[_activeBuffer];
		if (b._pendingReadback) {
			Log(Warning) << "Query pool eating it's tail. Insufficient buffers." << std::endl;
			return FrameId_Invalid;
		}
		assert(b._frameId == FrameId_Invalid);
		b._frameId = _nextFrameId;
		b._queryStart = _nextAllocation;
		b._queryCount = 0;
		++_nextFrameId;
		return b._frameId;
	}

	void TimeStampQueryPool::EndFrame(DeviceContext& context, FrameId frame)
	{
		auto& b = _buffers[_activeBuffer];
		b._pendingReadback = true;
		if (_nextAllocation >= b._queryStart) b._queryCount = _nextAllocation - b._queryStart;
		else b._queryCount = _nextAllocation + (_queryCount - b._queryStart);
		assert(b._queryCount != _queryCount);	// problems if we allocate all queries in a single frame currently
		// roll forward to the next buffer
		_activeBuffer = (_activeBuffer + 1) % s_bufferCount;
	}

	auto TimeStampQueryPool::GetFrameResults(DeviceContext& context, FrameId id) -> FrameResults
	{
		// Attempt to read the results from the query pool for the given frame.
		// The queries are completed asynchronously, so the results may not be available yet.
		// When the results are not available, just return nullptr

		for (unsigned c = 0; c < s_bufferCount; ++c) {
			auto& b = _buffers[c];
			if (b._frameId != id || !b._pendingReadback)
				continue;

			// Requesting 64 bit timestamps on all hardware. We can also
			// check the timestamp size by calling VkGetPhysicalDeviceProperties
			// Our query buffer is circular, so we may need to wrap around to the start
			if ((b._queryStart + b._queryCount) > _queryCount) {
				unsigned firstPartCount = _queryCount - b._queryStart;
				auto res = vkGetQueryPoolResults(
					_device, _timeStamps.get(),
					b._queryStart, firstPartCount,
					sizeof(uint64_t)*firstPartCount,
					&_timestampsBuffer[b._queryStart], sizeof(uint64_t),
					VK_QUERY_RESULT_64_BIT);
				if (res == VK_NOT_READY)
					return FrameResults{ false };
				if (res != VK_SUCCESS)
					Throw(VulkanAPIFailure(res, "Failed while retrieving query pool results"));

				res = vkGetQueryPoolResults(
					_device, _timeStamps.get(),
					0, b._queryCount-firstPartCount,
					sizeof(uint64_t)*(b._queryCount-firstPartCount),
					_timestampsBuffer.get(), sizeof(uint64_t),
					VK_QUERY_RESULT_64_BIT);
				if (res == VK_NOT_READY)
					return FrameResults{ false };
				if (res != VK_SUCCESS)
					Throw(VulkanAPIFailure(res, "Failed while retrieving query pool results"));
			} else if (b._queryCount) {
				auto res = vkGetQueryPoolResults(
					_device, _timeStamps.get(),
					b._queryStart, b._queryCount, 
					sizeof(uint64_t)*(b._queryCount),
					&_timestampsBuffer[b._queryStart], sizeof(uint64_t),
					VK_QUERY_RESULT_64_BIT);

				// we should frequently get "not ready" -- this means the query hasn't completed yet.
				if (res == VK_NOT_READY) 
					return FrameResults{false};
				if (res != VK_SUCCESS)
					Throw(VulkanAPIFailure(res, "Failed while retrieving query pool results"));
			}

			// Succesfully retrieved results for all queries. We can reset the pool
			b._pendingReadback = false;
			b._pendingReset = true;
			return FrameResults{true, false,
				_timestampsBuffer.get(), &_timestampsBuffer[_queryCount],
				_frequency};
		}

		// couldn't find any pending results for this frame
		return FrameResults{false};
	}

	TimeStampQueryPool::TimeStampQueryPool(ObjectFactory& factory) 
	{
		_queryCount = 96;
		_activeBuffer = 0;
		_nextFrameId = 0;
		_device = factory.GetDevice().get();
		_timestampsBuffer = std::make_unique<uint64_t[]>(_queryCount);
		_timeStamps = factory.CreateQueryPool(VK_QUERY_TYPE_TIMESTAMP, _queryCount);
		_nextAllocation = _nextFree = _allocatedCount = 0;

		for (unsigned c=0; c<s_bufferCount; ++c) {
			_buffers[c]._frameId = FrameId_Invalid;
			_buffers[c]._pendingReadback = false;
			_buffers[c]._pendingReset = false;
			_buffers[c]._queryStart = _buffers[c]._queryCount = 0;
		}

		// we must reset all queries first time around
		_buffers[0]._pendingReset = true;
		_buffers[0]._queryStart = 0;
		_buffers[0]._queryCount = _queryCount;
		_allocatedCount = _queryCount;

		VkPhysicalDeviceProperties physDevProps = {};
		vkGetPhysicalDeviceProperties(factory.GetPhysicalDevice(), &physDevProps);
		auto nanosecondsPerTick = physDevProps.limits.timestampPeriod;
		// awkwardly, DX uses frequency while Vulkan uses period. We have to use a divide somewhere to convert
		_frequency = uint64_t(1e9f / nanosecondsPerTick);
	}

	TimeStampQueryPool::~TimeStampQueryPool() {}

	auto QueryPool::Begin(DeviceContext& context) -> QueryId
	{
		// reset as many as we cana
		assert(_nextAllocation != (unsigned)_queryStates.size());
		unsigned i = _nextAllocation;
		while (i != (unsigned)_queryStates.size() && _queryStates[i] == QueryState::PendingReset) ++i;
		if (i != _nextAllocation) {
			context.GetActiveCommandList().ResetQueryPool(_underlying.get(), _nextAllocation, i-_nextAllocation);
			for (auto q=_nextAllocation; q!=i; ++q)
				_queryStates[q] = QueryState::Reset;
		}

		// If we didn't manage to reset the next query inline to be allocated, it might still be inflight
		// It might have completed on the GPU, but if we haven't called GetResults on the query, it will
		// stay in the inflight or ended state
		if (_queryStates[_nextAllocation] != QueryState::Reset)
			return ~0u;

		auto allocation = _nextAllocation;
		_nextAllocation = unsigned((_nextAllocation+1)%_queryStates.size());
		context.GetActiveCommandList().BeginQuery(_underlying.get(), allocation);
		_queryStates[allocation] = QueryState::Inflight;
		return allocation;
	}

	void QueryPool::End(DeviceContext& context, QueryId queryId)
	{
		assert(queryId != ~0u);
		assert(_queryStates[queryId] == QueryState::Inflight);
		context.GetActiveCommandList().EndQuery(_underlying.get(), queryId);
		_queryStates[queryId] = QueryState::Ended;
	}

	bool QueryPool::GetResults_Stall(
		DeviceContext& context,
		QueryId queryId, IteratorRange<void*> dst)
	{
		assert(queryId != ~0u);
		assert(_queryStates[queryId] == QueryState::Ended);

		unsigned results[5] = { 0, 0, 0, 0, 0 };
		assert(_outputCount <= dimof(results));
		auto res = vkGetQueryPoolResults(
			context.GetUnderlyingDevice(),
			_underlying.get(),
			queryId, 1, _outputCount * sizeof(unsigned), &results, sizeof(unsigned),
			VK_QUERY_RESULT_WAIT_BIT);
		assert(res == VK_SUCCESS); (void)res;
		_queryStates[queryId] = QueryState::PendingReset;

		switch (_type) {
		case QueryPool::QueryType::StreamOutput_Stream0:
			if (dst.size() >= sizeof(QueryResult_StreamOutput)) {
				((QueryResult_StreamOutput*)dst.begin())->_primitivesWritten = results[0];
				((QueryResult_StreamOutput*)dst.begin())->_primitivesNeeded = results[1];
			}
			break;
		case QueryPool::QueryType::ShaderInvocations:
			if (dst.size() >= sizeof(QueryResult_ShaderInvocations)) {
				auto& factory = GetObjectFactory(context);
				auto& result = *(QueryResult_ShaderInvocations*)dst.begin();
				XlZeroMemory(result);
				unsigned idx = 0;
				result._invocations[(unsigned)ShaderStage::Vertex] = results[idx++];
				if (factory.GetPhysicalDeviceFeatures().geometryShader)
					result._invocations[(unsigned)ShaderStage::Geometry] = results[idx++];
				result._invocations[(unsigned)ShaderStage::Pixel] = results[idx++];
				if (factory.GetPhysicalDeviceFeatures().tessellationShader)
					result._invocations[(unsigned)ShaderStage::Hull] = results[idx++];
				result._invocations[(unsigned)ShaderStage::Compute] = results[idx++];
				assert(idx == _outputCount);
			}
			break;
		default:
			assert(0);
		}
		return true;
	}

	void QueryPool::AbandonResults(QueryId queryId)
	{
		assert(queryId != ~0u);
		assert(_queryStates[queryId] == QueryState::Ended);
		_queryStates[queryId] = QueryState::PendingReset;
	}

	QueryPool::QueryPool(ObjectFactory& factory, QueryType type, unsigned count)
	: _type(type)
	{
		_queryStates = std::vector<QueryState>(count, QueryState::PendingReset);
		switch (type) {
		case QueryPool::QueryType::StreamOutput_Stream0:
			_underlying = factory.CreateQueryPool(VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT, count, 0);
			_outputCount = 2;
			break;
		case QueryPool::QueryType::ShaderInvocations:
			{
				VkQueryPipelineStatisticFlags pipelineStatistics =
					VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
					VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
					VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
				_outputCount = 3;
				if (factory.GetPhysicalDeviceFeatures().geometryShader) {
					pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
					++_outputCount;
				}
				if (factory.GetPhysicalDeviceFeatures().tessellationShader) {
					pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
					++_outputCount;
				}
				_underlying = factory.CreateQueryPool(VK_QUERY_TYPE_PIPELINE_STATISTICS, count, pipelineStatistics);
			}
			break;
		default:
			Throw(std::runtime_error("Unknown query type"));
		}
	}

	QueryPool::~QueryPool() {}

}}

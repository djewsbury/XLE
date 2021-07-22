// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "VulkanCore.h"
#include "../../Types.h"
#include "../../../Core/Types.h"
#include "../../../Utility/IteratorUtils.h"
#include <memory>

#define GPUANNOTATIONS_ENABLE

namespace RenderCore { class IDevice; }

namespace RenderCore { namespace Metal_Vulkan
{
	class DeviceContext;
	class ObjectFactory;

	class TimeStampQueryPool
	{
	public:
		using QueryId = unsigned;
		using FrameId = unsigned;

		static const QueryId QueryId_Invalid = ~0u;
		static const FrameId FrameId_Invalid = ~0u;

		QueryId		SetTimeStampQuery(DeviceContext& context);

		FrameId		BeginFrame(DeviceContext& context);
		void		EndFrame(DeviceContext& context, FrameId frame);

		struct FrameResults
		{
			bool		_resultsReady;
			bool		_isDisjoint;
			uint64_t*	_resultsStart;
			uint64_t*	_resultsEnd;
			uint64_t	_frequency;
		};
		FrameResults GetFrameResults(DeviceContext& context, FrameId id);

		TimeStampQueryPool(ObjectFactory& factory);
		~TimeStampQueryPool();

		TimeStampQueryPool(const TimeStampQueryPool&) = delete;
		TimeStampQueryPool& operator=(const TimeStampQueryPool&) = delete;
	private:
		static const unsigned s_bufferCount = 16u;
		VulkanUniquePtr<VkQueryPool> _timeStamps;
		unsigned _nextAllocation;
		unsigned _nextFree;
		unsigned _allocatedCount;

		class Buffer
		{
		public:
			FrameId		_frameId;
			bool		_pendingReadback;
			bool		_pendingReset;
			unsigned	_queryStart;
			unsigned	_queryCount;
		};
		Buffer		_buffers[s_bufferCount];
		unsigned	_activeBuffer;
		FrameId		_nextFrameId;

		VkDevice	_device;
		unsigned	_queryCount;
		uint64_t		_frequency;
		std::unique_ptr<uint64_t[]> _timestampsBuffer;
	};

	class QueryPool
	{
	public:
		enum class QueryType
		{
			StreamOutput_Stream0,
			ShaderInvocations
		};

		struct QueryResult_StreamOutput
		{
			unsigned _primitivesWritten;
			unsigned _primitivesNeeded;
		};

		struct QueryResult_ShaderInvocations
		{
			unsigned _invocations[(unsigned)ShaderStage::Max];
		};

		using QueryId = unsigned;
		QueryId Begin(DeviceContext& context);
		void End(DeviceContext& context, QueryId);

		bool GetResults_Stall(DeviceContext& context, QueryId query, IteratorRange<void*> dst);
		void AbandonResults(QueryId query);

		QueryPool(ObjectFactory& factory, QueryType type, unsigned count);
		~QueryPool();
	private:
		VulkanUniquePtr<VkQueryPool> _underlying;

		enum class QueryState { PendingReset, Reset, Inflight, Ended };
		std::vector<QueryState> _queryStates;
		unsigned _nextAllocation = 0;

		QueryType _type;
		unsigned _outputCount = 0;
	};

    #if defined(GPUANNOTATIONS_ENABLE)

        class GPUAnnotation
        {
        public:
			static void Begin(DeviceContext& context, const char annotationName[]) {}
			static void End(DeviceContext& context) {}

			GPUAnnotation(DeviceContext& context, const char annotationName[]) {}
        };

    #else

        class GPUAnnotation
        {
        public:
			static void Begin(DeviceContext& context, const char annotationName[]) {}
			static void End(DeviceContext& context, const char annotationName[]) {}
			GPUAnnotation(DeviceContext&, const char[]) {}
        };

    #endif
}}


// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IDevice.h"
#include <utility>
#include <memory>
#include <functional>

namespace RenderCore
{
	class IAnnotator
	{
	public:
		virtual void	Frame_Begin(unsigned frameID) = 0;
		virtual void	Frame_End() = 0;

		struct EventTypes
		{
			enum Flags
			{ 
				ProfileBegin = 1<<0, ProfileEnd = 1<<1,
				MarkerBegin = 1<<2, MarkerEnd = 1<<3
			};
			using BitField = unsigned;
		};
		virtual void	Event(const char name[], EventTypes::BitField types) = 0;

		using EventListener = std::function<void(const void* eventBufferBegin, const void* eventBufferEnd)>;
		virtual unsigned	AddEventListener(const EventListener& callback) = 0;
		virtual void		RemoveEventListener(unsigned listenerId) = 0;

		virtual bool		IsCaptureToolAttached() = 0;
		virtual void		BeginFrameCapture() = 0;
		virtual void		EndFrameCapture() = 0;

		virtual ~IAnnotator();
	};

    class GPUProfilerBlock
    {
    public:
        GPUProfilerBlock(IThreadContext& context, const char name[])
        : _context(&context), _name(name)
        {
            _context->GetAnnotator().Event(_name, IAnnotator::EventTypes::ProfileBegin);
        }

        ~GPUProfilerBlock()
        {
            _context->GetAnnotator().Event(_name, IAnnotator::EventTypes::ProfileEnd);
        }

        GPUProfilerBlock(const GPUProfilerBlock&) = delete;
        GPUProfilerBlock& operator=(const GPUProfilerBlock&) = delete;
    private:
        IThreadContext*		_context;
        const char*			_name;
    };

    class GPUAnnotation
    {
    public:
        GPUAnnotation(IThreadContext& context, const char name[])
        : _context(&context), _name(name)
        {
            _context->GetAnnotator().Event(_name, IAnnotator::EventTypes::MarkerBegin);
        }

        ~GPUAnnotation()
        {
            _context->GetAnnotator().Event(_name, IAnnotator::EventTypes::MarkerEnd);
        }

        GPUAnnotation(const GPUAnnotation&) = delete;
        GPUAnnotation& operator=(const GPUAnnotation&) = delete;
    private:
        IThreadContext*		_context;
        const char*			_name;
    };
}


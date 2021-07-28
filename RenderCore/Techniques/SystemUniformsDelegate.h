// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "DrawableDelegates.h"
#include "TechniqueUtils.h"
#include "../UniformsStream.h"
#include <memory>

namespace RenderCore { class ISampler; }
namespace RenderCore { namespace Techniques
{
	class SystemUniformsDelegate : public IShaderResourceDelegate
	{
	public:
		void SetLocalTransformFallback(const LocalTransformConstants& input) { _localTransformFallback = input; }

		const UniformsStreamInterface& GetInterface() override;
		void WriteImmediateData(ParsingContext& context, const void* objectContext, unsigned idx, IteratorRange<void*> dst) override;
		void WriteSamplers(ParsingContext& context, const void* objectContext, uint64_t bindingFlags, IteratorRange<ISampler**> dst) override;
		size_t GetImmediateDataSize(ParsingContext& context, const void* objectContext, unsigned idx) override;
		SystemUniformsDelegate(IDevice& device);
		~SystemUniformsDelegate();
	private:
		UniformsStreamInterface _interface;
		LocalTransformConstants _localTransformFallback;
		std::shared_ptr<ISampler> _samplers[4];
	};
}}

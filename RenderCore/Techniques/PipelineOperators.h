// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineCollection.h"
#include "../../Assets/AssetsCore.h"

namespace RenderCore 
{
	class IThreadContext;
	class UniformsStream;
	class UniformsStreamInterface;
	class IDescriptorSet;
}

namespace RenderCore { namespace Techniques
{
	class ParsingContext;
	
	class IShaderOperator
	{
	public:
		virtual void Draw(IThreadContext&, ParsingContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual ~IShaderOperator();
	};

	class RenderPassInstance;

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
		const RenderPassInstance& rpi,
		StringSection<> pixelShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi);

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<RenderCore::ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		StringSection<> pixelShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi);

	::Assets::PtrToFuturePtr<IShaderOperator> CreateComputeOperator(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		StringSection<> definesTable,
		const UniformsStreamInterface& usi);
}}

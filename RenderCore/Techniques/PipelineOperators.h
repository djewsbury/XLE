// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineCollection.h"
#include "TechniqueDelegates.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/ParameterBox.h"

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
	class SequencerUniformsHelper;

	class IShaderOperator
	{
	public:
		virtual void Draw(IThreadContext&, ParsingContext&, SequencerUniformsHelper&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;
		virtual ~IShaderOperator();
	};
	
	class IComputeShaderOperator
	{
	public:
		virtual void Dispatch(IThreadContext&, ParsingContext&, SequencerUniformsHelper&, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual void Dispatch(IThreadContext&, unsigned countX, unsigned countY, unsigned countZ, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}) = 0;
		virtual void BeginDispatches(IThreadContext&, ParsingContext&, SequencerUniformsHelper&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}, uint64_t pushConstantsBinding = 0) = 0;
		virtual void BeginDispatches(IThreadContext&, const UniformsStream&, IteratorRange<const IDescriptorSet* const*> = {}, uint64_t pushConstantsBinding = 0) = 0;
		virtual void EndDispatches() = 0;
		virtual void Dispatch(unsigned countX, unsigned countY, unsigned countZ, IteratorRange<const void*> pushConstants) = 0;
		virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;
		virtual ~IComputeShaderOperator();
	};

	class RenderPassInstance;

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi);

	::Assets::PtrToFuturePtr<IShaderOperator> CreateFullViewportOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> pixelShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAsset,
		const FrameBufferTarget& fbTarget,
		const UniformsStreamInterface& usi);

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		const UniformsStreamInterface& usi);

	::Assets::PtrToFuturePtr<IComputeShaderOperator> CreateComputeOperator(
		const std::shared_ptr<PipelinePool>& pool,
		StringSection<> computeShader,
		const ParameterBox& selectors,
		StringSection<> pipelineLayoutAsset,
		const UniformsStreamInterface& usi);
}}

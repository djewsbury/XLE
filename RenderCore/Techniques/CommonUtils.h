// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StateDesc.h"
#include "../Types.h"
#include "../Metal/Forward.h"		// for Metal::ShaderProgram
#include "../../Assets/AssetsCore.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <utility>

namespace RenderCore { class IResource; class IDevice; class CompiledShaderByteCode; class StreamOutputInitializers; class ICompiledPipelineLayout; }
namespace RenderCore { namespace Assets { class MaterialScaffoldMaterial; class PredefinedDescriptorSetLayout; class ShaderPatchCollection; }}

namespace RenderCore { namespace Techniques {

	std::shared_ptr<IResource> CreateStaticVertexBuffer(IDevice& device, IteratorRange<const void*> data);
	std::shared_ptr<IResource> CreateStaticIndexBuffer(IDevice& device, IteratorRange<const void*> data);

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& psCode,
		const std::string& programName = {});

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& gsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& psCode,
		const StreamOutputInitializers& soInit,
		const std::string& programName = {});

	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class IPipelineAcceleratorPool;
	std::pair<std::shared_ptr<PipelineAccelerator>, ::Assets::PtrToFuturePtr<DescriptorSetAccelerator>> 
		CreatePipelineAccelerator(
			IPipelineAcceleratorPool& pool,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& patchCollection,
			const RenderCore::Assets::MaterialScaffoldMaterial& material,
			IteratorRange<const InputElementDesc*> inputLayout,
			Topology topology = Topology::TriangleList);

}}


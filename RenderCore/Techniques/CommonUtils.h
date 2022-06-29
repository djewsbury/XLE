// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../StateDesc.h"
#include "../Types.h"
#include "../ResourceDesc.h"
#include "../Metal/Forward.h"		// for Metal::ShaderProgram
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <utility>

namespace RenderCore { class IResource; class IDevice; class CompiledShaderByteCode; class StreamOutputInitializers; class ICompiledPipelineLayout; }
namespace RenderCore { namespace Assets { class ModelScaffold; class PredefinedDescriptorSetLayout; class ShaderPatchCollection; class RenderStateSet; }}
namespace Assets { class IFileInterface; }
namespace Utility { class ParameterBox; }

namespace RenderCore { namespace Techniques {

	std::shared_ptr<IResource> CreateStaticVertexBuffer(IDevice& device, IteratorRange<const void*> data);
	std::shared_ptr<IResource> CreateStaticIndexBuffer(IDevice& device, IteratorRange<const void*> data);

	std::shared_ptr<IResource> LoadStaticResource(
		IDevice& device,
		IteratorRange<std::pair<unsigned, unsigned>*> loadRequests,
		unsigned resourceSize,
		::Assets::IFileInterface& file,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName);

	/// Both data load and resource construction is pushed to async thread
	BufferUploads::TransactionMarker LoadStaticResourceFullyAsync(
		BufferUploads::IManager& bufferUploads,
		IteratorRange<std::pair<unsigned, unsigned>*> loadRequests,
		unsigned resourceSize,
		std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
		BindFlag::BitField bindFlags,
		std::shared_ptr<BufferUploads::IResourcePool> resourceSource,
		StringSection<> resourceName);

	struct ModelScaffoldLoadRequest
	{
		std::shared_ptr<RenderCore::Assets::ModelScaffold> _modelScaffold;
		unsigned _offset = ~0u, _size = ~0u;
	};
	/// Both data load is pushed to async thread, however resource construction occurs synchronously
	std::pair<std::shared_ptr<IResource>, BufferUploads::TransactionMarker> LoadStaticResourcePartialAsync(
		IDevice& device,
		IteratorRange<const ModelScaffoldLoadRequest*> loadRequests,
		unsigned resourceSize,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName);

	::Assets::PtrToMarkerPtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& psCode,
		const std::string& programName = {});

	::Assets::PtrToMarkerPtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& gsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& psCode,
		const StreamOutputInitializers& soInit,
		const std::string& programName = {});

	class PipelineAccelerator;
	class DescriptorSetAccelerator;
	class IPipelineAcceleratorPool;
	std::pair<std::shared_ptr<PipelineAccelerator>, ::Assets::PtrToMarkerPtr<DescriptorSetAccelerator>> 
		CreatePipelineAccelerator(
			IPipelineAcceleratorPool& pool,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& patchCollection,
			const ParameterBox& materialSelectors,
			const Assets::RenderStateSet& renderStateSet,
			IteratorRange<const InputElementDesc*> inputLayout,
			Topology topology = Topology::TriangleList);

}}


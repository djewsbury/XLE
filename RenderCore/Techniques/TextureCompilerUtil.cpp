// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompilerUtil.h"
#include "Techniques.h"
#include "DeferredShaderResource.h"
#include "PipelineOperators.h"
#include "Services.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../IDevice.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/Marker.h"
#include "../../OSServices/Log.h"
#include "../../Utility/BitUtils.h"
#include "../../xleres/FileList.h"

using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
{
	class DataSourceFromResourceSynchronized : public BufferUploads::IAsyncDataSource
	{
	public:
		virtual std::future<ResourceDesc> GetDesc () override
		{
			std::promise<ResourceDesc> promise;
			auto result = promise.get_future(); 
			promise.set_value(_resource->GetDesc());
			return result;
		}

		virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources) override
		{
			Metal::ResourceMap map {*_device, *_resource, Metal::ResourceMap::Mode::Read};
			for (const auto& sr:subResources) {				
				auto data = map.GetData(sr._id);
				assert(sr._destination.size() == data.size());
				std::memcpy(sr._destination.begin(), data.data(), std::min(sr._destination.size(), data.size()));
			}
			std::promise<void> promise;
			auto result = promise.get_future(); 
			promise.set_value();
			return result;
		}

		virtual ::Assets::DependencyValidation GetDependencyValidation() const override { return _depVal; }

		virtual StringSection<> GetName() const override { return "data-source-from-resource"; }

		DataSourceFromResourceSynchronized(
			std::shared_ptr<IThreadContext> threadContext, 
			std::shared_ptr<IResource> resource,
			::Assets::DependencyValidation depVal)
		: _device(threadContext->GetDevice())
		, _depVal(std::move(depVal))
		{
			_resource = DestageResource(*threadContext, resource);
		}

		std::shared_ptr<IDevice> _device;
		std::shared_ptr<IResource> _resource;
		::Assets::DependencyValidation _depVal;
	};

	static std::string s_equRectFilterName { "texture-compiler (EquRectFilter)" };
	static std::string s_fromComputeShaderName { "texture-compiler (GenerateFromComputeShader)" };

	std::shared_ptr<BufferUploads::IAsyncDataSource> EquRectFilter(
		BufferUploads::IAsyncDataSource& dataSrc, const TextureDesc& targetDesc,
		EquRectFilterMode filter,
		const ProgressiveTextureFn& progressiveResults)
	{
		// We need to create a texture from the data source and run a shader process on it to generate
		// an output cubemap. We'll do this on the GPU and copy the results back into a new IAsyncDataSource
		if (filter != EquRectFilterMode::ProjectToSphericalHarmonic)
			assert(ActualArrayLayerCount(targetDesc) == 6 && targetDesc._dimensionality == TextureDesc::Dimensionality::CubeMap);
		auto threadContext = GetThreadContext();
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Input"_h);
		constexpr auto pushConstantsBinding = "FilterPassParams"_h;

		::Assets::PtrToMarkerPtr<IComputeShaderOperator> computeOpFuture;
		if (filter == EquRectFilterMode::ToCubeMap) {
			usi.BindResourceView(1, "OutputArray"_h);
 			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				EQUIRECTANGULAR_TO_CUBE_HLSL ":EquRectToCube",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquRectFilterMode::ToGlossySpecular) {
			usi.BindResourceView(1, "OutputArray"_h);
			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				IBL_PREFILTER_HLSL ":EquiRectFilterGlossySpecular",
				// IBL_PREFILTER_HLSL ":ReferenceDiffuseFilter",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else {
			assert(filter == EquRectFilterMode::ProjectToSphericalHarmonic);
			usi.BindResourceView(1, "Output"_h);
			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				IBL_PREFILTER_HLSL ":ProjectToSphericalHarmonic",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		}

		auto inputRes = CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, targetDesc), "texture-compiler");
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(threadContext.get()))
			threadContextVulkan->AttachNameToCommandList(s_equRectFilterName);

		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		auto depVal = ::Assets::GetDepValSys().Make();
		depVal.RegisterDependency(computeOp->GetDependencyValidation());
		depVal.RegisterDependency(dataSrc.GetDependencyValidation());

		auto inputView = inputRes->CreateTextureView(BindFlag::ShaderResource);
		for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
			TextureViewDesc view;
			view._mipRange = {mip, 1};
			auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
			IResourceView* resViews[] = { inputView.get(), outputView.get() };
			auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

			UniformsStream us;
			us._resourceViews = MakeIteratorRange(resViews);
			auto dispatchGroup = computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);

			if (filter == EquRectFilterMode::ToCubeMap) {
				auto passCount = (mipDesc._width+7)/8 * (mipDesc._height+7)/8 * 6;
				for (unsigned p=0; p<passCount; ++p) {
					struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, p, passCount, 0 };
					dispatchGroup.Dispatch(1, 1, 1, MakeOpaqueIteratorRange(filterPassParams));
				}
			} else if (filter == EquRectFilterMode::ToGlossySpecular) {
				auto pixelCount = mipDesc._width * mipDesc._height * ActualArrayLayerCount(targetDesc);
				auto revMipIdx = IntegerLog2(std::max(mipDesc._width, mipDesc._height));
				auto passesPerPixel = 16u-std::min(revMipIdx, 7u);		// increase the number of passes per pixel for lower mip maps, where there is greater roughness
				auto dispatchCount = passesPerPixel*pixelCount;
				auto dispatchesPerCommit = std::min(32768u, pixelCount);
				for (unsigned d=0; d<dispatchCount; ++d) {
					struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, d, dispatchCount, 0 };
					dispatchGroup.Dispatch(1, 1, 1, MakeOpaqueIteratorRange(filterPassParams));

					if ((d%dispatchesPerCommit) == (dispatchesPerCommit-1)) {
						dispatchGroup = {};
						if (progressiveResults) {
							Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::TransferSrc);
							auto intermediateData = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
							// note -- we could dispatch to another thread; but there's a potential risk of out of order execution
							progressiveResults(intermediateData);
							Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::TransferSrc, BindFlag::UnorderedAccess);
						} else {
							threadContext->CommitCommands();
						}

						dispatchGroup = computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);
						if (auto* threadContextVulkan = (RenderCore::IThreadContextVulkan*)threadContext->QueryInterface(TypeHashCode<RenderCore::IThreadContextVulkan>))
							threadContextVulkan->AttachNameToCommandList(s_equRectFilterName);
					} else {
						/* 	We shouldn't need a barrier here, because we won't write to the same pixel in the same
							cmd list. The pixel we're writing to is based on 'd' -- and this won't wrap around back to
							the start before we commit the command list
						VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
						barrier.pNext = nullptr;
						barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
						barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
						vkCmdPipelineBarrier(
							Metal::DeviceContext::Get(*threadContext)->GetActiveCommandList().GetUnderlying().get(),
							VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							0,
							1, &barrier,
							0, nullptr,
							0, nullptr);
						 */
					}
				}
			} else {
				assert(filter == EquRectFilterMode::ProjectToSphericalHarmonic);
				dispatchGroup.Dispatch(targetDesc._width, 1, 1);
			}

			dispatchGroup = {};
		}

		// We need a barrier before the transfer in DataSourceFromResourceSynchronized
		Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::TransferSrc);

		auto result = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
		threadContext->CommitCommands();
		// Release the command buffer pool, because Vulkan requires pumping the command buffer destroys regularly,
		// and we may not be doing that in this thread for awhile
		if (auto* threadContextVulkan = (RenderCore::IThreadContextVulkan*)threadContext->QueryInterface(TypeHashCode<RenderCore::IThreadContextVulkan>))
			threadContextVulkan->ReleaseCommandBufferPool();
		return result;
	}

	std::shared_ptr<BufferUploads::IAsyncDataSource> GenerateFromComputeShader(StringSection<> shader, const TextureDesc& targetDesc)
	{
		auto threadContext = GetThreadContext();
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Output"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);

 		auto computeOpFuture = CreateComputeOperator(
			std::make_shared<PipelineCollection>(threadContext->GetDevice()),
			shader, {}, TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain", usi);

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, targetDesc), "texture-compiler");
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		if (auto* threadContextVulkan = (RenderCore::IThreadContextVulkan*)threadContext->QueryInterface(TypeHashCode<RenderCore::IThreadContextVulkan>))
			threadContextVulkan->AttachNameToCommandList(s_equRectFilterName);

		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		threadContext->GetDevice()->Stall();		// sync with GPU, because of timing work below

		for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {

			auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
			unsigned totalPixelCount = mipDesc._width * mipDesc._height;

			// We have to baby the graphics API a little bit to avoid timeouts. We don't know
			// exactly how many pixels we can calculate in a single command list before we will
			// start to get timeouts.
			//
			// It doesn't matter how we distribute threads in groups or dispatches -- what matters
			// is the cost of the command list submit as a whole
			//
			// We will start with a small number of pixels and slowly increase while it seems safe
			// Unfortunately we have to do this with the CPU & GPU synced, because timing the GPU
			// would otherwise be awkward

			unsigned pixelsProcessed = 0;
			unsigned pixelsPerCmdList = 256;
			while (true) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { outputView.get() };
				struct ControlUniforms { unsigned _mipIndex, _groupIdOffset, _dummy0, _dummy1; } controlUniforms { mip, pixelsProcessed, 0, 0 };
				const UniformsStream::ImmediateData immData[] = { MakeOpaqueIteratorRange(controlUniforms) };
				UniformsStream us;
				us._resourceViews = MakeIteratorRange(resViews);
				us._immediateData = MakeIteratorRange(immData);
				
				auto thisCmdList = std::min(totalPixelCount - pixelsProcessed, pixelsPerCmdList);
				Log(Verbose) << "Attempting " << thisCmdList << " pixels" << std::endl;
				computeOp->Dispatch(*threadContext, thisCmdList, 1, 1, us);

				pixelsProcessed += thisCmdList;
				if (pixelsProcessed >= totalPixelCount) break;		// exit now to avoid a tiny cmd list after the last dispatch

				auto start = std::chrono::steady_clock::now();
				threadContext->CommitCommands(CommitCommandsFlags::WaitForCompletion);
				auto elapsed = std::chrono::steady_clock::now() - start;
				Log(Verbose) << "Processing " << thisCmdList << " pixels took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << std::endl;
				// On windows with default settings, timeouts begin at 2 seconds
				if (elapsed < std::chrono::milliseconds(750)) {
					pixelsPerCmdList *= 1 << IntegerLog2(uint32_t(1500 / std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
				}
			}
		}

		// We need a barrier before the transfer in DataSourceFromResourceSynchronized
		Metal::BarrierHelper{metalContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::TransferSrc);

		auto result = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, computeOp->GetDependencyValidation());
		// Release the command buffer pool, because Vulkan requires pumping the command buffer destroys regularly,
		// and we may not be doing that in this thread for awhile
		if (auto* threadContextVulkan = (RenderCore::IThreadContextVulkan*)threadContext->QueryInterface(TypeHashCode<RenderCore::IThreadContextVulkan>)) {
			threadContext->CommitCommands();
			threadContextVulkan->ReleaseCommandBufferPool();
		}
		return result;
	}

}}

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
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IntermediatesStore.h"
#include "../../Utility/BitUtils.h"
#include "../../xleres/FileList.h"

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
			for (const auto& sr:subResources) {
				Metal::ResourceMap map {
					*_device, *_resource, Metal::ResourceMap::Mode::Read,
					sr._id};
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

	ProcessedTexture EquRectFilter(BufferUploads::IAsyncDataSource& dataSrc, const TextureDesc& targetDesc, EquRectFilterMode filter)
	{
		// We need to create a texture from the data source and run a shader process on it to generate
		// an output cubemap. We'll do this on the GPU and copy the results back into a new IAsyncDataSource
		if (filter != EquRectFilterMode::ProjectToSphericalHarmonic)
			assert(ActualArrayLayerCount(targetDesc) == 6 && targetDesc._dimensionality == TextureDesc::Dimensionality::CubeMap);
		auto threadContext = GetThreadContext();
		
		ProcessedTexture result;

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Input"));
		const auto pushConstantsBinding = Hash64("FilterPassParams");

		::Assets::PtrToMarkerPtr<IComputeShaderOperator> computeOpFuture;
		if (filter == EquRectFilterMode::ToCubeMap) {
			usi.BindResourceView(1, Hash64("OutputArray"));
 			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				EQUIRECTANGULAR_TO_CUBE_HLSL ":EquRectToCube",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
			// todo -- we really want to extract the full set of dependencies from the depVal
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(EQUIRECTANGULAR_TO_CUBE_HLSL));
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(TOOLSHELPER_OPERATORS_PIPELINE));
		} else if (filter == EquRectFilterMode::ToGlossySpecular) {
			usi.BindResourceView(1, Hash64("OutputArray"));
			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				IBL_PREFILTER_HLSL ":EquiRectFilterGlossySpecular",
				// IBL_PREFILTER_HLSL ":ReferenceDiffuseFilter",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(IBL_PREFILTER_HLSL));
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(TOOLSHELPER_OPERATORS_PIPELINE));
		} else {
			assert(filter == EquRectFilterMode::ProjectToSphericalHarmonic);
			usi.BindResourceView(1, Hash64("Output"));
			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelineCollection>(threadContext->GetDevice()),
				IBL_PREFILTER_HLSL ":ProjectToSphericalHarmonic",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(IBL_PREFILTER_HLSL));
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(TOOLSHELPER_OPERATORS_PIPELINE));
		}

		auto inputRes = CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, 0, GPUAccess::Read|GPUAccess::Write, targetDesc, "texture-compiler"));
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		auto inputView = inputRes->CreateTextureView(BindFlag::ShaderResource);
		for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
			TextureViewDesc view;
			view._mipRange = {mip, 1};
			auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
			IResourceView* resViews[] = { inputView.get(), outputView.get() };
			auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

			UniformsStream us;
			us._resourceViews = MakeIteratorRange(resViews);
			computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);

			if (filter == EquRectFilterMode::ToCubeMap) {
				auto passCount = (mipDesc._width+7)/8 * (mipDesc._height+7)/8 * 6;
				for (unsigned p=0; p<passCount; ++p) {
					struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, p, passCount, 0 };
					computeOp->Dispatch(1, 1, 1, MakeOpaqueIteratorRange(filterPassParams));
				}
			} else if (filter == EquRectFilterMode::ToGlossySpecular) {
				auto pixelCount = mipDesc._width * mipDesc._height * ActualArrayLayerCount(targetDesc);
				auto revMipIdx = IntegerLog2(std::max(mipDesc._width, mipDesc._height));
				auto passesPerPixel = 16u-std::min(revMipIdx, 7u);		// increase the number of passes per pixel for lower mip maps, where there is greater roughness
				auto dispatchCount = passesPerPixel*pixelCount;
				auto dispatchesPerCommit = std::min(32768u, pixelCount);
				for (unsigned d=0; d<dispatchCount; ++d) {
					struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, d, dispatchCount, 0 };
					computeOp->Dispatch(1, 1, 1, MakeOpaqueIteratorRange(filterPassParams));

					if ((d%dispatchesPerCommit) == (dispatchesPerCommit-1)) {
						computeOp->EndDispatches();
						threadContext->CommitCommands();
						computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);
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
				computeOp->Dispatch(targetDesc._width, 1, 1);
			}

			computeOp->EndDispatches();
		}

		auto depVal = ::Assets::GetDepValSys().Make();
		depVal.RegisterDependency(computeOp->GetDependencyValidation());
		depVal.RegisterDependency(dataSrc.GetDependencyValidation());
		result._newDataSource = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
		return result;
	}

	ProcessedTexture GenerateFromComputeShader(StringSection<> shader, const TextureDesc& targetDesc)
	{
		auto threadContext = GetThreadContext();
		
		ProcessedTexture result;
		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Output"));
		usi.BindImmediateData(0, Hash64("FilterPassParams"));

 		auto computeOpFuture = CreateComputeOperator(
			std::make_shared<PipelineCollection>(threadContext->GetDevice()),
			shader, {}, TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain", usi);
		// todo -- we really want to extract the full set of dependencies from the depVal
		result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(shader));
		result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(TOOLSHELPER_OPERATORS_PIPELINE));

		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, 0, GPUAccess::Read|GPUAccess::Write, targetDesc, "texture-compiler"));
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
			TextureViewDesc view;
			view._mipRange = {mip, 1};
			auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
			IResourceView* resViews[] = { outputView.get() };
			struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, 0, 1, 0 };
			const UniformsStream::ImmediateData immData[] = { MakeOpaqueIteratorRange(filterPassParams) };
			UniformsStream us;
			us._resourceViews = MakeIteratorRange(resViews);
			us._immediateData = MakeIteratorRange(immData);
			auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
			computeOp->Dispatch(*threadContext, (mipDesc._width+7)/8, (mipDesc._height+7)/8, 1, us);
		}

		auto depVal = ::Assets::GetDepValSys().Make();
		depVal.RegisterDependency(computeOp->GetDependencyValidation());
		result._newDataSource = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
		return result;
	}

}}

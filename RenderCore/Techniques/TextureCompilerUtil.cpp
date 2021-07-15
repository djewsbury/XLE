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
#include "../IThreadContext.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/IntermediatesStore.h"

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
		assert(targetDesc._arrayCount == 6 && targetDesc._dimensionality == TextureDesc::Dimensionality::CubeMap);
		auto threadContext = GetThreadContext();
		
		ProcessedTexture result;

		UniformsStreamInterface usi;
		usi.BindResourceView(0, Hash64("Input"));
		usi.BindResourceView(1, Hash64("OutputArray"));
		const auto pushConstantsBinding = Hash64("FilterPassParams");

		::Assets::PtrToFuturePtr<IComputeShaderOperator> computeOpFuture;
		if (filter == EquRectFilterMode::ToCubeMap) {
 			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelinePool>(threadContext->GetDevice(), Services::GetCommonResources()),
				"xleres/ToolsHelper/EquirectangularToCube.hlsl:EquRectToCube",
				{},
				"xleres/ToolsHelper/operators.pipeline:ComputeMain",
				usi);
			// todo -- we really want to extract the full set of dependencies from the depVal
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState("xleres/ToolsHelper/EquirectangularToCube.hlsl"));
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState("xleres/ToolsHelper/operators.pipeline"));
		} else {
			assert(filter == EquRectFilterMode::ToGlossySpecular);
			computeOpFuture = CreateComputeOperator(
				std::make_shared<PipelinePool>(threadContext->GetDevice(), Services::GetCommonResources()),
				"xleres/ToolsHelper/IBLPrefilter.hlsl:EquiRectFilterGlossySpecular",
				{},
				"xleres/ToolsHelper/operators.pipeline:ComputeMain",
				usi);
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState("xleres/ToolsHelper/IBLPrefilter.hlsl"));
			result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState("xleres/ToolsHelper/operators.pipeline"));
		}

		auto inputRes = CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, 0, GPUAccess::Read|GPUAccess::Write, targetDesc, "texture-compiler"));
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		auto inputView = inputRes->CreateTextureView(BindFlag::ShaderResource);
		for (unsigned mip=0; mip<std::max(1u, (unsigned)targetDesc._mipCount); ++mip) {
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
			} else {
				assert(filter == EquRectFilterMode::ToGlossySpecular);
				auto passCount = mipDesc._width * mipDesc._height;
				for (unsigned p=0; p<passCount; ++p) {
					struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, p, passCount, 0 };
					computeOp->Dispatch(1, 1, 6, MakeOpaqueIteratorRange(filterPassParams));
				}
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
			std::make_shared<PipelinePool>(threadContext->GetDevice(), Services::GetCommonResources()),
			shader, {}, "xleres/ToolsHelper/operators.pipeline:ComputeMain", usi);
		// todo -- we really want to extract the full set of dependencies from the depVal
		result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState(shader));
		result._depFileStates.push_back(::Assets::IntermediatesStore::GetDependentFileState("xleres/ToolsHelper/operators.pipeline"));

		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, 0, GPUAccess::Read|GPUAccess::Write, targetDesc, "texture-compiler"));
		Metal::CompleteInitialization(*Metal::DeviceContext::Get(*threadContext), {outputRes.get()});
		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		for (unsigned mip=0; mip<std::max(1u, (unsigned)targetDesc._mipCount); ++mip) {
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

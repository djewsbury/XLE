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
#include "../IAnnotator.h"
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

	template<int Base, typename FloatType=float>
		static FloatType RadicalInverseSpecialized(uint64_t a)
	{
		const FloatType reciprocalBase = FloatType(1.0) / FloatType(Base);
		uint64_t reversedDigits = 0;
		FloatType reciprocalBaseN = 1;
		while (a) {
			uint64_t next = a / Base;
			uint64_t digit = a - next * Base;
			reversedDigits = reversedDigits * Base + digit;
			reciprocalBaseN *= reciprocalBase;
			a = next;
		}
		return reversedDigits * reciprocalBaseN;
	}

	class HaltonSamplerHelper
	{
	public:
		std::shared_ptr<IResourceView> _pixelToSampleIndex;
		std::shared_ptr<IResourceView> _pixelToSampleIndexParams;		// cbuffer

		// todo -- consider max resolution
		HaltonSamplerHelper(IThreadContext& threadContext, unsigned width, unsigned height)
		{
			// For a given texture, we're going to create a lookup table that converts from 
			// xy coords to first sample index in the Halton sequence
			//
			// That is, if (radical-inverse-base-2(i), radical-inverse-base-3(i)) is the xy
			// coords associated with sample i; we want to be able to go backwards and get i
			// from a given sample coords
			//
			// This will then allow us to generate more well distributed numbers based on i,
			// by using the deeper dimensions of the Halton sequence
			//
			// Furthermore, we can cause samples in a given pixel to repeat with a constant
			// interval by multiplying the sampling coordinate space by a specific scale
			//
			// See pbr-book chapter 7.4 for more reference on this
			// Though, we're not going to use a mathematically sophisticated method for this,
			// instead something pretty rudimentary

			float j = std::ceil(std::log2(float(width)));
			float log3Height = std::log(float(height))/std::log(3.0f);
			float k = std::ceil(log3Height);
			float scaledWidth = std::pow(2.f, j), scaledHeight = std::pow(3.f, k);

			auto data = std::make_unique<unsigned[]>(width*height);
			std::memset(data.get(), 0, sizeof(unsigned)*width*height);

			// We can do this in a smarter way by using the inverse-radical-inverse, and solving some simultaneous
			// equations with modular arithmetic. But since we're building a lookup table anyway, that doesn't seem
			// of any practical purpose
			unsigned repeatingStride = (unsigned)(scaledWidth*scaledHeight);
			for (unsigned sampleIdx=0; sampleIdx<repeatingStride; ++sampleIdx) {
				auto x = unsigned(scaledWidth * RadicalInverseSpecialized<2>(sampleIdx)), 
					y = unsigned(scaledHeight * RadicalInverseSpecialized<3>(sampleIdx));
				if (x >= width || y >= height) continue;
				data[x+y*width] = sampleIdx;
			}

			auto texture = threadContext.GetDevice()->CreateResource(
				CreateDesc(BindFlag::ShaderResource|BindFlag::TransferDst, TextureDesc::Plain2D(width, height, Format::R32_UINT)),
				"sample-idx-lookup");
			Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(
				*texture, 
				SubResourceInitData{MakeIteratorRange(data.get(), PtrAdd(data.get(), sizeof(unsigned)*width*height))},
				Format::R32_UINT,
				UInt3{width, height, 1},
				TexturePitches{width*sizeof(unsigned), width*height*sizeof(unsigned)});

			_pixelToSampleIndex = texture->CreateTextureView();

			struct Uniforms
			{
				float _j, _k;
				unsigned _repeatingStride;
				unsigned _dummy;
			} uniforms {
				j, k, repeatingStride, 0
			};

			auto cbuffer = threadContext.GetDevice()->CreateResource(
				CreateDesc(BindFlag::ConstantBuffer|BindFlag::TransferDst, LinearBufferDesc::Create(sizeof(Uniforms))),
				"sample-idx-uniforms");
			Metal::DeviceContext::Get(threadContext)->BeginBlitEncoder().Write(
				*cbuffer, MakeOpaqueIteratorRange(uniforms));
			_pixelToSampleIndexParams = cbuffer->CreateBufferView();
		}
	};

	struct BalancedSamplingShaderHelper
	{
	public:
		struct Uniforms {
			unsigned _thisPassSampleOffset, _thisPassSampleCount, _thisPassSampleStride, _totalSampleCount;
		};

		Uniforms BeginDispatch()
		{
			assert(_samplesPerCmdList != 0);
			auto thisCmdList = std::min(_totalSampleCount - _samplesProcessed, _samplesPerCmdList);
			auto initialSamplesProcessed = _samplesProcessed;
			_samplesProcessed += thisCmdList;
			return { initialSamplesProcessed, thisCmdList, 1, _totalSampleCount };
		}

		bool Finished() const { return _samplesProcessed == _totalSampleCount; }

		void CommitAndTimeCommandList(IThreadContext& threadContext, const Uniforms& uniforms, StringSection<> name)
		{
			auto start = std::chrono::steady_clock::now();
			threadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
			auto elapsed = std::chrono::steady_clock::now() - start;
			Log(Verbose) << "[" << name << "] Processing " << uniforms._thisPassSampleCount << " samples took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << std::endl;
			// On windows with default settings, timeouts begin at 2 seconds
			const unsigned idealCmdListCostMS = 500; // 1500;
			if (uniforms._thisPassSampleCount == _samplesPerCmdList && elapsed < std::chrono::milliseconds(idealCmdListCostMS/2)) {
				// increase by powers of two, roughly by proportion, just not too quickly
				auto increaser = IntegerLog2(uint32_t(idealCmdListCostMS / std::max(1u, (unsigned)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count())));
				increaser = std::min(increaser, 4u);
				if (xl_clz4(_samplesPerCmdList) >= increaser) {
					assert(_samplesPerCmdList << increaser);
					_samplesPerCmdList <<= increaser;
				}
			}
		}

		BalancedSamplingShaderHelper(unsigned totalSampleCount)
		: _totalSampleCount(totalSampleCount) {}
	private:
		unsigned _samplesProcessed = 0;
		unsigned _samplesPerCmdList = 256;
		unsigned _totalSampleCount = 0;
	};


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
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		auto pipelineCollection = std::make_shared<PipelineCollection>(threadContext->GetDevice());

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Input"_h);
		constexpr auto pushConstantsBinding = "FilterPassParams"_h;

		::Assets::PtrToMarkerPtr<IComputeShaderOperator> computeOpFuture;
		if (filter == EquRectFilterMode::ToCubeMap) {
			usi.BindResourceView(1, "OutputArray"_h);
 			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				EQUIRECTANGULAR_TO_CUBE_HLSL ":EquRectToCube",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquRectFilterMode::ToGlossySpecular) {
			usi.BindResourceView(1, "OutputArray"_h);
			usi.BindResourceView(2, "MarginalHorizontalCDF"_h);
			usi.BindResourceView(3, "MarginalVerticalCDF"_h);
			usi.BindResourceView(4, "SampleIndexLookup"_h);
			usi.BindResourceView(5, "SampleIndexUniforms"_h);
			usi.BindImmediateData(0, "ControlUniforms"_h);
			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":EquiRectFilterGlossySpecular",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquRectFilterMode::ToGlossySpecularReference) {
			usi.BindResourceView(1, "OutputArray"_h);
			usi.BindImmediateData(0, "ControlUniforms"_h);
 			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":EquiRectFilterGlossySpecular_Reference",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else {
			assert(filter == EquRectFilterMode::ProjectToSphericalHarmonic);
			usi.BindResourceView(1, "Output"_h);
			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":ProjectToSphericalHarmonic",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		}

		auto inputRes = CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, targetDesc), "texture-compiler");
		Metal::CompleteInitialization(metalContext, {outputRes.get()});
		if (auto* threadContextVulkan = query_interface_cast<IThreadContextVulkan*>(threadContext.get()))
			threadContextVulkan->AttachNameToCommandList(s_equRectFilterName);

		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		auto depVal = ::Assets::GetDepValSys().Make();
		depVal.RegisterDependency(computeOp->GetDependencyValidation());
		depVal.RegisterDependency(dataSrc.GetDependencyValidation());

		auto inputView = inputRes->CreateTextureView(BindFlag::ShaderResource);

		if (filter == EquRectFilterMode::ToCubeMap || filter == EquRectFilterMode::ProjectToSphericalHarmonic) {
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
				} else {
					assert(filter == EquRectFilterMode::ProjectToSphericalHarmonic);
					dispatchGroup.Dispatch(targetDesc._width, 1, 1);
				}

				dispatchGroup = {};
			}
		} else if (filter == EquRectFilterMode::ToGlossySpecular) {
			// glossy specular
			auto horizontalDensitiesFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":CalculateHorizontalMarginalDensities",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
			auto normalizeDensitiesFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":NormalizeMarginalDensities",
				{},
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
			horizontalDensitiesFuture->StallWhilePending();
			normalizeDensitiesFuture->StallWhilePending();
			auto horizontalDensities = horizontalDensitiesFuture->Actualize();
			auto normalizeDensities = normalizeDensitiesFuture->Actualize();

			depVal.RegisterDependency(horizontalDensities->GetDependencyValidation());
			depVal.RegisterDependency(normalizeDensities->GetDependencyValidation());

			auto inputDesc = inputRes->GetDesc()._textureDesc;
			const unsigned densityBlock = 16;
			UInt2 densitiesDims { (inputDesc._width+densityBlock-1)/densityBlock, (inputDesc._height+densityBlock-1)/densityBlock };
			auto marginalHorizontalCFG = threadContext->GetDevice()->CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, TextureDesc::Plain2D(densitiesDims[0], densitiesDims[1], Format::R32_FLOAT)),
				"marginal-horizontal-cdf")->CreateTextureView(BindFlag::UnorderedAccess);
			auto marginalVerticalCFG = threadContext->GetDevice()->CreateResource(
				CreateDesc(BindFlag::UnorderedAccess, TextureDesc::Plain1D(densitiesDims[1], Format::R32_FLOAT)),
				"marginal-vertical-cdf")->CreateTextureView(BindFlag::UnorderedAccess);
			RenderCore::IResource* toComplete[] { marginalHorizontalCFG->GetResource().get(), marginalVerticalCFG->GetResource().get() };
			Metal::CompleteInitialization(metalContext, toComplete);

			IResourceView* resViews[] = { inputView.get(), nullptr, marginalHorizontalCFG.get(), marginalVerticalCFG.get(), nullptr, nullptr };
			UniformsStream us;
			us._resourceViews = MakeIteratorRange(resViews);

			horizontalDensities->Dispatch(*threadContext, (densitiesDims[0]+8-1)/8, (densitiesDims[1]+8-1)/8, 1, us);
			Metal::BarrierHelper(metalContext).Add(*marginalHorizontalCFG->GetResource(), BindFlag::UnorderedAccess, BindFlag::UnorderedAccess);
			normalizeDensities->Dispatch(*threadContext, 1, 1, 1, us);
			Metal::BarrierHelper(metalContext)
				.Add(*marginalHorizontalCFG->GetResource(), BindFlag::UnorderedAccess, BindFlag::UnorderedAccess)
				.Add(*marginalVerticalCFG->GetResource(), BindFlag::UnorderedAccess, BindFlag::UnorderedAccess);

			threadContext->CommitCommands(CommitCommandsFlags::WaitForCompletion); // sync with GPU, because of timing work below

			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				resViews[1] = outputView.get();
				auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

				HaltonSamplerHelper samplerHelper{*threadContext, mipDesc._width, mipDesc._height};
				resViews[4] = samplerHelper._pixelToSampleIndex.get();
				resViews[5] = samplerHelper._pixelToSampleIndexParams.get();

				Metal::BarrierHelper{metalContext}.Add(*samplerHelper._pixelToSampleIndex->GetResource(), BindFlag::TransferDst, BindFlag::ShaderResource);

				{
					auto revMipIdx = IntegerLog2(std::max(mipDesc._width, mipDesc._height));
					auto passesPerPixel = 16u-std::min(revMipIdx, 7u);		// increase the number of passes per pixel for lower mip maps, where there is greater roughness
					auto samplesPerPass = 1024u; // 64*1024;
					auto totalSampleCount = passesPerPixel * samplesPerPass;

					BalancedSamplingShaderHelper samplingShaderHelper(totalSampleCount);
					while (!samplingShaderHelper.Finished()) {
						struct ControlUniforms
						{
							BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
							unsigned _mipIndex, _dummy0, _dummy1, _dummy2;
						} controlUniforms {
							samplingShaderHelper.BeginDispatch(),
							mip, 0, 0, 0
						};

						UniformsStream us;
						us._resourceViews = MakeIteratorRange(resViews);
						UniformsStream::ImmediateData immDatas[] = { MakeOpaqueIteratorRange(controlUniforms) };
						us._immediateData = immDatas;

						computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 6, us);

						if ((mip+1) == targetDesc._mipCount && samplingShaderHelper.Finished()) break;		// exit now to avoid a tiny cmd list after the last dispatch

						if (progressiveResults) {
							Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::TransferSrc);
						} else {
							Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::UnorderedAccess);
						}

						samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, "GlossySpecularBuild");
						if (progressiveResults) {
							auto intermediateData = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
							// note -- we could dispatch to another thread; but there's a potential risk of out of order execution
							progressiveResults(intermediateData);
							Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::TransferSrc, BindFlag::UnorderedAccess);
						}
					}

				}
			}
		} else if (filter == EquRectFilterMode::ToGlossySpecularReference) {
			auto inputDesc = inputRes->GetDesc()._textureDesc;
			auto totalSampleCount = inputDesc._width*inputDesc._height;
			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { inputView.get(), outputView.get() };
				auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

				UniformsStream us;
				us._resourceViews = MakeIteratorRange(resViews);

				BalancedSamplingShaderHelper samplingShaderHelper(totalSampleCount);
				while (!samplingShaderHelper.Finished()) {
					struct ControlUniforms
					{
						BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
						unsigned _mipIndex, _dummy0, _dummy1, _dummy2;
					} controlUniforms {
						samplingShaderHelper.BeginDispatch(),
						mip, 0, 0, 0
					};

					UniformsStream us;
					us._resourceViews = MakeIteratorRange(resViews);
					UniformsStream::ImmediateData immDatas[] = { MakeOpaqueIteratorRange(controlUniforms) };
					us._immediateData = immDatas;

					computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 6, us);

					if ((mip+1) == targetDesc._mipCount && samplingShaderHelper.Finished()) break;		// exit now to avoid a tiny cmd list after the last dispatch

					if (progressiveResults) {
						Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::TransferSrc);
					} else {
						Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::UnorderedAccess);
					}

					samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, "GlossySpecularReference");
					if (progressiveResults) {
						auto intermediateData = std::make_shared<DataSourceFromResourceSynchronized>(threadContext, outputRes, depVal);
						// note -- we could dispatch to another thread; but there's a potential risk of out of order execution
						progressiveResults(intermediateData);
						Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::TransferSrc, BindFlag::UnorderedAccess);
					}
				}
			}
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

	std::shared_ptr<BufferUploads::IAsyncDataSource> GenerateFromSamplingComputeShader(StringSection<> shader, const TextureDesc& targetDesc, unsigned totalSampleCount)
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
		Metal::CompleteInitialization(metalContext, {outputRes.get()});
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
			// We will start with a small number of samples per pixel and slowly increase while it seems safe
			// Let's do this with the CPU & GPU synced, because we don't want this thread to 
			// get ahead of the GPU anyway, and we also don't want to release this thread to the 
			// thread pool while waiting for the GPU

			BalancedSamplingShaderHelper samplingShaderHelper(totalSampleCount);
			while (true) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { outputView.get() };
				struct ControlUniforms
				{
					BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
					unsigned _mipIndex, _dummy0, _dummy1, _dummy2;
				} controlUniforms {
					samplingShaderHelper.BeginDispatch(), 
					mip, 0, 0, 0 
				};
				const UniformsStream::ImmediateData immData[] = { MakeOpaqueIteratorRange(controlUniforms) };
				UniformsStream us;
				us._resourceViews = MakeIteratorRange(resViews);
				us._immediateData = MakeIteratorRange(immData);
				
				computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 1, us);

				if (samplingShaderHelper.Finished()) break;		// exit now to avoid a tiny cmd list after the last dispatch
				samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, shader);
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

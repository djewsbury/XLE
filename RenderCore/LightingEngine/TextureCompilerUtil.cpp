// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompilerUtil.h"
#include "BlueNoiseGenerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/PipelineOperators.h"
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

namespace RenderCore { namespace LightingEngine
{
	class DataSourceFromResourceSynchronized : public BufferUploads::IAsyncDataSource
	{
	public:
		virtual std::future<ResourceDesc> GetDesc () override
		{
			std::promise<ResourceDesc> promise;
			auto result = promise.get_future(); 
			auto desc = _resource->GetDesc();
			desc._bindFlags = 0;
			desc._allocationRules = 0;		// don't pass the bind flags & allocation rules onto whoever uses this
			promise.set_value(desc);
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
			_resource = Techniques::DestageResource(*threadContext, resource);
		}

		std::shared_ptr<IDevice> _device;
		std::shared_ptr<IResource> _resource;
		::Assets::DependencyValidation _depVal;
	};

	static std::string s_equRectFilterName { "texture-compiler (EquirectFilter)" };
	static std::string s_fromComputeShaderName { "texture-compiler (GenerateFromComputeShader)" };

	struct BalancedSamplingShaderHelper
	{
	public:
		struct Uniforms {
			unsigned _thisPassSampleOffset, _thisPassSampleCount, _thisPassSampleStride, _totalSampleCount;
		};

		Uniforms ConfigureNextDispatch()
		{
			assert(_samplesPerCmdList != 0);
			auto thisCmdList = std::min(_totalSampleCount - _samplesProcessed, _samplesPerCmdList);
			auto initialSamplesProcessed = _samplesProcessed;
			_samplesProcessed += thisCmdList;
			return { initialSamplesProcessed, thisCmdList, 1, _totalSampleCount };
		}

		bool Finished() const { return _samplesProcessed == _totalSampleCount; }
		unsigned TotalSampleCount() const { return _totalSampleCount; }
		unsigned SamplesProcessedCount () const { return _samplesProcessed; }
		void ResetSamplesProcessed() { _samplesProcessed = 0; }

		void CommitAndTimeCommandList(IThreadContext& threadContext, const Uniforms& uniforms, StringSection<> name)
		{
			auto start = std::chrono::steady_clock::now();
			threadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
			auto elapsed = std::chrono::steady_clock::now() - start;
			Log(Verbose) << "[" << name << "] Processing " << uniforms._thisPassSampleCount << " samples took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << std::endl;
			// On windows with default settings, timeouts begin at 2 seconds
			if (uniforms._thisPassSampleCount == _samplesPerCmdList && elapsed < std::chrono::milliseconds(_idealCmdListCostMS/2)) {
				// increase by powers of two, roughly by proportion, just not too quickly
				auto increaser = IntegerLog2(uint32_t(_idealCmdListCostMS / std::max(1u, (unsigned)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count())));
				increaser = std::min(increaser, 4u);
				if (xl_clz4(_samplesPerCmdList) >= increaser) {
					assert(_samplesPerCmdList << increaser);
					_samplesPerCmdList <<= increaser;
					_samplesPerCmdList = std::min(_samplesPerCmdList, _maxSamplesPerCmdList);
				}
			}
		}

		BalancedSamplingShaderHelper(unsigned totalSampleCount, unsigned idealCmdListCostMS, unsigned maxSamplesPerCmdList)
		: _totalSampleCount(totalSampleCount), _idealCmdListCostMS(idealCmdListCostMS), _maxSamplesPerCmdList(maxSamplesPerCmdList)
		{
			assert(_totalSampleCount);
			assert(_idealCmdListCostMS);
			_samplesPerCmdList = std::min(_samplesPerCmdList, _maxSamplesPerCmdList);
		}
	private:
		unsigned _samplesProcessed = 0;
		unsigned _samplesPerCmdList = 256;
		unsigned _totalSampleCount = 0;
		unsigned _idealCmdListCostMS = 1500;
		unsigned _maxSamplesPerCmdList = ~0u;
	};

	std::shared_ptr<BufferUploads::IAsyncDataSource> EquirectFilter(
		BufferUploads::IAsyncDataSource& dataSrc, const TextureDesc& targetDesc,
		EquirectFilterMode filter,
		const EquirectFilterParams& params,
		::Assets::OperationContextHelper& opHelper,
		const ProgressiveTextureFn& progressiveResults)
	{
		// We need to create a texture from the data source and run a shader process on it to generate
		// an output cubemap. We'll do this on the GPU and copy the results back into a new IAsyncDataSource
		if (filter != EquirectFilterMode::ProjectToSphericalHarmonic)
			assert(ActualArrayLayerCount(targetDesc) == 6 && targetDesc._dimensionality == TextureDesc::Dimensionality::CubeMap);

		auto threadContext = Techniques::GetThreadContext();
		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		auto pipelineCollection = std::make_shared<Techniques::PipelineCollection>(threadContext->GetDevice());

		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Input"_h);
		constexpr auto pushConstantsBinding = "FilterPassParams"_h;

		ParameterBox sharedParameterBox;
		sharedParameterBox.SetParameter("UPDIRECTION", params._upDirection);

		::Assets::PtrToMarkerPtr<Techniques::IComputeShaderOperator> computeOpFuture;
		if (filter == EquirectFilterMode::ToCubeMap) {
			usi.BindResourceView(1, "OutputArray"_h);
 			computeOpFuture = Techniques::CreateComputeOperator(
				pipelineCollection,
				EQUIRECTANGULAR_TO_CUBE_HLSL ":EquirectToCube",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquirectFilterMode::ToCubeMapBokeh) {
			usi.BindResourceView(1, "OutputArray"_h);
 			computeOpFuture = Techniques::CreateComputeOperator(
				pipelineCollection,
				EQUIRECTANGULAR_TO_CUBE_BOKEH_HLSL ":EquirectToCubeBokeh",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquirectFilterMode::ToGlossySpecular) {
			usi.BindResourceView(1, "OutputArray"_h);
			usi.BindResourceView(2, "MarginalHorizontalCDF"_h);
			usi.BindResourceView(3, "MarginalVerticalCDF"_h);
			usi.BindResourceView(4, "SampleIndexLookup"_h);
			usi.BindResourceView(5, "SampleIndexUniforms"_h);
			usi.BindImmediateData(0, "ControlUniforms"_h);
			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":EquirectFilterGlossySpecular",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquirectFilterMode::ToGlossySpecularReference) {
			usi.BindResourceView(1, "OutputArray"_h);
			usi.BindImmediateData(0, "ControlUniforms"_h);
 			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":EquirectFilterGlossySpecular_Reference",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else if (filter == EquirectFilterMode::ToDiffuseReference) {
			usi.BindResourceView(1, "OutputArray"_h);
			usi.BindImmediateData(0, "ControlUniforms"_h);
			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":EquirectFilterDiffuse_Reference",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		} else {
			assert(filter == EquirectFilterMode::ProjectToSphericalHarmonic);
			usi.BindResourceView(1, "Output"_h);
			computeOpFuture = CreateComputeOperator(
				pipelineCollection,
				IBL_PREFILTER_HLSL ":ProjectToSphericalHarmonic",
				std::move(sharedParameterBox),
				TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain",
				usi);
		}

		auto inputRes = Techniques::CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
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

		if (filter == EquirectFilterMode::ToCubeMap || filter == EquirectFilterMode::ToCubeMapBokeh || filter == EquirectFilterMode::ProjectToSphericalHarmonic) {
			unsigned totalDispatchCount = 0, completedDispatchCount = 0;
			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip)
				if (filter == EquirectFilterMode::ToCubeMap) {
					auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
					auto passCount = (mipDesc._width+7)/8 * (mipDesc._height+7)/8 * 6;
					totalDispatchCount += passCount;
				} else
					++totalDispatchCount;

			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { inputView.get(), outputView.get() };
				auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

				UniformsStream us;
				us._resourceViews = MakeIteratorRange(resViews);
				auto dispatchGroup = computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);

				if (filter == EquirectFilterMode::ToCubeMap) {
					auto passCount = (mipDesc._width+7)/8 * (mipDesc._height+7)/8 * 6;
					for (unsigned p=0; p<passCount; ++p) {
						struct FilterPassParams { unsigned _mipIndex, _passIndex, _passCount, _dummy; } filterPassParams { mip, p, passCount, 0 };
						dispatchGroup.Dispatch(1, 1, 1, MakeOpaqueIteratorRange(filterPassParams));
						++completedDispatchCount; if (opHelper) opHelper.SetProgress(completedDispatchCount, totalDispatchCount);
					}
				} else if (filter == EquirectFilterMode::ToCubeMapBokeh) {

					BalancedSamplingShaderHelper samplingShaderHelper(params._sampleCount, params._idealCmdListCostMS, params._maxSamplesPerCmdList);
					while (!samplingShaderHelper.Finished()) {
						struct ControlUniforms
						{
							BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
						} controlUniforms {
							samplingShaderHelper.ConfigureNextDispatch(),
						};

						dispatchGroup.Dispatch((mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 6, MakeOpaqueIteratorRange(controlUniforms));

						if ((mip+1) == targetDesc._mipCount && samplingShaderHelper.Finished()) break;		// exit now to avoid a tiny cmd list after the last dispatch

						dispatchGroup = {};
						Metal::BarrierHelper{*threadContext}.Add(*outputRes, BindFlag::UnorderedAccess, BindFlag::UnorderedAccess);
						samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, "ToCubeMapBokeh");
						dispatchGroup = computeOp->BeginDispatches(*threadContext, us, {}, pushConstantsBinding);		// hack -- because we're ending the display list we have to begin and end the dispatch group
					}

					++completedDispatchCount; if (opHelper) opHelper.SetProgress(completedDispatchCount, totalDispatchCount);

				} else {
					assert(filter == EquirectFilterMode::ProjectToSphericalHarmonic);
					dispatchGroup.Dispatch(targetDesc._width, 1, 1);
					++completedDispatchCount; if (opHelper) opHelper.SetProgress(completedDispatchCount, totalDispatchCount);
				}

				dispatchGroup = {};
			}
		} else if (filter == EquirectFilterMode::ToGlossySpecular) {
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

			// We must limit the maximum dimensions of the sampling pattern significantly, because the number of samples
			// we can fit within 32bit limits is proportional to the number of pixels in this sampling pattern
			const unsigned maxSamplePatternWidth = 32, maxSamplePatternHeight = 27;
			std::vector<HaltonSamplerHelper> samplerHelpers;
			samplerHelpers.reserve(targetDesc._mipCount);
			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
				auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
				samplerHelpers.push_back(
					HaltonSamplerHelper{*threadContext, std::min(mipDesc._width, maxSamplePatternWidth), std::min(mipDesc._height, maxSamplePatternHeight)});
			}

			std::vector<BalancedSamplingShaderHelper> samplingShaderHelpers;
			samplingShaderHelpers.reserve(targetDesc._mipCount);
			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
				auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
				auto revMipIdx = IntegerLog2(std::max(mipDesc._width, mipDesc._height));
				auto passesPerPixel = 16u-std::min(revMipIdx, 7u);		// increase the number of passes per pixel for lower mip maps, where there is greater roughness
				auto samplesPerPass = params._sampleCount; // approx 32u*1024u is a reasonable sample count
				auto totalSampleCount = passesPerPixel * samplesPerPass;
				// If you hit the following, the quantity of samples is going to exceed precision available with 32 bit ints
				assert((uint64_t(totalSampleCount)*uint64_t(samplerHelpers[mip]._repeatingStride)) < (1ull<<30ull));
				samplingShaderHelpers.push_back(BalancedSamplingShaderHelper{totalSampleCount, params._idealCmdListCostMS, params._maxSamplesPerCmdList});
			}

			uint64_t totalSampleCount = 0, samplesCompleted = 0;
			for (auto& s:samplingShaderHelpers) totalSampleCount += s.TotalSampleCount();

			// We need to mip all of the mips at the same time, rather than one mip at a time, so we will loop
			// better mips until they are all done
			// we record the samples/time separately for each mip, because it depends on the number of pixels
			std::vector<unsigned> activeMips;
			activeMips.reserve(targetDesc._mipCount);
			for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) activeMips.push_back(mip);
			while (!activeMips.empty()) {
				for (auto i=activeMips.begin(); i!=activeMips.end();) {
					auto mip = *i;
					auto& samplingShaderHelper = samplingShaderHelpers[mip];
					assert(!samplingShaderHelper.Finished());

					TextureViewDesc view;
					view._mipRange = {mip, 1};
					auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
					resViews[1] = outputView.get();
					auto mipDesc = CalculateMipMapDesc(targetDesc, mip);

					const auto& samplerHelper = samplerHelpers[mip];
					resViews[4] = samplerHelper._pixelToSampleIndex.get();
					resViews[5] = samplerHelper._pixelToSampleIndexParams.get();
					auto initialCompletedSamples = samplingShaderHelper.SamplesProcessedCount();

					struct ControlUniforms
					{
						BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
						unsigned _mipIndex, _mipCount, _arrayLayerIndex, _arrayLayerCount;
					} controlUniforms {
						samplingShaderHelper.ConfigureNextDispatch(),
						mip, targetDesc._mipCount, 0, 1
					};

					UniformsStream us;
					us._resourceViews = MakeIteratorRange(resViews);
					UniformsStream::ImmediateData immDatas[] = { MakeOpaqueIteratorRange(controlUniforms) };
					us._immediateData = immDatas;

					computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 6, us);

					// update the progress tracker (reduce sample counts a little bit, to avoid exceeding 32 bit integers)
					samplesCompleted += samplingShaderHelper.SamplesProcessedCount() - initialCompletedSamples;
					if (opHelper)
						opHelper.SetProgress(unsigned(samplesCompleted>>8ull), unsigned(totalSampleCount>>8ull));

					if (samplingShaderHelper.Finished()) {
						i = activeMips.erase(i);
					} else
						++i;

					if (activeMips.empty()) break;		// exit now to avoid a tiny cmd list after the last dispatch

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
						// Sleep & yield a little bit of GPU time to ensure that the rendering context can actually commit and complete cmd lists
						Threading::Sleep(8);
					}
				}
			}

		} else if (filter == EquirectFilterMode::ToGlossySpecularReference || filter == EquirectFilterMode::ToDiffuseReference) {
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

				BalancedSamplingShaderHelper samplingShaderHelper(totalSampleCount, params._idealCmdListCostMS, params._maxSamplesPerCmdList);
				while (!samplingShaderHelper.Finished()) {
					struct ControlUniforms
					{
						BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
						unsigned _mipIndex, _mipCount, _arrayLayerIndex, _arrayLayerCount;
					} controlUniforms {
						samplingShaderHelper.ConfigureNextDispatch(),
						mip, targetDesc._mipCount, 0, 1
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

					samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, (filter == EquirectFilterMode::ToGlossySpecularReference)?"GlossySpecularReference":"DiffuseReference");
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

		if (progressiveResults)
			progressiveResults(result);

		return result;
	}

	std::shared_ptr<BufferUploads::IAsyncDataSource> GenerateFromSamplingComputeShader(StringSection<> shader, const TextureDesc& targetDesc, unsigned totalSampleCount, unsigned idealCmdListCostMS, unsigned maxSamplesPerCmdList)
	{
		auto threadContext = Techniques::GetThreadContext();
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Output"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);

 		auto computeOpFuture = Techniques::CreateComputeOperator(
			std::make_shared<Techniques::PipelineCollection>(threadContext->GetDevice()),
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

			BalancedSamplingShaderHelper samplingShaderHelper(totalSampleCount, idealCmdListCostMS, maxSamplesPerCmdList);
			for (unsigned a=0; a<ActualArrayLayerCount(targetDesc); ++a) {
				samplingShaderHelper.ResetSamplesProcessed();

				TextureViewDesc view;
				view._mipRange = {mip, 1};
				view._arrayLayerRange = {a, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { outputView.get() };

				while (!samplingShaderHelper.Finished()) {
					struct ControlUniforms
					{
						BalancedSamplingShaderHelper::Uniforms _samplingShaderUniforms;
						unsigned _mipIndex, _mipCount, _arrayLayerIndex, _arrayLayerCount;
					} controlUniforms {
						samplingShaderHelper.ConfigureNextDispatch(),
						mip, targetDesc._mipCount, a, ActualArrayLayerCount(targetDesc)
					};
					const UniformsStream::ImmediateData immData[] = { MakeOpaqueIteratorRange(controlUniforms) };
					UniformsStream us;
					us._resourceViews = MakeIteratorRange(resViews);
					us._immediateData = MakeIteratorRange(immData);
					
					computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 1, us);

					if (samplingShaderHelper.Finished() && (a+1) == ActualArrayLayerCount(targetDesc) && (mip+1) == targetDesc._mipCount) break;		// exit now to avoid a tiny cmd list after the last dispatch
					samplingShaderHelper.CommitAndTimeCommandList(*threadContext, controlUniforms._samplingShaderUniforms, shader);
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

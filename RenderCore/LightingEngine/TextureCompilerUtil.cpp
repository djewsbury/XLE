// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TextureCompilerUtil.h"
#include "BlueNoiseGenerator.h"
#include "../Techniques/Techniques.h"
#include "../Techniques/DeferredShaderResource.h"
#include "../Techniques/PipelineOperators.h"
#include "../Techniques/Services.h"
#include "../Assets/TextureCompiler.h"
#include "../Assets/TextureCompilerRegistrar.h"
#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"
#include "../IDevice.h"
#include "../IAnnotator.h"
#include "../Vulkan/IDeviceVulkan.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/Marker.h"
#include "../../Assets/CompoundAsset.h"
#include "../../Assets/AssetsCore.h"
#include "../../Formatters/FormatterUtils.h"
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

	std::shared_ptr<BufferUploads::IAsyncDataSource> MakeAsyncDataSourceFromResource(
		std::shared_ptr<IThreadContext> threadContext, 
		std::shared_ptr<IResource> resource,
		::Assets::DependencyValidation depVal)
	{
		return std::make_shared<DataSourceFromResourceSynchronized>(std::move(threadContext), std::move(resource), std::move(depVal));
	}

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

	std::shared_ptr<BufferUploads::IAsyncDataSource> ConversionComputeShader(
		StringSection<> shader,
		BufferUploads::IAsyncDataSource& dataSrc,
		const TextureDesc& targetDesc)
	{
		auto threadContext = Techniques::GetThreadContext();
		
		UniformsStreamInterface usi;
		usi.BindResourceView(0, "Output"_h);
		usi.BindResourceView(1, "Input"_h);
		usi.BindImmediateData(0, "ControlUniforms"_h);

 		auto computeOpFuture = Techniques::CreateComputeOperator(
			std::make_shared<Techniques::PipelineCollection>(threadContext->GetDevice()),
			shader, {}, TOOLSHELPER_OPERATORS_PIPELINE ":ComputeMain", usi);

		auto& metalContext = *Metal::DeviceContext::Get(*threadContext);
		auto inputRes = Techniques::CreateResourceImmediately(*threadContext, dataSrc, BindFlag::ShaderResource);
		auto inputView = inputRes->CreateTextureView(BindFlag::ShaderResource);
		auto outputRes = threadContext->GetDevice()->CreateResource(CreateDesc(BindFlag::UnorderedAccess|BindFlag::TransferSrc, targetDesc), "texture-compiler");
		Metal::CompleteInitialization(metalContext, {outputRes.get()});
		if (auto* threadContextVulkan = (RenderCore::IThreadContextVulkan*)threadContext->QueryInterface(TypeHashCode<RenderCore::IThreadContextVulkan>))
			threadContextVulkan->AttachNameToCommandList(s_equRectFilterName);

		computeOpFuture->StallWhilePending();
		auto computeOp = computeOpFuture->Actualize();

		// run the actual compute shader once per output pixel
		for (unsigned mip=0; mip<targetDesc._mipCount; ++mip) {
			auto mipDesc = CalculateMipMapDesc(targetDesc, mip);
			for (unsigned a=0; a<ActualArrayLayerCount(targetDesc); ++a) {
				TextureViewDesc view;
				view._mipRange = {mip, 1};
				view._arrayLayerRange = {a, 1};
				auto outputView = outputRes->CreateTextureView(BindFlag::UnorderedAccess, view);
				IResourceView* resViews[] = { outputView.get(), inputView.get() };

				struct ControlUniforms
				{
					unsigned _mipIndex, _mipCount, _arrayLayerIndex, _arrayLayerCount;
				} controlUniforms {
					mip, targetDesc._mipCount, a, ActualArrayLayerCount(targetDesc)
				};
				const UniformsStream::ImmediateData immData[] = { MakeOpaqueIteratorRange(controlUniforms) };
				UniformsStream us;
				us._resourceViews = MakeIteratorRange(resViews);
				us._immediateData = MakeIteratorRange(immData);
				
				computeOp->Dispatch(*threadContext, (mipDesc._width+8-1)/8, (mipDesc._height+8-1)/8, 1, us);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::optional<EquirectFilterMode> AsEquirectFilterMode(StringSection<> str)
	{
		if (XlEqString(str, "ToCubeMap")) return EquirectFilterMode::ToCubeMap;
		if (XlEqString(str, "ToCubeMapBokeh")) return EquirectFilterMode::ToCubeMapBokeh;
		if (XlEqString(str, "ToGlossySpecular")) return EquirectFilterMode::ToGlossySpecular;
		if (XlEqString(str, "ProjectToSphericalHarmonic")) return EquirectFilterMode::ProjectToSphericalHarmonic;
		if (XlEqString(str, "ToGlossySpecularReference")) return EquirectFilterMode::ToGlossySpecularReference;
		if (XlEqString(str, "ToDiffuseReference")) return EquirectFilterMode::ToDiffuseReference;
		return {};
	}

	const char* AsString(EquirectFilterMode mode)
	{
		if (mode == EquirectFilterMode::ToCubeMap) return "ToCubeMap";
		if (mode == EquirectFilterMode::ToCubeMapBokeh) return "ToCubeMapBokeh";
		if (mode == EquirectFilterMode::ToGlossySpecular) return "ToGlossySpecular";
		if (mode == EquirectFilterMode::ProjectToSphericalHarmonic) return "ProjectToSphericalHarmonic";
		if (mode == EquirectFilterMode::ToGlossySpecularReference) return "ToGlossySpecularReference";
		if (mode == EquirectFilterMode::ToDiffuseReference) return "ToDiffuseReference";
		return "<<unknown>>";
	}

	std::optional<EquirectToCubemap::MipMapFilter> AsMipMapFilter(StringSection<> str)
	{
		if (XlEqString(str, "None")) return EquirectToCubemap::MipMapFilter::None;
		if (XlEqString(str, "FromSource")) return EquirectToCubemap::MipMapFilter::FromSource;
		return {};
	}

	static bool TryDeserializeKey(Formatters::TextInputFormatter<>& fmttr, StringSection<> kn, EquirectFilterParams& params)
	{
		if (XlEqString(kn, "SampleCount")) {
			params._sampleCount = Formatters::RequireCastValue<decltype(params._sampleCount)>(fmttr);
		} else if (XlEqString(kn, "MaxSamplesPerCmdList")) {
			params._maxSamplesPerCmdList = Formatters::RequireCastValue<decltype(params._maxSamplesPerCmdList)>(fmttr);
		} else if (XlEqString(kn, "CommandListInterval")) {
			params._idealCmdListCostMS = Formatters::RequireCastValue<decltype(params._idealCmdListCostMS)>(fmttr);
		} else if (XlEqString(kn, "CoordinateSystem")) {
			auto mode = Formatters::RequireStringValue(fmttr);
			if (XlEqString(mode, "ZUp")) params._upDirection = 2;
			else if (XlEqString(mode, "YUp")) params._upDirection = 1;
			else Throw(Formatters::FormatException("Unknown 'CoordinateSystem' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
		} else
			return false;
		return true;
	}

	static void DeserializationOperator(Formatters::TextInputFormatter<>& fmttr, EquirectToCubemap& dst)
	{
		StringSection<> kn;
		while (fmttr.TryKeyedItem(kn)) {
			if (XlEqString(kn, "FilterMode")) {
				auto mode = Formatters::RequireStringValue(fmttr);
				if (auto m = AsEquirectFilterMode(mode)) dst._filterMode = *m;
				else Throw(Formatters::FormatException("Unknown 'FilterMode' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
			} else if (XlEqString(kn, "Format")) {
				auto mode = Formatters::RequireStringValue(fmttr);
				if (auto fmtOpt = AsFormat(mode)) dst._format = *fmtOpt;
				else Throw(Formatters::FormatException("Unknown 'Format' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
			} else if (XlEqString(kn, "FaceDim")) {
				dst._faceDim = Formatters::RequireCastValue<decltype(dst._faceDim)>(fmttr);
			} else if (XlEqString(kn, "MipMapFilter")) {
				auto mode = Formatters::RequireStringValue(fmttr);
				if (auto fmtOpt = AsMipMapFilter(mode)) dst._mipMapFilter = *fmtOpt;
				else Throw(Formatters::FormatException("Unknown 'MipMapFilter' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
			} else if (XlEqString(kn, "CoefficientCount")) {
				dst._coefficientCount = Formatters::RequireCastValue<decltype(dst._coefficientCount)>(fmttr);
			} else if (TryDeserializeKey(fmttr, kn, dst._params)) {
			} else 
				Formatters::SkipValueOrElement(fmttr);
		}
	}

	T1(Type) static ::AssetsNew::CompoundAssetUtil::ConditionalWrapper<Type> Actualize(
		::AssetsNew::CompoundAssetUtil& util,
		uint64_t componentTypeName, const ::AssetsNew::ScaffoldIndexer& rootEntity)
	{
		auto future = util.GetFuture<Type>(componentTypeName, rootEntity);
		YieldToPool(future);
		return future.get();
	}

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_EquirectFilter(
		const EquirectToCubemap& mainComponent,
		const Assets::TextureCompilerSource& sourceComponent)
	{
		class Compiler : public Assets::ITextureCompiler
		{
		public:
			EquirectToCubemap _mainComponent;
			Assets::TextureCompilerSource _sourceComponent;

			std::string IntermediateName() const override
			{
				return (StringMeld<1024>() 
					<< _sourceComponent._srcFile 
					<< "-" << AsString(_mainComponent._filterMode)
					<< "-" << AsString(_mainComponent._format)
					<< "-" << _mainComponent._faceDim
					<< "-" << _mainComponent._params._sampleCount
					<< "-" << uint32_t(_mainComponent._mipMapFilter) + (_mainComponent._coefficientCount<<4) + (_mainComponent._params._upDirection<<8)
					).AsString();
			}

			std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& context) override
			{
				auto srcPkt = Techniques::Services::GetInstance().CreateTextureDataSource(_sourceComponent._srcFile, 0);
				context._dependencies.push_back(srcPkt->GetDependencyValidation());

				ProgressiveTextureFn dummyIntermediateFn;
				ProgressiveTextureFn* intermediateFunction = &dummyIntermediateFn;
				if (context._conduit->Has<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>(0))
					intermediateFunction = &context._conduit->Get<void(std::shared_ptr<BufferUploads::IAsyncDataSource>)>(0);

				if (_mainComponent._filterMode == EquirectFilterMode::ToCubeMap) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-to-cubemap{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._faceDim << "x" << _mainComponent._faceDim << " " << AsString(_mainComponent._format)).AsString());
					}

					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = _mainComponent._faceDim;
					targetDesc._height = _mainComponent._faceDim;
					targetDesc._depth = 1;
					targetDesc._arrayCount = 0u;
					targetDesc._mipCount = (_mainComponent._mipMapFilter == EquirectToCubemap::MipMapFilter::FromSource) ? IntegerLog2(targetDesc._width)+1 : 1;
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ToCubeMap, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else if (_mainComponent._filterMode == EquirectFilterMode::ToCubeMapBokeh) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-to-cubemap-bokeh{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._faceDim << "x" << _mainComponent._faceDim << " " << AsString(_mainComponent._format)).AsString());
					}

					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = _mainComponent._faceDim;
					targetDesc._height = _mainComponent._faceDim;
					targetDesc._depth = 1;
					targetDesc._arrayCount = 0u;
					targetDesc._mipCount = (_mainComponent._mipMapFilter == EquirectToCubemap::MipMapFilter::FromSource) ? IntegerLog2(targetDesc._width)+1 : 1;
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ToCubeMapBokeh, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else if (_mainComponent._filterMode == EquirectFilterMode::ToGlossySpecular) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-to-specular{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._faceDim << "x" << _mainComponent._faceDim << " " << AsString(_mainComponent._format)).AsString());
					}

					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = _mainComponent._faceDim;
					targetDesc._height = _mainComponent._faceDim;
					targetDesc._depth = 1;
					targetDesc._arrayCount = 0u;
					targetDesc._mipCount = IntegerLog2(targetDesc._width)+1;
					targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ToGlossySpecular, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else if (_mainComponent._filterMode == EquirectFilterMode::ToGlossySpecularReference) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-to-specular-reference{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._faceDim << "x" << _mainComponent._faceDim << " " << AsString(_mainComponent._format)).AsString());
					}

					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = _mainComponent._faceDim;
					targetDesc._height = _mainComponent._faceDim;
					targetDesc._depth = 1;
					targetDesc._arrayCount = 0u;
					targetDesc._mipCount = IntegerLog2(targetDesc._width)+1;
					targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ToGlossySpecularReference, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else if (_mainComponent._filterMode == EquirectFilterMode::ToDiffuseReference) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-to-diffuse-reference{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._faceDim << "x" << _mainComponent._faceDim << " " << AsString(_mainComponent._format)).AsString());
					}

					auto srcDst = srcPkt->GetDesc();
					srcDst.wait();
					auto targetDesc = srcDst.get()._textureDesc;
					targetDesc._width = _mainComponent._faceDim;
					targetDesc._height = _mainComponent._faceDim;
					targetDesc._depth = 1;
					targetDesc._arrayCount = 0u;
					targetDesc._mipCount = 1;
					targetDesc._format = Format::R32G32B32A32_FLOAT; // use full float precision for the pre-compression format
					targetDesc._dimensionality = TextureDesc::Dimensionality::CubeMap;
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ToDiffuseReference, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else if (_mainComponent._filterMode == EquirectFilterMode::ProjectToSphericalHarmonic) {

					if (context._opContext) {
						context._opContext->SetDescription(Concatenate("{color:66d0a4}Equirectangular-project-to-spherical-harmonic{color:} compilation: ", ColouriseFilename(_sourceComponent._srcFile)));
						context._opContext->SetMessage((StringMeld<256>() << _mainComponent._coefficientCount << " coefficients").AsString());
					}

					auto targetDesc = TextureDesc::Plain2D(_mainComponent._coefficientCount, 1, Format::R32G32B32A32_FLOAT);
					srcPkt = EquirectFilter(*srcPkt, targetDesc, EquirectFilterMode::ProjectToSphericalHarmonic, _mainComponent._params, *context._opContext, *intermediateFunction);
					context._dependencies.push_back(srcPkt->GetDependencyValidation());

				} else {

					Throw(std::runtime_error("Unknown filter mode: " + std::to_string((uint32_t)_mainComponent._filterMode)));

				}

				return srcPkt;
			}
		};

		auto result = std::make_shared<Compiler>();
		result->_mainComponent = mainComponent;
		result->_sourceComponent = sourceComponent;
		return result; 
	}

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_EquirectFilter(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		// look for "EquirectToCubemap" component
		auto scaffold = indexer._scaffold.get();
		if (scaffold->HasComponent(indexer._entityNameHash, "EquirectToCubemap"_h) && scaffold->HasComponent(indexer._entityNameHash, "Source"_h))
			return TextureCompiler_EquirectFilter(
				Actualize<EquirectToCubemap>(*util, "EquirectToCubemap"_h, indexer).get(),
				Actualize<Assets::TextureCompilerSource>(*util, "Source"_h, indexer).get());

		return nullptr;
	}

	static uint64_t CalculateHash(const EquirectFilterParams&);

	std::shared_ptr<Assets::ITextureCompiler> TextureCompiler_ComputeShader(
		std::shared_ptr<::AssetsNew::CompoundAssetUtil> util,
		const ::AssetsNew::ScaffoldAndEntityName& indexer)
	{
		auto scaffold = indexer._scaffold.get();

		class Compiler_SamplingComputeShader : public Assets::ITextureCompiler
		{
		public:
			unsigned _width = 512, _height = 512;
			unsigned _arrayLayerCount = 0;
			std::string _shader;
			EquirectFilterParams _params;

			std::string IntermediateName() const override { return (StringMeld<128>() << _shader << "-" << _width << "x" << _height << "x" << _arrayLayerCount << "-" << CalculateHash(_params)).AsString(); }
			std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& context) override
			{
				if (context._opContext) {
					context._opContext->SetDescription(Concatenate("Generating texture from {color:66d0a4}compute shader{color:}: ", ColouriseFilename(_shader)));
					context._opContext->SetMessage((StringMeld<256>() << _width << "x" << _height).AsString());
				}

				auto targetDesc = TextureDesc::Plain2D(
					_width,
					_height,
					Format::R32G32B32A32_FLOAT, // use full float precision for the pre-compression format
					1, _arrayLayerCount);
				assert(!_shader.empty());
				return LightingEngine::GenerateFromSamplingComputeShader(_shader, targetDesc, _params._sampleCount, _params._idealCmdListCostMS, _params._maxSamplesPerCmdList);
			}

			Compiler_SamplingComputeShader(Formatters::TextInputFormatter<>& fmttr)
			{
				StringSection<> kn;
				while (fmttr.TryKeyedItem(kn)) {
					if (XlEqString(kn, "Width")) _width = Formatters::RequireCastValue<decltype(_width)>(fmttr);
					else if (XlEqString(kn, "Height")) _height = Formatters::RequireCastValue<decltype(_height)>(fmttr);
					else if (XlEqString(kn, "ArrayLayerCount")) _height = Formatters::RequireCastValue<decltype(_arrayLayerCount)>(fmttr);
					else if (XlEqString(kn, "Shader")) _shader = Formatters::RequireStringValue(fmttr).AsString();
					else if (TryDeserializeKey(fmttr, kn, _params)) {}
					else Formatters::SkipValueOrElement(fmttr);
				}

				if (_shader.empty())
					Throw(Formatters::FormatException("Expecting 'Shader' field in texture compiler file", fmttr.GetLocation()));
			}
		};

		if (scaffold->HasComponent(indexer._entityNameHash, "SamplingComputeShader"_h))
			return Actualize<std::shared_ptr<Compiler_SamplingComputeShader>>(*util, "SamplingComputeShader"_h, indexer).get();

		class Compiler_ConversionComputeShader : public Assets::ITextureCompiler
		{
		public:
			unsigned _width = 512, _height = 512;
			unsigned _arrayLayerCount = 0;
			Format _format = Format::Unknown;
			std::string _shader;
			Assets::TextureCompilerSource _sourceComponent;

			std::string IntermediateName() const override { return (StringMeld<1024>() << _sourceComponent._srcFile << "-" << _shader << "-" << _width << "x" << _height << "x" << _arrayLayerCount << "-" << AsString(_format)).AsString(); }
			std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& context) override
			{
				if (context._opContext) {
					context._opContext->SetDescription(Concatenate("Generating texture from {color:66d0a4}conversion compute shader{color:}: ", ColouriseFilename(_shader), ", ", ColouriseFilename(_sourceComponent._srcFile)));
					context._opContext->SetMessage((StringMeld<256>() << _width << "x" << _height << " " << AsString(_format)).AsString());
				}

				auto srcPkt = Techniques::Services::GetInstance().CreateTextureDataSource(_sourceComponent._srcFile, 0);
				context._dependencies.push_back(srcPkt->GetDependencyValidation());

				auto targetDesc = TextureDesc::Plain2D(
					_width,
					_height,
					_format,		// since we're not sampling, we can go directly to the output format
					1, _arrayLayerCount);
				assert(!_shader.empty());
				return LightingEngine::ConversionComputeShader(_shader, *srcPkt, targetDesc);
			}

			Compiler_ConversionComputeShader(Formatters::TextInputFormatter<>& fmttr)
			{
				StringSection<> kn;
				while (fmttr.TryKeyedItem(kn)) {
					if (XlEqString(kn, "Width")) _width = Formatters::RequireCastValue<decltype(_width)>(fmttr);
					else if (XlEqString(kn, "Height")) _height = Formatters::RequireCastValue<decltype(_height)>(fmttr);
					else if (XlEqString(kn, "ArrayLayerCount")) _height = Formatters::RequireCastValue<decltype(_arrayLayerCount)>(fmttr);
					else if (XlEqString(kn, "Shader")) _shader = Formatters::RequireStringValue(fmttr).AsString();
					else if (XlEqString(kn, "Format")) {
						auto mode = Formatters::RequireStringValue(fmttr);
						if (auto fmtOpt = AsFormat(mode)) _format = *fmtOpt;
						else Throw(Formatters::FormatException("Unknown 'Format' field in texture compiler file: " + mode.AsString(), fmttr.GetLocation()));
					} else Formatters::SkipValueOrElement(fmttr);
				}

				if (_shader.empty())
					Throw(Formatters::FormatException("Expecting 'Shader' field in texture compiler file", fmttr.GetLocation()));
			}
		};

		if (scaffold->HasComponent(indexer._entityNameHash, "ConversionComputeShader"_h) && scaffold->HasComponent(indexer._entityNameHash, "Source"_h)) {
			auto result = Actualize<std::shared_ptr<Compiler_ConversionComputeShader>>(*util, "ConversionComputeShader"_h, indexer).get();
			result->_sourceComponent = Actualize<Assets::TextureCompilerSource>(*util, "Source"_h, indexer).get();
			return result;
		}

		return nullptr;
	}

}}

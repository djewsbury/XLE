// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "CommonUtils.h"
#include "PipelineAccelerator.h"
#include "CompiledShaderPatchCollection.h"
#include "DescriptorSetAccelerator.h"
#include "Services.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../Types.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/Shader.h"
#include "../../Assets/AssetFuture.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../xleres/FileList.h"
#include <sstream>

namespace RenderCore { namespace Techniques
{

	RenderCore::IResourcePtr CreateStaticVertexBuffer(IDevice& device, IteratorRange<const void*> data)
	{
		return device.CreateResource(
			CreateDesc(
				BindFlag::VertexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create(unsigned(data.size())),
				"vb"),
			[data](SubResourceId subres) {
				assert(subres._arrayLayer == 0 && subres._mip == 0);
				return SubResourceInitData{ data };
			});
	}

	RenderCore::IResourcePtr CreateStaticIndexBuffer(IDevice& device, IteratorRange<const void*> data)
	{
		return device.CreateResource(
			CreateDesc(
				BindFlag::IndexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create(unsigned(data.size())),
				"ib"),
			[data](SubResourceId subres) {
				assert(subres._arrayLayer == 0 && subres._mip == 0);
				return SubResourceInitData{ data };
			});
	}

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& psCode,
		const std::string& programName)
	{
		assert(vsCode && psCode);
		auto future = std::make_shared<::Assets::FuturePtr<Metal::ShaderProgram>>(programName);
		::Assets::WhenAll(vsCode, psCode).ThenConstructToPromise(
			future->AdoptPromise(),
			[pipelineLayout](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
				return std::make_shared<Metal::ShaderProgram>(
					RenderCore::Metal::GetObjectFactory(), pipelineLayout, *vsActual, *psActual);
			});
		return future;
	}

	::Assets::PtrToFuturePtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& gsCode,
		const ::Assets::PtrToFuturePtr<CompiledShaderByteCode>& psCode,
		const StreamOutputInitializers& soInit,
		const std::string& programName)
	{
		assert(vsCode && psCode && gsCode);
		std::vector<RenderCore::InputElementDesc> soElements { soInit._outputElements.begin(), soInit._outputElements.end() };
		std::vector<unsigned> soStrides { soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end() };
		auto future = std::make_shared<::Assets::FuturePtr<Metal::ShaderProgram>>(programName);
		::Assets::WhenAll(vsCode, gsCode, psCode).ThenConstructToPromise(
			future->AdoptPromise(),
			[soElements, soStrides, pipelineLayout](
				std::shared_ptr<CompiledShaderByteCode> vsActual, 
				std::shared_ptr<CompiledShaderByteCode> gsActual, 
				std::shared_ptr<CompiledShaderByteCode> psActual) {

				StreamOutputInitializers soInit;
				soInit._outputElements = MakeIteratorRange(soElements);
				soInit._outputBufferStrides = MakeIteratorRange(soStrides);

				return std::make_shared<RenderCore::Metal::ShaderProgram>(
					RenderCore::Metal::GetObjectFactory(), pipelineLayout, *vsActual, *gsActual, *psActual, soInit);
			});
		return future;
	}

	std::pair<std::shared_ptr<PipelineAccelerator>, ::Assets::PtrToFuturePtr<DescriptorSetAccelerator>>
		CreatePipelineAccelerator(
			IPipelineAcceleratorPool& pool,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& patchCollection,
			const RenderCore::Assets::MaterialScaffoldMaterial& material,
			IteratorRange<const RenderCore::InputElementDesc*> inputLayout,
			Topology topology)
	{
		::Assets::PtrToFuturePtr<DescriptorSetAccelerator> descriptorSetAccelerator;

		auto matSelectors = material._matParams;

		/*if (patchCollection) {
			const auto* descriptorSetLayout = patchCollection->GetInterface().GetMaterialDescriptorSet().get();
			if (!descriptorSetLayout) {
				descriptorSetLayout = &RenderCore::Techniques::GetFallbackMaterialDescriptorSetLayout();
			}
			descriptorSetAccelerator = RenderCore::Techniques::MakeDescriptorSetAccelerator(
				*pool.GetDevice(),
				material._constants, material._bindings,
				*descriptorSetLayout,
				"MaterialVisualizationScene");
			
			// Also append the "RES_HAS_" constants for each resource that is both in the descriptor set and that we have a binding for
			for (const auto&r:descriptorSetLayout->_slots) {
				if (r._type == DescriptorType::Sampler || r._type == DescriptorType::UniformBuffer)
					continue;
				if (material._bindings.HasParameter(MakeStringSection(r._name)))
					matSelectors.SetParameter(MakeStringSection(std::string{"RES_HAS_"} + r._name).Cast<utf8>(), 1);
			}
		}*/

		auto pipelineAccelerator = pool.CreatePipelineAccelerator(
			patchCollection,
			matSelectors,
			inputLayout,
			topology,
			material._stateSet);

		return std::make_pair(pipelineAccelerator, descriptorSetAccelerator);
	}

}}


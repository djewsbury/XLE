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
#include "../Assets/ModelScaffold.h"
#include "../IDevice.h"
#include "../ResourceDesc.h"
#include "../Types.h"
#include "../Metal/ObjectFactory.h"
#include "../Metal/Shader.h"
#include "../Metal/Resource.h"				// for Metal::ResourceMap
#include "../../Assets/Marker.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Continuation.h"
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

	std::shared_ptr<IResource> LoadStaticResource(
		IDevice& device,
		IteratorRange<std::pair<unsigned, unsigned>*> loadRequests,
		unsigned resourceSize,
		::Assets::IFileInterface& file,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName)
	{
		auto initialOffset = file.TellP();
		// todo -- avoid the need for a host access buffer here
		auto result = device.CreateResource(
			CreateDesc(
				bindFlags, CPUAccess::Write, GPUAccess::Read,
				LinearBufferDesc::Create(resourceSize),
				resourceName));
		Metal::ResourceMap map{device, *result, Metal::ResourceMap::Mode::WriteDiscardPrevious};
		unsigned iterator = 0;
		std::sort(loadRequests.begin(), loadRequests.end(), [](auto lhs, auto rhs) { return lhs.first < rhs.first; });
		for (auto i=loadRequests.begin(); i!=loadRequests.end();) {
			auto start = i; ++i;
			while (i != loadRequests.end() && i->first == ((i-1)->first + (i-1)->second)) ++i;		// combine adjacent loads
			
			auto endPoint = (i-1)->first + (i-1)->second;
			file.Seek(start->first + initialOffset);
			file.Read(PtrAdd(map.GetData().begin(), iterator), endPoint-start->first, 1);
			iterator += endPoint-start->first;
		}
		assert(iterator == resourceSize);
		return result;
	}

	namespace Internal
	{
		class ModelScaffoldDataSource : public BufferUploads::IAsyncDataSource
		{
		public:
			virtual std::future<ResourceDesc> GetDesc ()
			{
				std::promise<ResourceDesc> promise;
				promise.set_value(_resourceDesc);
				return promise.get_future();
			}
			virtual std::future<void> PrepareData(IteratorRange<const SubResource*> subResources)
			{
				std::promise<void> promise;
				assert(subResources.size() == 1);
				assert(subResources[0]._id == RenderCore::SubResourceId{});

				for (auto i=_loadRequests.begin(); i!=_loadRequests.end(); ) {
					auto startModelScaffold = i;
					i++;
					while (i!=_loadRequests.end() && i->_modelScaffold == startModelScaffold->_modelScaffold) ++i;

					auto file = startModelScaffold->_modelScaffold->OpenLargeBlocks();
					auto initialOffset = file->TellP();
					
					// loadRequests must be sorted by _srcOffset on entry (after model scaffold storing)
					for (auto i2=startModelScaffold; i2!=i;) {
						auto start = i2;
						++i2;
						while (i2 != i && i2->_srcOffset == ((i2-1)->_srcOffset + (i2-1)->_size) && i2->_dstOffset == ((i2-1)->_dstOffset + (i2-1)->_size)) ++i2;		// combine adjacent loads
						
						auto finalSize = ((i2-1)->_srcOffset + (i2-1)->_size) - start->_srcOffset;
						assert((start->_dstOffset + finalSize) <= subResources[0]._destination.size());
						file->Seek(start->_srcOffset + initialOffset);
						file->Read(PtrAdd(subResources[0]._destination.begin(), start->_dstOffset), finalSize);
					}
				}
				
				promise.set_value();
				return promise.get_future();
			}
			virtual ::Assets::DependencyValidation GetDependencyValidation() const
			{
				if (_depVal) return _depVal;

				std::vector<::Assets::DependencyValidationMarker> depVals;
				for (auto i=_loadRequests.begin(); i!=_loadRequests.end(); ) {
					auto start = i;
					i++;
					while (i!=_loadRequests.end() && i->_modelScaffold == start->_modelScaffold) ++i;
					depVals.push_back(start->_modelScaffold->GetDependencyValidation());
				}
				_depVal = ::Assets::GetDepValSys().MakeOrReuse(depVals);		// cache this for reuse later
				return _depVal;
			}

			ResourceDesc _resourceDesc;
			struct LoadRequests
			{
				std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm> _modelScaffold;
				unsigned _dstOffset, _srcOffset, _size; 
			};
			std::vector<LoadRequests> _loadRequests;
			mutable ::Assets::DependencyValidation _depVal;
		};

		static std::vector<ModelScaffoldDataSource::LoadRequests> AsLoadRequests(IteratorRange<const ModelScaffoldLoadRequest*> loadRequests)
		{
			std::vector<ModelScaffoldDataSource::LoadRequests> result;
			result.reserve(loadRequests.size());
			unsigned iterator = 0;
			for (auto a:loadRequests) {
				result.push_back({a._modelScaffold, iterator, a._offset, a._size});
				iterator += a._size;
			}
			std::sort(
				result.begin(), result.end(),
				[](const auto& lhs, const auto& rhs) {
					if (lhs._modelScaffold < rhs._modelScaffold) return true;
					if (lhs._modelScaffold > rhs._modelScaffold) return false;
					return lhs._srcOffset < rhs._srcOffset;		// sorting by _srcOffset required by PrepareData
				});

			return result;
		}

		static std::vector<ModelScaffoldDataSource::LoadRequests> AsLoadRequests(
			std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm> modelScaffold,
			IteratorRange<const std::pair<unsigned, unsigned>*> loadRequests)
		{
			std::vector<ModelScaffoldDataSource::LoadRequests> result;
			result.reserve(loadRequests.size());
			unsigned iterator = 0;
			for (auto a:loadRequests) {
				result.push_back({modelScaffold, iterator, a.first, a.second});
				iterator += a.second;
			}
			// sorting by _srcOffset required by PrepareData
			std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) { return lhs._srcOffset < rhs._srcOffset; });
			return result;
		}
	}

	BufferUploads::TransactionMarker LoadStaticResourceFullyAsync(
		BufferUploads::IManager& bufferUploads,
		IteratorRange<std::pair<unsigned, unsigned>*> loadRequests,
		unsigned resourceSize,
		std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm> modelScaffold,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName)
	{
		auto dataSource = std::make_shared<Internal::ModelScaffoldDataSource>();
		dataSource->_resourceDesc = CreateDesc(
			bindFlags | BindFlag::TransferDst, 0, GPUAccess::Read,
			LinearBufferDesc::Create(resourceSize),
			resourceName);
		dataSource->_loadRequests = Internal::AsLoadRequests(modelScaffold, loadRequests);

		return bufferUploads.Transaction_Begin(dataSource, bindFlags);
	}

	std::pair<std::shared_ptr<IResource>, BufferUploads::TransactionMarker> LoadStaticResourcePartialAsync(
		IDevice& device,
		IteratorRange<const ModelScaffoldLoadRequest*> loadRequests,
		unsigned resourceSize,
		std::shared_ptr<RenderCore::Assets::ModelScaffoldCmdStreamForm> modelScaffold,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName)
	{
		auto dataSource = std::make_shared<Internal::ModelScaffoldDataSource>();
		dataSource->_resourceDesc = CreateDesc(
			bindFlags | BindFlag::TransferDst, 0, GPUAccess::Read,
			LinearBufferDesc::Create(resourceSize),
			resourceName);
		dataSource->_loadRequests = Internal::AsLoadRequests(loadRequests);

		auto resource = device.CreateResource(dataSource->_resourceDesc);
		auto marker = Services::GetBufferUploads().Transaction_Begin(resource, dataSource, bindFlags);
		return {std::move(resource), std::move(marker)};
	}

	std::pair<std::shared_ptr<IResource>, BufferUploads::TransactionMarker> LoadStaticResourcePartialAsync(		// deprecated version
		IDevice& device,
		IteratorRange<std::pair<unsigned, unsigned>*> loadRequests,
		unsigned resourceSize,
		std::shared_ptr<RenderCore::Assets::ModelScaffold> modelScaffold,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName)
	{
		assert(0);
		return {};
	}

	::Assets::PtrToMarkerPtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& psCode,
		const std::string& programName)
	{
		assert(vsCode && psCode);
		auto future = std::make_shared<::Assets::MarkerPtr<Metal::ShaderProgram>>(programName);
		::Assets::WhenAll(vsCode, psCode).ThenConstructToPromise(
			future->AdoptPromise(),
			[pipelineLayout](std::shared_ptr<CompiledShaderByteCode> vsActual, std::shared_ptr<CompiledShaderByteCode> psActual) {
				return std::make_shared<Metal::ShaderProgram>(
					RenderCore::Metal::GetObjectFactory(), pipelineLayout, *vsActual, *psActual);
			});
		return future;
	}

	::Assets::PtrToMarkerPtr<Metal::ShaderProgram> CreateShaderProgramFromByteCode(
		const std::shared_ptr<ICompiledPipelineLayout>& pipelineLayout,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& vsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& gsCode,
		const ::Assets::PtrToMarkerPtr<CompiledShaderByteCode>& psCode,
		const StreamOutputInitializers& soInit,
		const std::string& programName)
	{
		assert(vsCode && psCode && gsCode);
		std::vector<RenderCore::InputElementDesc> soElements { soInit._outputElements.begin(), soInit._outputElements.end() };
		std::vector<unsigned> soStrides { soInit._outputBufferStrides.begin(), soInit._outputBufferStrides.end() };
		auto future = std::make_shared<::Assets::MarkerPtr<Metal::ShaderProgram>>(programName);
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

	std::pair<std::shared_ptr<PipelineAccelerator>, ::Assets::PtrToMarkerPtr<DescriptorSetAccelerator>>
		CreatePipelineAccelerator(
			IPipelineAcceleratorPool& pool,
			const std::shared_ptr<RenderCore::Assets::ShaderPatchCollection>& patchCollection,
			const RenderCore::Assets::MaterialScaffoldMaterial& material,
			IteratorRange<const RenderCore::InputElementDesc*> inputLayout,
			Topology topology)
	{
		::Assets::PtrToMarkerPtr<DescriptorSetAccelerator> descriptorSetAccelerator;

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


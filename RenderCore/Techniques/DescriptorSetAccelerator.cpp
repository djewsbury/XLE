// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetAccelerator.h"
#include "DeferredShaderResource.h"
#include "TechniqueUtils.h"
#include "Services.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Metal/State.h"
#include "../Metal/InputLayout.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../UniformsStream.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques 
{

	void DescriptorSetAccelerator::Apply(
		Metal::DeviceContext& devContext,
		Metal::GraphicsEncoder& encoder,
		Metal::BoundUniforms& boundUniforms) const
	{
		const IResourceView* resourceViews[32];
		unsigned resourceViewCount = 0;

		for (auto i=_constantBuffers.begin(); i!=_constantBuffers.end(); ++i)
			resourceViews[resourceViewCount++] = i->get();

		for (auto i=_shaderResources.begin(); i!=_shaderResources.end(); ++i)
			resourceViews[resourceViewCount++] = (*i)->GetShaderResource().get();

		UniformsStream result;
		result._resourceViews = MakeIteratorRange(resourceViews, resourceViews+resourceViewCount);
		
		boundUniforms.ApplyLooseUniforms(devContext, encoder, result);
	}

	::Assets::FuturePtr<RenderCore::IDescriptorSet> MakeDescriptorSetFuture(
		const std::shared_ptr<IDevice>& device,
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout)
	{
		auto shrLanguage = GetDefaultShaderLanguage();

		struct DescriptorSetInProgress
		{
			struct Resource
			{
				::Assets::FuturePtr<DeferredShaderResource> _pendingResource;
				std::shared_ptr<IResourceView> _fixedResource;
			};
			std::vector<Resource> _resources;
			std::vector<std::shared_ptr<ISampler>> _samplers;

			struct Slot
			{
				DescriptorSetInitializer::BindType _bindType = DescriptorSetInitializer::BindType::Empty;
				unsigned _resourceIdx = ~0u;
				std::string _slotName;
				DescriptorType _slotType;
			};
			std::vector<Slot> _slots;
		};
		DescriptorSetInProgress working;
		working._slots.reserve(layout._slots.size());
		for (const auto& s:layout._slots) {
			DescriptorSetInProgress::Slot slotInProgress;
			slotInProgress._slotName = s._name;
			slotInProgress._slotType = s._type;

			auto hashName = Hash64(s._name);
			auto boundResource = resourceBindings.GetParameterAsString(hashName);
			if (boundResource.has_value() && !boundResource.value().empty()) {
				if (s._type != DescriptorType::Texture)
					Throw(std::runtime_error("Attempting to bind resource to non-texture descriptor slot for slot " + s._name));

				slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
				slotInProgress._resourceIdx = (unsigned)working._resources.size();
				DescriptorSetInProgress::Resource res;
				res._pendingResource = ::Assets::MakeAsset<DeferredShaderResource>(MakeStringSection(boundResource.value()));
				working._resources.push_back(res);
			} else if (s._type == DescriptorType::ConstantBuffer && s._cbIdx < (unsigned)layout._constantBuffers.size()) {
				auto& cbLayout = layout._constantBuffers[s._cbIdx];
				auto buffer = cbLayout->BuildCBDataAsVector(constantBindings, shrLanguage);

				auto cb = 
					device->CreateResource(
						CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create((unsigned)buffer.size()), s._name),
						SubResourceInitData{buffer});

				slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
				slotInProgress._resourceIdx = (unsigned)working._resources.size();
				DescriptorSetInProgress::Resource res;
				res._fixedResource = cb->CreateBufferView(BindFlag::ConstantBuffer);
				working._resources.push_back(res);
			} else
				Throw(std::runtime_error("No binding provided for descriptor slot " + s._name));
		}

		auto result = std::make_shared<::Assets::AssetFuture<RenderCore::IDescriptorSet>>("descriptor-set");
		result->SetPollingFunction(
			[working, device](::Assets::AssetFuture<RenderCore::IDescriptorSet>& thatFuture) -> bool {

				std::vector<::Assets::DepValPtr> subDepVals;
				std::vector<std::shared_ptr<IResourceView>> finalResources;
				finalResources.reserve(working._resources.size());
				subDepVals.reserve(working._resources.size());

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				for (const auto&d:working._resources) {
					if (d._pendingResource) {
						::Assets::AssetPtr<DeferredShaderResource> actualized;
						::Assets::DepValPtr depVal;
						::Assets::Blob actualizationLog;
						auto status = d._pendingResource->CheckStatusBkgrnd(actualized, depVal, actualizationLog);
						if (status == ::Assets::AssetState::Pending) {
							return true;		// keep waiting
						} else if (status == ::Assets::AssetState::Ready) {
							finalResources.push_back(actualized->GetShaderResource());
						} else {
							// todo -- use some kind of "invalid marker" for this resource
							finalResources.push_back(nullptr);
						}

						if (depVal)
							subDepVals.push_back(depVal);
					} else {
						finalResources.push_back(d._fixedResource);
					}
				}

				auto depVal = std::make_shared<::Assets::DependencyValidation>();
				for (const auto&d:subDepVals) ::Assets::RegisterAssetDependency(depVal, d);

				DescriptorSetSignature signature;
				std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
				bindTypesAndIdx.reserve(working._slots.size());
				signature._slots.reserve(working._slots.size());
				for (const auto&s:working._slots) {
					bindTypesAndIdx.push_back(DescriptorSetInitializer::BindTypeAndIdx{s._bindType, s._resourceIdx});
					signature._slots.push_back(DescriptorSlot{s._slotType});
				}
				std::vector<const IResourceView*> resourceViews;
				std::vector<const ISampler*> samplers;
				resourceViews.reserve(finalResources.size());
				samplers.reserve(working._samplers.size());
				for (const auto&r:finalResources) resourceViews.push_back(r.get());
				for (const auto&r:working._samplers) samplers.push_back(r.get());

				DescriptorSetInitializer initializer;
				initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx);
				initializer._bindItems._resourceViews = MakeIteratorRange(resourceViews);
				initializer._bindItems._samplers = MakeIteratorRange(samplers);
				initializer._signature = &signature;

				auto finalDescriptorSet = device->CreateDescriptorSet(initializer);
				thatFuture.SetAsset(std::move(finalDescriptorSet), {});
				return false;
			});

		return result;
	}


}}

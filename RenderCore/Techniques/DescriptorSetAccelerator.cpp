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
#include "../StateDesc.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Utility/ParameterBox.h"

namespace RenderCore { namespace Techniques 
{
	void ConstructDescriptorSet(
		::Assets::FuturePtr<RenderCore::IDescriptorSet>& future,
		const std::shared_ptr<IDevice>& device,
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, std::shared_ptr<ISampler>>*> samplerBindings,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		DescriptorSetBindingInfo* bindingInfo)
	{
		auto shrLanguage = GetDefaultShaderLanguage();

		struct DescriptorSetInProgress
		{
			struct Resource
			{
				::Assets::PtrToFuturePtr<DeferredShaderResource> _pendingResource;
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

			DescriptorSetSignature _signature;
		};
		DescriptorSetInProgress working;
		working._slots.reserve(layout._slots.size());
		working._signature._slots.reserve(working._slots.size());
		if (bindingInfo) {
			bindingInfo->_slots.clear();
			bindingInfo->_slots.reserve(working._slots.size());
		}
		char stringMeldBuffer[512];
		for (const auto& s:layout._slots) {
			DescriptorSetInProgress::Slot slotInProgress;
			slotInProgress._slotName = s._name;
			slotInProgress._slotType = s._type;

			DescriptorSetBindingInfo::Slot slotBindingInfo;
			slotBindingInfo._layoutName = s._name;
			slotBindingInfo._layoutSlotType = s._type;

			bool gotBinding = false;
			auto hashName = Hash64(s._name);
			auto boundResource = resourceBindings.GetParameterAsString(hashName);
			if (boundResource.has_value() && !boundResource.value().empty()) {
				if (s._type != DescriptorType::SampledTexture)
					Throw(std::runtime_error("Attempting to bind resource to non-texture descriptor slot for slot " + s._name));

				slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
				slotInProgress._resourceIdx = (unsigned)working._resources.size();
				DescriptorSetInProgress::Resource res;
				res._pendingResource = ::Assets::MakeAsset<DeferredShaderResource>(MakeStringSection(boundResource.value()));
				working._resources.push_back(res);
				gotBinding = true;

				if (bindingInfo)
					slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "DeferredShaderResource: " << boundResource.value()).AsString();

			} else if (s._type == DescriptorType::UniformBuffer && s._cbIdx < (unsigned)layout._constantBuffers.size()) {
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
				gotBinding = true;

				if (bindingInfo) {
					std::stringstream str;
					cbLayout->DescribeCB(str, MakeIteratorRange(buffer), shrLanguage);
					slotBindingInfo._binding = str.str();
				}
			} else if (s._type == DescriptorType::Sampler) {
				auto i = std::find_if(samplerBindings.begin(), samplerBindings.end(), [hashName](const auto& c) { return c.first == hashName; });
				if (i != samplerBindings.end()) {
					slotInProgress._bindType = DescriptorSetInitializer::BindType::Sampler;
					slotInProgress._resourceIdx = (unsigned)working._samplers.size();
					working._samplers.push_back(i->second);
					gotBinding = true;

					if (bindingInfo)
						slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "Sampler: " << i->second->GetDesc()).AsString();
				}
			} 
			
			if (!gotBinding)
				slotInProgress._bindType = DescriptorSetInitializer::BindType::Empty;
			working._signature._slots.push_back(DescriptorSlot{s._type});
			working._slots.push_back(slotInProgress);
			if (bindingInfo) {
				slotBindingInfo._bindType = slotInProgress._bindType;
				bindingInfo->_slots.push_back(slotBindingInfo);
			}
		}

		future.SetPollingFunction(
			[working, device](::Assets::FuturePtr<RenderCore::IDescriptorSet>& thatFuture) -> bool {

				std::vector<::Assets::DependencyValidation> subDepVals;
				std::vector<std::shared_ptr<IResourceView>> finalResources;
				finalResources.reserve(working._resources.size());
				subDepVals.reserve(working._resources.size());

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				for (const auto&d:working._resources) {
					if (d._pendingResource) {
						::Assets::AssetPtr<DeferredShaderResource> actualized;
						::Assets::DependencyValidation depVal;
						::Assets::Blob actualizationLog;
						auto status = d._pendingResource->CheckStatusBkgrnd(actualized, depVal, actualizationLog);
						if (status == ::Assets::AssetState::Pending) {
							return true;		// keep waiting
						} else if (status == ::Assets::AssetState::Ready) {
							finalResources.push_back(actualized->GetShaderResource());
						} else {
							// If any subassets fail, we consider the entire descriptor set to be invalid
							// We'll return, and propagate the actualization log back
							std::stringstream str;
							str << "Failed to actualize subasset resource (" << d._pendingResource->Initializer() << "): ";
							if (actualizationLog) { str << ::Assets::AsString(actualizationLog); } else { str << std::string("<<no log>>"); }
							thatFuture.SetInvalidAsset(depVal, ::Assets::AsBlob(str.str()));
							return false;
						}

						if (depVal)
							subDepVals.push_back(depVal);
					} else {
						finalResources.push_back(d._fixedResource);
					}
				}

				auto depVal = ::Assets::GetDepValSys().Make();
				for (const auto&d:subDepVals) depVal.RegisterDependency(d);

				std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
				bindTypesAndIdx.reserve(working._slots.size());
				for (const auto&s:working._slots) {
					bindTypesAndIdx.push_back(DescriptorSetInitializer::BindTypeAndIdx{s._bindType, s._resourceIdx});
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
				initializer._signature = &working._signature;

				auto finalDescriptorSet = device->CreateDescriptorSet(initializer);
				thatFuture.SetAsset(std::move(finalDescriptorSet), {});
				return false;
			});
	}


}}

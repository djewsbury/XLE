// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetAccelerator.h"
#include "DeferredShaderResource.h"
#include "TechniqueUtils.h"
#include "CommonResources.h"
#include "Services.h"
#include "DeformAccelerator.h"
#include "DrawableDelegates.h"
#include "Drawables.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Metal/State.h"
#include "../Metal/InputLayout.h"
#include "../IDevice.h"
#include "../BufferView.h"
#include "../UniformsStream.h"
#include "../StateDesc.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/BitUtils.h"

namespace RenderCore { namespace Techniques 
{
	struct DescriptorSetInProgress : public ::Assets::IAsyncMarker
	{
		struct Resource
		{
			::Assets::PtrToMarkerPtr<DeferredShaderResource> _pendingResource;
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
		DescriptorSetBindingInfo _bindingInfo;

		std::shared_ptr<AnimatedUniformBufferHelper> _animHelper;

		::Assets::AssetState GetAssetState() const override
		{
			// just check status right now
			auto result = ::Assets::AssetState::Ready;
			for (const auto&d:_resources) {
				if (d._pendingResource) {
					::Assets::DependencyValidation depVal;
					::Assets::Blob actualizationLog;
					auto status = d._pendingResource->CheckStatusBkgrnd(depVal, actualizationLog);
					// return true only when everything is ready/invalid
					if (status == ::Assets::AssetState::Pending) {
						return ::Assets::AssetState::Pending;
					} else if (status == ::Assets::AssetState::Invalid)
						result = ::Assets::AssetState::Invalid;
				}
			}
			return result;
		}

		std::optional<::Assets::AssetState> StallWhilePending(std::chrono::microseconds timeout) const override
		{
			auto timeoutTime = std::chrono::steady_clock::now() + timeout;
			auto result = ::Assets::AssetState::Ready;
			for (const auto&d:_resources) {
				if (d._pendingResource) {
					auto now = std::chrono::steady_clock::now();
					auto partialTimeout = std::chrono::duration_cast<std::chrono::microseconds>(timeoutTime-now);
					if (now >= timeoutTime || partialTimeout.count() == 0)
						return {};		// timed out before we can even begin
					auto status = d._pendingResource->StallWhilePending(partialTimeout);
					if (!status.has_value() || status.value() == ::Assets::AssetState::Pending)
						return status;		// timed out during StallWhilePending
					if (status.value() == ::Assets::AssetState::Invalid)
						result = ::Assets::AssetState::Invalid;
				}
			}
			return result;
		}
	};

	void ConstructDescriptorSet(
		std::promise<ActualizedDescriptorSet>&& promise,
		const std::shared_ptr<IDevice>& device,
		const RenderCore::Assets::PredefinedDescriptorSetLayout& layout,
		const Utility::ParameterBox& constantBindings,
		const Utility::ParameterBox& resourceBindings,
		IteratorRange<const std::pair<uint64_t, std::shared_ptr<ISampler>>*> samplerBindings,
		SamplerPool* samplerPool,
		IteratorRange<const AnimatedParameterBinding*> animatedBindings,
		const std::shared_ptr<IResourceView>& dynamicPageResource,
		PipelineType pipelineType,
		bool generateBindingInfo)
	{
		auto shrLanguage = GetDefaultShaderLanguage();

		int maxSlotIdx = -1;
		for (auto& slot:layout._slots)
			maxSlotIdx = std::max(maxSlotIdx, int(slot._slotIdx));
		assert(maxSlotIdx >= 0);
		
		DescriptorSetInProgress working;
		working._slots.resize(maxSlotIdx+1);
		if (generateBindingInfo)
			working._bindingInfo._slots.resize(working._slots.size());

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
				res._pendingResource = ::Assets::MakeAssetPtr<DeferredShaderResource>(MakeStringSection(boundResource.value()));
				working._resources.push_back(res);
				gotBinding = true;

				if (generateBindingInfo)
					slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "DeferredShaderResource: " << boundResource.value()).AsString();

			} else if ((s._type == DescriptorType::UniformBuffer || s._type == DescriptorType::UniformBufferDynamicOffset) && s._cbIdx < (unsigned)layout._constantBuffers.size()) {

				auto& cbLayout = layout._constantBuffers[s._cbIdx];
				auto buffer = cbLayout->BuildCBDataAsVector(constantBindings, shrLanguage);

				const bool animated = false;
				if (!animated) {
					// non animated fixed buffer
					auto cb = 
						device->CreateResource(
							CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create((unsigned)buffer.size()), s._name),
							SubResourceInitData{buffer});

					slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
					slotInProgress._resourceIdx = (unsigned)working._resources.size();

					DescriptorSetInProgress::Resource res;
					res._fixedResource = cb->CreateBufferView(BindFlag::ConstantBuffer);
					working._resources.push_back(res);
				} else {
					assert(0);		// use the animation page resource, and require a dynamic offset binding point
				}
					
				gotBinding = true;

				if (generateBindingInfo) {
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

					if (generateBindingInfo)
						slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "Sampler: " << i->second->GetDesc()).AsString();
				}
			} 
			
			if (gotBinding) {
				if (working._slots[s._slotIdx]._bindType != DescriptorSetInitializer::BindType::Empty)
					Throw(std::runtime_error("Multiple resources bound to the same slot in ConstructDescriptorSet(). Attempting to bind slot " + s._name));

				assert(s._slotIdx < working._slots.size());
				working._slots[s._slotIdx] = slotInProgress;
				if (generateBindingInfo) {
					slotBindingInfo._bindType = slotInProgress._bindType;
					working._bindingInfo._slots[s._slotIdx] = slotBindingInfo;
				}
			}
		}

		working._signature = layout.MakeDescriptorSetSignature(samplerPool);

		auto futureWorkingDescSet = ::Assets::MakeASyncMarkerBridge(std::move(working));
		::Assets::WhenAll(std::move(futureWorkingDescSet)).ThenConstructToPromise(
			std::move(promise),
			[device, pipelineType](DescriptorSetInProgress working) {
				std::vector<::Assets::DependencyValidation> subDepVals;
				std::vector<std::shared_ptr<IResourceView>> finalResources;
				finalResources.reserve(working._resources.size());
				subDepVals.reserve(working._resources.size());
				BufferUploads::CommandListID completionCommandList = BufferUploads::CommandListID_Invalid;

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				for (const auto&d:working._resources) {
					if (d._pendingResource) {
						std::shared_ptr<DeferredShaderResource> actualized;
						::Assets::DependencyValidation depVal;
						::Assets::Blob actualizationLog;
						auto status = d._pendingResource->CheckStatusBkgrnd(actualized, depVal, actualizationLog);
						if (status == ::Assets::AssetState::Pending) {
							Throw(std::runtime_error("Unexpected pending asset"));		// should not happen, because the future should not have triggered until we are ready
						} else if (status == ::Assets::AssetState::Ready) {
							finalResources.push_back(actualized->GetShaderResource());

							auto resCmdList = actualized->GetCompletionCommandList();
							if (resCmdList != BufferUploads::CommandListID_Invalid) {
								if (completionCommandList == BufferUploads::CommandListID_Invalid) {
									completionCommandList = resCmdList;
								} else  
									completionCommandList = std::max(resCmdList, completionCommandList);
							}
						} else {
							// If any subassets fail, we consider the entire descriptor set to be invalid
							// We'll return, and propagate the actualization log back
							std::stringstream str;
							str << "Failed to actualize subasset resource (" << d._pendingResource->Initializer() << "): ";
							if (actualizationLog) { str << ::Assets::AsString(actualizationLog); } else { str << std::string("<<no log>>"); }
							Throw(::Assets::Exceptions::ConstructionError(
								::Assets::Exceptions::ConstructionError::Reason::Unknown,
								depVal, ::Assets::AsBlob(str.str())));
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
				initializer._pipelineType = pipelineType;

				ActualizedDescriptorSet actualized;
				actualized._descriptorSet = device->CreateDescriptorSet(initializer);
				actualized._depVal = std::move(depVal);
				actualized._bindingInfo = std::move(working._bindingInfo);
				actualized._completionCommandList = completionCommandList;
				return actualized;
			});
	}


	std::shared_ptr<IDescriptorSet> ConstructDescriptorSet(
		IDevice& device,
		const Assets::PredefinedDescriptorSetLayout& layout,
		const UniformsStreamInterface& usi,
		const UniformsStream& us,
		SamplerPool* samplerPool,
		PipelineType pipelineType)
	{
		assert(usi._immediateDataBindings.empty());		// imm data bindings not supported here
		DescriptorSetInitializer::BindTypeAndIdx bindTypesAndIdx[layout._slots.size()];

		auto* bind = bindTypesAndIdx;
		for (const auto& slot:layout._slots) {
			auto hash = Hash64(slot._name);

			auto i = std::find(usi._resourceViewBindings.begin(), usi._resourceViewBindings.end(), hash);
			if (i != usi._resourceViewBindings.end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::ResourceView,
					(unsigned)std::distance(usi._resourceViewBindings.begin(), i)};
				continue;
			}

			i = std::find(usi._samplerBindings.begin(), usi._samplerBindings.end(), hash);
			if (i != usi._samplerBindings.end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::Sampler,
					(unsigned)std::distance(usi._samplerBindings.begin(), i)};
				continue;
			}

			*bind++ = DescriptorSetInitializer::BindTypeAndIdx{DescriptorSetInitializer::BindType::Empty, 0};
		}

		// awkwardly we need to construct a descriptor set signature here
		auto sig = layout.MakeDescriptorSetSignature(samplerPool);

		DescriptorSetInitializer initializer;
		initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx, &bindTypesAndIdx[layout._slots.size()]);
		initializer._bindItems._resourceViews = us._resourceViews;
		initializer._bindItems._samplers = us._samplers;
		initializer._signature = &sig;
		initializer._pipelineType = pipelineType;

		return device.CreateDescriptorSet(initializer);
	}

	ActualizedDescriptorSet::ActualizedDescriptorSet() = default;
	ActualizedDescriptorSet::ActualizedDescriptorSet(ActualizedDescriptorSet&&) = default;
	ActualizedDescriptorSet& ActualizedDescriptorSet::operator=(ActualizedDescriptorSet&&) = default;
	ActualizedDescriptorSet::ActualizedDescriptorSet(const ActualizedDescriptorSet&) = default;
	ActualizedDescriptorSet& ActualizedDescriptorSet::operator=(const ActualizedDescriptorSet&) = default;
	ActualizedDescriptorSet::~ActualizedDescriptorSet() = default;

}}

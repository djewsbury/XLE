// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetAccelerator.h"
#include "DeferredShaderResource.h"
#include "TechniqueUtils.h"
#include "CommonResources.h"
#include "DeformUniformsInfrastructure.h"
#include "Services.h"
#include "ResourceConstructionContext.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Assets/MaterialMachine.h"
#include "../Assets/AssetUtils.h"
#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../StateDesc.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/Assets.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/BitUtils.h"

namespace RenderCore { namespace Techniques 
{
	namespace Internal
	{
		struct DescriptorSetInProgress
		{
			struct Resource
			{
				std::shared_future<std::shared_ptr<DeferredShaderResource>> _deferredShaderResource;
				std::pair<unsigned, unsigned> _cbResourceOffsetAndSize = {0,0};
				unsigned _cbUploadIdx = ~0u;
				std::shared_ptr<IResourceView> _fixedResource;
			};
			std::vector<Resource> _resources;
			std::vector<std::shared_ptr<ISampler>> _samplers;
			std::vector<BufferUploads::TransactionMarker> _cbUploadMarkers;
			size_t _allReadyBefore = 0;
			std::string _name;

			struct Slot
			{
				DescriptorSetInitializer::BindType _bindType = DescriptorSetInitializer::BindType::ResourceView;
				unsigned _resourceIdx = ~0u;
				unsigned _descriptorSetSlot = ~0u;
				unsigned _descriptorSetArrayIdx = 0u;
				std::string _slotName;
				DescriptorType _slotType;
			};

			struct DescriptorSet
			{
				std::vector<Slot> _slots;
				DescriptorSetSignature _signature;
				DescriptorSetBindingInfo _bindingInfo;
				std::shared_ptr<AnimatedUniformBufferHelper> _animHelper;
				bool _applyDeformAcceleratorOffset = false;
			};
			std::vector<DescriptorSet> _descriptorSets;

			template<typename TimeMarker>
				::Assets::PollStatus UpdatePollUntil(TimeMarker timeoutTime)
			{
				// we can't reorder _resources -- the order is important
				for (size_t c=_allReadyBefore; c<_resources.size(); ++c) {
					auto& res = _resources[c];
					if (res._fixedResource) continue;
					if (res._cbResourceOffsetAndSize.second) continue;
					if (res._deferredShaderResource.wait_until(timeoutTime) == std::future_status::timeout) {
						_allReadyBefore = c;
						return ::Assets::PollStatus::Continue;
					}
				}

				for (size_t c=_allReadyBefore-_resources.size(); c<_cbUploadMarkers.size(); ++c) {
					if (_cbUploadMarkers[c]._future.wait_until(timeoutTime) == std::future_status::timeout) {
						_allReadyBefore = _resources.size()+c;
						return ::Assets::PollStatus::Continue;
					}
				}

				return ::Assets::PollStatus::Finish;
			}
		};

		struct InterpretMaterialMachineHelper
		{
			const ParameterBox* _resourceBindings = nullptr;
			const ParameterBox* _constantBindings = nullptr;
			IteratorRange<const std::pair<uint64_t, SamplerDesc>*> _samplerBindings;

			uint64_t CalculateHash() const
			{
				uint64_t result = DefaultSeed64;
				if (_resourceBindings) {
					result = HashCombine(_resourceBindings->GetHash(), result);
					result = HashCombine(_resourceBindings->GetParameterNamesHash(), result);
				}
				if (_constantBindings) {
					result = HashCombine(_constantBindings->GetHash(), result);
					result = HashCombine(_constantBindings->GetParameterNamesHash(), result);
				}
				for (const auto& c:_samplerBindings) {
					result = HashCombine(c.first, result);
					result = HashCombine(c.second.Hash(), result);
				}
				return result;
			}

			InterpretMaterialMachineHelper(IteratorRange<Assets::ScaffoldCmdIterator> materialMachine)
			{
				for (auto cmd:materialMachine) {
					switch (cmd.Cmd()) {
					case (uint32_t)Assets::MaterialCommand::AttachShaderResourceBindings:
						_resourceBindings = &cmd.As<ParameterBox>();
						break;
					case (uint32_t)Assets::MaterialCommand::AttachConstants:
						_constantBindings = &cmd.As<ParameterBox>();
						break;
					case (uint32_t)Assets::MaterialCommand::AttachSamplerBindings:
						_samplerBindings = cmd.RawData().Cast<const std::pair<uint64_t, SamplerDesc>*>();
						break;
					}
				}
			}
		};
	}

	static const std::string s_multipleDescSetCBs = "DescSetCBs";

	void ConstructDescriptorSetHelper::Construct(
		ResourceConstructionContext* context,
		const Assets::PredefinedDescriptorSetLayout& layout,
		IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
		const DeformerToDescriptorSetBinding* deformBinding,
		std::string&& name)
	{
		// todo -- this might be better if we could construct multiple descriptor sets all at once. Ie, one compound load for an entire model,
		//		rather than a bunch of individual operations

		auto shrLanguage = GetDefaultShaderLanguage();

		int maxSlotIdx = -1;
		for (auto& slot:layout._slots)
			maxSlotIdx = std::max(maxSlotIdx, int(slot._slotIdx));
		assert(maxSlotIdx >= 0);
		
		if (!_working)
			_working = std::make_shared<Internal::DescriptorSetInProgress>();

		Internal::DescriptorSetInProgress::DescriptorSet ds;
		if (_generateBindingInfo)
			ds._bindingInfo._slots.resize(maxSlotIdx+1);

		Internal::InterpretMaterialMachineHelper machineHelper{materialMachine};
		bool applyDeformAcceleratorOffset = false;

		std::vector<uint8_t> cbUploadBuffer;
		const unsigned cbAlignmentRules = 64;		// todo -- get from device
		std::optional<StringSection<>> cbName;

		char stringMeldBuffer[512];
		for (const auto& s:layout._slots) {
			Internal::DescriptorSetInProgress::Slot slotInProgress;
			slotInProgress._slotName = s._name;
			slotInProgress._slotType = s._type;

			DescriptorSetBindingInfo::Slot slotBindingInfo;
			slotBindingInfo._layoutName = s._name;
			slotBindingInfo._layoutSlotType = s._type;

			for (unsigned a=0; a<std::max(s._arrayElementCount, 1u); ++a) {

				bool gotBinding = false;
				auto hashName = Hash64(s._name) + a;
				std::optional<std::string> boundResource = machineHelper._resourceBindings ? machineHelper._resourceBindings->GetParameterAsString(hashName) : std::optional<std::string>{};
				if (boundResource.has_value() && !boundResource.value().empty()) {
					if (s._type != DescriptorType::SampledTexture)
						Throw(std::runtime_error("Attempting to bind resource to non-texture descriptor slot for slot " + s._name));

					slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
					slotInProgress._resourceIdx = (unsigned)_working->_resources.size();
					Internal::DescriptorSetInProgress::Resource res;
					if (context) {
						res._deferredShaderResource = context->ConstructShaderResource(MakeStringSection(boundResource.value()));
					} else
						res._deferredShaderResource = ::Assets::MakeAssetPtr<DeferredShaderResource>(MakeStringSection(boundResource.value()));
					_working->_resources.push_back(res);
					gotBinding = true;

					if (_generateBindingInfo)
						slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "DeferredShaderResource: " << boundResource.value()).AsString();

				} else if ((s._type == DescriptorType::UniformBuffer || s._type == DescriptorType::UniformBufferDynamicOffset) && s._cbIdx < (unsigned)layout._constantBuffers.size()) {

					bool animated = false;
					if (deformBinding) {
						animated = std::find_if(deformBinding->_animatedSlots.begin(), deformBinding->_animatedSlots.end(), [slotIdx=s._slotIdx](const auto& q) { return q.first == slotIdx; }) != deformBinding->_animatedSlots.end();
					}

					if (!animated) {
						auto& cbLayout = layout._constantBuffers[s._cbIdx];
						auto cbSize = cbLayout->GetSize(shrLanguage);
						if (cbSize) {
							auto uploadBufferStart = CeilToMultiple(unsigned(cbUploadBuffer.size()), cbAlignmentRules);
							auto uploadBufferEnd = CeilToMultiple(uploadBufferStart+cbSize, cbAlignmentRules);
							cbUploadBuffer.resize(uploadBufferEnd);
							auto uploadBufferRange = MakeIteratorRange(PtrAdd(cbUploadBuffer.data(), uploadBufferStart), PtrAdd(cbUploadBuffer.data(), uploadBufferEnd));
							
							if (machineHelper._constantBindings) {
								cbLayout->BuildCB(uploadBufferRange, *machineHelper._constantBindings, shrLanguage);
							} else {
								cbLayout->BuildCB(uploadBufferRange, {}, shrLanguage);
							}

							slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
							slotInProgress._resourceIdx = (unsigned)_working->_resources.size();

							Internal::DescriptorSetInProgress::Resource res;
							res._cbResourceOffsetAndSize = {uploadBufferStart, cbSize};
							res._cbUploadIdx = (unsigned)_working->_cbUploadMarkers.size();		// upload marker added later
							_working->_resources.push_back(res);

							if (_generateBindingInfo) {
								std::stringstream str;
								cbLayout->DescribeCB(str, uploadBufferRange, shrLanguage);
								slotBindingInfo._binding = str.str();
							}
							cbName = cbName.has_value() ? MakeStringSection(s_multipleDescSetCBs) : MakeStringSection(s._name);
						}
					} else {
						applyDeformAcceleratorOffset = true;
						slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
						slotInProgress._resourceIdx = (unsigned)_working->_resources.size();

						Internal::DescriptorSetInProgress::Resource res;
						res._fixedResource = deformBinding->_dynamicPageResource->CreateBufferView(BindFlag::ConstantBuffer);
						_working->_resources.push_back(res);
						
						if (_generateBindingInfo)
							slotBindingInfo._binding = "Animated Uniforms";
					}
						
					gotBinding = true;
					
				} else if (s._type == DescriptorType::Sampler && _samplerPool) {
					auto i = std::find_if(machineHelper._samplerBindings.begin(), machineHelper._samplerBindings.end(), [hashName](const auto& c) { return c.first == hashName; });
					if (i != machineHelper._samplerBindings.end()) {
						slotInProgress._bindType = DescriptorSetInitializer::BindType::Sampler;
						slotInProgress._resourceIdx = (unsigned)_working->_samplers.size();
						auto metalSampler = _samplerPool->GetSampler(i->second);
						_working->_samplers.push_back(metalSampler);
						gotBinding = true;

						if (_generateBindingInfo)
							slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "Sampler: " << metalSampler->GetDesc()).AsString();
					}
				}
			
				if (gotBinding) {
					slotInProgress._descriptorSetSlot = s._slotIdx;
					slotInProgress._descriptorSetArrayIdx = a;

					auto existing = std::find_if(ds._slots.begin(), ds._slots.end(), [s=slotInProgress._descriptorSetSlot, a=slotInProgress._descriptorSetArrayIdx](const auto& q) {
							return q._descriptorSetSlot == s && q._descriptorSetArrayIdx == a;
						});
					if (existing != ds._slots.end())
						Throw(std::runtime_error("Multiple resources bound to the same slot in ConstructDescriptorSet(). Attempting to bind slot " + s._name));

					ds._slots.push_back(slotInProgress);
					if (_generateBindingInfo) {
						slotBindingInfo._bindType = slotInProgress._bindType;
						ds._bindingInfo._slots[s._slotIdx] = slotBindingInfo;
					}
				}
			}
		}

		ds._signature = layout.MakeDescriptorSetSignature(_samplerPool);
		ds._applyDeformAcceleratorOffset = applyDeformAcceleratorOffset;

		if (!cbUploadBuffer.empty()) {
			auto& bu = Services::GetBufferUploads();
			auto size = cbUploadBuffer.size();
			_working->_cbUploadMarkers.push_back(
				bu.Begin(
					CreateDesc(BindFlag::ConstantBuffer, LinearBufferDesc::Create((unsigned)size)),
					BufferUploads::CreateBasicPacket(std::move(cbUploadBuffer), cbName.value().AsString())));
		}

		_working->_descriptorSets.emplace_back(std::move(ds));
		_working->_name = std::move(name);
	}

	void ConstructDescriptorSetHelper::CompleteToPromise(
		std::promise<std::vector<ActualizedDescriptorSet>>&& promise)
	{
		::Assets::PollToPromise(
			std::move(promise),
			[working=_working](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				return working->UpdatePollUntil(timeoutTime);
			},
			[working=_working, device=_device, pipelineType=_pipelineType]() {
				std::vector<::Assets::DependencyValidation> subDepVals;
				std::vector<std::shared_ptr<IResourceView>> finalResources;
				finalResources.reserve(working->_resources.size());
				subDepVals.reserve(working->_resources.size());
				BufferUploads::CommandListID completionCommandList = 0;

				std::vector<BufferUploads::ResourceLocator> uploadedCBs;
				uploadedCBs.reserve(working->_cbUploadMarkers.size());
				for (auto& cb:working->_cbUploadMarkers) {
					assert (cb.IsValid());
					uploadedCBs.push_back(cb._future.get());
					completionCommandList = std::max(completionCommandList, (uploadedCBs.end()-1)->GetCompletionCommandList());
				}

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				for (auto&d:working->_resources) {
					if (d._cbResourceOffsetAndSize.second) {
						assert(d._cbUploadIdx != ~0u);
						finalResources.push_back(uploadedCBs[d._cbUploadIdx].CreateBufferView(BindFlag::ConstantBuffer, d._cbResourceOffsetAndSize.first, d._cbResourceOffsetAndSize.second));
						subDepVals.push_back({});
					} else if (!d._fixedResource) {
						auto actualized = d._deferredShaderResource.get();		// note -- on invalidate, the only dep val returned will be the one that is invalid
						finalResources.push_back(actualized->GetShaderResource());

						auto resCmdList = actualized->GetCompletionCommandList();
						if (resCmdList != BufferUploads::CommandListID_Invalid)
							completionCommandList = std::max(resCmdList, completionCommandList);

						subDepVals.push_back(actualized->GetDependencyValidation());
					} else {
						finalResources.push_back(d._fixedResource);
						subDepVals.push_back({});
					}
				}

				assert(completionCommandList != BufferUploads::CommandListID_Invalid);		// use zero when not required, instead

				std::vector<ActualizedDescriptorSet> finalDescriptorSets;
				finalDescriptorSets.reserve(working->_descriptorSets.size());
				for (auto&ds:working->_descriptorSets) {
					std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
					std::vector<::Assets::DependencyValidationMarker> subDepValMarkers;
					subDepValMarkers.reserve(ds._slots.size());
					bindTypesAndIdx.reserve(ds._slots.size());
					for (const auto& s:ds._slots) {
						bindTypesAndIdx.push_back(DescriptorSetInitializer::BindTypeAndIdx{s._bindType, s._resourceIdx, s._descriptorSetSlot, s._descriptorSetArrayIdx});
						if (s._bindType == DescriptorSetInitializer::BindType::ResourceView && subDepVals[s._resourceIdx])
							subDepValMarkers.push_back(subDepVals[s._resourceIdx]);
					}

					::Assets::DependencyValidation depVal;
					{
						// create a dep val for the subdepvals, removing any duplicates
						std::sort(subDepValMarkers.begin(), subDepValMarkers.end());
						subDepValMarkers.erase(std::unique(subDepValMarkers.begin(), subDepValMarkers.end()), subDepValMarkers.end());
						depVal = ::Assets::GetDepValSys().MakeOrReuse(subDepValMarkers);
					}

					std::vector<const IResourceView*> resourceViews;
					std::vector<const ISampler*> samplers;
					resourceViews.reserve(finalResources.size());
					samplers.reserve(working->_samplers.size());
					for (const auto&r:finalResources) resourceViews.push_back(r.get());
					for (const auto&r:working->_samplers) samplers.push_back(r.get());

					DescriptorSetInitializer initializer;
					initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx);
					initializer._bindItems._resourceViews = MakeIteratorRange(resourceViews);
					initializer._bindItems._samplers = MakeIteratorRange(samplers);

					ActualizedDescriptorSet actualized;
					actualized._descriptorSet = device->CreateDescriptorSet(pipelineType, ds._signature, working->_name);
					actualized._descriptorSet->Write(initializer);
					actualized._depVal = std::move(depVal);
					actualized._bindingInfo = std::move(ds._bindingInfo);
					actualized._completionCommandList = completionCommandList;
					actualized._applyDeformAcceleratorOffset = ds._applyDeformAcceleratorOffset;
					finalDescriptorSets.emplace_back(actualized);
				}
				return finalDescriptorSets;
			});
	}

	uint64_t HashMaterialMachine(IteratorRange<Assets::ScaffoldCmdIterator> materialMachine)
	{
		return Internal::InterpretMaterialMachineHelper{materialMachine}.CalculateHash();
	}

	std::shared_ptr<IDescriptorSet> ConstructDescriptorSetHelper::ConstructImmediately(
		const Assets::PredefinedDescriptorSetLayout& layout,
		const UniformsStreamInterface& usi,
		const UniformsStream& us,
		StringSection<> name)
	{
		assert(usi.GetImmediateDataBindings().empty());		// imm data bindings not supported here
		VLA_UNSAFE_FORCE(DescriptorSetInitializer::BindTypeAndIdx, bindTypesAndIdx, layout._slots.size());

		auto* bind = bindTypesAndIdx;
		for (unsigned slotIdx=0; slotIdx<layout._slots.size(); ++slotIdx) {
			const auto& slot = layout._slots[slotIdx];
			auto hash = Hash64(slot._name);

			auto i = std::find(usi.GetResourceViewBindings().begin(), usi.GetResourceViewBindings().end(), hash);
			if (i != usi.GetResourceViewBindings().end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::ResourceView,
					(unsigned)std::distance(usi.GetResourceViewBindings().begin(), i),
					slotIdx};
				continue;
			}

			i = std::find(usi.GetSamplerBindings().begin(), usi.GetSamplerBindings().end(), hash);
			if (i != usi.GetSamplerBindings().end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::Sampler,
					(unsigned)std::distance(usi.GetSamplerBindings().begin(), i),
					slotIdx};
				continue;
			}
		}

		// awkwardly we need to construct a descriptor set signature here
		auto sig = layout.MakeDescriptorSetSignature(_samplerPool);

		DescriptorSetInitializer initializer;
		initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx, bind);
		initializer._bindItems._resourceViews = us._resourceViews;
		initializer._bindItems._samplers = us._samplers;

		auto result = _device->CreateDescriptorSet(_pipelineType, sig, name);
		result->Write(initializer);
		return result;
	}

	const uint64_t DeformerToDescriptorSetBinding::GetHash() const
	{
		return Hash64(MakeIteratorRange(_animatedSlots), _dynamicPageResource->GetGUID());
	}

	ActualizedDescriptorSet::ActualizedDescriptorSet() = default;
	ActualizedDescriptorSet::ActualizedDescriptorSet(ActualizedDescriptorSet&&) = default;
	ActualizedDescriptorSet& ActualizedDescriptorSet::operator=(ActualizedDescriptorSet&&) = default;
	ActualizedDescriptorSet::ActualizedDescriptorSet(const ActualizedDescriptorSet&) = default;
	ActualizedDescriptorSet& ActualizedDescriptorSet::operator=(const ActualizedDescriptorSet&) = default;
	ActualizedDescriptorSet::~ActualizedDescriptorSet() = default;

}}

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DescriptorSetAccelerator.h"
#include "DeferredShaderResource.h"
#include "TechniqueUtils.h"
#include "CommonResources.h"
#include "DeformUniformsInfrastructure.h"
#include "Services.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Assets/MaterialMachine.h"
#include "../Assets/AssetUtils.h"
#include "../IDevice.h"
#include "../UniformsStream.h"
#include "../StateDesc.h"
#include "../../BufferUploads/IBufferUploads.h"
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
				std::shared_ptr<IResourceView> _fixedResource;
			};
			std::vector<Resource> _resources;
			std::vector<std::shared_ptr<ISampler>> _samplers;
			size_t _allReadyBefore = 0;
			BufferUploads::TransactionMarker _cbUploadMarker;

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

				if (_cbUploadMarker._future.wait_until(timeoutTime) == std::future_status::timeout) 
					return ::Assets::PollStatus::Continue;

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
		std::promise<ActualizedDescriptorSet>&& promise,
		const Assets::PredefinedDescriptorSetLayout& layout,
		IteratorRange<Assets::ScaffoldCmdIterator> materialMachine,
		const DeformerToDescriptorSetBinding* deformBinding)
	{
		// todo -- this might be better if we could construct multiple descriptor sets all at once. Ie, one compound load for an entire model,
		//		rather than a bunch of individual operations

		auto shrLanguage = GetDefaultShaderLanguage();

		int maxSlotIdx = -1;
		for (auto& slot:layout._slots)
			maxSlotIdx = std::max(maxSlotIdx, int(slot._slotIdx));
		assert(maxSlotIdx >= 0);
		
		auto working = std::make_shared<Internal::DescriptorSetInProgress>();
		working->_slots.resize(maxSlotIdx+1);
		if (_generateBindingInfo)
			working->_bindingInfo._slots.resize(working->_slots.size());

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

			bool gotBinding = false;
			auto hashName = Hash64(s._name);
			std::optional<std::string> boundResource = machineHelper._resourceBindings ? machineHelper._resourceBindings->GetParameterAsString(hashName) : std::optional<std::string>{};
			if (boundResource.has_value() && !boundResource.value().empty()) {
				if (s._type != DescriptorType::SampledTexture)
					Throw(std::runtime_error("Attempting to bind resource to non-texture descriptor slot for slot " + s._name));

				slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
				slotInProgress._resourceIdx = (unsigned)working->_resources.size();
				Internal::DescriptorSetInProgress::Resource res;
				res._deferredShaderResource = ::Assets::MakeAssetPtr<DeferredShaderResource>(MakeStringSection(boundResource.value()))->ShareFuture();
				working->_resources.push_back(res);
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
						slotInProgress._resourceIdx = (unsigned)working->_resources.size();

						Internal::DescriptorSetInProgress::Resource res;
						res._cbResourceOffsetAndSize = {uploadBufferStart, cbSize};
						working->_resources.push_back(res);

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
					slotInProgress._resourceIdx = (unsigned)working->_resources.size();

					Internal::DescriptorSetInProgress::Resource res;
					res._fixedResource = deformBinding->_dynamicPageResource->CreateBufferView(BindFlag::ConstantBuffer);
					working->_resources.push_back(res);
					
					if (_generateBindingInfo)
						slotBindingInfo._binding = "Animated Uniforms";
				}
					
				gotBinding = true;
				
			} else if (s._type == DescriptorType::Sampler && _samplerPool) {
				auto i = std::find_if(machineHelper._samplerBindings.begin(), machineHelper._samplerBindings.end(), [hashName](const auto& c) { return c.first == hashName; });
				if (i != machineHelper._samplerBindings.end()) {
					slotInProgress._bindType = DescriptorSetInitializer::BindType::Sampler;
					slotInProgress._resourceIdx = (unsigned)working->_samplers.size();
					auto metalSampler = _samplerPool->GetSampler(i->second);
					working->_samplers.push_back(metalSampler);
					gotBinding = true;

					if (_generateBindingInfo)
						slotBindingInfo._binding = (StringMeldInPlace(stringMeldBuffer) << "Sampler: " << metalSampler->GetDesc()).AsString();
				}
			} 
			
			if (gotBinding) {
				if (working->_slots[s._slotIdx]._bindType != DescriptorSetInitializer::BindType::Empty)
					Throw(std::runtime_error("Multiple resources bound to the same slot in ConstructDescriptorSet(). Attempting to bind slot " + s._name));

				assert(s._slotIdx < working->_slots.size());
				working->_slots[s._slotIdx] = slotInProgress;
				if (_generateBindingInfo) {
					slotBindingInfo._bindType = slotInProgress._bindType;
					working->_bindingInfo._slots[s._slotIdx] = slotBindingInfo;
				}
			}
		}

		working->_signature = layout.MakeDescriptorSetSignature(_samplerPool);

		if (!cbUploadBuffer.empty()) {
			auto& bu = Services::GetBufferUploads();
			auto size = cbUploadBuffer.size();
			working->_cbUploadMarker = bu.Transaction_Begin(
				CreateDesc(BindFlag::ConstantBuffer, 0, GPUAccess::Read, LinearBufferDesc::Create((unsigned)size), cbName.value()),
				BufferUploads::CreateBasicPacket(std::move(cbUploadBuffer)));
		}

		::Assets::PollToPromise(
			std::move(promise),
			[working](auto timeout) {
				auto timeoutTime = std::chrono::steady_clock::now() + timeout;
				return working->UpdatePollUntil(timeoutTime);
			},
			[working, device=_device, pipelineType=_pipelineType, applyDeformAcceleratorOffset]() {
				std::vector<::Assets::DependencyValidation> subDepVals;
				std::vector<std::shared_ptr<IResourceView>> finalResources;
				finalResources.reserve(working->_resources.size());
				subDepVals.reserve(working->_resources.size());
				BufferUploads::CommandListID completionCommandList = 0;

				BufferUploads::ResourceLocator uploadedCB;
				if (working->_cbUploadMarker.IsValid()) {
					uploadedCB = working->_cbUploadMarker._future.get();
					completionCommandList = std::max(completionCommandList, uploadedCB.GetCompletionCommandList());
				}

				// Construct the final descriptor set; even if we got some (or all) invalid assets
				for (auto&d:working->_resources) {
					if (d._cbResourceOffsetAndSize.second) {
						finalResources.push_back(uploadedCB.CreateBufferView(BindFlag::ConstantBuffer, d._cbResourceOffsetAndSize.first, d._cbResourceOffsetAndSize.second));
					} else if (!d._fixedResource) {
						auto actualized = d._deferredShaderResource.get();		// note -- on invalidate, the only dep val returned will be the one that is invalid
						finalResources.push_back(actualized->GetShaderResource());

						auto resCmdList = actualized->GetCompletionCommandList();
						if (resCmdList != BufferUploads::CommandListID_Invalid)
							completionCommandList = std::max(resCmdList, completionCommandList);

						auto depVal = actualized->GetDependencyValidation();
						if (depVal)
							subDepVals.push_back(depVal);
					} else {
						finalResources.push_back(d._fixedResource);
					}
				}

				assert(completionCommandList != BufferUploads::CommandListID_Invalid);		// use zero when not required, instead

				::Assets::DependencyValidation depVal;
				{
					// create a dep val for the subdepvals, removing any duplicates
					std::vector<::Assets::DependencyValidationMarker> subDepValMarkers;
					subDepValMarkers.reserve(subDepVals.size());
					for (const auto&d:subDepVals) subDepValMarkers.emplace_back(d);
					std::sort(subDepValMarkers.begin(), subDepValMarkers.end());
					subDepValMarkers.erase(std::unique(subDepValMarkers.begin(), subDepValMarkers.end()), subDepValMarkers.end());
					depVal = ::Assets::GetDepValSys().MakeOrReuse(subDepValMarkers);
				}

				std::vector<DescriptorSetInitializer::BindTypeAndIdx> bindTypesAndIdx;
				bindTypesAndIdx.reserve(working->_slots.size());
				for (const auto&s:working->_slots) {
					bindTypesAndIdx.push_back(DescriptorSetInitializer::BindTypeAndIdx{s._bindType, s._resourceIdx});
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
				initializer._signature = &working->_signature;
				initializer._pipelineType = pipelineType;

				ActualizedDescriptorSet actualized;
				actualized._descriptorSet = device->CreateDescriptorSet(initializer);
				actualized._depVal = std::move(depVal);
				actualized._bindingInfo = std::move(working->_bindingInfo);
				actualized._completionCommandList = completionCommandList;
				actualized._applyDeformAcceleratorOffset = applyDeformAcceleratorOffset;
				return actualized;
			});
	}

	uint64_t HashMaterialMachine(IteratorRange<Assets::ScaffoldCmdIterator> materialMachine)
	{
		return Internal::InterpretMaterialMachineHelper{materialMachine}.CalculateHash();
	}

	std::shared_ptr<IDescriptorSet> ConstructDescriptorSetHelper::ConstructImmediately(
		const Assets::PredefinedDescriptorSetLayout& layout,
		const UniformsStreamInterface& usi,
		const UniformsStream& us)
	{
		assert(usi.GetImmediateDataBindings().empty());		// imm data bindings not supported here
		DescriptorSetInitializer::BindTypeAndIdx bindTypesAndIdx[layout._slots.size()];

		auto* bind = bindTypesAndIdx;
		for (const auto& slot:layout._slots) {
			auto hash = Hash64(slot._name);

			auto i = std::find(usi.GetResourceViewBindings().begin(), usi.GetResourceViewBindings().end(), hash);
			if (i != usi.GetResourceViewBindings().end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::ResourceView,
					(unsigned)std::distance(usi.GetResourceViewBindings().begin(), i)};
				continue;
			}

			i = std::find(usi.GetSamplerBindings().begin(), usi.GetSamplerBindings().end(), hash);
			if (i != usi.GetSamplerBindings().end()) {
				*bind++ = DescriptorSetInitializer::BindTypeAndIdx{
					DescriptorSetInitializer::BindType::Sampler,
					(unsigned)std::distance(usi.GetSamplerBindings().begin(), i)};
				continue;
			}

			*bind++ = DescriptorSetInitializer::BindTypeAndIdx{DescriptorSetInitializer::BindType::Empty, 0};
		}

		// awkwardly we need to construct a descriptor set signature here
		auto sig = layout.MakeDescriptorSetSignature(_samplerPool);

		DescriptorSetInitializer initializer;
		initializer._slotBindings = MakeIteratorRange(bindTypesAndIdx, &bindTypesAndIdx[layout._slots.size()]);
		initializer._bindItems._resourceViews = us._resourceViews;
		initializer._bindItems._samplers = us._samplers;
		initializer._signature = &sig;
		initializer._pipelineType = _pipelineType;

		return _device->CreateDescriptorSet(initializer);
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

// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformUniformsInfrastructure.h"
#include "CompiledShaderPatchCollection.h"
#include "CompiledLayoutPool.h"
#include "DeformerConstruction.h"
#include "../Assets/ModelRendererConstruction.h"
#include "../Assets/PredefinedDescriptorSetLayout.h"
#include "../Assets/PredefinedCBLayout.h"
#include "../Assets/MaterialScaffold.h"
#include "../Assets/MaterialMachine.h"
#include "../UniformsStream.h"
#include "../../Assets/Assets.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
	struct AnimatedUniformBufferHelper
	{
		struct Mapping
		{
			ImpliedTyping::TypeDesc _srcFormat, _dstFormat;
			unsigned _srcOffset, _dstOffset;
		};
		std::vector<Mapping> _parameters;
		std::vector<uint8_t> _baseContents;
		uint64_t _hash = 0ull;
	};

	static std::vector<std::pair<unsigned, AnimatedUniformBufferHelper>> FindAnimatedUniformsBuffers(
		const Assets::PredefinedDescriptorSetLayout& descSetLayout,
		IteratorRange<const AnimatedUniform*> animatedUniforms,
		const ParameterBox* fixedConstants,
		ShaderLanguage shrLanguage)
	{
		std::vector<std::pair<unsigned, AnimatedUniformBufferHelper>> result;
		std::vector<AnimatedUniformBufferHelper::Mapping> parameterMapping;
		for (const auto& slot:descSetLayout._slots) {
			if ((slot._type != DescriptorType::UniformBuffer && slot._type != DescriptorType::UniformBufferDynamicOffset) || slot._cbIdx == ~0u)
				continue;

			auto& cbLayout = *descSetLayout._constantBuffers[slot._cbIdx];
			
			parameterMapping.clear();
			for (const auto& e:cbLayout._elements) {
				auto i = std::find_if(animatedUniforms.begin(), animatedUniforms.end(), [name=e._hash](const auto& q) { return q._name == name; });
				if (i != animatedUniforms.end()) {
					assert(e._arrayElementCount == 0);		// can't animate array elements
					parameterMapping.push_back({
						i->_type, e._type,
						i->_instanceValuesOffset, e._offsetsByLanguage[(unsigned)shrLanguage]});
				}
			}
			if (parameterMapping.empty()) continue;		// no elements are animated

			AnimatedUniformBufferHelper helper;
			helper._parameters = std::move(parameterMapping);
			if (fixedConstants) helper._baseContents = cbLayout.BuildCBDataAsVector(*fixedConstants, shrLanguage);
			else helper._baseContents = cbLayout.BuildCBDataAsVector({}, shrLanguage);

			helper._hash = Hash64(MakeIteratorRange(helper._parameters));
			helper._hash = Hash64(MakeIteratorRange(helper._baseContents), helper._hash);
			result.emplace_back(std::make_pair(slot._slotIdx, std::move(helper)));
		}
		return result;
	}

	static void WriteAnimatedUniforms(IteratorRange<void*> dst, const AnimatedUniformBufferHelper& animHelper, IteratorRange<const void*> srcValues)
	{
		std::memcpy(dst.begin(), animHelper._baseContents.data(), animHelper._baseContents.size());
		for (const auto& p:animHelper._parameters)
			ImpliedTyping::Cast(
				MakeIteratorRange(PtrAdd(dst.begin(), p._dstOffset), dst.end()), p._dstFormat, 
				MakeIteratorRange(PtrAdd(srcValues.begin(), p._srcOffset), srcValues.end()), p._srcFormat);
	}

	class DeformUniformsAttachment : public IDeformUniformsAttachment
	{
	public:
		virtual void Execute(
			IteratorRange<const unsigned*> instanceIdx, 
			IteratorRange<void*> dst) override
		{
			for (auto i:instanceIdx) {
				assert(dst.size() >= _instanceOutputStride);

				if ((i+1) * _instanceInputStride <= _instanceInputValues.size()) {
					WriteAnimatedUniforms(
						dst, _mainUniformHelper,
						MakeIteratorRange(_instanceInputValues.begin() + i*_instanceInputStride, _instanceInputValues.begin() + (i+1*_instanceInputStride)));
				} else {
					std::memcpy(dst.begin(), AsPointer(_defaultInstanceData.begin()), _defaultInstanceData.size());
				}

				dst.first = PtrAdd(dst.first, _instanceOutputStride);
			}
		}

		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes) override
		{
			gpuBufferBytes = _instanceOutputStride * instanceCount;
		}

		virtual const UniformDeformerToRendererBinding& GetDeformerToRendererBinding() const override
		{
			return _rendererBinding;
		}

		virtual void SetInputValues(unsigned instanceIdx, IteratorRange<const void*> data) override
		{
			// would we be better off with an interface that could just get the latest input values when we need it, just for
			// the instances we need?
			assert(data.size() == _instanceInputStride);
			if ((instanceIdx + 1) * _instanceInputStride > _instanceInputValues.size()) {
				_instanceInputValues.reserve((instanceIdx + 1) * _instanceInputStride);
				while ((instanceIdx + 1) * _instanceInputStride > _instanceInputValues.size())
					_instanceInputValues.insert(_instanceInputValues.end(), _defaultInputValues.begin(), _defaultInputValues.end());
			}
			std::memcpy(
				AsPointer(_instanceInputValues.begin() + instanceIdx*_instanceInputStride),
				data.begin(),
				_instanceInputStride);
		}

		virtual IteratorRange<const AnimatedUniform*> GetInputValuesLayout() const override
		{
			return _inputValuesLayout;
		}

		DeformUniformsAttachment(
			AnimatedUniformBufferHelper&& mainUniformHelper,
			IteratorRange<const AnimatedUniform*> inputValuesLayout,
			IteratorRange<const void*> defaultInputValues,
			UniformDeformerToRendererBinding&& rendererBinding)
		: _mainUniformHelper(mainUniformHelper)
		, _instanceInputStride(defaultInputValues.size())
		, _inputValuesLayout{inputValuesLayout.begin(), inputValuesLayout.end()}
		, _rendererBinding(std::move(rendererBinding))
		, _defaultInputValues((const uint8_t*)defaultInputValues.begin(), (const uint8_t*)defaultInputValues.end())
		{
			_instanceOutputStride = (unsigned)_mainUniformHelper._baseContents.size();

			// generate the default instance data from the buffer helper
			_defaultInstanceData = _mainUniformHelper._baseContents;
			WriteAnimatedUniforms(MakeIteratorRange(_defaultInstanceData), _mainUniformHelper, defaultInputValues);
		}

		AnimatedUniformBufferHelper _mainUniformHelper;
		unsigned _instanceOutputStride, _instanceInputStride;
		std::vector<uint8_t> _instanceInputValues;
		std::vector<uint8_t> _defaultInputValues;
		std::vector<uint8_t> _defaultInstanceData;
		std::vector<AnimatedUniform> _inputValuesLayout;
		UniformDeformerToRendererBinding _rendererBinding;
	};

	void ConfigureDeformUniformsAttachment(
		DeformerConstruction& deformerConstruction,
		const Assets::ModelRendererConstruction& rendererConstruction,
		RenderCore::Techniques::ICompiledLayoutPool& compiledLayoutPool,
		IteratorRange<const AnimatedUniform*> animatedUniforms,
		IteratorRange<const void*> defaultInstanceData)
	{
		auto shrLanguage = GetDefaultShaderLanguage();

		UniformDeformerToRendererBinding deformerToRendererBinding;
		std::vector<std::pair<unsigned, AnimatedUniformBufferHelper>> uniqueUniformBuffersAndPageOffsets;
		unsigned pageIterator = 0;

		unsigned elementIdx = 0;
		for (auto ele:rendererConstruction) {
			auto materialScaffold = ele.GetMaterialScaffold();
			if (!materialScaffold) {
				++elementIdx;
				continue;
			}

			for (auto materialGuid:materialScaffold->GetMaterials()) {
				auto materialMachine = materialScaffold->GetMaterialMachine(materialGuid);
				const ParameterBox* constants = nullptr;
				std::shared_ptr<Assets::ShaderPatchCollection> shaderPatchCollection;
				for (auto cmd:materialMachine) {
					switch (cmd.Cmd()) {
					case (uint32_t)Assets::MaterialCommand::AttachConstants:
						constants = &cmd.As<ParameterBox>();
						break;
					case (uint32_t)Assets::MaterialCommand::AttachPatchCollectionId:
						shaderPatchCollection = materialScaffold->GetShaderPatchCollection(cmd.As<uint64_t>());
						break;
					}
				}

				// Match our animatable uniforms to the uniforms in the layout from the final 
				// material descriptor set. We need to use the same material desc set layout that
				// PipelineAcceleratorPool will use when instantiating the main descriptor set. This
				// includes any modifications made by the CompiledShaderPatchCollection...
				std::vector<std::pair<unsigned, AnimatedUniformBufferHelper>> animBuffers;
				if (shaderPatchCollection) {
					auto patchCollectionFuture = compiledLayoutPool.GetPatchCollectionFuture(*shaderPatchCollection);
					patchCollectionFuture->StallWhilePending();
					auto compiledPatchCollection = patchCollectionFuture->Actualize();
					animBuffers = FindAnimatedUniformsBuffers(
						compiledPatchCollection->GetInterface().GetMaterialDescriptorSet(),
						animatedUniforms,
						constants,
						shrLanguage);
				} else {
					animBuffers = FindAnimatedUniformsBuffers(
						compiledLayoutPool.GetBaseMaterialDescriptorSetLayout(),
						animatedUniforms,
						constants,
						shrLanguage);
				}

				if (animBuffers.empty()) continue;

				// compare found uniform buffers and combine with any others that are identical
				UniformDeformerToRendererBinding::MaterialBinding matBinding;
				matBinding._animatedSlots.reserve(animBuffers.size());
				for (auto& b:animBuffers) {
					auto i = std::find_if(uniqueUniformBuffersAndPageOffsets.begin(), uniqueUniformBuffersAndPageOffsets.end(), [hash=b.second._hash](const auto& q) { return q.second._hash == hash; });
					unsigned pageOffset = 0;
					if (i == uniqueUniformBuffersAndPageOffsets.end()) {
						pageOffset = pageIterator;
						pageIterator += b.second._baseContents.size();
						uniqueUniformBuffersAndPageOffsets.emplace_back(pageOffset, std::move(b.second));
					} else
						pageOffset = i->first;
					matBinding._animatedSlots.emplace_back(b.first, pageOffset);
				}

				deformerToRendererBinding._materialBindings.emplace_back(
					std::make_pair(elementIdx, materialGuid),
					std::move(matBinding));
			}

			++elementIdx;
		}

		if (uniqueUniformBuffersAndPageOffsets.empty()) return;

		// merge all of the unique uniform buffers into one uber AnimatedUniformBufferHelper
		unsigned totalParametersCount = 0;
		unsigned totalBaseConstantsSize = 0;
		for (auto& c:uniqueUniformBuffersAndPageOffsets) {
			totalParametersCount += c.second._parameters.size();
			totalBaseConstantsSize += c.second._baseContents.size();
		}

		AnimatedUniformBufferHelper finalBufferHelper;
		finalBufferHelper._parameters.reserve(totalParametersCount);
		finalBufferHelper._baseContents.reserve(totalBaseConstantsSize);
		for (auto& c:uniqueUniformBuffersAndPageOffsets) {
			for (auto& p:c.second._parameters) {
				auto param = p;
				param._dstOffset += c.first;		// offset into final buffer
				finalBufferHelper._parameters.push_back(param);
			}
			finalBufferHelper._baseContents.insert(
				finalBufferHelper._baseContents.end(),
				c.second._baseContents.begin(), c.second._baseContents.end());
		}

		auto attachment = std::make_shared<DeformUniformsAttachment>(
			std::move(finalBufferHelper),
			animatedUniforms,
			defaultInstanceData,
			std::move(deformerToRendererBinding));

		deformerConstruction.Add(std::move(attachment));
	}

#if 0
	struct DynamicPageResourceHelper
	{
		RenderCore::Metal_Vulkan::TemporaryStorageResourceMap _dynamicPageResourceWorkingAllocation;
		unsigned _dynamicPageResourceAlignment = 16u;
		unsigned _dynamicPageMovingGPUOffset = 0, _dynamicPageMovingGPUEnd = 0;
		IDeformAcceleratorPool* _deformAccelerators = nullptr;

		void AllocateSpace(unsigned sizeBytes)
		{
			// if it fits in the existing block, go with that; otherwise allocate a new block
			unsigned preAlign = CeilToMultiple((size_t)_dynamicPageMovingGPUOffset, _dynamicPageResourceAlignment) - (size_t)_dynamicPageMovingGPUOffset;
			if ((_dynamicPageMovingGPUOffset+preAlign+sizeBytes) > _dynamicPageMovingGPUEnd) {
				const unsigned defaultBlockSize = 16*1024;
				assert(_deformAccelerators);
				_dynamicPageResourceWorkingAllocation = AllocateFromDynamicPageResource(*_deformAccelerators, std::max(sizeBytes, defaultBlockSize));
				_dynamicPageMovingGPUOffset = 0;
				_dynamicPageMovingGPUEnd = _dynamicPageMovingGPUOffset + _dynamicPageResourceWorkingAllocation.GetData().size();
				preAlign = 0;
			}

			// deal with alignments and allocate our space from the allocated block
			IteratorRange<void*> dynamicPageBufferSpace;
			dynamicPageBufferSpace.first = PtrAdd(_dynamicPageResourceWorkingAllocation.GetData().begin(), _dynamicPageMovingGPUOffset+preAlign);
			dynamicPageBufferSpace.second = PtrAdd(dynamicPageBufferSpace.first, sizeBytes);
			unsigned dynamicOffset = _dynamicPageResourceWorkingAllocation.GetBeginAndEndInResource().first+_dynamicPageMovingGPUOffset+preAlign;
			assert((dynamicOffset % _dynamicPageResourceAlignment) == 0);
			_dynamicPageMovingGPUOffset += preAlign+sizeBytes;
		}

		DynamicPageResourceHelper(IDeformAcceleratorPool& deformAccelerators)
		{
			_dynamicPageResourceAlignment = deformAccelerators.GetDynamicPageResourceAlignment();
			_deformAccelerators = &deformAccelerators;
		}
	};
#endif

}}


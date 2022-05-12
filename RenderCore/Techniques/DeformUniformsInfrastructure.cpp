// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformUniformsInfrastructure.h"
#include "DeformOperationFactory.h"
#include "CompiledShaderPatchCollection.h"
#include "PipelineOperators.h"
#include "../Assets/ScaffoldCmdStream.h"
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
						i->_offset, e._offsetsByLanguage[(unsigned)shrLanguage]});
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

#if 0
	class DeformUniformsAttachment : public IDeformUniformsAttachment
	{
	public:
		virtual void Execute(
			IteratorRange<const unsigned*> instanceIdx, 
			IteratorRange<void*> dst) override
		{
			static float time = 0.f;
			time += 1.0f / 100.f;

			assert(!dst.empty());
			assert(_outputInstanceStride);
			for (auto i:instanceIdx) {
				auto* d = PtrAdd(dst.data(), i*_outputInstanceStride);
				*((Float2*)d) = Float2(0, /*-1.f/3.f*/ -1.f/8.3f * time);
			}
		}

		virtual void ReserveBytesRequired(unsigned instanceCount, unsigned& gpuBufferBytes, unsigned& cpuBufferBytes) override
		{
			cpuBufferBytes = _outputInstanceStride * instanceCount;
		}

		DeformUniformsAttachment()
		{
			_bindings.push_back({Hash64("UV_Offset"), ImpliedTyping::TypeOf<Float2>(), 0});
			_outputInstanceStride = sizeof(Float2);
		}

		unsigned _outputInstanceStride;
		std::vector<Bindings> _bindings;
	};

	std::shared_ptr<IDeformUniformsAttachment> CreateDeformUniformsAttachment(
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		// todo -- we need to be able to decide when to create a deform parameters attachment, and when not to
		return nullptr;
		// return std::make_shared<DeformParametersAttachment>();
	}
#endif

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
					WriteAnimatedUniforms(dst, _mainUniformHelper, MakeIteratorRange(_defaultInstanceData));
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
					_instanceInputValues.insert(_instanceInputValues.end(), _defaultInstanceData.begin(), _defaultInstanceData.end());
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
			IteratorRange<const void*> defaultInstanceData,
			IteratorRange<const AnimatedUniform*> inputValuesLayout,
			unsigned instanceOutputStride,
			UniformDeformerToRendererBinding&& rendererBinding)
		: _mainUniformHelper(mainUniformHelper)
		, _instanceOutputStride(instanceOutputStride)
		, _instanceInputStride(defaultInstanceData.size())
		, _defaultInstanceData{(const uint8_t*)defaultInstanceData.begin(), (const uint8_t*)defaultInstanceData.end()}
		, _inputValuesLayout{inputValuesLayout.begin(), inputValuesLayout.end()}
		, _rendererBinding(std::move(rendererBinding))
		{
		}

		AnimatedUniformBufferHelper _mainUniformHelper;
		unsigned _instanceOutputStride, _instanceInputStride;
		std::vector<uint8_t> _instanceInputValues;
		std::vector<uint8_t> _defaultInstanceData;
		std::vector<AnimatedUniform> _inputValuesLayout;
		UniformDeformerToRendererBinding _rendererBinding;
	};

	void ConfigureDeformUniformsAttachment(
		DeformerConstruction& deformerConstruction,
		const Assets::RendererConstruction& rendererConstruction,
		const DescriptorSetLayoutAndBinding& matDescSetLayout,
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
					auto patchCollectionFuture = ::Assets::MakeAssetPtr<CompiledShaderPatchCollection>(*shaderPatchCollection, matDescSetLayout);
					patchCollectionFuture->StallWhilePending();
					auto compiledPatchCollection = patchCollectionFuture->Actualize();
					animBuffers = FindAnimatedUniformsBuffers(
						compiledPatchCollection->GetInterface().GetMaterialDescriptorSet(),
						animatedUniforms,
						constants,
						shrLanguage);
				} else {
					animBuffers = FindAnimatedUniformsBuffers(
						*matDescSetLayout.GetLayout(),
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
			defaultInstanceData,
			animatedUniforms,
			defaultInstanceData.size(),
			std::move(deformerToRendererBinding));

		deformerConstruction.Add(std::move(attachment));
	}

#if 0

///////////////////////////////////////////////////////

	unsigned dynamicPageBufferResourceIdx = ~0u;
	unsigned dynamicPageBufferMovingOffset = 0u;
	const unsigned dynamicPageBufferAlignment = 256u;		// todo -- get this from somewhere

///////////////////////////////////////////////////////

	auto& cbLayout = layout._constantBuffers[s._cbIdx];
	auto buffer = cbLayout->BuildCBDataAsVector(constantBindings, shrLanguage);

	// try to match parameters in the cb layout to the animated parameter list
	std::vector<AnimatedUniformBufferHelper::Mapping> parameterMapping;
	unsigned dynamicPageBufferStart = CeilToMultiple(dynamicPageBufferMovingOffset, dynamicPageBufferAlignment);
	for (const auto& p:cbLayout->_elements) {
		auto i = std::find_if(animatedBindings.begin(), animatedBindings.end(), [&p](const auto& q) { return q._name == p._hash; });
		if (i != animatedBindings.end()) {
			assert(p._arrayElementCount == 0);		// can't animate array elements
			parameterMapping.push_back({
				i->_type, p._type,
				i->_offset, dynamicPageBufferStart+p._offsetsByLanguage[(unsigned)shrLanguage]});
		}
	}

	if (!parameterMapping.empty()) {
		// animated dynamically written buffer
		if (dynamicPageBufferResourceIdx == ~0u) {
			dynamicPageBufferResourceIdx = (unsigned)working._resources.size();
			DescriptorSetInProgress::Resource res;
			assert(dynamicPageResource);
			res._fixedResource = dynamicPageResource;		// todo -- if there are multiple buffers that reference this, should we have an offset here?
			working._resources.push_back(res);
		}

		assert(s._type == DescriptorType::UniformBufferDynamicOffset);		// we must have a dynamic offset for this mechanism to work
		slotInProgress._bindType = DescriptorSetInitializer::BindType::ResourceView;
		slotInProgress._resourceIdx = dynamicPageBufferResourceIdx;

		dynamicPageBufferMovingOffset = dynamicPageBufferStart + (unsigned)buffer.size();
		if (!working._animHelper)
			working._animHelper = std::make_shared<AnimatedUniformBufferHelper>();
		working._animHelper->_baseContents.resize(dynamicPageBufferMovingOffset, 0);
		std::memcpy(PtrAdd(working._animHelper->_baseContents.data(), dynamicPageBufferStart), buffer.data(), buffer.size());
		working._animHelper->_parameters.insert(working._animHelper->_parameters.end(), parameterMapping.begin(), parameterMapping.end());
	}

	//////////////////////////////////
	if (working._animHelper)
		working._animHelper->_dynamicPageBufferSize = dynamicPageBufferMovingOffset;

	namespace Internal
	{
		bool PrepareDynamicPageResource(
			const ActualizedDescriptorSet& descSet,
			IteratorRange<const void*> animatedParameters,
			IteratorRange<void*> dynamicPageBuffer)
		{
			if (!descSet._animHelper)
				return false;

			// copy in the parameter values to their expected destinations
			auto& animHelper = *descSet._animHelper;
			std::memcpy(dynamicPageBuffer.begin(), animHelper._baseContents.data(), animHelper._baseContents.size());
			for (const auto& p:animHelper._parameters)
				ImpliedTyping::Cast(
					MakeIteratorRange(PtrAdd(dynamicPageBuffer.begin(), p._dstOffset), dynamicPageBuffer.end()), p._dstFormat, 
					MakeIteratorRange(PtrAdd(animatedParameters.begin(), p._srcOffset), animatedParameters.end()), p._srcFormat);
			return true;
		}
	}
#endif

}}


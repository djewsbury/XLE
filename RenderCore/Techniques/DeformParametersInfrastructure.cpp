// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformParametersInfrastructure.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{

	class DeformParametersAttachment : public IDeformParametersAttachment
	{
	public:
		virtual void SetInputParameters(unsigned instanceIdx, const Utility::ParameterBox& parameters) override {}

		virtual IteratorRange<const Bindings*> GetOutputParameterBindings() const override
		{
			return _bindings;
		}

		virtual void Execute(
			IteratorRange<const unsigned*> instanceIdx, 
			IteratorRange<void*> dst,
			unsigned outputInstanceStride) override
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

		unsigned GetOutputInstanceStride() const override { return _outputInstanceStride; }

		DeformParametersAttachment()
		{
			_bindings.push_back({Hash64("UV_Offset"), ImpliedTyping::TypeOf<Float2>(), 0});
			_outputInstanceStride = sizeof(Float2);
		}

		unsigned _outputInstanceStride;
		std::vector<Bindings> _bindings;
	};

	std::shared_ptr<IDeformParametersAttachment> CreateDeformParametersAttachment(
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName)
	{
		// todo -- we need to be able to decide when to create a deform parameters attachment, and when not to
		return nullptr;
		// return std::make_shared<DeformParametersAttachment>();
	}

#if 0

	struct AnimatedUniformBufferHelper
	{
		struct Mapping
		{
			ImpliedTyping::TypeDesc _srcFormat, _dstFormat;
			unsigned _srcOffset, _dstOffset;
		};
		std::vector<Mapping> _parameters;
		std::vector<uint8_t> _baseContents;
		unsigned _dynamicPageBufferSize = 0;
	};

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


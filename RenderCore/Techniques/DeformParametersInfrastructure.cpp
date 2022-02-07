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
			time += 1.0f / 30.f;

			for (auto i:instanceIdx) {
				auto* d = PtrAdd(dst.data(), i*_outputInstanceStride);
				*((Float2*)d) = Float2(0, -(1/30.f) * time);
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
		return std::make_shared<DeformParametersAttachment>();
	}

}}


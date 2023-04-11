// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <future>
#include <memory>

namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class FragmentStitchingContext; class TechniqueSetFile; }}
namespace RenderCore { class FrameBufferProperties; }
namespace Assets { class DependencyValidation; }

namespace RenderCore { namespace LightingEngine
{
	struct GBufferOperatorDesc
	{
		enum class NormalType { None, Packed8Bit, Float16, Float32 };
		NormalType _normalType = NormalType::None;

		enum class MotionType { None, Packed8Bit };
		MotionType _motionType = MotionType::None;

		enum class ParametersType { None, Roughness, Full };
		ParametersType _parametersType = ParametersType::None;

		enum class ColorType { None, DiffusePacked8Bit };
		ColorType _colorType = ColorType::None;

		enum class HistoryConfidenceType { None, Enabled };
		HistoryConfidenceType _historyConfidenceType = HistoryConfidenceType::None;

		GBufferOperatorDesc() = default;
		GBufferOperatorDesc(const GBufferOperatorDesc&) = default;
		GBufferOperatorDesc& operator=(const GBufferOperatorDesc&) = default;

		template<typename Formatter, typename std::enable_if_t<!std::is_same_v<std::decay_t<Formatter>, GBufferOperatorDesc>>* =nullptr>
			GBufferOperatorDesc(Formatter&);

		uint64_t GetHash() const;
	};

	class SharedTechniqueDelegateBox;
	class RenderStepFragmentInterface;
	enum class GBufferDelegateType;

	class GBufferOperator
	{
	public:
		RenderStepFragmentInterface CreateFragment();
		void PreregisterAttachments(RenderCore::Techniques::FragmentStitchingContext& stitching, const FrameBufferProperties& fbProps);
		::Assets::DependencyValidation GetDependencyValidation() const;

		GBufferOperator(std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> techDel, const GBufferOperatorDesc& opDesc);
		GBufferOperator();

		static void ConstructToPromise(
			std::promise<GBufferOperator>&& promise,
			std::shared_ptr<SharedTechniqueDelegateBox> sharedTechDel,
			const GBufferOperatorDesc& opDesc);

		static void ConstructToPromise(
			std::promise<GBufferOperator>&& promise,
			const GBufferOperatorDesc& opDesc);

	private:
		std::shared_ptr<RenderCore::Techniques::ITechniqueDelegate> _techDel;
		GBufferOperatorDesc _opDesc;
		GBufferDelegateType _gbufferType;

		class ResourceDelegate;
	};

	enum class GBufferDelegateType
	{
		Depth,
		DepthMotion,
		DepthMotionNormal,
		DepthMotionNormalRoughness,
		DepthMotionNormalRoughnessAccumulation,

		DepthNormal,
		DepthNormalParameters
	};

	void CreateTechniqueDelegate_GBuffer(
		std::promise<std::shared_ptr<Techniques::ITechniqueDelegate>>&& promise,
		const std::shared_future<std::shared_ptr<Techniques::TechniqueSetFile>>& techniqueSet,
		GBufferDelegateType type);

}}


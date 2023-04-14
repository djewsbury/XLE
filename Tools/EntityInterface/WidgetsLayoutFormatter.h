// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/StringUtils.h"
#include <type_traits>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}
namespace RenderOverlays { class LayoutEngine; class GuidStackHelper; }

namespace EntityInterface
{
	template<typename UnderlyingType>
		class MinimalBindingValue;
	class MinimalBindingEngine;
	class HierarchicalEnabledStatesHelper;

	namespace Internal
	{
		template <typename T> static auto UnderlyingTypeHelper(T) -> typename T::UnderlyingType;
		template <typename T> static auto UnderlyingTypeHelper(...) -> std::decay_t<T>;
		template <typename T> using UnderlyingType = decltype(UnderlyingTypeHelper<T>(std::declval<T>()));
	}

	enum class HierarchicalEnabledState { NoImpact, DisableChildren, EnableChildren };

	class IWidgetsLayoutContext
	{
	public:
		virtual MinimalBindingEngine& GetBindingEngine() = 0;
		virtual std::shared_ptr<MinimalBindingEngine> GetBindingEnginePtr() = 0;
		virtual RenderOverlays::LayoutEngine& GetLayoutEngine() = 0;
		virtual RenderOverlays::GuidStackHelper& GetGuidStack() = 0;

		virtual void PushHierarchicalEnabledState(uint64_t guid) = 0;
		virtual void PopHierarchicalEnabledState() = 0;
		virtual HierarchicalEnabledState EnabledByHierarchy() const = 0;
		
		virtual ~IWidgetsLayoutContext();
	};

	class ICommonWidgetsStyler
	{
	public:
		template<typename T> using V = MinimalBindingValue<T>;

		virtual void WriteHalfDoubleInt(IWidgetsLayoutContext&, StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& min, const V<int64_t>& max) = 0;
		virtual void WriteHalfDoubleFloat(IWidgetsLayoutContext&, StringSection<> label, const V<float>& initialValue, const V<float>& min, const V<float>& max) = 0;
		virtual void WriteDecrementIncrementInt(IWidgetsLayoutContext&, StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& min, const V<int64_t>& max) = 0;
		virtual void WriteDecrementIncrementFloat(IWidgetsLayoutContext&, StringSection<> label, const V<float>& initialValue, const V<float>& min, const V<float>& max) = 0;
		virtual void WriteBoundedInt(IWidgetsLayoutContext&, StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& leftSideValue, const V<int64_t>& rightSideValue) = 0;
		virtual void WriteBoundedFloat(IWidgetsLayoutContext&, StringSection<> label, const V<float>& initialValue, const V<float>& leftSideValue, const V<float>& rightSideValue) = 0;

		virtual void WriteHorizontalCombo(IWidgetsLayoutContext&, StringSection<> label, const V<int64_t>& initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) = 0;
		virtual void WriteCheckbox(IWidgetsLayoutContext&, StringSection<> label, const V<bool>& initialValue) = 0;

		virtual bool BeginCollapsingContainer(IWidgetsLayoutContext&, StringSection<> label) = 0;
		virtual void BeginContainer(IWidgetsLayoutContext&) = 0;
		virtual void EndContainer(IWidgetsLayoutContext&) = 0;

		template<typename A, typename B, typename C>
			constexpr void WriteHalfDouble(IWidgetsLayoutContext& context, StringSection<> label, const A& initialValue, const B& minValue, const C& maxValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteHalfDoubleInt(context, label, initialValue, minValue, maxValue);
			} else {
				WriteHalfDoubleFloat(context, label, initialValue, minValue, maxValue);
			}
		}

		template<typename A, typename B, typename C>
			void WriteDecrementIncrement(IWidgetsLayoutContext& context, StringSection<> label, const A& initialValue, const B& minValue, const C& maxValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteDecrementIncrementInt(context, label, initialValue, minValue, maxValue);
			} else {
				WriteDecrementIncrementFloat(context, label, initialValue, minValue, maxValue);
			}
		}

		template<typename A, typename B, typename C>
			void WriteBounded(IWidgetsLayoutContext& context, StringSection<> label, const A& initialValue, const B& leftSideValue, const C& rightSideValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteBoundedInt(context, label, initialValue, leftSideValue, rightSideValue);
			} else {
				WriteBoundedFloat(context, label, initialValue, leftSideValue, rightSideValue);
			}
		}
	};

	class HierarchicalEnabledStatesHelper
	{
	public:
		void Push(uint64_t guid) { _hierarchicalEnabledStates.push_back(guid); }
		void Pop() { _hierarchicalEnabledStates.pop_back(); }
		HierarchicalEnabledState EnabledByHierarchy() const;

	private:
		std::vector<uint64_t> _hierarchicalEnabledStates;
	};

	using WriteToLayoutFormatter = std::function<void(IWidgetsLayoutContext&)>;
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(
		std::shared_ptr<MinimalBindingEngine> doc,
		WriteToLayoutFormatter&& layoutFn);

	std::shared_ptr<ICommonWidgetsStyler> CreateCommonWidgetsStyler();

}


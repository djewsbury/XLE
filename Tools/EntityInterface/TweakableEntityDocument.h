// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "TweakableEntityDocumentInternal.h"
#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}

namespace EntityInterface
{
	template<typename UnderlyingType>
		class MinimalBindingValue;
	class MinimalBindingEngine;

	namespace Internal
	{
		template <typename T> static auto UnderlyingTypeHelper(T) -> typename T::UnderlyingType;
		template <typename T> static auto UnderlyingTypeHelper(...) -> std::decay_t<T>;
		template <typename T> using UnderlyingType = decltype(UnderlyingTypeHelper<T>(std::declval<T>()));
	}

	class IWidgetsLayoutFormatter
	{
	public:
		template<typename T> using V = MinimalBindingValue<T>;

		template<typename A, typename B, typename C>
			constexpr void WriteHalfDouble(StringSection<> label, const A& initialValue, const B& minValue, const C& maxValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteHalfDoubleInt(label, initialValue, minValue, maxValue);
			} else {
				WriteHalfDoubleFloat(label, initialValue, minValue, maxValue);
			}
		}

		template<typename A, typename B, typename C>
			void WriteDecrementIncrement(StringSection<> label, const A& initialValue, const B& minValue, const C& maxValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteDecrementIncrementInt(label, initialValue, minValue, maxValue);
			} else {
				WriteDecrementIncrementFloat(label, initialValue, minValue, maxValue);
			}
		}

		template<typename A, typename B, typename C>
			void WriteBounded(StringSection<> label, const A& initialValue, const B& leftSideValue, const C& rightSideValue)
		{
			constexpr auto integralA = std::is_integral_v<Internal::UnderlyingType<A>>;
			constexpr auto integralB = std::is_integral_v<Internal::UnderlyingType<B>>;
			constexpr auto integralC = std::is_integral_v<Internal::UnderlyingType<C>>;

			if constexpr (integralA && integralB && integralC) {
				WriteBoundedInt(label, initialValue, leftSideValue, rightSideValue);
			} else {
				WriteBoundedFloat(label, initialValue, leftSideValue, rightSideValue);
			}
		}

		virtual void WriteHalfDoubleInt(StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& min, const V<int64_t>& max) = 0;
		virtual void WriteHalfDoubleFloat(StringSection<> label, const V<float>& initialValue, const V<float>& min, const V<float>& max) = 0;
		virtual void WriteDecrementIncrementInt(StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& min, const V<int64_t>& max) = 0;
		virtual void WriteDecrementIncrementFloat(StringSection<> label, const V<float>& initialValue, const V<float>& min, const V<float>& max) = 0;
		virtual void WriteBoundedInt(StringSection<> label, const V<int64_t>& initialValue, const V<int64_t>& leftSideValue, const V<int64_t>& rightSideValue) = 0;
		virtual void WriteBoundedFloat(StringSection<> label, const V<float>& initialValue, const V<float>& leftSideValue, const V<float>& rightSideValue) = 0;

		virtual void WriteHorizontalCombo(StringSection<> label, const V<int64_t>& initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) = 0;
		virtual void WriteCheckbox(StringSection<> label, const V<bool>& initialValue) = 0;

		virtual bool BeginCollapsingContainer(StringSection<> label) = 0;
		virtual void BeginContainer() = 0;
		virtual void EndContainer() = 0;

		virtual MinimalBindingEngine& GetBindingEngine() = 0;

		virtual ~IWidgetsLayoutFormatter();
	};

	class IDynamicOutputFormatter		// todo -- consider moving to Formatters namespace
	{
	public:
		using ElementId = unsigned;
		virtual ElementId BeginKeyedElement(StringSection<> label) = 0;
		virtual ElementId BeginSequencedElement() = 0;
		virtual void EndElement(ElementId) = 0;

		virtual void WriteKeyedValue(StringSection<> label, StringSection<> value) = 0;
		virtual void WriteSequencedValue(StringSection<> value) = 0;

		virtual void WriteKeyedValue(StringSection<> label, IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) = 0;
		virtual void WriteSequencedValue(IteratorRange<const void*> data, const ImpliedTyping::TypeDesc& type) = 0;

		template<typename Type>
			void WriteKeyedValue(StringSection<> label, Type value)
		{
			WriteKeyedValue(label, MakeOpaqueIteratorRange(value), ImpliedTyping::TypeOf<Type>());
		}

		template<typename Type>
			void WriteSequencedValue(Type value)
		{
			WriteSequencedValue(MakeOpaqueIteratorRange(value), ImpliedTyping::TypeOf<Type>());
		}

		virtual ~IDynamicOutputFormatter();
	};

	class MinimalBindingEngine;
	class ITweakableDocumentInterface : public IEntityDocument
	{
	public:
		virtual void IncreaseValidationIndex() = 0;
		virtual std::shared_ptr<MinimalBindingEngine> GetBindingEngine() = 0;
	};

	using WriteToModelFormatter = std::function<void(IDynamicOutputFormatter&)>;
	std::shared_ptr<ITweakableDocumentInterface> CreateTweakableDocumentInterface(WriteToModelFormatter&& modelFn);

	using WriteToLayoutFormatter = std::function<void(IWidgetsLayoutFormatter&)>;
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(std::shared_ptr<ITweakableDocumentInterface> doc, WriteToLayoutFormatter&& layoutFn);
}


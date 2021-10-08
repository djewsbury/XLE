// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include <memory>

namespace RenderOverlays { namespace DebuggingDisplay { class IWidget; }}

namespace EntityInterface
{
	class IWidgetsLayoutFormatter
	{
	public:
		template<typename Type, typename std::enable_if<std::is_integral_v<Type>>::type* =nullptr>
			void WriteHalfDouble(StringSection<> name, Type initialValue, Type minValue, Type maxValue) 
		{
			WriteHalfDoubleInt(name, (int64_t)initialValue, (int64_t)minValue, (int64_t)maxValue);
		}
		template<typename Type, typename std::enable_if<!std::is_integral_v<Type>>::type* =nullptr>
			void WriteHalfDouble(StringSection<> name, Type initialValue, Type minValue, Type maxValue) 
		{
			WriteHalfDoubleFloat(name, (float)initialValue, (float)minValue, (float)maxValue);
		}

		template<typename Type, typename std::enable_if<std::is_integral_v<Type>>::type* =nullptr>
			void WriteDecrementIncrement(StringSection<> name, Type initialValue, Type minValue, Type maxValue) 
		{
			WriteDecrementIncrementInt(name, (int64_t)initialValue, (int64_t)minValue, (int64_t)maxValue);
		}
		template<typename Type, typename std::enable_if<!std::is_integral_v<Type>>::type* =nullptr>
			void WriteDecrementIncrement(StringSection<> name, Type initialValue, Type minValue, Type maxValue) 
		{
			WriteDecrementIncrementFloat(name, (float)initialValue, (float)minValue, (float)maxValue);
		}

		template<typename Type, typename std::enable_if<std::is_integral_v<Type>>::type* =nullptr>
			void WriteBounded(StringSection<> name, Type initialValue, Type leftSideValue, Type rightSideValue)
		{
			WriteBoundedInt(name, (int64_t)initialValue, (int64_t)leftSideValue, (int64_t)rightSideValue);
		}

		template<typename Type, typename std::enable_if<!std::is_integral_v<Type>>::type* =nullptr>
			void WriteBounded(StringSection<> name, Type initialValue, Type leftSideValue, Type rightSideValue)
		{
			WriteBoundedFloat(name, (float)initialValue, (float)leftSideValue, (float)rightSideValue);
		}

		virtual void WriteHalfDoubleInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) = 0;
		virtual void WriteHalfDoubleFloat(StringSection<> name, float initialValue, float min, float max) = 0;
		virtual void WriteDecrementIncrementInt(StringSection<> name, int64_t initialValue, int64_t min, int64_t max) = 0;
		virtual void WriteDecrementIncrementFloat(StringSection<> name, float initialValue, float min, float max) = 0;
		virtual void WriteBoundedInt(StringSection<> name, int64_t initialValue, int64_t leftSideValue, int64_t rightSideValue) = 0;
		virtual void WriteBoundedFloat(StringSection<> name, float initialValue, float leftSideValue, float rightSideValue) = 0;

		virtual void WriteHorizontalCombo(StringSection<> name, int64_t initialValue, IteratorRange<const std::pair<int64_t, const char*>*> options) = 0;
		virtual void WriteCheckbox(StringSection<> name, bool initialValue) = 0;

		virtual bool GetCheckbox(StringSection<> name, bool initialValue) = 0;

		virtual bool BeginCollapsingContainer(StringSection<> name) = 0;
		virtual void BeginContainer() = 0;
		virtual void EndContainer() = 0;

		using ElementId = unsigned;
		virtual ElementId BeginKeyedElement(StringSection<> name) = 0;
		virtual ElementId BeginSequencedElement() = 0;
		virtual void EndElement(ElementId) = 0;

		virtual void WriteKeyedValue(StringSection<> name, StringSection<> value) = 0;
		virtual void WriteSequencedValue(StringSection<> value) = 0;
	};

	class ArbiterState;
	class ITweakableDocumentInterface : public IEntityDocument
	{
	public:
		virtual void ExecuteOnFormatter(IWidgetsLayoutFormatter& fmttr) = 0;
		virtual void IncreaseValidationIndex() = 0;
		virtual std::shared_ptr<ArbiterState> GetArbiterState() = 0;
	};

	using WriteToLayoutFormatter = std::function<void(IWidgetsLayoutFormatter&)>;
	std::shared_ptr<ITweakableDocumentInterface> CreateTweakableDocumentInterface(WriteToLayoutFormatter&& fn);
	std::shared_ptr<RenderOverlays::DebuggingDisplay::IWidget> CreateWidgetGroup(std::shared_ptr<ITweakableDocumentInterface> doc);
}


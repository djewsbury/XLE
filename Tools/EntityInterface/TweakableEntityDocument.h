// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include <memory>

namespace EntityInterface
{
	class IDynamicOutputFormatter
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
	class IOutputFormatterWithDataBinding : public IDynamicOutputFormatter
	{
	public:
		virtual void WriteKeyedModelValue(StringSection<> label) = 0;
		virtual void WriteSequencedModelValue() = 0;
		virtual MinimalBindingEngine& GetBindingEngine() = 0;
	};

	class IEntityDocumentWithDataBinding : public IEntityDocument
	{
	public:
		virtual void TestUpstreamValidationIndex() = 0;
	};

	using WriteToDataBindingFormatter = std::function<void(IOutputFormatterWithDataBinding&)>;
	std::shared_ptr<IEntityDocumentWithDataBinding> CreateEntityDocumentWithDataBinding(
		std::shared_ptr<MinimalBindingEngine> bindingEngine,
		WriteToDataBindingFormatter&& modelFn);
}


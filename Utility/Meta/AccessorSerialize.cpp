// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AccessorSerialize.h"
#include "ClassAccessors.h"
#include "ClassAccessorsImpl.h"

#include "../../OSServices/Log.h"
#include "../Streams/StreamFormatter.h"
#include "../Streams/OutputStreamFormatter.h"
#include "../Streams/FormatterUtils.h"
#include "../StringFormat.h"
#include "../ParameterBox.h"
#include "../MemoryUtils.h"
#include "../Conversion.h"

#include <iostream>

// #define SUPPORT_POLYMORPHIC_EXTENSIONS

namespace Utility
{
    template<typename Formatter>
        void AccessorDeserialize(
            Formatter& formatter,
            void* obj, const ClassAccessors& props)
    {
        for (;;) {
            switch (formatter.PeekNext()) {
            case FormatterBlob::KeyedItem:
                {
                    auto name = RequireKeyedItem(formatter);

                    if (formatter.PeekNext() == FormatterBlob::Value) {
                        auto value = RequireStringValue(formatter);
                        if (!props.SetFromString(obj, name, value)) {
                            std::cout << "Failure while assigning property during deserialization -- " << name << std::endl;
                        }
                    } else if (formatter.PeekNext() == FormatterBlob::BeginElement) {
#if SUPPORT_POLYMORPHIC_EXTENSIONS
                        auto* legacyExtensions = dynamic_cast<const Legacy::ClassAccessorsWithChildLists*>(&props)
                        if (legacyExtensions) {
                            formatter.BeginElement();

                            auto created = legacyExtensions->TryCreateChild(obj, Hash64(name._start, name._end));
                            if (created.first) {
                                AccessorDeserialize(formatter, created.first, *created.second);
                            } else {
                                std::cout << "Couldn't find a match for element name during deserialization -- " << name << std::endl;
                                SkipElement(formatter);
                            }

                            formatter.EndElement();
                        } else 
#endif
                        {
                            Throw(FormatException("Children elements not supported for this type", formatter.GetLocation()));
                        }
                    } else {
                        Throw(FormatException("Expecting either a value or an element", formatter.GetLocation()));
                    }
                }
                break;
                    
            case FormatterBlob::Value:
            case FormatterBlob::BeginElement:
            case FormatterBlob::CharacterData:
                assert(0);
                break;

            case FormatterBlob::EndElement:
            case FormatterBlob::None:
                return;
            }
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void AccessorSerialize(
        OutputStreamFormatter& formatter,
        const void* obj, const ClassAccessors& accessors)
    {
        for (const auto&p:accessors.GetProperties()) {
            auto str = accessors.GetAsString(obj, p.first);
            if (!str.has_value()) continue;

            auto v = str.value();
            formatter.WriteKeyedValue(
                MakeStringSection(p.second._name),
                MakeStringSection(str.value()));
        }

#if SUPPORT_POLYMORPHIC_EXTENSIONS
        auto* legacyExtensions = dynamic_cast<const Legacy::ClassAccessorsWithChildLists*>(&accessors)
        if (legacyExtensions) {
            for (size_t i=0; i<legacyExtensions->GetChildListCount(); ++i) {
                const auto& childList = legacyExtensions->GetChildListByIndex(i);
                auto count = childList._getCount(obj);
                for (size_t e=0; e<count; ++e) {
                    const auto* child = childList._getByIndex(obj, e);
                    auto eleId = formatter.BeginElement(childList._name);
                    AccessorSerialize(formatter, child, *childList._childProps);
                    formatter.EndElement(eleId);
                }
            }
        }
#endif
    }

    template
        void AccessorDeserialize(
            InputStreamFormatter<utf8>& formatter,
            void* obj, const ClassAccessors& props);

///////////////////////////////////////////////////////////////////////////////////////////////////

    void SetParameters(
        void* obj, const ClassAccessors& accessors,
        const ParameterBox& paramBox)
    {
        // we can choose to iterate through the parameters in either way:
        // either by iterating through the accessors in "accessors" and pulling
        // values from the parameter box...
        // or by iterating through the parameters in "paramBox" and pushing those
        // values in.
        // We have to consider array cases -- perhaps it easier to go through the
        // parameters in the parameter box
        for (const auto&i:paramBox) {
            auto name = i.Name();
            accessors.Set(
                obj,
                name, i.RawValue(), 
                i.Type());
        }
    }

}

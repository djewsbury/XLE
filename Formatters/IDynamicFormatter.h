// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/StringFormat.h"

namespace Assets { class DependencyValidation; }
namespace Utility { namespace ImpliedTyping { class TypeDesc; }}

namespace Formatters
{
    class IDynamicFormatter
    {
    public:
        using InteriorSection = StringSection<>;
        virtual FormatterBlob PeekNext() = 0;

        virtual bool TryBeginElement() = 0;
		virtual bool TryEndElement() = 0;
		virtual bool TryKeyedItem(StringSection<>& name) = 0;

        //
        // Different underlying formatters work with values in different ways. In order for each
        // formatter type to work most efficiently with it's prefered interface style, we have to
        // introduce a few different variations of the value getter.
        // TryStringValue variations
        //      TryStringValue()        --> if the underlying type is a string, returns that directly (no copies or conversions)
        //      TryRawValue()           --> returns the underlying type and data for the underlying type (no copies or conversions)
        //      TryCastValue()          --> attempts to cast the underlying value to the type given and into the destination buffer given (copy and maybe conversion required)
        //
		virtual bool TryStringValue(StringSection<>& value) = 0;
        virtual bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& type) = 0;
        virtual bool TryCastValue(IteratorRange<void*> destinationBuffer, const ImpliedTyping::TypeDesc& type) = 0;

        virtual void SkipValueOrElement() = 0;

        virtual StreamLocation GetLocation() const = 0;
        virtual ::Assets::DependencyValidation GetDependencyValidation() const = 0;

        virtual ~IDynamicFormatter() = default;
    };
}
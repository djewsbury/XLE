// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/ConfigFileContainer.h"
#include "../../Utility/Streams/StreamTypes.h"

namespace EntityInterface
{
    class IDynamicFormatter;
    class IEntityDocument;

    std::shared_ptr<IDynamicFormatter> CreateDynamicFormatter(
        std::shared_ptr<::Assets::ConfigFileContainer<>> cfgFile,
        StringSection<> internalSection);

    std::shared_ptr<IDynamicFormatter> CreateDynamicFormatter(
        MemoryOutputStream<>&& formatter,
        ::Assets::DependencyValidation&& depVal);

    std::shared_ptr<IEntityDocument> CreateTextEntityDocument(StringSection<> filename);
}

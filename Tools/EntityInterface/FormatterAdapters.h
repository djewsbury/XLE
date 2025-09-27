// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/ConfigFileContainer.h"
#include <iosfwd>

namespace Formatters { class IDynamicInputFormatter; }
namespace EntityInterface
{
    class IEntityDocument;

    std::shared_ptr<Formatters::IDynamicInputFormatter> CreateDynamicFormatter(
        std::shared_ptr<::Assets::ConfigFileContainer<>> cfgFile,
        StringSection<> internalSection);

    std::shared_ptr<Formatters::IDynamicInputFormatter> CreateDynamicFormatter(
        std::stringstream&&,
        ::Assets::DependencyValidation&& depVal);

    std::shared_ptr<IEntityDocument> CreateTextEntityDocument(StringSection<> filename);
}

// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Core/Types.h"

namespace Utility { class OutputStreamFormatter; }

namespace EntityInterface
{
    class RetainedEntities;

    void ExportGameObjects(
        Utility::OutputStreamFormatter& formatter,
        const RetainedEntities& flexGobInterface);
}


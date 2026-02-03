// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/StringUtils.h"
#include <cstdint>
#include <future>

namespace EntityInterface { class IEntityMountingTree; using DocumentId = uint64_t; }
namespace Formatters { class IDynamicInputFormatter; }

namespace ToolsRig
{
    class IPreviewSceneRegistry;
    class Services
    {
    public:
        static EntityInterface::IEntityMountingTree& GetEntityMountingTree();
        static bool HasEntityMountingTree();
        static IPreviewSceneRegistry& GetPreviewSceneRegistry();
    };

    EntityInterface::DocumentId MountTextEntityDocument(StringSection<> mntPoint, StringSection<> srcFile);
    void UnmountEntityDocument(EntityInterface::DocumentId);

    std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginMountedFormatter(StringSection<> mntPoint);
}


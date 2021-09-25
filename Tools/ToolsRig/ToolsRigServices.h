// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

namespace EntityInterface { class IEntityMountingTree; }

namespace ToolsRig
{
    class IPreviewSceneRegistry;
    class Services
    {
    public:
        static EntityInterface::IEntityMountingTree& GetEntityMountingTree();
        static IPreviewSceneRegistry& GetPreviewSceneRegistry();
    };
}


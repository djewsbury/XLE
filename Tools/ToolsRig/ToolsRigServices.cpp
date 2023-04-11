// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ToolsRigServices.h"
#include "PreviewSceneRegistry.h"
#include "../EntityInterface/EntityInterface.h"
#include "../EntityInterface/FormatterAdapters.h"
#include "../../ConsoleRig/AttachablePtr.h"

namespace ToolsRig
{
	static ConsoleRig::WeakAttachablePtr<EntityInterface::IEntityMountingTree> s_entityMountingTree;
    static ConsoleRig::WeakAttachablePtr<IPreviewSceneRegistry> s_previewSceneRegistry;

	IPreviewSceneRegistry& Services::GetPreviewSceneRegistry()
	{
		return *s_previewSceneRegistry.lock();
	}

	EntityInterface::IEntityMountingTree& Services::GetEntityMountingTree()
	{
		return *s_entityMountingTree.lock();
	}

	bool Services::HasEntityMountingTree()
	{
		return !s_entityMountingTree.expired();
	}

	EntityInterface::DocumentId MountTextEntityDocument(Utility::StringSection<> mntPoint, Utility::StringSection<> srcFile)
	{
		return Services::GetEntityMountingTree().MountDocument(mntPoint, EntityInterface::CreateTextEntityDocument(srcFile));
	}

    void UnmountEntityDocument(EntityInterface::DocumentId docId)
	{
		Services::GetEntityMountingTree().UnmountDocument(docId);
	}
}


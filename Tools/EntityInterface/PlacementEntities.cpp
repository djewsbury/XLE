// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementEntities.h"
#include "RetainedEntities.h"
#include "../../SceneEngine/PlacementsManager.h"
#include "../../SceneEngine/DynamicImposters.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Meta/AccessorSerialize.h"
#include "../../Math/Transformations.h"

namespace EntityInterface
{
    static const DocumentTypeId DocumentType_Placements = 1;
    static const EntityTypeId ObjectType_Placement = 1;
    static const PropertyId Property_LocalToWorld = 100;
    static const PropertyId Property_Visible = 101;
    static const PropertyId Property_Model = 102;
    static const PropertyId Property_Material = 103;
    static const PropertyId Property_Supplements = 104;
    static const PropertyId Property_Bounds = 105;
    static const PropertyId Property_LocalBounds = 106;


    DocumentId PlacementEntities::CreateDocument(DocumentTypeId docType, const char initializer[])
    {
        if (docType != DocumentType_Placements) { assert(0); return 0; }

        StringMeld<MaxPath, ::Assets::ResChar> meld;
        meld << "[dyn] " << initializer << (_cellCounter++);

            // todo -- boundary of this cell should be set to something reasonable
            //          (or at least adapt as objects are added and removed)
        auto result = (DocumentId)_editor->CreateCell(meld, Float2(-100000.f, -100000.f), Float2(100000.f, 100000.f));
		auto hiddenResult = _hiddenObjects->CreateCell(meld, Float2(-100000.f, -100000.f), Float2(100000.f, 100000.f));
		(void)hiddenResult;
		assert(result == hiddenResult);	// ids must match up
		return result;
	}

	bool PlacementEntities::DeleteDocument(DocumentId doc, DocumentTypeId docType)
	{
		if (docType != DocumentType_Placements) { assert(0); return false; }
		bool result = _editor->RemoveCell(doc);
		result |= _hiddenObjects->RemoveCell(doc);
		return result;
	}

	EntityId PlacementEntities::AssignObjectId(DocumentId doc, EntityTypeId type) const
	{
		if (type != ObjectType_Placement) { assert(0); return 0; }
		return _editor->GenerateObjectGUID();
	}

	enum VisibilityChange { None, MakeVisible, MakeHidden };
	static VisibilityChange GetVisibilityChange(const PropertyInitializer initializers[], size_t initializerCount)
	{
		VisibilityChange visChange = None;
		for (unsigned c = 0; c < initializerCount; ++c) {
			if (initializers[c]._prop == Property_Visible && !initializers[c]._src.empty()) {
				assert(!initializers[c]._src.empty());
				bool flagValue = (*(const uint8*)initializers[c]._src.begin()) != 0;
				visChange = flagValue ? MakeVisible : MakeHidden;
			}
		}
		return visChange;
	}

	static bool SetObjProperty(
		SceneEngine::PlacementsEditor::ObjTransDef& obj,
		const PropertyInitializer& prop)
	{
		if (prop._prop == Property_LocalToWorld) {
			// note -- putting in a transpose here, because the level editor matrix
			//          math uses a transposed form
			if (prop._elementType == (unsigned)ImpliedTyping::TypeCat::Float && prop._arrayCount >= 16) {
				assert(prop._src.size() >= sizeof(Float4x4));
				obj._localToWorld = AsFloat3x4(Transpose(*(const Float4x4*)prop._src.begin()));
				return true;
			}
		}
		else if (prop._prop == Property_Model || prop._prop == Property_Material || prop._prop == Property_Supplements) {
			std::string value = {(const char*)prop._src.begin(), (const char*)prop._src.end()};
			if (prop._prop == Property_Model) {
				obj._model = value;
			}
			else if (prop._prop == Property_Supplements) {
				obj._supplements = value;
			}
			else {
				obj._material = value;
			}
			return true;
		}
		return false;
	}

	bool PlacementEntities::CreateObject(
		const Identifier& id,
		const PropertyInitializer initializers[], size_t initializerCount)
	{
		if (id.ObjectType() != ObjectType_Placement) { assert(0); return false; }

		SceneEngine::PlacementsEditor::ObjTransDef newObj;
		newObj._localToWorld = Identity<decltype(newObj._localToWorld)>();
		for (size_t c = 0; c < initializerCount; ++c)
			SetObjProperty(newObj, initializers[c]);

		auto visChange = GetVisibilityChange(initializers, initializerCount);

		auto guid = SceneEngine::PlacementGUID(id.Document(), id.Object());
		std::shared_ptr<SceneEngine::PlacementsEditor::ITransaction> transaction;
		if (visChange == MakeHidden) {
			transaction = _hiddenObjects->Transaction_Begin(nullptr, nullptr);
		} else
			transaction = _editor->Transaction_Begin(nullptr, nullptr);
		if (transaction->Create(guid, newObj)) {
			transaction->Commit();
			return true;
		}

		return false;
	}

	static std::shared_ptr<SceneEngine::PlacementsEditor::ITransaction> Begin(SceneEngine::PlacementsEditor& editor, SceneEngine::PlacementGUID guid)
	{
		auto result = editor.Transaction_Begin(
			&guid, &guid + 1,
			SceneEngine::PlacementsEditor::TransactionFlags::IgnoreIdTop32Bits);
		assert(result);
		return result;
	}

	static bool FoundExistingObject(const SceneEngine::PlacementsEditor::ITransaction& trans)
	{
		for (unsigned c=0; c<trans.GetObjectCount(); ++c)
			if (trans.GetObject(c)._transaction == SceneEngine::PlacementsEditor::ObjTransDef::Unchanged)
				return true;
		return false;
	}

    bool PlacementEntities::DeleteObject(const Identifier& id)
    {
        if (id.ObjectType() != ObjectType_Placement) { assert(0); return false; }

		bool result = false;
        auto guid = SceneEngine::PlacementGUID(id.Document(), id.Object());

			// delete from both the hidden and visible lists ---

        auto transaction = Begin(*_editor, guid);
        if (transaction->GetObjectCount()==1) {
            transaction->Delete(0);
            transaction->Commit();
            result |= true;
        }

		transaction = Begin(*_hiddenObjects, guid);
		if (transaction->GetObjectCount() == 1) {
			transaction->Delete(0);
			transaction->Commit();
			result |= true;
		}

        return result;
    }

    bool PlacementEntities::SetProperty(
        const Identifier& id,
        const PropertyInitializer initializers[], size_t initializerCount)
    {
            // find the object, and set the given property (as per the new value specified in the string)
            //  We need to create a transaction, make the change and then commit it back.
            //  If the transaction returns no results, then we must have got a bad object or document id.
        if (id.ObjectType() != ObjectType_Placement) { assert(0); return false; }

		auto guid = SceneEngine::PlacementGUID(id.Document(), id.Object());
		using TransType = SceneEngine::PlacementsEditor::ObjTransDef::TransactionType;

		bool pendingTransactionCommit = false;
		std::shared_ptr<SceneEngine::PlacementsEditor::ITransaction> mainTransaction;

			// First -- Look for changes to the "visible" flag.
			//			We may need to move the object from the list of hidden objects to the
			//			visible objects lists.
			//
			// We maintain two lists of objects -- one visible and one hidden
			// Objects will normally exist in either one or the other.
			// However, if we find that we have an object in both lists, then the
			// object in the visible list will always be considered authorative
			//
			// All of this transaction stuff is mostly thread safe and well
			// ordered. But playing around with separate hidden and visible object
			// lists is not!
		auto visChange = GetVisibilityChange(initializers, initializerCount); 
		if (visChange == MakeVisible) {
			// if the object is not already in the visible list, then we have to move
			// it's properties across from the hidden list (and destroy the version
			// in the hidden list)
			auto visibleTrans = Begin(*_editor, guid);
			if (!FoundExistingObject(*visibleTrans)) {
				auto hiddenTrans = Begin(*_hiddenObjects, guid);
				if (FoundExistingObject(*hiddenTrans)) {
					// Copy across, delete the hidden item, and then commit the result
					visibleTrans->SetObject(0, hiddenTrans->GetObject(0));
					hiddenTrans->Delete(0);
					hiddenTrans->Commit();
					pendingTransactionCommit = true;
				}
			}

			mainTransaction = std::move(visibleTrans);
		} else if (visChange == MakeHidden) {
			auto hiddenTrans = Begin(*_hiddenObjects, guid);
			if (hiddenTrans->GetObjectCount() > 0) {
				auto visibleTrans = Begin(*_editor, guid);
				if (FoundExistingObject(*visibleTrans)) {
					hiddenTrans->SetObject(0, visibleTrans->GetObject(0));
					visibleTrans->Delete(0);
					visibleTrans->Commit();
					pendingTransactionCommit = true;
				}
			}

			mainTransaction = std::move(hiddenTrans);
		} else {
			mainTransaction = Begin(*_editor, guid);
			if (!FoundExistingObject(*mainTransaction)) {
				// if we're threatening to create the object, let's first check to
				// see if a hidden object exists instead (and if so, switch to that 
				// transaction instead)
				auto hiddenTrans = Begin(*_hiddenObjects, guid);
				if (FoundExistingObject(*hiddenTrans))
					mainTransaction = std::move(hiddenTrans);
			}
		}

            // note --  This object search is quite slow! We might need a better way to
            //          record a handle to the object. Perhaps the "EntityId" should not
            //          match the actual placements guid. Some short-cut will probably be
            //          necessary given that we could get there several thousand times during
            //          startup for an average scene.
        
        if (mainTransaction && mainTransaction->GetObjectCount() > 0) {
            auto originalObject = mainTransaction->GetObject(0);
            for (size_t c=0; c<initializerCount; ++c)
                pendingTransactionCommit |= SetObjProperty(originalObject, initializers[c]);
            if (pendingTransactionCommit) {
				mainTransaction->SetObject(0, originalObject);
				mainTransaction->Commit();
                return true;
            }
        }

        return false;
    }

    bool PlacementEntities::GetProperty(
        const Identifier& id, PropertyId prop, 
        void* dest, unsigned* destSize) const
    {
        if (id.ObjectType() != ObjectType_Placement) { assert(0); return false; }
        if (prop != Property_LocalToWorld && prop != Property_Bounds && prop != Property_LocalBounds) { assert(0); return false; }
        assert(destSize);

        typedef std::pair<Float3, Float3> BoundingBox;

        auto guid = SceneEngine::PlacementGUID(id.Document(), id.Object());
        auto transaction = Begin(*_editor, guid);
        if (transaction && transaction->GetObjectCount()==1) {

			// if the object didn't previous exist in the visible list, then check the hidden list
			if (transaction->GetObject(0)._transaction == SceneEngine::PlacementsEditor::ObjTransDef::Error) {
				auto hiddenTrans = Begin(*_hiddenObjects, guid);
				if (hiddenTrans->GetObjectCount() == 0 || hiddenTrans->GetObject(0)._transaction != SceneEngine::PlacementsEditor::ObjTransDef::Error)
					transaction = hiddenTrans;
			}

            if (prop == Property_LocalToWorld) {
                if (*destSize >= sizeof(Float4x4)) {
                    auto originalObject = transaction->GetObject(0);
                        // note -- putting in a transpose here, because the level editor matrix
                        //          math uses a transposed form
                    *(Float4x4*)dest = Transpose(AsFloat4x4(originalObject._localToWorld));
                    return true;
                }
                *destSize = sizeof(Float4x4);
            } else if (prop == Property_Bounds) {
                if (*destSize >= sizeof(BoundingBox)) {
                    *(BoundingBox*)dest = transaction->GetWorldBoundingBox(0);
                    return true;
                }
                *destSize = sizeof(BoundingBox);
            } else if (prop == Property_LocalBounds) {
                if (*destSize >= sizeof(BoundingBox)) {
                    *(BoundingBox*)dest = transaction->GetLocalBoundingBox(0);
                    return true;
                }
                *destSize = sizeof(BoundingBox);
			}
        }

        return false;
    }

    bool PlacementEntities::SetParent(const Identifier& child, const Identifier& parent, ChildListId childList, int insertionPosition)
    {
        return false;
    }

    EntityTypeId PlacementEntities::GetTypeId(const char name[]) const
    {
        if (!XlCompareString(name, "PlacementObject")) return ObjectType_Placement;
        return 0;
    }

    DocumentTypeId PlacementEntities::GetDocumentTypeId(const char name[]) const
    {
        if (!XlCompareString(name, "PlacementsDocument")) return DocumentType_Placements;
        return 0;
    }

    PropertyId PlacementEntities::GetPropertyId(EntityTypeId type, const char name[]) const
    {
        if (!XlCompareString(name, "LocalToWorld"))     return Property_LocalToWorld;
        if (!XlCompareString(name, "VisibleHierarchy")) return Property_Visible;
        if (!XlCompareString(name, "model"))            return Property_Model;
        if (!XlCompareString(name, "material"))         return Property_Material;
        if (!XlCompareString(name, "supplements"))      return Property_Supplements;
        if (!XlCompareString(name, "Bounds"))           return Property_Bounds;
        if (!XlCompareString(name, "LocalBounds"))      return Property_LocalBounds;
        return 0;
    }

    ChildListId PlacementEntities::GetChildListId(EntityTypeId type, const char name[]) const
    {
        return 0;
    }

	void PlacementEntities::PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const
	{
		assert(0);
		stream << "PlacementEntities document (printing not supported)" << std::endl;
	}

    PlacementEntities::PlacementEntities(
        std::shared_ptr<SceneEngine::PlacementsManager> manager,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
		std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects)
    : _manager(std::move(manager))
    , _editor(std::move(editor))
	, _hiddenObjects(std::move(hiddenObjects))
    , _cellCounter(0) 
    {}

    PlacementEntities::~PlacementEntities() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void RegisterDynamicImpostersFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::DynamicImposters> imposters)
    {
        std::weak_ptr<SceneEngine::DynamicImposters> weakPtrToManager = imposters;
        flexSys.RegisterCallback(
            flexSys.GetTypeId("DynamicImpostersConfig"),
            [weakPtrToManager](
                const RetainedEntities& flexSys, const Identifier& obj,
                RetainedEntities::ChangeType changeType)
            {
                auto mgr = weakPtrToManager.lock();
                if (!mgr) return;

                if (changeType == RetainedEntities::ChangeType::Delete) {
                    mgr->Disable();
                    return;
                }

                auto* object = flexSys.GetEntity(obj);
                if (object)
                    mgr->Load(
                        CreateFromParameters<SceneEngine::DynamicImposters::Config>(object->_properties));
            }
        );
    }

}

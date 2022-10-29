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
#include "../../Math/Matrix.h"
#include "../../Math/Vector.h"
#include "../../Math/MathSerialization.h"

namespace EntityInterface
{
    static const uint64_t Property_LocalToWorld = Hash64("LocalToWorld");
    static const uint64_t Property_Visible = Hash64("VisibleHierarchy");
    static const uint64_t Property_Model = Hash64("model");
    static const uint64_t Property_Material = Hash64("material");
    static const uint64_t Property_Supplements = Hash64("supplements");
    static const uint64_t Property_Bounds = Hash64("Bounds");
    static const uint64_t Property_LocalBounds = Hash64("LocalBounds");

	enum VisibilityChange { None, MakeVisible, MakeHidden };
	static VisibilityChange GetVisibilityChange(IteratorRange<const PropertyInitializer*> initializers)
	{
		VisibilityChange visChange = None;
		for (unsigned c = 0; c < initializers.size(); ++c) {
			if (initializers[c]._prop.second == Property_Visible && !initializers[c]._data.empty()) {
				assert(!initializers[c]._data.empty());
				bool flagValue = (*(const uint8*)initializers[c]._data.begin()) != 0;
				visChange = flagValue ? MakeVisible : MakeHidden;
			}
		}
		return visChange;
	}

	static bool SetObjProperty(
		SceneEngine::PlacementsEditor::ObjTransDef& obj,
		const PropertyInitializer& prop)
	{
		if (prop._prop.second == Property_LocalToWorld) {
			if (prop._type._type == ImpliedTyping::TypeCat::Float && prop._type._arrayCount >= 16) {
				assert(prop._data.size() >= sizeof(Float4x4));
				obj._localToWorld = AsFloat3x4(*(const Float4x4*)prop._data.begin());
				return true;
			}
		}
		else if (prop._prop.second == Property_Model || prop._prop.second == Property_Material || prop._prop.second == Property_Supplements) {
			std::string value = {(const char*)prop._data.begin(), (const char*)prop._data.end()};
			if (prop._prop.second == Property_Model) {
				obj._model = value;
			}
			else if (prop._prop.second == Property_Supplements) {
				obj._supplements = value;
			}
			else {
				obj._material = value;
			}
			return true;
		}
		return false;
	}

	EntityId PlacementEntities::AssignEntityId()
	{
		return _editor->GenerateObjectGUID();
	}

	bool PlacementEntities::CreateEntity(
		StringAndHash typeId,
		EntityId entityId,
		IteratorRange<const PropertyInitializer*> initializers)
	{
		SceneEngine::PlacementsEditor::ObjTransDef newObj;
		newObj._localToWorld = Identity<decltype(newObj._localToWorld)>();
		for (size_t c = 0; c < initializers.size(); ++c)
			SetObjProperty(newObj, initializers[c]);

		auto visChange = GetVisibilityChange(initializers);

		auto guid = SceneEngine::PlacementGUID(_cellId, entityId);
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

    bool PlacementEntities::DeleteEntity(EntityId id)
    {
		bool result = false;
        auto guid = SceneEngine::PlacementGUID(_cellId, id);

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
        EntityId id,
        IteratorRange<const PropertyInitializer*> initializers)
    {
            // find the object, and set the given property (as per the new value specified in the string)
            //  We need to create a transaction, make the change and then commit it back.
            //  If the transaction returns no results, then we must have got a bad object or document id.
		auto guid = SceneEngine::PlacementGUID(_cellId, id);

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
		auto visChange = GetVisibilityChange(initializers); 
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
            for (size_t c=0; c<initializers.size(); ++c)
                pendingTransactionCommit |= SetObjProperty(originalObject, initializers[c]);
            if (pendingTransactionCommit) {
				mainTransaction->SetObject(0, originalObject);
				mainTransaction->Commit();
                return true;
            }
        }

        return false;
    }

    std::optional<ImpliedTyping::TypeDesc> PlacementEntities::GetProperty(
        EntityId entityId, StringAndHash prop,
        IteratorRange<void*> destinationBuffer) const
    {
        if (prop.second != Property_LocalToWorld && prop.second != Property_Bounds && prop.second != Property_LocalBounds) { assert(0); return {}; }
        assert(!destinationBuffer.empty());

        using BoundingBox = std::pair<Float3, Float3>;

        auto guid = SceneEngine::PlacementGUID(_cellId, entityId);
        auto transaction = Begin(*_editor, guid);
        if (transaction && transaction->GetObjectCount()==1) {

			// if the object didn't previous exist in the visible list, then check the hidden list
			if (transaction->GetObject(0)._transaction == SceneEngine::PlacementsEditor::ObjTransDef::Error) {
				auto hiddenTrans = Begin(*_hiddenObjects, guid);
				if (hiddenTrans->GetObjectCount() == 0 || hiddenTrans->GetObject(0)._transaction != SceneEngine::PlacementsEditor::ObjTransDef::Error)
					transaction = hiddenTrans;
			}

            if (prop.second == Property_LocalToWorld) {
                if (destinationBuffer.size() >= sizeof(Float4x4)) {
                    auto originalObject = transaction->GetObject(0);
                    *(Float4x4*)destinationBuffer.begin() = AsFloat4x4(originalObject._localToWorld);
                } else {
					std::memset(destinationBuffer.begin(), 0, destinationBuffer.size());
				}
                return ImpliedTyping::TypeOf<Float4x4>();
            } else if (prop.second == Property_Bounds) {
                if (destinationBuffer.size() >= sizeof(BoundingBox)) {
                    *(BoundingBox*)destinationBuffer.begin() = transaction->GetWorldBoundingBox(0);
                } else {
					std::memset(destinationBuffer.begin(), 0, destinationBuffer.size());
				}
                return ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Float, 6};
            } else if (prop.second == Property_LocalBounds) {
                if (destinationBuffer.size() >= sizeof(BoundingBox)) {
                    *(BoundingBox*)destinationBuffer.begin() = transaction->GetLocalBoundingBox(0);
                } else {
					std::memset(destinationBuffer.begin(), 0, destinationBuffer.size());
				}
                return ImpliedTyping::TypeDesc{ImpliedTyping::TypeCat::Float, 6};
			}
        }

        return {};
    }

    bool PlacementEntities::SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition)
    {
        return false;
    }

	void PlacementEntities::PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const
	{
		assert(0);
		stream << "PlacementEntities document (printing not supported)" << std::endl;
	}

	std::pair<uint64_t, uint64_t> PlacementEntities::QueryHighlightableId(EntityId entityId)
	{
		// Somewhat awkwardly, we have to call out to the placements system to "fixup" the entity reference here
		// we don't store the entire id in the entityId value, but we need the remaining part in order to
		// construct a "highlightable" id that works with the placements filtering stuff
		std::pair<uint64_t, uint64_t> result { _cellId, entityId };
		_editor->PerformGUIDFixup(&result, &result+1);
		return result;
	}

    PlacementEntities::PlacementEntities(
        std::shared_ptr<SceneEngine::PlacementsManager> manager,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
		std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects,
		StringSection<> initializer,
		uint64_t documentId)
    : _manager(std::move(manager))
    , _editor(std::move(editor))
	, _hiddenObjects(std::move(hiddenObjects))
	, _cellId(documentId)
    {
            // todo -- boundary of this cell should be set to something reasonable
            //          (or at least adapt as objects are added and removed)
        _editor->CreateCell(_cellId, Float2(-100000.f, -100000.f), Float2(100000.f, 100000.f));
		_hiddenObjects->CreateCell(_cellId, Float2(-100000.f, -100000.f), Float2(100000.f, 100000.f));
	}

    PlacementEntities::~PlacementEntities()
	{
		bool result = _editor->RemoveCell(_cellId);
		result |= _hiddenObjects->RemoveCell(_cellId);
		assert(result);
	}

	class PlacementEntitiesType : public Switch::IDocumentType
	{
	public:
		std::shared_ptr<IMutableEntityDocument> CreateDocument(StringSection<> initializer, DocumentId docId)
		{
			return std::make_shared<PlacementEntities>(_manager, _editor, _hiddenObjects, initializer, docId);
		}

		PlacementEntitiesType(
            std::shared_ptr<SceneEngine::PlacementsManager> manager,
            std::shared_ptr<SceneEngine::PlacementsEditor> editor,
			std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects)
		: _manager(std::move(manager))
		, _editor(std::move(editor))
		, _hiddenObjects(std::move(hiddenObjects))
		, _cellCounter(_cellCounter) {}
		~PlacementEntitiesType() {}

	protected:
        std::shared_ptr<SceneEngine::PlacementsManager> _manager;
        std::shared_ptr<SceneEngine::PlacementsEditor> _editor;
		std::shared_ptr<SceneEngine::PlacementsEditor> _hiddenObjects;
		unsigned _cellCounter;
	};

	std::shared_ptr<Switch::IDocumentType> CreatePlacementEntitiesSwitch(
        std::shared_ptr<SceneEngine::PlacementsManager> manager,
        std::shared_ptr<SceneEngine::PlacementsEditor> editor,
        std::shared_ptr<SceneEngine::PlacementsEditor> hiddenObjects)
	{
		return std::make_shared<PlacementEntitiesType>(std::move(manager), std::move(editor), std::move(hiddenObjects));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void RegisterDynamicImpostersFlexObjects(
        RetainedEntities& flexSys, 
        std::shared_ptr<SceneEngine::DynamicImposters> imposters)
    {
        std::weak_ptr<SceneEngine::DynamicImposters> weakPtrToManager = imposters;
        flexSys.RegisterCallback(
            Hash64("DynamicImpostersConfig"),
            [weakPtrToManager](
                const RetainedEntities& flexSys, EntityId obj,
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

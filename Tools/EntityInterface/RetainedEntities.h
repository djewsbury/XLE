// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "../../Utility/ParameterBox.h"
#include <string>
#include <vector>
#include <functional>
#include <iosfwd>

namespace Utility { template<typename Type> class InputStreamFormatter; }

namespace EntityInterface
{
    class RetainedEntity
    {
    public:
        ObjectId _id = 0;
        DocumentId _doc = 0;
        ObjectTypeId _type = 0;

        ParameterBox _properties;
        std::vector<std::pair<ChildListId, ObjectId>> _children;
        ObjectId _parent = 0;
    };

    /// <summary>Stores entity data generically</summary>
    /// This implemention simply stores all information that comes from IObjectType
    /// in a generic data structure.
    ///
    /// Clients can put callbacks on specific object types to watch for changes.
    /// This can make it easier to implement lightweight object types. Instead of
    /// having to implement the IEntityInterface, simply set a callback with
    /// RegisterCallback().
    ///
    /// All of the properties and data related to that object will be available in
    /// the callback.
    class RetainedEntities
    {
    public:
        const RetainedEntity* GetEntity(DocumentId doc, ObjectId obj) const;
        const RetainedEntity* GetEntity(const Identifier&) const;
        std::vector<const RetainedEntity*> FindEntitiesOfType(ObjectTypeId typeId) const;

        enum class ChangeType 
        {
            SetProperty, Create, Delete,
            
            // SetParent, AddChild and RemoveChild are all invoked after the change
            // takes place (so, in the callback, the parents and children will be in
            // the new configuration). This means that the callback does not have
            // access to the old parent pointer in SetParent.
            // For a single SetParent operation, the order of callbacks is always:
            //      RemoveChild, SetParent, AddChild
            // (though, obviously, some callbacks will be skipped if there was no
            // previous parent, or no new parent)
            SetParent, AddChild, RemoveChild, 

            // The following occur when there have been changes lower in
            // the hierachy:
            //      ChildSetProperty -- some object in our subtree has a property change
            //      ChangeHierachy --   an object was added or removed somewhere in our
            //                          subtree (not including immediate children)
            ChildSetProperty, ChangeHierachy
        };

        using OnChangeDelegate = 
            std::function<
                void(const RetainedEntities& flexSys, const Identifier&, ChangeType)
            >;
        unsigned RegisterCallback(ObjectTypeId typeId, OnChangeDelegate&& onChange);
        void DeregisterCallback(unsigned callbackId);

        ObjectTypeId    GetTypeId(const char name[]) const;
		PropertyId      GetPropertyId(ObjectTypeId typeId, const char name[]) const;
		ChildListId     GetChildListId(ObjectTypeId typeId, const char name[]) const;

        std::string		GetTypeName(ObjectTypeId id) const;
		std::string		GetPropertyName(ObjectTypeId typeId, PropertyId id) const;
		std::string		GetChildListName(ObjectTypeId typeId, ChildListId id) const;

		void			PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const;

		class ChildConstIterator
		{
		public:
			bool operator==(const ChildConstIterator&);
			bool operator!=(const ChildConstIterator&);
			void operator++();
			void operator--();
			friend bool operator<(const ChildConstIterator& lhs, const ChildConstIterator& rhs);
			friend ChildConstIterator operator+(const ChildConstIterator& lhs, ptrdiff_t advance);

			using difference_type = size_t;
			using value_type = RetainedEntity;
			using pointer = const RetainedEntity*;
			using reference = const RetainedEntity&;
			using iterator_category = std::bidirectional_iterator_tag;

			reference operator*() const;
			reference operator->() const;
			reference operator[](size_t idx) const;

			using UnderlyingIterator = std::vector<std::pair<ChildListId, ObjectId>>::const_iterator;

			ChildConstIterator(
				const RetainedEntities& entitySystem,
				const RetainedEntity& parent, UnderlyingIterator i, ChildListId childList);
			ChildConstIterator();
			ChildConstIterator(nullptr_t);
		protected:
			const RetainedEntities* _entitySystem; 
			const RetainedEntity* _parentObject;
			ChildListId _childListId;
			ptrdiff_t _childIdx;
		};

		IteratorRange<ChildConstIterator> GetChildren(DocumentId doc, ObjectId parentObj, ChildListId childList) const;
		IteratorRange<ChildConstIterator> GetChildren(const RetainedEntity& parent, ChildListId childList) const;

        RetainedEntities();
        ~RetainedEntities();
    protected:
        mutable ObjectId _nextObjectId;
        mutable std::vector<RetainedEntity> _objects;

        class RegisteredObjectType
        {
        public:
            std::string _name;
            std::vector<std::string> _properties;
            std::vector<std::string> _childLists;

            std::vector<std::pair<unsigned, OnChangeDelegate>> _onChange;

            RegisteredObjectType(const std::string& name) : _name(name) {}
        };
        mutable std::vector<std::pair<ObjectTypeId, RegisteredObjectType>> _registeredObjectTypes;

        mutable ObjectTypeId _nextObjectTypeId;
        unsigned _nextCallbackId;

        RegisteredObjectType* GetObjectType(ObjectTypeId id) const;
        void InvokeOnChange(RegisteredObjectType& type, RetainedEntity& obj, ChangeType changeType) const;
        RetainedEntity* GetEntityInt(DocumentId doc, ObjectId obj) const;
        bool SetSingleProperties(RetainedEntity& dest, const RegisteredObjectType& type, const PropertyInitializer& initializer) const;
		void PrintEntity(std::ostream& stream, const RetainedEntity& entity, StringSection<> childListName, unsigned indent) const;

        friend class RetainedEntityInterface;
    };

    /// <summary>Implements IMutableEntityDocument for retained entities</summary>
    /// This implementation will simply accept all incoming data, and store
    /// it in a generic data structure.
    class RetainedEntityInterface : public IMutableEntityDocument
    {
    public:
        DocumentId CreateDocument(DocumentTypeId docType, const char initializer[]);
		bool DeleteDocument(DocumentId doc, DocumentTypeId docType);

		ObjectId AssignObjectId(DocumentId doc, ObjectTypeId type) const;
		bool CreateObject(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount);
		bool DeleteObject(const Identifier& id);
		bool SetProperty(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount);
		bool GetProperty(const Identifier& id, PropertyId prop, void* dest, unsigned* destSize) const;
        bool SetParent(const Identifier& child, const Identifier& parent, ChildListId childList, int insertionPosition);

		ObjectTypeId    GetTypeId(const char name[]) const;
		DocumentTypeId  GetDocumentTypeId(const char name[]) const;
		PropertyId      GetPropertyId(ObjectTypeId typeId, const char name[]) const;
		ChildListId     GetChildListId(ObjectTypeId typeId, const char name[]) const;

		void			PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const;

		RetainedEntityInterface(std::shared_ptr<RetainedEntities> scene);
		~RetainedEntityInterface();
    protected:
        std::shared_ptr<RetainedEntities> _scene;
    };

    void Deserialize(
        Utility::InputStreamFormatter<utf8>& formatter,
        IEntityInterface& interf, DocumentTypeId docType);
}




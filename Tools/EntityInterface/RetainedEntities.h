// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EntityInterface.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/FunctionUtils.h"
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
        EntityId _id = 0;
        uint64_t _typeNameHash = 0;

        ParameterBox _properties;
        std::vector<std::pair<uint64_t, EntityId>> _children;
        EntityId _parent = 0;
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
        const RetainedEntity* GetEntity(EntityId) const;

        using TypeNameHash = uint64_t;
        using ChildListNameHash = uint64_t;
        std::vector<const RetainedEntity*> FindEntitiesOfType(TypeNameHash typeId) const;

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
                void(const RetainedEntities& flexSys, EntityId, ChangeType)
            >;
        unsigned RegisterCallback(TypeNameHash typeId, OnChangeDelegate&& onChange);
        void DeregisterCallback(unsigned callbackId);

		void			PrintDocument(std::ostream& stream, unsigned indent) const;

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

			using UnderlyingIterator = std::vector<std::pair<ChildListNameHash, EntityId>>::const_iterator;

			ChildConstIterator(
				const RetainedEntities& entitySystem,
				const RetainedEntity& parent, UnderlyingIterator i, ChildListNameHash childList);
			ChildConstIterator();
			ChildConstIterator(nullptr_t);
		protected:
			const RetainedEntities* _entitySystem; 
			const RetainedEntity* _parentObject;
			ChildListNameHash _childListId;
			ptrdiff_t _childIdx;
		};

		IteratorRange<ChildConstIterator> GetChildren(EntityId parentObj, ChildListNameHash childList) const;
		IteratorRange<ChildConstIterator> GetChildren(const RetainedEntity& parent, ChildListNameHash childList) const;

        RetainedEntities();
        ~RetainedEntities();
    protected:
        mutable EntityId _nextEntityId;
        mutable std::vector<RetainedEntity> _objects;

        class RegisteredObjectType
        {
        public:
            std::string _name;
            std::vector<std::pair<unsigned, OnChangeDelegate>> _onChange;
            RegisteredObjectType(const std::string& name) : _name(name) {}
        };
        mutable std::vector<std::pair<TypeNameHash, RegisteredObjectType>> _registeredObjectTypes;

        unsigned _nextCallbackId;

        RegisteredObjectType* GetObjectType(TypeNameHash id) const;
        RegisteredObjectType* GetObjectType(StringAndHash id) const;
        void InvokeOnChange(RegisteredObjectType& type, RetainedEntity& obj, ChangeType changeType) const;
        bool SetSingleProperties(RetainedEntity& dest, const PropertyInitializer& initializer) const;
		void PrintEntity(std::ostream& stream, const RetainedEntity& entity, StringSection<> childListName, unsigned indent) const;
        RetainedEntity* GetEntityWriteable(EntityId);

        friend class RetainedEntitiesAdapter;
    };

    /// <summary>Implements IMutableEntityDocument for retained entities</summary>
    /// This implementation will simply accept all incoming data, and store
    /// it in a generic data structure.
    class RetainedEntitiesAdapter : public IMutableEntityDocument
    {
    public:
        EntityId AssignEntityId() override;
		bool CreateEntity(StringAndHash, EntityId, IteratorRange<const PropertyInitializer*>) override;
		bool DeleteEntity(EntityId) override;
		bool SetProperty(EntityId, IteratorRange<const PropertyInitializer*>) override;
		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override;
        bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override;

		RetainedEntitiesAdapter(std::shared_ptr<RetainedEntities> scene);
		~RetainedEntitiesAdapter();
    protected:
        std::shared_ptr<RetainedEntities> _scene;
    };

    void Deserialize(
        Utility::InputStreamFormatter<utf8>& formatter,
        IMutableEntityDocument& interf);
}




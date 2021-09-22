// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Core/Types.h"
#include <memory>
#include <vector>
#include <string>

namespace Assets { class DependencyValidation; }

namespace EntityInterface
{
    using ObjectTypeId = uint32;
    using DocumentTypeId = uint32;
    using PropertyId = uint32;
    using ChildListId = uint32;

    using DocumentId = uint64;
    using ObjectId = uint64;

    class IDynamicFormatter
    {
    public:
        virtual FormatterBlob PeekNext() = 0;

        virtual bool TryBeginElement() = 0;
		virtual bool TryEndElement() = 0;
		virtual bool TryKeyedItem(StringSection<>& name) = 0;
		virtual bool TryValue(StringSection<>& value) = 0;
		virtual bool TryCharacterData(StringSection<>&) = 0;

        virtual StreamLocation GetLocation() const = 0;

        virtual ~IDynamicFormatter() = default;
    };

    class PropertyInitializer
    {
    public:
        StringSection<> _name;
        IteratorRange<const void*> _src;
        ImpliedTyping::TypeDesc _type;
    };

    class IMutableEntity
    {
    public:
        virtual bool SetProperties(IteratorRange<const PropertyInitializer*> initializers) = 0;
        virtual ~IMutableEntity() = default;
    };

    class IEntityDocument
    {
    public:
        virtual std::shared_ptr<IDynamicFormatter> BeginFormatter();
        virtual ::Assets::DependencyValidation GetDependencyValidation() const;

        virtual ~IEntityDocument() = default;
    };

    class IEntityMountingTree
    {
    public:
        virtual DocumentId AttachDocument(
            StringSection<> mountPount,
            std::shared_ptr<IEntityDocument>) = 0;
        virtual bool DetachDocument(DocumentId doc, DocumentTypeId docType) = 0;

        // todo -- how should this work with sequences? What if we want to add/remove
        // modify objects within a sequence -- is that just not possible?
        virtual std::shared_ptr<IMutableEntity> CreateMutableEntity(StringSection<> mountPount) = 0;
        virtual void DetachMutations(const IMutableEntity&) = 0;

        // returns a dependency validation that advances if any properties at that mount point,
        // (or underneath) change 
        virtual ::Assets::DependencyValidation GetDependencyValidation(StringSection<> mountPount) const;

        virtual ~IEntityMountingTree() = default;
    };

    class Identifier
    {
    public:
        DocumentId Document() const         { return _doc; }
        ObjectTypeId ObjectType() const     { return _objType; }
        ObjectId Object() const             { return _obj; }

        Identifier(DocumentId doc = 0, ObjectId obj = 0, ObjectTypeId objType = 0)
		: _doc(doc), _obj(obj), _objType(objType) {}

		friend bool operator<(const Identifier& lhs, const Identifier& rhs)
		{
			if (lhs._doc < rhs._doc) return true;
			if (lhs._doc > rhs._doc) return false;
			if (lhs._objType < rhs._objType) return true;
			if (lhs._objType > rhs._objType) return false;
			if (lhs._obj < rhs._obj) return true;
			return false;
		}

		friend bool operator==(const Identifier& lhs, const Identifier& rhs)
		{
			return (lhs._doc == rhs._doc) || (lhs._objType == rhs._objType) || (lhs._obj == rhs._obj);
		}

    protected:
        DocumentId _doc;
        ObjectId _obj;
        ObjectTypeId _objType;
    };

    /// <summary>Defines rules for creation, deletion and update of entities</summary>
    ///
    /// Implementors of this interface will define rules for working with entities of 
    /// a specific types.
    ///
    /// Entities are imaginary objects with these properties:
    ///     * they have a "type"
    ///     * they exist within a tree hierarchy
    ///     * they have properties with string names and typed values
    ///
    /// To clients, data appears to be arranged according to these rules. However, 
    /// the underlying data structures may be quite different. We use these interfaces
    /// to "reimagine" complex objects as hierachies of entities.s
    ///
    /// This provides a simple, universal way to query and modify data throughout the 
    /// system.
    ///
    /// A good example is the "placements" interface. In reality, placement objects are
    /// stored within the native PlacementManager in their optimised native form,
    /// However, we can create an implementation of the "IObjectType" interface to make
    /// that data appear to be a hierarchy of entities.
    ///
    /// Sometimes the underlying data is actually just a hierarchy of objects with 
    /// properties, however. In these cases, IObjectType is just a generic way to access
    /// that data.
    ///
    /// This is important for interact with the level editor. The level editor natively
    /// uses XML DOM based data structures to define everything in the scene. This
    /// maps onto the entities concept easily. So we can use this idea to move data
    /// freely between the level editor and native objects.
    ///
    /// But it also suggests other uses that require querying and setting values in
    /// various objects in the scene. Such as animation of objects in the scene 
    /// and for scripting purposes.
    class IEntityInterface
    {
    public:
        virtual DocumentId CreateDocument(DocumentTypeId docType, const char initializer[]) = 0;
        virtual bool DeleteDocument(DocumentId doc, DocumentTypeId docType) = 0;

        virtual ObjectId AssignObjectId(DocumentId doc, ObjectTypeId objType) const = 0;
        virtual bool CreateObject(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount) = 0;
        virtual bool DeleteObject(const Identifier& id) = 0;
        virtual bool SetProperty(const Identifier& id, const PropertyInitializer initializers[], size_t initializerCount) = 0;
        virtual bool GetProperty(const Identifier& id, PropertyId prop, void* dest, unsigned* destSize) const = 0;
        virtual bool SetParent(const Identifier& child, const Identifier& parent, ChildListId childList, int insertionPosition) = 0;

        virtual ObjectTypeId GetTypeId(const char name[]) const = 0;
        virtual DocumentTypeId GetDocumentTypeId(const char name[]) const = 0;
        virtual PropertyId GetPropertyId(ObjectTypeId type, const char name[]) const = 0;
        virtual ChildListId GetChildListId(ObjectTypeId type, const char name[]) const = 0;

		virtual void PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const = 0;

        virtual ~IEntityInterface();
    };

    class IEnumerableEntityInterface
    {
    public:
        virtual std::vector<std::pair<ObjectTypeId, std::string>> FindObjectTypes() const = 0;
        virtual std::vector<std::pair<DocumentTypeId, std::string>> FindDocumentTypes() const = 0;

        virtual std::vector<std::pair<DocumentId, DocumentTypeId>> FindDocuments(DocumentTypeId docType) const = 0;
        virtual std::vector<ObjectId> FindEntities(DocumentId doc, ObjectTypeId objectType) const = 0;
        virtual std::vector<std::pair<PropertyId, std::string>> FindProperties(ObjectTypeId objectType) const = 0;
        virtual std::vector<std::pair<ChildListId, std::string>> FindChildLists(ObjectTypeId objectType) const = 0;

        virtual ~IEnumerableEntityInterface();
    };

    /// <summary>Holds a collection of IObjectType interface, and selects the appropriate interface for a given object</summary>
    ///
    /// Normally a scene will contain multiple different types of objects. Each object type
    /// might have a different IObjectType implementation to access that data.
    /// 
    /// This class will keep a collection of interfaces, and will select the right interface
    /// for any given operation.
    class Switch
    {
    public:
        IEntityInterface* GetInterface(
            Identifier& translatedId, 
            const Identifier& inputId) const;

        DocumentId  CreateDocument(DocumentTypeId docType, const char initializer[]);
        bool        DeleteDocument(DocumentId doc, DocumentTypeId docType);
        ObjectId    AssignObjectId(DocumentId doc, ObjectTypeId objType) const;

        ObjectTypeId    GetTypeId(const char name[]) const;
        DocumentTypeId  GetDocumentTypeId(const char name[]) const;
        PropertyId      GetPropertyId(ObjectTypeId type, const char name[]) const;
        ChildListId     GetChildListId(ObjectTypeId type, const char name[]) const;

        uint32  MapTypeId(ObjectTypeId type, const IEntityInterface& owner);
        void    RegisterInterface(const std::shared_ptr<IEntityInterface>& type);
		void	UnregisterInterface(const std::shared_ptr<IEntityInterface>& type);
		void    RegisterDefaultInterface(const std::shared_ptr<IEntityInterface>& type);

		void PrintDocument(std::ostream& stream, DocumentId doc, unsigned indent) const;

        Switch();
        ~Switch();
    protected:
        std::vector<std::shared_ptr<IEntityInterface>> _types;
		std::shared_ptr<IEntityInterface> _defaultType;

        class KnownType
        {
        public:
            std::shared_ptr<IEntityInterface> _owner;
            std::string _name;
            uint32 _mappedTypeId;
        };
        mutable std::vector<KnownType> _knownObjectTypes;
        mutable std::vector<KnownType> _knownDocumentTypes;

        mutable ObjectId _nextObjectId;
    };
}



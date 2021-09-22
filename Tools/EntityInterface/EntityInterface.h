// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetFuture.h"
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
    using ObjectTypeId = uint32_t;
    using PropertyId = uint32_t;
    using ChildListId = uint32_t;

    using DocumentId = uint64_t;
    using ObjectId = uint64_t;

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
        virtual ::Assets::DependencyValidation GetDependencyValidation() const;

        virtual ~IDynamicFormatter() = default;
    };

    class PropertyInitializer
    {
    public:
        StringSection<> _name;
        IteratorRange<const void*> _src;
        ImpliedTyping::TypeDesc _type;
    };

    class IEntityDocument
    {
    public:
        virtual ::Assets::PtrToFuturePtr<IDynamicFormatter> BeginFormatter(StringSection<> internalPoint);
        virtual ::Assets::DependencyValidation GetDependencyValidation() const;

        virtual void Lock();
        virtual void Unlock();

        virtual ~IEntityDocument() = default;
    };

    class IEntityMountingTree
    {
    public:
        virtual DocumentId MountDocument(
            StringSection<> mountPount,
            std::shared_ptr<IEntityDocument>) = 0;
        virtual bool UnmountDocument(DocumentId doc) = 0;

        // returns a dependency validation that advances if any properties at that mount point,
        // (or underneath) change 
        virtual ::Assets::DependencyValidation GetDependencyValidation(StringSection<> mountPount) const;
        virtual ::Assets::PtrToFuturePtr<IDynamicFormatter> BeginFormatter(StringSection<> mountPoint) const;

        virtual ~IEntityMountingTree() = default;
    };

    std::shared_ptr<IEntityMountingTree> CreateMountingTree();

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
    class IMutableEntityDocument
    {
    public:
        virtual std::optional<ObjectId> CreateObject(ObjectTypeId objType, IteratorRange<const PropertyInitializer*>) = 0;
        virtual bool DeleteObject(ObjectId id) = 0;
        virtual bool SetProperty(ObjectId id, IteratorRange<const PropertyInitializer*>) = 0;
        virtual std::optional<size_t> GetProperty(ObjectId id, PropertyId prop, IteratorRange<void*> destinationBuffer) const = 0;
        virtual bool SetParent(ObjectId child, ObjectId parent, ChildListId childList, int insertionPosition) = 0;

        virtual ObjectTypeId GetTypeId(StringSection<> name) const = 0;
        virtual PropertyId GetPropertyId(ObjectTypeId type, StringSection<> name) const = 0;
        virtual ChildListId GetChildListId(ObjectTypeId type, StringSection<> name) const = 0;

        virtual ~IMutableEntityDocument() = default;
    };

#if 0
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
#endif
}

// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Utility/IteratorUtils.h"
#include "../../Utility/ImpliedTyping.h"
#include "../../Utility/StringUtils.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Core/Types.h"
#include <memory>
#include <vector>
#include <string>

namespace Assets { class DependencyValidation; class DirectorySearchRules; }
namespace Formatters { class IDynamicInputFormatter; }
namespace std { template<typename T> class future; }

namespace EntityInterface
{
    using DocumentId = uint64_t;
    class IEntityDocument;

    class IEntityDocument
    {
    public:
        virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> internalPoint = {}) = 0;
        virtual const ::Assets::DependencyValidation& GetDependencyValidation() const = 0;
        virtual const ::Assets::DirectorySearchRules& GetDirectorySearchRules() const = 0;

        virtual void Lock() = 0;
        virtual bool TryLock() = 0;
        virtual void Unlock() = 0;

        virtual ~IEntityDocument() = default;
    };

    class IEntityMountingTree
    {
    public:
        virtual DocumentId MountDocument(
            StringSection<> mountPoint,
            std::shared_ptr<IEntityDocument>) = 0;
        virtual bool UnmountDocument(DocumentId doc) = 0;

        // returns a dependency validation that advances if any properties at that mount point,
        // (or underneath) change 
        virtual ::Assets::DependencyValidation GetDependencyValidation(StringSection<> mountPoint) const = 0;
        virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> mountPoint) const = 0;
        virtual std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> TryBeginFormatter(StringSection<> mountPoint) const = 0;

        virtual ~IEntityMountingTree() = default;
    };

    namespace MountingTreeFlags {
        enum Flags { LogMountPoints = 1<<0 };
        using BitField = unsigned;
    }

    std::shared_ptr<IEntityMountingTree> CreateMountingTree(MountingTreeFlags::BitField = 0);

    std::shared_ptr<Formatters::IDynamicInputFormatter> CreateEmptyFormatter();

    using EntityId = uint64_t;
    using StringAndHash = std::pair<StringSection<>, uint64_t>;

    StringAndHash MakeStringAndHash(StringSection<>);

    struct PropertyInitializer : public ImpliedTyping::VariantNonRetained
    {
        StringAndHash _prop = {{}, 0ull};
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
    class IMutableEntityDocument
    {
    public:
        virtual EntityId AssignEntityId() = 0;
        virtual bool CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*>) = 0;
        virtual bool DeleteEntity(EntityId id) = 0;
        virtual bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*>) = 0;
        virtual std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const = 0;
        virtual bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) = 0;
        virtual ~IMutableEntityDocument() = default;
    };

    class MultiInterfaceDocument;
    class Switch
    {
    public:
        using DocumentId = uint64_t;
        DocumentId CreateDocument(StringSection<> docType, StringSection<> initializer);
        DocumentId CreateDocument(std::shared_ptr<IMutableEntityDocument> doc);
        bool DeleteDocument(DocumentId doc);
        IMutableEntityDocument* GetInterface(DocumentId);

        class IDocumentType
        {
        public:
            virtual std::shared_ptr<IMutableEntityDocument> CreateDocument(StringSection<> initializer, DocumentId) = 0;
            virtual ~IDocumentType() = default;
        };

        void RegisterDocumentType(StringSection<> name, std::shared_ptr<IDocumentType>);
        void DeregisterDocumentType(StringSection<> name);

        void RegisterDefaultDocument(StringAndHash objType, DocumentId doc);
        void RegisterDefaultDocument(DocumentId doc);

        Switch();
        ~Switch();
        Switch(Switch&&) = delete;
        Switch& operator=(Switch&&) = delete;
    private:
        std::vector<std::pair<DocumentId, std::shared_ptr<IMutableEntityDocument>>> _documents;
        std::vector<std::pair<std::string, std::shared_ptr<IDocumentType>>> _documentTypes;
        std::shared_ptr<MultiInterfaceDocument> _defaultDocument;
        DocumentId _nextDocumentId = 1;
    };

    class ITranslateHighlightableId
    {
    public:
        virtual std::pair<uint64_t, uint64_t> QueryHighlightableId(EntityId) = 0;
        ~ITranslateHighlightableId() = default;
    };

#if 0
    class IEnumerableEntityInterface
    {
    public:
        virtual std::vector<std::pair<EntityTypeId, std::string>> FindObjectTypes() const = 0;
        virtual std::vector<std::pair<DocumentTypeId, std::string>> FindDocumentTypes() const = 0;

        virtual std::vector<std::pair<DocumentId, DocumentTypeId>> FindDocuments(DocumentTypeId docType) const = 0;
        virtual std::vector<EntityId> FindEntities(DocumentId doc, EntityTypeId objectType) const = 0;
        virtual std::vector<std::pair<PropertyId, std::string>> FindProperties(EntityTypeId objectType) const = 0;
        virtual std::vector<std::pair<ChildListId, std::string>> FindChildLists(EntityTypeId objectType) const = 0;

        virtual ~IEnumerableEntityInterface();
    };
#endif
}

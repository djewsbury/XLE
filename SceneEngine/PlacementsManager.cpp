// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PlacementsManager.h"
#include "GenericQuadTree.h"
#include "DynamicImposters.h"

#include "../RenderCore/Techniques/ModelCache.h"
#include "../RenderCore/Techniques/SimpleModelRenderer.h"
#include "../RenderCore/Assets/ModelScaffold.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/RenderStateResolver.h"

#include "../Assets/IFileSystem.h"
#include "../Assets/AssetTraits.h"
#include "../Assets/DepVal.h"
#include "../Assets/Assets.h"
#include "../Assets/ChunkFile.h"

#include "../RenderCore/RenderUtils.h"

#include "../OSServices/Log.h"
#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../Math/Matrix.h"
#include "../Math/Transformations.h"
#include "../Math/ProjectionMath.h"
#include "../Math/Geometry.h"
#include "../Math/MathSerialization.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/HeapUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringFormat.h"
#include "../OSServices/RawFS.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/StreamDOM.h"
#include "../Utility/Conversion.h"

#include <random>

namespace SceneEngine
{
    using Assets::ResChar;

    using RenderCore::Assets::ModelScaffold;
    using RenderCore::Assets::MaterialScaffold;

    using SupplementRange = IteratorRange<const uint64_t*>;

///////////////////////////////////////////////////////////////////////////////////////////////////

        // Note that "placements" that interface methods in Placements are actually
        // very rarely called. So it should be fine to make those methods into virtual
        // methods, and use an abstract base class.
    class Placements
    {
    public:
        typedef std::pair<Float3, Float3> BoundingBox;

        class ObjectReference
        {
        public:
            Float3x4    _localToCell;
            BoundingBox _cellSpaceBoundary;
            unsigned    _modelFilenameOffset;       // note -- hash values should be stored with the filenames
            unsigned    _materialFilenameOffset;
            unsigned    _supplementsOffset;
            uint64_t    _guid;
            Float3x3    _decomposedRotation;
            Float3      _decomposedScale;
        };
        
        const ObjectReference*  GetObjectReferences() const;
        unsigned                GetObjectReferenceCount() const;
        const void*             GetFilenamesBuffer() const;
        const uint64_t*           GetSupplementsBuffer() const;

        void Write(const Assets::ResChar destinationFile[]) const;
        void LogDetails(const char title[]) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const	{ return _dependencyValidation; }
		static const ::Assets::ArtifactRequest ChunkRequests[1];

        Placements(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        Placements();
        ~Placements();
    protected:
        std::vector<ObjectReference>    _objects;
        std::vector<uint8>              _filenamesBuffer;
        std::vector<uint64_t>             _supplementsBuffer;

		::Assets::DependencyValidation				_dependencyValidation;
        void ReplaceString(const char oldString[], const char newString[]);
    };

    auto            Placements::GetObjectReferences() const -> const ObjectReference*   { return AsPointer(_objects.begin()); }
    unsigned        Placements::GetObjectReferenceCount() const                         { return unsigned(_objects.size()); }
    const void*     Placements::GetFilenamesBuffer() const                              { return AsPointer(_filenamesBuffer.begin()); }
    const uint64_t*   Placements::GetSupplementsBuffer() const                            { return AsPointer(_supplementsBuffer.begin()); }

    static const uint64_t ChunkType_Placements = ConstHash64<'Plac','emen','ts'>::Value;

    class PlacementsHeader
    {
    public:
        unsigned _version;
        unsigned _objectRefCount;
        unsigned _filenamesBufferSize;
        unsigned _supplementsBufferSize;
        unsigned _dummy;
    };

    void Placements::Write(const Assets::ResChar destinationFile[]) const
    {
        using namespace Assets::ChunkFile;
		auto libVersion = ConsoleRig::GetLibVersionDesc();
        SimpleChunkFileWriter fileWriter(
			::Assets::MainFileSystem::OpenBasicFile(destinationFile, "wb", 0),
            1, libVersion._versionString, libVersion._buildDateString);
        fileWriter.BeginChunk(ChunkType_Placements, 0, "Placements");

        PlacementsHeader hdr;
        hdr._version = 0;
        hdr._objectRefCount = (unsigned)_objects.size();
        hdr._filenamesBufferSize = unsigned(_filenamesBuffer.size());
        hdr._supplementsBufferSize = unsigned(_supplementsBuffer.size() * sizeof(uint64_t));
        hdr._dummy = 0;
        auto writeResult0 = fileWriter.Write(&hdr, sizeof(hdr), 1);
        auto writeResult1 = fileWriter.Write(AsPointer(_objects.begin()), sizeof(ObjectReference), hdr._objectRefCount);
        auto writeResult2 = fileWriter.Write(AsPointer(_filenamesBuffer.begin()), 1, hdr._filenamesBufferSize);
        auto writeResult3 = fileWriter.Write(AsPointer(_supplementsBuffer.begin()), 1, hdr._supplementsBufferSize);

        if (    writeResult0 != 1
            ||  writeResult1 != hdr._objectRefCount
            ||  writeResult2 != hdr._filenamesBufferSize
            ||  writeResult3 != hdr._supplementsBufferSize)
            Throw(::Exceptions::BasicLabel("Failure in file write while saving placements"));
    }

    void Placements::LogDetails(const char title[]) const
    {
        // write some details about this placements file to the log
        Log(Verbose) << "---<< Placements file: " << title << " >>---" << std::endl;
        Log(Verbose) << "    (" << _objects.size() << ") object references -- " << sizeof(ObjectReference) * _objects.size() / 1024.f << "k in objects, " << _filenamesBuffer.size() / 1024.f << "k in string table" << std::endl;

        unsigned configCount = 0;
        auto i = _objects.cbegin();
        while (i != _objects.cend()) {
            auto starti = i;
            while (i != _objects.cend() 
                && i->_materialFilenameOffset == starti->_materialFilenameOffset 
                && i->_modelFilenameOffset == starti->_modelFilenameOffset
                && i->_supplementsOffset == starti->_supplementsOffset) { ++i; }
            ++configCount;
        }
        Log(Verbose) << "    (" << configCount << ") configurations" << std::endl;

        i = _objects.cbegin();
        while (i != _objects.cend()) {
            auto starti = i;
            while (i != _objects.cend() 
                && i->_materialFilenameOffset == starti->_materialFilenameOffset 
                && i->_modelFilenameOffset == starti->_modelFilenameOffset
                && i->_supplementsOffset == starti->_supplementsOffset) { ++i; }

            auto modelName = (const ResChar*)PtrAdd(AsPointer(_filenamesBuffer.begin()), starti->_modelFilenameOffset + sizeof(uint64_t));
            auto materialName = (const ResChar*)PtrAdd(AsPointer(_filenamesBuffer.begin()), starti->_materialFilenameOffset + sizeof(uint64_t));
            auto supplementCount = !_supplementsBuffer.empty() ? _supplementsBuffer[starti->_supplementsOffset] : 0;
            Log(Verbose) << "    [" << (i-starti) << "] objects (" << modelName << "), (" << materialName << "), (" << supplementCount << ")" << std::endl;
        }
    }

    void Placements::ReplaceString(const ResChar oldString[], const ResChar newString[])
    {
        unsigned replacementStart = 0, preReplacementEnd = 0;
        unsigned postReplacementEnd = 0;

        uint64_t oldHash = Hash64(oldString);
        uint64_t newHash = Hash64(newString);

            //  first, look through and find the old string.
            //  then, 
        auto i = _filenamesBuffer.begin();
        for(;i !=_filenamesBuffer.end();) {
            auto starti = i;
            if (std::distance(i, _filenamesBuffer.end()) < sizeof(uint64_t)) {
                assert(0);
                break;  // not enough room for a full hash code. Seems like the string table is corrupted
            }
            i += sizeof(uint64_t);
            while (i != _filenamesBuffer.end() && *i) { ++i; }
            if (i != _filenamesBuffer.end()) { ++i; }   // one more increment to include the null character

            if (*(const uint64_t*)AsPointer(starti) == oldHash) {

                    // if this is our string, then we need to erase the old content and insert
                    // the new

                auto length = XlStringSize(newString);
                std::vector<uint8> replacementContent(sizeof(uint64_t) + (length + 1) * sizeof(ResChar), 0);
                *(uint64_t*)AsPointer(replacementContent.begin()) = newHash;

                XlCopyString(
                    (ResChar*)AsPointer(replacementContent.begin() + sizeof(uint64_t)),
                    length+1, newString);

                replacementStart = (unsigned)std::distance(_filenamesBuffer.begin(), starti);
                preReplacementEnd = (unsigned)std::distance(_filenamesBuffer.begin(), i);
                postReplacementEnd = unsigned(replacementStart + replacementContent.size());
                i = _filenamesBuffer.erase(starti, i);
                auto dst = std::distance(_filenamesBuffer.begin(), i);
                _filenamesBuffer.insert(i, replacementContent.begin(), replacementContent.end());
                i = _filenamesBuffer.begin();
                std::advance(i, dst + replacementContent.size());

                    // Now we have to adjust all of the offsets in the ObjectReferences
                for (auto o=_objects.begin(); o!=_objects.end(); ++o) {
                    if (o->_modelFilenameOffset > replacementStart) {
                        o->_modelFilenameOffset -= preReplacementEnd - postReplacementEnd;
                        assert(o->_modelFilenameOffset > replacementStart);
                    }
                    if (o->_materialFilenameOffset > replacementStart) {
                        o->_materialFilenameOffset -= preReplacementEnd - postReplacementEnd;
                        assert(o->_materialFilenameOffset > replacementStart);
                    }
                }
                return;
            }
        }
    }

    const ::Assets::ArtifactRequest Placements::ChunkRequests[]
    {
        ::Assets::ArtifactRequest
        {
            "Placements", ChunkType_Placements, 0, 
            ::Assets::ArtifactRequest::DataType::Raw 
        }
    };

    Placements::Placements(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal)
	: _dependencyValidation(depVal)
    {
        assert(chunks.size() == 1);

            //
            //      Extremely simple file format for placements
            //      We just need 2 blocks:
            //          * list of object references
            //          * list of filenames / strings
            //      The strings are kept separate from the object placements
            //      because many of the string will be referenced multiple
            //      times. It just helps reduce file size.
            //

        void const* i = chunks[0]._buffer.get();
        const auto& hdr = *(const PlacementsHeader*)i;
        if (hdr._version != 0)
            Throw(::Exceptions::BasicLabel(
                StringMeld<128>() << "Unexpected version number (" << hdr._version << ")"));
        i = PtrAdd(i, sizeof(PlacementsHeader));

        _objects.clear();
        _filenamesBuffer.clear();
        _supplementsBuffer.clear();

        _objects.insert(_objects.end(),
            (const ObjectReference*)i, (const ObjectReference*)i + hdr._objectRefCount);
        i = (const ObjectReference*)i + hdr._objectRefCount;

        _filenamesBuffer.insert(_filenamesBuffer.end(),
            (const uint8*)i, (const uint8*)i + hdr._filenamesBufferSize);
        i = (const uint8*)i + hdr._filenamesBufferSize;

        _supplementsBuffer.insert(_supplementsBuffer.end(),
            (const uint64_t*)i, (const uint64_t*)PtrAdd(i, hdr._supplementsBufferSize));
        i = PtrAdd(i, hdr._supplementsBufferSize);

        #if defined(_DEBUG)
            if (!_objects.empty())
                LogDetails("<<unknown>>");
        #endif
    }

	Placements::Placements() 
	{}

    Placements::~Placements()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PlacementCell
    {
    public:
        uint64_t      _filenameHash;
        Float3x4    _cellToWorld;
        Float3      _aabbMin, _aabbMax;
        Float2      _captureMins, _captureMaxs;
        ResChar     _filename[256];
    };

    class GenericQuadTree;

    class PlacementsCache
    {
    public:
        class Item
        {
        public:
            ::Assets::rstring _filename;
            std::unique_ptr<Placements> _placements;

            void Reload();

            Item() {}
            Item(Item&& moveFrom) : _filename(std::move(moveFrom._filename)), _placements(std::move(moveFrom._placements)) {}
            Item& operator=(Item&& moveFrom) 
            {
                _filename = std::move(moveFrom._filename);
                _placements = std::move(moveFrom._placements);
                return *this;
            }

            Item& operator=(const Item&) = delete;
            Item(const Item&) = delete;
        };
        Item* Get(uint64_t filenameHash, const ResChar filename[] = nullptr);

        PlacementsCache();
        ~PlacementsCache();
    protected:
        std::vector<std::pair<uint64_t, std::unique_ptr<Item>>> _items;
    };

    auto PlacementsCache::Get(uint64_t filenameHash, const ResChar filename[]) -> Item*
    {
        auto i = LowerBound(_items, filenameHash);
        if (i != _items.end() && i->first == filenameHash) {
            if (i->second->_placements->GetDependencyValidation().GetValidationIndex()!=0)
                i->second->Reload();
            return i->second.get();
        } 

            // When filename is null, we can only return an object that has been created before. 
            // if it hasn't been created, we will return null
        if (!filename) return nullptr;

        auto newItem = std::make_unique<Item>();
        newItem->_filename = filename;
        newItem->Reload();
        i = _items.emplace(i, std::make_pair(filenameHash, std::move(newItem)));
        return i->second.get();
    }

    void PlacementsCache::Item::Reload()
    {
        _placements.reset();
        _placements = ::Assets::AutoConstructAsset<std::unique_ptr<Placements>>(MakeStringSection(_filename));
    }

    PlacementsCache::PlacementsCache() {}
    PlacementsCache::~PlacementsCache() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static ::Assets::AssetState TryGetBoundingBox(
        Placements::BoundingBox& result, 
        PlacementsModelCache& modelCache, const ResChar modelFilename[], 
        unsigned LOD = 0, bool stallWhilePending = false)
    {
		auto model = modelCache.GetModelScaffold(modelFilename);
		::Assets::AssetState state = model->GetAssetState();
		if (stallWhilePending) {
			auto res = model->StallWhilePending();
			if (!res.has_value()) return ::Assets::AssetState::Pending;
			state = res.value();
		}

		if (state != ::Assets::AssetState::Ready) return state;

		result = model->Actualize()->GetStaticBoundingBox(LOD);
		return ::Assets::AssetState::Ready;
    }

    class PlacementsRenderer::Pimpl
    {
    public:
        Placements* CullCell(
            std::vector<unsigned>& visiblePlacements,
            const SceneView& view,
            const PlacementCell& cell);

        void CullCell(
            std::vector<unsigned>& visiblePlacements,
            const Float4x4& cellToCullSpace,
            const Placements& placements,
            const GenericQuadTree* quadTree);

        void BuildDrawables(
            const ExecuteSceneContext& executeContext,
            const Placements& placements,
            IteratorRange<unsigned*> objects,
            const Float3x4& cellToWorld,
            const uint64_t* filterStart = nullptr, const uint64_t* filterEnd = nullptr);

        auto GetCachedQuadTree(uint64_t cellFilenameHash) const -> const GenericQuadTree*;
        PlacementsModelCache& GetModelCache() { return *_cache; }

        Pimpl(
            std::shared_ptr<PlacementsCache> placementsCache, 
            std::shared_ptr<PlacementsModelCache> modelCache);
        ~Pimpl();

        class CellRenderInfo
        {
        public:
            PlacementsCache::Item* _placements;
            std::unique_ptr<GenericQuadTree> _quadTree;

            CellRenderInfo() {}
            CellRenderInfo(CellRenderInfo&& moveFrom) never_throws
            : _placements(moveFrom._placements)
            , _quadTree(std::move(moveFrom._quadTree))
            {
                moveFrom._placements = nullptr;
            }

            CellRenderInfo& operator=(CellRenderInfo&& moveFrom) never_throws
            {
                _placements = moveFrom._placements;
                moveFrom._placements = nullptr;
                _quadTree = std::move(moveFrom._quadTree);
                return *this;
            }

        private:
            CellRenderInfo(const CellRenderInfo&);
            CellRenderInfo& operator=(const CellRenderInfo&);
        };

        std::vector<std::pair<uint64_t, CellRenderInfo>> _cells;
        std::shared_ptr<PlacementsCache> _placementsCache;
        std::shared_ptr<PlacementsModelCache> _cache;

        std::shared_ptr<DynamicImposters> _imposters;
    };

    class PlacementsManager::Pimpl
    {
    public:
        std::shared_ptr<PlacementsRenderer> _renderer;
        std::shared_ptr<PlacementsCache> _placementsCache;
        std::shared_ptr<PlacementsModelCache> _modelCache;
        std::shared_ptr<PlacementsIntersections> _intersections;
    };

    class PlacementCellSet::Pimpl
    {
    public:
        std::vector<PlacementCell> _cells;
        std::vector<std::pair<uint64_t, std::shared_ptr<Placements>>> _cellOverrides;

        void SetOverride(uint64_t guid, std::shared_ptr<Placements> placements);
        Placements* GetOverride(uint64_t guid);
    };

    Placements* PlacementCellSet::Pimpl::GetOverride(uint64_t guid)
    {
        auto i = LowerBound(_cellOverrides, guid);
        if (i != _cellOverrides.end() && i->first == guid)
            return i->second.get();
        return nullptr;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    auto PlacementsRenderer::Pimpl::GetCachedQuadTree(uint64_t cellFilenameHash) const -> const GenericQuadTree*
    {
        auto i2 = LowerBound(_cells, cellFilenameHash);
        if (i2!=_cells.end() && i2->first == cellFilenameHash) {
            return i2->second._quadTree.get();
        }
        return nullptr;
    }

    Placements* PlacementsRenderer::Pimpl::CullCell(
        std::vector<unsigned>& visibleObjects,
        const SceneView& view,
        const PlacementCell& cell)
    {
        // Look for a "RenderInfo" for this cell.. and create it if it doesn't exist
        // Note that there's a bit of extra overhead here:
        //  * in this design, we need to search for the cell by guid id
        //  * however, the cells are probably arranged in a 2d grid, and we probably
        //      know the 2d address -- which means we could go right to the correct
        //      cell.
        //
        // But this design allows for a little extra flexibility. We're no restricted
        // on how the placement cells are arranged, so we can have overlapping cells, or
        // separate cells for inside/outside/underwater/etc. Or we can have cells that
        // represent different states (like stages of building a castle, or if a zone
        // changes over time). 
        //
        // It seems useful to me. But if the overhead becomes too great, we can just change
        // to a basic 2d addressing model.
        if (cell._filename[0] == '[') return nullptr;   // hack -- if the cell filename begins with '[', it is a cell from the editor (and should be using _cellOverrides)

        auto i2 = LowerBound(_cells, cell._filenameHash);
        if (i2 == _cells.end() || i2->first != cell._filenameHash) {
            CellRenderInfo newRenderInfo;
            newRenderInfo._placements = _placementsCache->Get(cell._filenameHash, cell._filename);
            i2 = _cells.insert(i2, std::make_pair(cell._filenameHash, std::move(newRenderInfo)));
        }

            // check if we need to reload placements
        if (i2->second._placements->_placements->GetDependencyValidation().GetValidationIndex() != 0) {
            i2->second._placements->Reload();
            i2->second._quadTree.reset();
        }

        if (!i2->second._quadTree) {
            const unsigned leafThreshold = 12;
            auto dataBlock = GenericQuadTree::BuildQuadTree(
                &i2->second._placements->_placements->GetObjectReferences()->_cellSpaceBoundary,
                sizeof(Placements::ObjectReference), 
                i2->second._placements->_placements->GetObjectReferenceCount(),
                leafThreshold);
            i2->second._quadTree = std::make_unique<GenericQuadTree>(std::move(dataBlock.first));
        }

        __declspec(align(16)) auto cellToCullSpace = Combine(cell._cellToWorld, view._projection._worldToProjection);
        CullCell(
            visibleObjects, cellToCullSpace, 
            *i2->second._placements->_placements, 
            i2->second._quadTree.get());

        return i2->second._placements->_placements.get();
    }

    static SupplementRange AsSupplements(const uint64_t* supplementsBuffer, unsigned supplementsOffset)
    {
        if (!supplementsOffset) return SupplementRange();
        return SupplementRange(
            supplementsBuffer+supplementsOffset+1, 
            supplementsBuffer+supplementsOffset+1+supplementsBuffer[supplementsOffset]);
    }

    namespace Internal
    {
        class RendererHelper
        {
        public:
            template<bool UseImposters = true>
                void Render(
					IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
                    PlacementsModelCache& cache,
                    const void* filenamesBuffer,
                    const uint64_t* supplementsBuffer,
                    const Placements::ObjectReference& obj,
                    const Float3x4& cellToWorld,
                    const Float3& cameraPosition);

            class Metrics
            {
            public:
                unsigned _instancesPrepared;
                unsigned _uniqueModelsPrepared;
                unsigned _impostersQueued;

                Metrics()
                {
                    _instancesPrepared = 0;
                    _uniqueModelsPrepared = 0;
                    _impostersQueued = 0;
                }
            };

            Metrics _metrics;

            RendererHelper(DynamicImposters* imposters)
            {
                _currentModel = _currentMaterial = 0ull;
                _currentSupplements = 0u;

                auto maxDistance = 1000.f;
                if (imposters && imposters->IsEnabled())
                    maxDistance = imposters->GetThresholdDistance();
                _maxDistanceSq = maxDistance * maxDistance;

                _imposters = imposters;
                _currentModelRendered = false;
            }
        protected:
            uint64_t _currentModel, _currentMaterial;
            unsigned _currentSupplements;
            ::Assets::PtrToFuturePtr<RenderCore::Techniques::SimpleModelRenderer> _current;
            float _maxDistanceSq;
            bool _currentModelRendered;
            DynamicImposters* _imposters;
        };

        template<bool UseImposters>
            void RendererHelper::Render(
                IteratorRange<RenderCore::Techniques::DrawablesPacket** const> pkts,
				PlacementsModelCache& cache,
                const void* filenamesBuffer,
                const uint64_t* supplementsBuffer,
                const Placements::ObjectReference& obj,
                const Float3x4& cellToWorld,
                const Float3& cameraPosition)
        {
                // Basic draw distance calculation
                // many objects don't need to render out to the far clip

            float distanceSq = MagnitudeSquared(
                .5f * (obj._cellSpaceBoundary.first + obj._cellSpaceBoundary.second) - cameraPosition);

            if (constant_expression<!UseImposters>::result() && distanceSq > _maxDistanceSq)
                return; 

                //  Objects should be sorted by model & material. This is important for
                //  reducing the work load in "_cache". Typically cells will only refer
                //  to a limited number of different types of objects, but the same object
                //  may be repeated many times. In these cases, we want to minimize the
                //  workload for every repeat.
            auto modelHash = *(uint64_t*)PtrAdd(filenamesBuffer, obj._modelFilenameOffset);
            auto materialHash = *(uint64_t*)PtrAdd(filenamesBuffer, obj._materialFilenameOffset);
            materialHash = HashCombine(materialHash, modelHash);

                // Simple LOD calculation based on distanceSq from camera...
                //      Currently all models have only the single LOD. But this
                //      may cause problems with models with multiple LOD, because
                //      it may mean rapidly switching back and forth between 
                //      renderers (which can be expensive)

            // todo -- we need to improve this in 2 critical ways:
            //      1) more complex metric for selecting the LOD (eg, considering size on screen, maybe triangle density)
            //              - also consider best approach for selecting LOD for very large objects
            //      2) we need to record the result from last frame, so we can do transitions (eg, using a dither based fade)
            //
            // However, that functionality should probably be combined with a change to the scene parser that 
            // to add a more formal "prepare" step. So it will have to wait for now.
            // auto LOD = unsigned(distanceSq / (75.f*75.f));

            if (    modelHash != _currentModel 
                ||  materialHash != _currentMaterial 
                ||  obj._supplementsOffset != _currentSupplements) {

                _current = cache.GetModelRenderer(
                    (const ResChar*)PtrAdd(filenamesBuffer, obj._modelFilenameOffset + sizeof(uint64_t)),
                    (const ResChar*)PtrAdd(filenamesBuffer, obj._materialFilenameOffset + sizeof(uint64_t)));
                _currentModel = modelHash;
                _currentMaterial = materialHash;
                _currentSupplements = obj._supplementsOffset;
                _currentModelRendered = false;
            }

			auto current = _current->TryActualize();
			if (!current) return;

            auto localToWorld = Combine(obj._localToCell, cellToWorld);

            if (constant_expression<UseImposters>::result() && distanceSq > _maxDistanceSq) {
                assert(_imposters);
                // _imposters->Queue(*current, *current->GetModelScaffold(), localToWorld, cameraPosition);
                ++_metrics._impostersQueued;
                return; 
            }

                //  if we have internal transforms, we must use them.
                //  But some models don't have any internal transforms -- in these
                //  cases, the _defaultTransformCount will be zero
            (*current)->BuildDrawables(pkts, AsFloat4x4(localToWorld));

            ++_metrics._instancesPrepared;
            _metrics._uniqueModelsPrepared += !_currentModelRendered;
            _currentModelRendered = true;
        }
    }

    /*static Utility::Internal::StringMeldInPlace<char> QuickMetrics(RenderCore::Techniques::ParsingContext& parserContext)
    {
        return StringMeldAppend(parserContext._stringHelpers->_quickMetrics);
    }*/

    void PlacementsRenderer::Pimpl::CullCell(
        std::vector<unsigned>& visiblePlacements,
        const Float4x4& cellToCullSpace,
        const Placements& placements,
        const GenericQuadTree* quadTree)
    {
        auto placementCount = placements.GetObjectReferenceCount();
        if (!placementCount)
            return;
        
        const auto* objRef = placements.GetObjectReferences();
        
        if (quadTree) {
            auto cullResults = quadTree->GetMaxResults();
            visiblePlacements.resize(cullResults);
            GenericQuadTree::Metrics metrics;
			assert(placementCount < (1<<28));
            quadTree->CalculateVisibleObjects(
                cellToCullSpace, RenderCore::Techniques::GetDefaultClipSpaceType(),
                &objRef->_cellSpaceBoundary,
                sizeof(Placements::ObjectReference),
                AsPointer(visiblePlacements.begin()), cullResults, cullResults,
                &metrics);
            visiblePlacements.resize(cullResults);

            // QuickMetrics(parserContext) << "Cull placements cell... AABB test: (" << metrics._nodeAabbTestCount << ") nodes + (" << metrics._payloadAabbTestCount << ") payloads\n";

                // we have to sort to return to our expected order
            std::sort(visiblePlacements.begin(), visiblePlacements.end());
        } else {
            visiblePlacements.reserve(placementCount);
            for (unsigned c=0; c<placementCount; ++c) {
                auto& obj = objRef[c];
                if (CullAABB_Aligned(cellToCullSpace, obj._cellSpaceBoundary.first, obj._cellSpaceBoundary.second, RenderCore::Techniques::GetDefaultClipSpaceType()))
                    continue;
                visiblePlacements.push_back(c);
            }
        }
    }

    void PlacementsRenderer::Pimpl::BuildDrawables(
        const ExecuteSceneContext& executeContext,
        const Placements& placements,
        IteratorRange<unsigned*> objects,
        const Float3x4& cellToWorld,
        const uint64_t* filterStart, const uint64_t* filterEnd)
    {
            //
            //  Here we render all of the placements defined by the placement
            //  file in renderInfo._placements.
            //
            //  Many engines would drop back to a scene-tree representation 
            //  for this kind of thing. The advantage of the scene-tree, is that
            //  nodes can become many different things.
            //
            //  But here, in this case, we want to deal with exactly one type
            //  of thing -- just an object placed in the world. We can always
            //  render other types of things afterwards. So long as we use
            //  the same shared state set and the same prepared state objects,
            //  they will be sorted efficiently for rendering.
            //
            //  If we know that all objects are just placements -- we can write
            //  a very straight-forward and efficient implementation of exactly
            //  the behaviour we want. That's the advantage of this model. 
            //
            //  Using a scene tree, or some other generic structure, often the
            //  true behaviour of the system can be obscured by layers of
            //  generality. But the behaviour of the system is the most critical
            //  thing in a system like this. We want to be able to design and chart
            //  out the behaviour, and get the exact results we want. Especially
            //  when the behaviour is actually fairly simple.
            //
            //  So, to that end... Let's find all of the objects to render (using
            //  whatever culling/occlusion methods we need) and prepare them all
            //  for rendering.
            //  

        const uint64_t* filterIterator = filterStart;
        const bool doFilter = filterStart != filterEnd;
        Internal::RendererHelper helper(_imposters.get());

        auto cameraPositionCell = ExtractTranslation(executeContext._view._projection._cameraToWorld);
        cameraPositionCell = TransformPointByOrthonormalInverse(cellToWorld, cameraPositionCell);
        
        const auto* filenamesBuffer = placements.GetFilenamesBuffer();
        const auto* supplementsBuffer = placements.GetSupplementsBuffer();
        const auto* objRef = placements.GetObjectReferences();

            // Filtering is required in some cases (for example, if we want to render only
            // a single object in highlighted state). Rendering only part of a cell isn't
            // ideal for this architecture. Mostly the cell is intended to work as a 
            // immutable atomic object. However, we really need filtering for some things.

		RenderCore::Techniques::DrawablesPacket* pkts[unsigned(RenderCore::Techniques::BatchFilter::Max)];
        pkts[unsigned(executeContext._batchFilter)] = executeContext._destinationPkt;

        if (_imposters && _imposters->IsEnabled()) { //////////////////////////////////////////////////////////////////
            if (doFilter) {
                for (auto o:objects) {
                    auto& obj = objRef[o];
                    while (filterIterator != filterEnd && *filterIterator < obj._guid) { ++filterIterator; }
                    if (filterIterator == filterEnd || *filterIterator != obj._guid) { continue; }
                    helper.Render<true>(
                        MakeIteratorRange(pkts), *_cache,
                        filenamesBuffer, supplementsBuffer, obj, cellToWorld, cameraPositionCell);
                }
            } else {
                for (auto o:objects)
                    helper.Render<true>(
                        MakeIteratorRange(pkts), *_cache,
                        filenamesBuffer, supplementsBuffer, objRef[o], cellToWorld, cameraPositionCell);
            }
        } else { //////////////////////////////////////////////////////////////////////////////////////////////////////
            if (doFilter) {
                for (auto o:objects) {
                    auto& obj = objRef[o];
                    while (filterIterator != filterEnd && *filterIterator < obj._guid) { ++filterIterator; }
                    if (filterIterator == filterEnd || *filterIterator != obj._guid) { continue; }
                    helper.Render<false>(
                        MakeIteratorRange(pkts), *_cache,
                        filenamesBuffer, supplementsBuffer, obj, cellToWorld, cameraPositionCell);
                }
            } else {
                for (auto o:objects)
                    helper.Render<false>(
                        MakeIteratorRange(pkts), *_cache,
                        filenamesBuffer, supplementsBuffer, objRef[o], cellToWorld, cameraPositionCell);
            }
        } /////////////////////////////////////////////////////////////////////////////////////////////////////////////

        // QuickMetrics(parserContext) << "Placements cell: (" << helper._metrics._instancesPrepared << ") instances from (" << helper._metrics._uniqueModelsPrepared << ") models. Imposters: (" << helper._metrics._impostersQueued << ")\n";
    }

    PlacementsRenderer::Pimpl::Pimpl(
        std::shared_ptr<PlacementsCache> placementsCache, 
        std::shared_ptr<PlacementsModelCache> modelCache)
    : _placementsCache(std::move(placementsCache))
    , _cache(std::move(modelCache))
    {}

    PlacementsRenderer::Pimpl::~Pimpl() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void PlacementsRenderer::SetImposters(std::shared_ptr<DynamicImposters> imposters)
    {
        _pimpl->_imposters = std::move(imposters);
    }

    PlacementsRenderer::PlacementsRenderer(
        std::shared_ptr<PlacementsCache> placementsCache, 
        std::shared_ptr<PlacementsModelCache> modelCache)
    {
        _pimpl = std::make_unique<Pimpl>(std::move(placementsCache), std::move(modelCache));
    }

    PlacementsRenderer::~PlacementsRenderer() {}

    class PreCulledPlacements
    {
    public:
        class Cell
        {
        public:
            unsigned                _cellIndex;
            std::vector<unsigned>   _objects;
            Placements*             _placements;
            Float3x4                _cellToWorld;
        };

        std::vector<std::unique_ptr<Cell>> _cells;
    };

    void PlacementsRenderer::BuildDrawables(
        const ExecuteSceneContext& executeContext,
        const PlacementCellSet& cellSet)
    {
        if (!Tweakable("DoPlacements", true)) {
            return;
        }

        static std::vector<unsigned> visibleObjects;
        const auto& view = executeContext._view;

            // Render every registered cell
            // We catch exceptions on a cell based level (so pending cells won't cause other cells to flicker)
            // non-asset exceptions will throw back to the caller and bypass EndRender()
        auto& cells = cellSet._pimpl->_cells;
        for (auto i=cells.begin(); i!=cells.end(); ++i) {

            if (CullAABB_Aligned(view._projection._worldToProjection, i->_aabbMin, i->_aabbMax, RenderCore::Techniques::GetDefaultClipSpaceType()))
				continue;

                //  We need to look in the "_cellOverride" list first.
                //  The overridden cells are actually designed for tools. When authoring 
                //  placements, we need a way to render them before they are flushed to disk.
            visibleObjects.clear();
			auto ovr = LowerBound(cellSet._pimpl->_cellOverrides, i->_filenameHash);

			if (ovr != cellSet._pimpl->_cellOverrides.end() && ovr->first == i->_filenameHash) {
                __declspec(align(16)) auto cellToCullSpace = Combine(i->_cellToWorld, view._projection._worldToProjection);
                _pimpl->CullCell(visibleObjects, cellToCullSpace, *ovr->second.get(), nullptr);
				_pimpl->BuildDrawables(executeContext, *ovr->second.get(), MakeIteratorRange(visibleObjects), i->_cellToWorld);
			} else {
				auto* plc = _pimpl->CullCell(visibleObjects, view, *i);
				if (plc)
					_pimpl->BuildDrawables(executeContext, *plc, MakeIteratorRange(visibleObjects), i->_cellToWorld);
			}
        }
    }

    void PlacementsRenderer::BuildDrawables(
        const ExecuteSceneContext& executeContext,
        const PlacementCellSet& cellSet,
        const PlacementGUID* begin, const PlacementGUID* end,
        const std::shared_ptr<RenderCore::Techniques::ICustomDrawDelegate>& preDrawDelegate)
    {
        static std::vector<unsigned> visibleObjects;

            //  We need to take a copy, so we don't overwrite
            //  and reorder the caller's version.
        if (begin || end) {
            std::vector<PlacementGUID> copy(begin, end);
            std::sort(copy.begin(), copy.end());

            auto ci = cellSet._pimpl->_cells.begin();
            for (auto i=copy.begin(); i!=copy.end();) {
                auto i2 = i+1;
                for (; i2!=copy.end() && i2->first == i->first; ++i2) {}
                while (ci != cellSet._pimpl->_cells.end() && ci->_filenameHash < i->first) { ++ci; }
                if (ci != cellSet._pimpl->_cells.end() && ci->_filenameHash == i->first) {

                        // re-write the object guids for the renderer's convenience
                    uint64_t* tStart = &i->first;
                    uint64_t* t = tStart;
                    while (i < i2) { *t++ = i->second; i++; }

                    visibleObjects.clear();

					auto ovr = LowerBound(cellSet._pimpl->_cellOverrides, ci->_filenameHash);
					if (ovr != cellSet._pimpl->_cellOverrides.end() && ovr->first == ci->_filenameHash) {
                        const auto& view = executeContext._view;
                        __declspec(align(16)) auto cellToCullSpace = Combine(ci->_cellToWorld, view._projection._worldToProjection);
                        _pimpl->CullCell(visibleObjects, cellToCullSpace, *ovr->second.get(), nullptr);
						_pimpl->BuildDrawables(executeContext, *ovr->second, MakeIteratorRange(visibleObjects), ci->_cellToWorld, tStart, t);
					} else {
						auto* plcmnts = _pimpl->CullCell(visibleObjects, executeContext._view, *ci);
						if (plcmnts)
							_pimpl->BuildDrawables(executeContext, *plcmnts, MakeIteratorRange(visibleObjects), ci->_cellToWorld, tStart, t);
					}

                } else {
                    i = i2;
                }
            }
        } else {
                // in this case we're not filtering by object GUID (though we may apply a predicate on the prepared draw calls)
            for (auto i=cellSet._pimpl->_cells.begin(); i!=cellSet._pimpl->_cells.end(); ++i) {
                visibleObjects.clear();

				auto ovr = LowerBound(cellSet._pimpl->_cellOverrides, i->_filenameHash);
				if (ovr != cellSet._pimpl->_cellOverrides.end() && ovr->first == i->_filenameHash) {
                    const auto& view = executeContext._view;
                    __declspec(align(16)) auto cellToCullSpace = Combine(i->_cellToWorld, view._projection._worldToProjection);
                
                    _pimpl->CullCell(visibleObjects, cellToCullSpace, *ovr->second.get(), nullptr);
					_pimpl->BuildDrawables(executeContext, *ovr->second, MakeIteratorRange(visibleObjects), i->_cellToWorld);
				} else {
					auto* plcmnts = _pimpl->CullCell(visibleObjects, executeContext._view, *i);
					if (plcmnts)
						_pimpl->BuildDrawables(executeContext, *plcmnts, MakeIteratorRange(visibleObjects), i->_cellToWorld);
				}
            }
        }
    }

    auto PlacementsRenderer::GetVisibleQuadTrees(const PlacementCellSet& cellSet, const Float4x4& worldToClip) const
            -> std::vector<std::pair<Float3x4, const GenericQuadTree*>>
    {
        std::vector<std::pair<Float3x4, const GenericQuadTree*>> result;
        for (auto i=cellSet._pimpl->_cells.begin(); i!=cellSet._pimpl->_cells.end(); ++i) {
            if (!CullAABB(worldToClip, i->_aabbMin, i->_aabbMax, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                auto* tree = _pimpl->GetCachedQuadTree(i->_filenameHash);
                result.push_back(std::make_pair(i->_cellToWorld, tree));
            }
        }
        return std::move(result);
    }

    auto PlacementsRenderer::GetObjectBoundingBoxes(const PlacementCellSet& cellSet, const Float4x4& worldToClip) const
            -> std::vector<std::pair<Float3x4, ObjectBoundingBoxes>>
    {
        std::vector<std::pair<Float3x4, ObjectBoundingBoxes>> result;
        for (auto i=cellSet._pimpl->_cells.begin(); i!=cellSet._pimpl->_cells.end(); ++i) {
            if (!CullAABB(worldToClip, i->_aabbMin, i->_aabbMax, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                auto& placements = Assets::Legacy::GetAsset<Placements>(i->_filename);
                ObjectBoundingBoxes obb;
                obb._boundingBox = &placements.GetObjectReferences()->_cellSpaceBoundary;
                obb._stride = sizeof(Placements::ObjectReference);
                obb._count = placements.GetObjectReferenceCount();
                result.push_back(std::make_pair(i->_cellToWorld, obb));
            }
        }
        return std::move(result);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    const std::shared_ptr<PlacementsRenderer>& PlacementsManager::GetRenderer()
    {
        return _pimpl->_renderer;
    }

    const std::shared_ptr<PlacementsIntersections>& PlacementsManager::GetIntersections()
    {
        return _pimpl->_intersections;
    }

    std::shared_ptr<PlacementsEditor> PlacementsManager::CreateEditor(
        const std::shared_ptr<PlacementCellSet>& cellSet)
    {
        return std::make_shared<PlacementsEditor>(
            cellSet, shared_from_this(),
            _pimpl->_placementsCache, _pimpl->_modelCache);
    }

    PlacementsManager::PlacementsManager(std::shared_ptr<PlacementsModelCache> modelCache)
    {
            //  Using the given config file, let's construct the list of 
            //  placement cells
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_placementsCache = std::make_shared<PlacementsCache>();
        _pimpl->_modelCache = modelCache;
        _pimpl->_renderer = std::make_shared<PlacementsRenderer>(_pimpl->_placementsCache, modelCache);
        _pimpl->_intersections = std::make_shared<PlacementsIntersections>(_pimpl->_placementsCache, modelCache);
    }

    PlacementsManager::~PlacementsManager() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    void PlacementCellSet::Pimpl::SetOverride(uint64_t guid, std::shared_ptr<Placements> placements)
    {
        auto i = LowerBound(_cellOverrides, guid);
        if (i ==_cellOverrides.end() || i->first != guid) {
            if (placements) {
                _cellOverrides.insert(i, std::make_pair(guid, std::move(placements)));
            }
        } else {
            if (placements) {
                i->second = std::move(placements); // override the previous one
            } else {
                _cellOverrides.erase(i);
            }
        }
    }
    
    PlacementCellSet::PlacementCellSet(const WorldPlacementsConfig& cfg, const Float3& worldOffset)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_cells.reserve(cfg._cells.size());
        for (auto c=cfg._cells.cbegin(); c!=cfg._cells.cend(); ++c) {
            PlacementCell cell;
            XlCopyString(cell._filename, c->_file);
            cell._filenameHash = Hash64(cell._filename);
            cell._cellToWorld = AsFloat3x4(worldOffset + c->_offset);

                // note -- we could shrink wrap this bounding box around the objects
                //      inside. This might be necessary, actually, because some objects
                //      may be straddling the edges of the area, so the cell bounding box
                //      should be slightly larger.
            cell._aabbMin = c->_offset + c->_mins;
            cell._aabbMax = c->_offset + c->_maxs;

            _pimpl->_cells.push_back(cell);
        }
    }

    PlacementCellSet::~PlacementCellSet() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class DynamicPlacements : public Placements
    {
    public:
        uint64_t AddPlacement(
            const Float3x4& objectToCell, 
            const std::pair<Float3, Float3>& cellSpaceBoundary,
            StringSection<ResChar> modelFilename, StringSection<ResChar> materialFilename,
            SupplementRange supplements,
            uint64_t objectGuid);

        std::vector<ObjectReference>& GetObjects() { return _objects; }
        bool HasObject(uint64_t guid);

        unsigned AddString(StringSection<ResChar> str);
        unsigned AddSupplements(SupplementRange supplements);

        DynamicPlacements(const Placements& copyFrom);
        DynamicPlacements();
    };

    static uint32 BuildGuid32()
    {
        static std::mt19937 generator(std::random_device().operator()());
        return generator();
    }

    unsigned DynamicPlacements::AddString(StringSection<ResChar> str)
    {
        unsigned result = ~unsigned(0x0);
        auto stringHash = Hash64(str.begin(), str.end());

        auto* start = AsPointer(_filenamesBuffer.begin());
        auto* end = AsPointer(_filenamesBuffer.end());
        
        for (auto i=start; i<end && result == ~unsigned(0x0);) {
            auto h = *(uint64_t*)AsPointer(i);
            if (h == stringHash) { result = (unsigned)(ptrdiff_t(i) - ptrdiff_t(start)); }

            i += sizeof(uint64_t);
            i = (uint8*)std::find((const ResChar*)i, (const ResChar*)end, ResChar('\0'));
            i += sizeof(ResChar);
        }

        if (result == ~unsigned(0x0)) {
            result = unsigned(_filenamesBuffer.size());
            auto lengthInBaseChars = str.end() - str.begin();
            _filenamesBuffer.resize(_filenamesBuffer.size() + sizeof(uint64_t) + (lengthInBaseChars + 1) * sizeof(ResChar));
            auto* dest = &_filenamesBuffer[result];
            *(uint64_t*)dest = stringHash;
            XlCopyString((ResChar*)PtrAdd(dest, sizeof(uint64_t)), lengthInBaseChars+1, str);
        }

        return result;
    }

    unsigned DynamicPlacements::AddSupplements(SupplementRange supplements)
    {
        if (supplements.empty()) return 0;

        auto* start = AsPointer(_supplementsBuffer.begin());
        auto* end = AsPointer(_supplementsBuffer.end());
        
        for (auto i=start; i<end;) {
            const auto count = size_t(*i);
            if ((count == supplements.size()) && !XlCompareMemory(i+1, supplements.begin(), count*sizeof(uint64_t)))
                return unsigned(i-start);
            i += 1+count;
        }

        if (_supplementsBuffer.empty())
            _supplementsBuffer.push_back(0);    // sentinal in place 0 (since an offset of '0' is used to mean no supplements)

        auto r = _supplementsBuffer.size();
        _supplementsBuffer.push_back(supplements.size());
        _supplementsBuffer.insert(_supplementsBuffer.end(), supplements.begin(), supplements.end());
        return unsigned(r);
    }

    uint64_t DynamicPlacements::AddPlacement(
        const Float3x4& objectToCell,
        const std::pair<Float3, Float3>& cellSpaceBoundary,
        StringSection<ResChar> modelFilename, StringSection<ResChar> materialFilename,
        SupplementRange supplements,
        uint64_t objectGuid)
    {
		assert(modelFilename.Length() > 0);
        ObjectReference newReference;
        newReference._localToCell = objectToCell;
        newReference._cellSpaceBoundary = cellSpaceBoundary;
        newReference._modelFilenameOffset = AddString(modelFilename);
        newReference._materialFilenameOffset = AddString(materialFilename);
        newReference._supplementsOffset = AddSupplements(supplements);
        newReference._guid = objectGuid;
        ScaleRotationTranslationM decomposed(objectToCell);
        newReference._decomposedRotation = decomposed._rotation;
        newReference._decomposedScale = decomposed._scale;

            // Insert the new object in sorted order
            //  We're sorting by GUID, which is an arbitrary random number. So the final
            //  order should end up very arbitrary. We could alternatively also sort by model name
            //  (or just encode the model name into to guid somehow)
        auto i = std::lower_bound(_objects.begin(), _objects.end(), newReference, 
            [](const ObjectReference& lhs, const ObjectReference& rhs) { return lhs._guid < rhs._guid; });
        assert(i == _objects.end() || i->_guid != newReference._guid);  // hitting this means a GUID collision. Should be extremely unlikely
        _objects.insert(i, newReference);

        return newReference._guid;
    }

    bool DynamicPlacements::HasObject(uint64_t guid)
    {
        ObjectReference dummy;
        XlZeroMemory(dummy);
        dummy._guid = guid;
        auto i = std::lower_bound(_objects.begin(), _objects.end(), dummy, 
            [](const ObjectReference& lhs, const ObjectReference& rhs) { return lhs._guid < rhs._guid; });
        return (i != _objects.end() && i->_guid == guid);
    }

    DynamicPlacements::DynamicPlacements(const Placements& copyFrom)
        : Placements(copyFrom)
    {}

    DynamicPlacements::DynamicPlacements() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    struct CompareFilenameHash
    {
    public:
        bool operator()(const PlacementCell& lhs, const PlacementCell& rhs) const   { return lhs._filenameHash < rhs._filenameHash; }
        bool operator()(const PlacementCell& lhs, uint64_t rhs) const                 { return lhs._filenameHash < rhs; }
        bool operator()(uint64_t lhs, const PlacementCell& rhs) const                 { return lhs < rhs._filenameHash; }
    };

    class PlacementsEditor::Pimpl
    {
    public:
        std::vector<std::pair<uint64_t, std::shared_ptr<DynamicPlacements>>> _dynPlacements;

        std::shared_ptr<PlacementsCache>    _placementsCache;
        std::shared_ptr<PlacementsModelCache>         _modelCache;
        std::shared_ptr<PlacementCellSet>   _cellSet;
        std::shared_ptr<PlacementsManager>  _manager;

        std::shared_ptr<DynamicPlacements>  GetDynPlacements(uint64_t cellGuid);
        Float3x4                            GetCellToWorld(uint64_t cellGuid);
        const PlacementCell*                GetCell(uint64_t cellGuid);
    };

    const PlacementCell* PlacementsEditor::Pimpl::GetCell(uint64_t cellGuid)
    {
        auto p = std::lower_bound(_cellSet->_pimpl->_cells.cbegin(), _cellSet->_pimpl->_cells.cend(), cellGuid, CompareFilenameHash());
        if (p != _cellSet->_pimpl->_cells.end() && p->_filenameHash == cellGuid)
            return AsPointer(p);
        return nullptr;
    }

    Float3x4 PlacementsEditor::Pimpl::GetCellToWorld(uint64_t cellGuid)
    {
        auto p = std::lower_bound(_cellSet->_pimpl->_cells.cbegin(), _cellSet->_pimpl->_cells.cend(), cellGuid, CompareFilenameHash());
        if (p != _cellSet->_pimpl->_cells.end() && p->_filenameHash == cellGuid)
            return p->_cellToWorld;
        return Identity<Float3x4>();
    }

    static const Placements* GetPlacements(const PlacementCell& cell, const PlacementCellSet& set, PlacementsCache& cache)
    {
        auto* ovr = set._pimpl->GetOverride(cell._filenameHash);
        if (ovr) return ovr;

            //  We can get an invalid resource here. It probably means the file
            //  doesn't exist -- which can happen with an uninitialized data
            //  directory.
        assert(cell._filename[0]);

		if (cell._filename[0] != '[') {		// used in the editor for dynamic placements
			TRY {
                auto* i = cache.Get(cell._filenameHash, cell._filename);
				return i ? i->_placements.get() : nullptr;
			} CATCH (const std::exception& e) {
                Log(Warning) << "Got invalid resource while loading placements file (" << cell._filename << "). Error: (" << e.what() << ")." << std::endl;
			} CATCH_END
		}
		return nullptr;
    }

    std::shared_ptr<DynamicPlacements> PlacementsEditor::Pimpl::GetDynPlacements(uint64_t cellGuid)
    {
        auto p = LowerBound(_dynPlacements, cellGuid);
        if (p == _dynPlacements.end() || p->first != cellGuid) {
            std::shared_ptr<DynamicPlacements> placements;

                //  We can get an invalid resource here. It probably means the file
                //  doesn't exist -- which can happen with an uninitialized data
                //  directory.
            auto cell = GetCell(cellGuid);
            assert(cell && cell->_filename[0]);

			if (cell->_filename[0] != '[') {		// used in the editor for dynamic placements
				TRY {
					auto& sourcePlacements = Assets::Legacy::GetAsset<Placements>(cell->_filename);
					placements = std::make_shared<DynamicPlacements>(sourcePlacements);
				} CATCH (const std::exception& e) {
					Log(Warning) << "Got invalid resource while loading placements file (" << cell->_filename << "). If this file exists, but is corrupted, the next save will overwrite it. Error: (" << e.what() << ")." << std::endl;
				} CATCH_END
			}

            if (!placements)
                placements = std::make_shared<DynamicPlacements>();
            _cellSet->_pimpl->SetOverride(cellGuid, placements);
            p = _dynPlacements.insert(p, std::make_pair(cellGuid, std::move(placements)));
        }

        return p->second;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class PlacementsIntersections::Pimpl
    {
    public:
        void Find_RayIntersection(
            const PlacementCellSet& set,
            std::vector<PlacementGUID>& result,
            const PlacementCell& cell,
            const std::pair<Float3, Float3>& cellSpaceRay,
            const std::function<bool(const IntersectionDef&)>& predicate);

        void Find_FrustumIntersection(
            const PlacementCellSet& set,
            std::vector<PlacementGUID>& result,
            const PlacementCell& cell,
            const Float4x4& cellToProjection,
            const std::function<bool(const IntersectionDef&)>& predicate);

        void Find_BoxIntersection(
            const PlacementCellSet& set,
            std::vector<PlacementGUID>& result,
            const PlacementCell& cell,
            const std::pair<Float3, Float3>& cellSpaceBB,
            const std::function<bool(const IntersectionDef&)>& predicate);

        std::shared_ptr<PlacementsCache> _placementsCache;
        std::shared_ptr<PlacementsModelCache> _modelCache;
    };

    void PlacementsIntersections::Pimpl::Find_RayIntersection(
        const PlacementCellSet& set,
        std::vector<PlacementGUID>& result, const PlacementCell& cell,
        const std::pair<Float3, Float3>& cellSpaceRay,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
        auto* p = GetPlacements(cell, set, *_placementsCache);
        if (!p) return;

        for (unsigned c=0; c<p->GetObjectReferenceCount(); ++c) {
            auto& obj = p->GetObjectReferences()[c];
                //  We're only doing a very rough world space bounding box vs ray test here...
                //  Ideally, we should follow up with a more accurate test using the object local
                //  space bounding box
            if (!RayVsAABB(cellSpaceRay, obj._cellSpaceBoundary.first, obj._cellSpaceBoundary.second))
                continue;

            Placements::BoundingBox localBoundingBox;
            auto assetState = TryGetBoundingBox(
                localBoundingBox, *_modelCache, 
                (const ResChar*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset + sizeof(uint64_t)));

                // When assets aren't yet ready, we can't perform any intersection tests on them
            if (assetState != ::Assets::AssetState::Ready)
                continue;

            auto decomTranslation = ExtractTranslation(obj._localToCell);
            std::pair<Float3, Float3> localRay { cellSpaceRay.first - decomTranslation, cellSpaceRay.second - decomTranslation };
            localRay.first = Transpose(obj._decomposedRotation) * localRay.first;
            localRay.second = Transpose(obj._decomposedRotation) * localRay.second;
            localRay.first = Float3{localRay.first[0]/obj._decomposedScale[0], localRay.first[1]/obj._decomposedScale[1], localRay.first[2]/obj._decomposedScale[2]};
            localRay.second = Float3{localRay.second[0]/obj._decomposedScale[0], localRay.second[1]/obj._decomposedScale[1], localRay.second[2]/obj._decomposedScale[2]};

            if (!RayVsAABB(localRay, localBoundingBox.first, localBoundingBox.second)) {
                continue;
            }

            if (predicate) {
                IntersectionDef def;
                def._localToWorld = Combine(obj._localToCell, cell._cellToWorld);

                    // note -- we have access to the cell space bounding box. But the local
                    //          space box would be better.
                def._localSpaceBoundingBox = localBoundingBox;
                def._model = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset);
                def._material = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._materialFilenameOffset);

                    // allow the predicate to exclude this item
                if (!predicate(def)) { continue; }
            }

            result.push_back(std::make_pair(cell._filenameHash, obj._guid));
        }
    }

    void PlacementsIntersections::Pimpl::Find_FrustumIntersection(
        const PlacementCellSet& set,
        std::vector<PlacementGUID>& result,
        const PlacementCell& cell,
        const Float4x4& cellToProjection,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
        auto* p = GetPlacements(cell, set, *_placementsCache);
        if (!p) return;

        for (unsigned c=0; c<p->GetObjectReferenceCount(); ++c) {
            auto& obj = p->GetObjectReferences()[c];
                //  We're only doing a very rough world space bounding box vs ray test here...
                //  Ideally, we should follow up with a more accurate test using the object loca
                //  space bounding box
            if (CullAABB(cellToProjection, obj._cellSpaceBoundary.first, obj._cellSpaceBoundary.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                continue;
            }

            Placements::BoundingBox localBoundingBox;
            auto assetState = TryGetBoundingBox(
                localBoundingBox, *_modelCache, 
                (const ResChar*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset + sizeof(uint64_t)));

                // When assets aren't yet ready, we can't perform any intersection tests on them
            if (assetState != ::Assets::AssetState::Ready)
                continue;

            if (CullAABB(Combine(AsFloat4x4(obj._localToCell), cellToProjection), localBoundingBox.first, localBoundingBox.second, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                continue;
            }

            if (predicate) {
                IntersectionDef def;
                def._localToWorld = Combine(obj._localToCell, cell._cellToWorld);

                    // note -- we have access to the cell space bounding box. But the local
                    //          space box would be better.
                def._localSpaceBoundingBox = localBoundingBox;
                def._model = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset);
                def._material = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._materialFilenameOffset);

                    // allow the predicate to exclude this item
                if (!predicate(def)) { continue; }
            }

            result.push_back(std::make_pair(cell._filenameHash, obj._guid));
        }
    }

    void PlacementsIntersections::Pimpl::Find_BoxIntersection(
        const PlacementCellSet& set,
        std::vector<PlacementGUID>& result,
        const PlacementCell& cell,
        const std::pair<Float3, Float3>& cellSpaceBB,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
        auto* p = GetPlacements(cell, set, *_placementsCache);
        if (!p) return;

        for (unsigned c=0; c<p->GetObjectReferenceCount(); ++c) {
            auto& obj = p->GetObjectReferences()[c];
            if (   cellSpaceBB.second[0] < obj._cellSpaceBoundary.first[0]
                || cellSpaceBB.second[1] < obj._cellSpaceBoundary.first[1]
                || cellSpaceBB.second[2] < obj._cellSpaceBoundary.first[2]
                || cellSpaceBB.first[0]  > obj._cellSpaceBoundary.second[0]
                || cellSpaceBB.first[1]  > obj._cellSpaceBoundary.second[1]
                || cellSpaceBB.first[2]  > obj._cellSpaceBoundary.second[2]) {
                continue;
            }

            if (predicate) {
                IntersectionDef def;
                def._localToWorld = Combine(obj._localToCell, cell._cellToWorld);

                Placements::BoundingBox localBoundingBox;
                auto assetState = TryGetBoundingBox(
                    localBoundingBox, *_modelCache, 
                    (const ResChar*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset + sizeof(uint64_t)));

                    // When assets aren't yet ready, we can't perform any intersection tests on them
                if (assetState != ::Assets::AssetState::Ready)
                    continue;

                    // note -- we have access to the cell space bounding box. But the local
                    //          space box would be better.
                def._localSpaceBoundingBox = localBoundingBox;
                def._model = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._modelFilenameOffset);
                def._material = *(uint64_t*)PtrAdd(p->GetFilenamesBuffer(), obj._materialFilenameOffset);
                        

                    // allow the predicate to exclude this item
                if (!predicate(def)) { continue; }
            }

            result.push_back(std::make_pair(cell._filenameHash, obj._guid));
        }
    }

    std::vector<PlacementGUID> PlacementsIntersections::Find_RayIntersection(
        const PlacementCellSet& cellSet,
        const Float3& rayStart, const Float3& rayEnd,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
        std::vector<PlacementGUID> result;
        const float placementAssumedMaxRadius = 100.f;
        for (auto i=cellSet._pimpl->_cells.cbegin(); i!=cellSet._pimpl->_cells.cend(); ++i) {
            Float3 cellMin = i->_aabbMin - Float3(placementAssumedMaxRadius, placementAssumedMaxRadius, placementAssumedMaxRadius);
            Float3 cellMax = i->_aabbMax + Float3(placementAssumedMaxRadius, placementAssumedMaxRadius, placementAssumedMaxRadius);
            if (!RayVsAABB(std::make_pair(rayStart, rayEnd), cellMin, cellMax))
                continue;

                // We need to suppress any exception that occurs (we can get invalid/pending assets here)
                // \todo -- we need to prepare all shaders and assets required here. It's better to stall
                //      and load the asset than it is to miss an intersection
            auto worldToCell = InvertOrthonormalTransform(i->_cellToWorld);
            TRY {
                _pimpl->Find_RayIntersection(
                    cellSet,
                    result, *i, 
                    std::make_pair(
                        TransformPoint(worldToCell, rayStart),
                        TransformPoint(worldToCell, rayEnd)), 
                    predicate);
            } 
            CATCH (const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END
        }

        return std::move(result);
    }

    std::vector<PlacementGUID> PlacementsIntersections::Find_FrustumIntersection(
        const PlacementCellSet& cellSet,
        const Float4x4& worldToProjection,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
        std::vector<PlacementGUID> result;
        const float placementAssumedMaxRadius = 100.f;
        for (auto i=cellSet._pimpl->_cells.cbegin(); i!=cellSet._pimpl->_cells.cend(); ++i) {
            Float3 cellMin = i->_aabbMin - Float3(placementAssumedMaxRadius, placementAssumedMaxRadius, placementAssumedMaxRadius);
            Float3 cellMax = i->_aabbMax + Float3(placementAssumedMaxRadius, placementAssumedMaxRadius, placementAssumedMaxRadius);
            if (CullAABB(worldToProjection, cellMin, cellMax, RenderCore::Techniques::GetDefaultClipSpaceType())) {
                continue;
            }

            auto cellToProjection = Combine(i->_cellToWorld, worldToProjection);

            TRY { _pimpl->Find_FrustumIntersection(cellSet, result, *i, cellToProjection, predicate); } 
            CATCH (const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END
        }

        return std::move(result);
    }

    std::vector<PlacementGUID> PlacementsIntersections::Find_BoxIntersection(
        const PlacementCellSet& cellSet,
        const Float3& worldSpaceMins, const Float3& worldSpaceMaxs,
        const std::function<bool(const IntersectionDef&)>& predicate)
    {
            //  Look through all placements to find any that intersect with the given
            //  world space bounding box. 
            //
            //  Note that there's a potential issue here -- the world space bounding
            //  box of the cell isn't updated when the dynamic placements change. So
            //  it's possible that some dynamic placements might intersect with our
            //  test bounding box, but not the cell bounding box... We have to be 
            //  careful about this. It might mean that we have to test more cells than
            //  expected.

        std::vector<PlacementGUID> result;

        const float placementAssumedMaxRadius = 100.f;
        for (auto i=cellSet._pimpl->_cells.cbegin(); i!=cellSet._pimpl->_cells.cend(); ++i) {
            if (    worldSpaceMaxs[0] < (i->_aabbMin[0] - placementAssumedMaxRadius)
                ||  worldSpaceMaxs[1] < (i->_aabbMin[1] - placementAssumedMaxRadius)
                ||  worldSpaceMins[0] > (i->_aabbMax[0] + placementAssumedMaxRadius)
                ||  worldSpaceMins[1] > (i->_aabbMax[1] + placementAssumedMaxRadius)) {
                continue;
            }

                //  This cell intersects with the bounding box (or almost does).
                //  We have to test all internal objects. First, transform the bounding
                //  box into local cell space.
            auto cellSpaceBB = TransformBoundingBox(
                InvertOrthonormalTransform(i->_cellToWorld),
                std::make_pair(worldSpaceMins, worldSpaceMaxs));

                //  We need to use the renderer to get either the asset or the 
                //  override placements associated with this cell. It's a little awkward
                //  Note that we could use the quad tree to acceleration these tests.
            TRY { _pimpl->Find_BoxIntersection(cellSet, result, *i, cellSpaceBB, predicate); } 
            CATCH (const ::Assets::Exceptions::RetrievalError&) {}
            CATCH_END
        }

        return std::move(result);
    }

    PlacementsIntersections::PlacementsIntersections(
        std::shared_ptr<PlacementsCache> placementsCache, 
        std::shared_ptr<PlacementsModelCache> modelCache)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_placementsCache = std::move(placementsCache);
        _pimpl->_modelCache = std::move(modelCache);
    }

    PlacementsIntersections::~PlacementsIntersections() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class CompareObjectId
    {
    public:
        bool operator()(const Placements::ObjectReference& lhs, uint64_t rhs) { return lhs._guid < rhs; }
        bool operator()(uint64_t lhs, const Placements::ObjectReference& rhs) { return lhs < rhs._guid; }
        bool operator()(const Placements::ObjectReference& lhs, const Placements::ObjectReference& rhs) { return lhs._guid < rhs._guid; }
    };

    class Transaction : public PlacementsEditor::ITransaction
    {
    public:
        typedef PlacementsEditor::ObjTransDef ObjTransDef;
        typedef PlacementsEditor::PlacementsTransform PlacementsTransform;

        const ObjTransDef&  GetObject(unsigned index) const;
        const ObjTransDef&  GetObjectOriginalState(unsigned index) const;
        PlacementGUID       GetGuid(unsigned index) const;
        PlacementGUID       GetOriginalGuid(unsigned index) const;
        unsigned            GetObjectCount() const;
        std::pair<Float3, Float3>   GetLocalBoundingBox(unsigned index) const;
        std::pair<Float3, Float3>   GetWorldBoundingBox(unsigned index) const;
        std::string         GetMaterialName(unsigned objectIndex, uint64_t materialGuid) const;

        virtual void        SetObject(unsigned index, const ObjTransDef& newState);

        virtual bool        Create(const ObjTransDef& newState);
        virtual bool        Create(PlacementGUID guid, const ObjTransDef& newState);
        virtual void        Delete(unsigned index);

        virtual void    Commit();
        virtual void    Cancel();
        virtual void    UndoAndRestart();

        Transaction(
            PlacementsEditor::Pimpl*    editorPimpl,
            const PlacementGUID*        placementsBegin,
            const PlacementGUID*        placementsEnd,
            PlacementsEditor::TransactionFlags::BitField transactionFlags = 0);
        ~Transaction();

    protected:
        PlacementsEditor::Pimpl*    _editorPimpl;

        std::vector<ObjTransDef>    _originalState;
        std::vector<ObjTransDef>    _objects;

        std::vector<PlacementGUID>  _originalGuids;
        std::vector<PlacementGUID>  _pushedGuids;

        void PushObj(unsigned index, const ObjTransDef& newState);

        bool GetLocalBoundingBox_Stall(
            std::pair<Float3, Float3>& result,
            const ResChar filename[]) const;

        enum State { Active, Committed };
        State _state;
    };

    auto    Transaction::GetObject(unsigned index) const -> const ObjTransDef&              { return _objects[index]; }
    auto    Transaction::GetObjectOriginalState(unsigned index) const -> const ObjTransDef& { return _originalState[index]; }
    auto    Transaction::GetGuid(unsigned index) const -> PlacementGUID                     { return _pushedGuids[index]; }
    auto    Transaction::GetOriginalGuid(unsigned index) const -> PlacementGUID             { return _originalGuids[index]; }

    unsigned    Transaction::GetObjectCount() const
    {
        assert(_originalGuids.size() == _originalState.size());
        assert(_originalGuids.size() == _objects.size());
        assert(_originalGuids.size() == _pushedGuids.size());
        return (unsigned)_originalGuids.size();
    }

    bool Transaction::GetLocalBoundingBox_Stall(std::pair<Float3, Float3>& result, const ResChar filename[]) const
    {
            // get the local bounding box for a model
            // ... but stall waiting for any pending resources
        auto model = _editorPimpl->_modelCache->GetModelScaffold(filename);
        auto state = model->StallWhilePending();
        if (state != ::Assets::AssetState::Ready) {
            result = std::make_pair(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));
            return false;
        }

        result = model->Actualize()->GetStaticBoundingBox();
        return true;
    }

    std::pair<Float3, Float3>   Transaction::GetLocalBoundingBox(unsigned index) const
    {
        std::pair<Float3, Float3> result;
        GetLocalBoundingBox_Stall(result, _objects[index]._model.c_str());
        return result;
    }

    std::pair<Float3, Float3>   Transaction::GetWorldBoundingBox(unsigned index) const
    {
        auto guid = _pushedGuids[index];
        auto cellToWorld = _editorPimpl->GetCellToWorld(guid.first);
        const Placements* placements = nullptr;
        auto* cell = _editorPimpl->GetCell(guid.first);
        if (cell)
            placements = GetPlacements(*cell, *_editorPimpl->_cellSet, *_editorPimpl->_placementsCache);
        if (!placements) return std::make_pair(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));

        auto count = placements->GetObjectReferenceCount();
        auto* objects = placements->GetObjectReferences();

        auto dst = std::lower_bound(objects, &objects[count], guid.second, CompareObjectId());
        return TransformBoundingBox(cellToWorld, dst->_cellSpaceBoundary);
    }

    std::string Transaction::GetMaterialName(unsigned objectIndex, uint64_t materialGuid) const
    {
        if (objectIndex >= _objects.size()) return std::string();

            // attempt to get the 
        auto scaff = _editorPimpl->_modelCache->GetMaterialScaffold(
            MakeStringSection(_objects[objectIndex]._material),
			MakeStringSection(_objects[objectIndex]._model));
        if (!scaff) return std::string();

		auto actual = scaff->TryActualize();
		if (!actual) return std::string();

        return (*actual)->GetMaterialName(materialGuid).AsString();
    }

    void    Transaction::SetObject(unsigned index, const ObjTransDef& newState)
    {
        auto& currentState = _objects[index];
        auto currTrans = currentState._transaction;
        if (currTrans != ObjTransDef::Deleted) {
			currentState = newState;
            currentState._transaction = (currTrans == ObjTransDef::Created || currTrans == ObjTransDef::Error) ? ObjTransDef::Created : ObjTransDef::Modified;
            PushObj(index, currentState);
        }
    }

    static bool CompareGUID(const PlacementGUID& lhs, const PlacementGUID& rhs)
    {
        if (lhs.first == rhs.first) { return lhs.second < rhs.second; }
        return lhs.first < rhs.first;
    }

    static uint32 EverySecondBit(uint64_t input)
    {
        uint32 result = 0;
        for (unsigned c=0; c<32; ++c) {
            result |= uint32((input >> (uint64_t(c)*2ull)) & 0x1ull)<<c;
        }
        return result;
    }

    static uint64_t ObjectIdTopPart(const std::string& model, const std::string& material)
    {
        auto modelAndMaterialHash = Hash64(model, Hash64(material));
        return uint64_t(EverySecondBit(modelAndMaterialHash)) << 32ull;
    }
    
    static std::vector<uint64_t> StringToSupplementGuids(const char stringNames[])
    {
        if (!stringNames || !*stringNames) return std::vector<uint64_t>();

        std::vector<uint64_t> result;
        const auto* i = stringNames;
        const auto* end = XlStringEnd(stringNames);
        for (;;) {
            auto comma = XlFindChar(i, ',');
            if (!comma) comma = end;
            if (i == comma) break;

                // if the string is exactly a hex number, then
                // will we just use that value. (otherwise we
                // need to hash the string)
            const char* parseEnd = nullptr;
            auto hash = XlAtoUI64(i, &parseEnd, 16);
            if (parseEnd != comma)
                hash = ConstHash64FromString(i, comma);

            result.push_back(hash);
            i = comma;
        }
        return std::move(result);
    }

    static std::string SupplementsGuidsToString(SupplementRange guids)
    {
        if (guids.empty()) return std::string();

        std::stringstream str;
        const auto* i = guids.begin();
        str << std::hex << *i++;
        for (;i<guids.end(); ++i)
            str << ',' << std::hex << *i;
        return str.str();
    }

    bool    Transaction::Create(const ObjTransDef& newState)
    {
        //  Add a new placement with the given transformation
        //  * first, we need to look for the cell that is registered at this location
        //  * if there is a dynamic placements object already created for that cell,
        //      then we can just add it to the dynamic placements object.
        //  * otherwise, we need to create a new dynamic placements object (which will
        //      be initialized with the static placements)
        //
        //  Note that we're going to need to know the bounding box for this model,
        //  whatever happens. So, if the first thing we can do is load the scaffold
        //  to get at the bounding box and use the center point of that box to search
        //  for the right cell.
        //
        //  Objects that straddle a cell boundary must be placed in only one of those
        //  cells -- so sometimes objects will stick out the side of a cell.

        std::pair<Float3, Float3> boundingBox;
        if (!GetLocalBoundingBox_Stall(boundingBox, newState._model.c_str())) {
                // if we can't get a bounding box, then we can't really 
                // create this object. We need to cancel the creation operation
            return false;
        }

        auto boundingBoxCentre = LinearInterpolate(boundingBox.first, boundingBox.second, 0.5f);
        auto worldSpaceCenter = TransformPoint(newState._localToWorld, boundingBoxCentre);

        std::string materialFilename = newState._material;

        PlacementGUID guid(0, 0);
        PlacementsTransform localToCell = Identity<PlacementsTransform>();

        auto& cells = _editorPimpl->_cellSet->_pimpl->_cells;
        for (auto i=cells.cbegin(); i!=cells.cend(); ++i) {
            if (    worldSpaceCenter[0] >= i->_captureMins[0] && worldSpaceCenter[0] < i->_captureMaxs[0]
                &&  worldSpaceCenter[1] >= i->_captureMins[1] && worldSpaceCenter[1] < i->_captureMaxs[1]) {
                
                    // This is the correct cell. Look for a dynamic placement associated
                auto dynPlacements = _editorPimpl->GetDynPlacements(i->_filenameHash);

                localToCell = Combine(newState._localToWorld, InvertOrthonormalTransform(i->_cellToWorld));
                
                    //  Build a GUID for this object. We're going to sort by GUID, and we want
                    //  objects with the name model and material to appear together. So let's
                    //  build the top 32 bits from the model and material hash. The bottom 
                    //  32 bits can be a random number.
                    //  Note that it's possible that the bottom 32 bits could collide with an
                    //  existing object. It's unlikely, but possible. So let's make sure we
                    //  have a unique GUID before we add it.
                uint64_t id, idTopPart = ObjectIdTopPart(newState._model, materialFilename);
                for (;;) {
                    auto id32 = BuildGuid32();
                    id = idTopPart | uint64_t(id32);
                    if (!dynPlacements->HasObject(id)) { break; }
                }

                auto suppGuid = StringToSupplementGuids(newState._supplements.c_str());
                dynPlacements->AddPlacement(
                    localToCell, TransformBoundingBox(localToCell, boundingBox),
                    MakeStringSection(newState._model), MakeStringSection(materialFilename), 
                    MakeIteratorRange(suppGuid), id);

                guid = PlacementGUID(i->_filenameHash, id);
                break;

            }
        }

        if (guid.first == 0 && guid.second == 0) return false;    // couldn't find a way to create this object
        
        ObjTransDef newObj = newState;
        newObj._transaction = ObjTransDef::Created;

        ObjTransDef originalState;
        originalState._localToWorld = Identity<decltype(originalState._localToWorld)>();
        originalState._transaction = ObjTransDef::Error;

        auto insertLoc = std::lower_bound(_originalGuids.begin(), _originalGuids.end(), guid, CompareGUID);
        auto insertIndex = std::distance(_originalGuids.begin(), insertLoc);

        _originalState.insert(_originalState.begin() + insertIndex, originalState);
        _objects.insert(_objects.begin() + insertIndex, newObj);
        _originalGuids.insert(_originalGuids.begin() + insertIndex, guid);
        _pushedGuids.insert(_pushedGuids.begin() + insertIndex, guid);

        return true;
    }

    bool    Transaction::Create(PlacementGUID guid, const ObjTransDef& newState)
    {
        std::pair<Float3, Float3> boundingBox;
        if (!GetLocalBoundingBox_Stall(boundingBox, newState._model.c_str())) {
                // if we can't get a bounding box, then we can't really 
                // create this object. We need to cancel the creation operation
            return false;
        }

        auto boundingBoxCentre = LinearInterpolate(boundingBox.first, boundingBox.second, 0.5f);
        auto worldSpaceCenter = TransformPoint(newState._localToWorld, boundingBoxCentre);

        std::string materialFilename = newState._material;

        PlacementsTransform localToCell = Identity<PlacementsTransform>();
        bool foundCell = false;

        auto& cells = _editorPimpl->_cellSet->_pimpl->_cells;
        for (auto i=cells.cbegin(); i!=cells.cend(); ++i) {
            if (i->_filenameHash == guid.first) {
                auto dynPlacements = _editorPimpl->GetDynPlacements(i->_filenameHash);
                localToCell = Combine(newState._localToWorld, InvertOrthonormalTransform(i->_cellToWorld));

                auto idTopPart = ObjectIdTopPart(newState._model, materialFilename);
                uint64_t id = idTopPart | uint64_t(guid.second & 0xffffffffull);
                if (dynPlacements->HasObject(id)) {
                    assert(0);      // got a hash collision or duplicated id
                    return false;
                }

                auto supp = StringToSupplementGuids(newState._supplements.c_str());
                dynPlacements->AddPlacement(
                    localToCell, TransformBoundingBox(localToCell, boundingBox),
                    MakeStringSection(newState._model), MakeStringSection(materialFilename), 
                    MakeIteratorRange(supp), id);

                guid.second = id;
                foundCell = true;
                break;
            }
        }
        if (!foundCell) return false;    // couldn't find a way to create this object
        
        ObjTransDef newObj = newState;
        newObj._transaction = ObjTransDef::Created;

        ObjTransDef originalState;
        originalState._localToWorld = Identity<decltype(originalState._localToWorld)>();
        originalState._transaction = ObjTransDef::Error;

        auto insertLoc = std::lower_bound(_originalGuids.begin(), _originalGuids.end(), guid, CompareGUID);
        auto insertIndex = std::distance(_originalGuids.begin(), insertLoc);

        _originalState.insert(_originalState.begin() + insertIndex, originalState);
        _objects.insert(_objects.begin() + insertIndex, newObj);
        _originalGuids.insert(_originalGuids.begin() + insertIndex, guid);
        _pushedGuids.insert(_pushedGuids.begin() + insertIndex, guid);

        return true;
    }

    void    Transaction::Delete(unsigned index)
    {
		if (_objects[index]._transaction != ObjTransDef::Error) {
			_objects[index]._transaction = ObjTransDef::Deleted;
			PushObj(index, _objects[index]);
		}
    }

    void Transaction::PushObj(unsigned index, const ObjTransDef& newState)
    {
            // update the DynPlacements object with the changes to the object at index "index"
        std::vector<ObjTransDef> originalState;
        
        auto& guid = _pushedGuids[index];

        auto cellToWorld = _editorPimpl->GetCellToWorld(guid.first);
        auto dynPlacements = _editorPimpl->GetDynPlacements(guid.first);
        auto& objects = dynPlacements->GetObjects();

        auto dst = std::lower_bound(objects.begin(), objects.end(), guid.second, CompareObjectId());

        std::pair<Float3, Float3> cellSpaceBoundary;
        PlacementsTransform localToCell;
        std::string materialFilename = newState._material;
        if (newState._transaction != ObjTransDef::Deleted && newState._transaction != ObjTransDef::Error) {
            localToCell = Combine(newState._localToWorld, InvertOrthonormalTransform(cellToWorld));

            std::pair<Float3, Float3> boundingBox;
            if (GetLocalBoundingBox_Stall(boundingBox, newState._model.c_str())) {
                cellSpaceBoundary = TransformBoundingBox(localToCell, boundingBox);
            } else {
                Log(Warning) << "Cannot get bounding box for model (" << newState._model << ") while updating placement object." << std::endl;
                cellSpaceBoundary = std::make_pair(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));
            }
        }

            // todo --  handle the case where an object should move to another cell!
            //          this should actually change the first part of the GUID
            //          Also, if the type of the object changes, it should change the guid... Which
            //          means that it should change location in the list of objects. In this case
            //          we should erase the old object and create a new one

        bool isDeleteOp = newState._transaction == ObjTransDef::Deleted || newState._transaction == ObjTransDef::Error;
        bool destroyExisting = isDeleteOp;
        bool hasExisting = dst != objects.end() && dst->_guid == guid.second;

            // awkward case where the object id has changed... This can happen
            // if the object model or material was changed
        auto newIdTopPart = ObjectIdTopPart(newState._model, materialFilename);
        bool objectIdChanged = newIdTopPart != (guid.second & 0xffffffff00000000ull);
        if (objectIdChanged) {
            auto id32 = uint32(guid.second);
            for (;;) {
                guid.second = newIdTopPart | uint64_t(id32);
                if (!dynPlacements->HasObject(guid.second)) { break; }
                id32 = BuildGuid32();
            }

                // destroy & re-create
            destroyExisting = true;
        }

        if (destroyExisting && hasExisting) {
            objects.erase(dst);
            hasExisting = false;
        } 
        
        if (!isDeleteOp) {
            auto suppGuids = StringToSupplementGuids(newState._supplements.c_str());
            if (hasExisting) {
                dst->_localToCell = localToCell;
                dst->_modelFilenameOffset = dynPlacements->AddString(MakeStringSection(newState._model));
                dst->_materialFilenameOffset = dynPlacements->AddString(MakeStringSection(materialFilename));
                dst->_supplementsOffset = dynPlacements->AddSupplements(MakeIteratorRange(suppGuids));
                dst->_cellSpaceBoundary = cellSpaceBoundary;
                ScaleRotationTranslationM decomp(localToCell);
                dst->_decomposedRotation = decomp._rotation;
                dst->_decomposedScale = decomp._scale;
            } else {
                dynPlacements->AddPlacement(
                    localToCell, cellSpaceBoundary, 
                    MakeStringSection(newState._model), MakeStringSection(materialFilename), 
                    MakeIteratorRange(suppGuids), guid.second);
            }
        }
    }

    void    Transaction::Commit()
    {
        _state = Committed;
    }

    void    Transaction::Cancel()
    {
        if (_state == Active) {
                // we need to revert all of the objects to their original state
            UndoAndRestart();
        }

        _state = Committed;
    }

    void    Transaction::UndoAndRestart()
    {
        if (_state != Active) return;

            // we just have to reset all objects to their previous state
        for (unsigned c=0; c<_objects.size(); ++c) {
            _objects[c] = _originalState[c];
            PushObj(c, _originalState[c]);
        }
    }
    
    Transaction::Transaction(
        PlacementsEditor::Pimpl*    editorPimpl,
        const PlacementGUID*        guidsBegin,
        const PlacementGUID*        guidsEnd,
        PlacementsEditor::TransactionFlags::BitField transactionFlags)
    {
            //  We need to sort; because this method is mostly assuming we're working
            //  with a sorted list. Most of the time originalPlacements will be close
            //  to sorted order (which, of course, means that quick sort isn't ideal, but, anyway...)
        auto guids = std::vector<PlacementGUID>(guidsBegin, guidsEnd);
        std::sort(guids.begin(), guids.end(), CompareGUID);

        std::vector<ObjTransDef> originalState;
        auto& cells = editorPimpl->_cellSet->_pimpl->_cells;
        auto cellIterator = cells.begin();
        for (auto i=guids.begin(); i!=guids.end();) {
            auto iend = std::find_if(i, guids.end(), 
                [&](const PlacementGUID& guid) { return guid.first != i->first; });

            cellIterator = std::lower_bound(cellIterator, cells.end(), i->first, CompareFilenameHash());
            if (cellIterator == cells.end() || cellIterator->_filenameHash != i->first) {
                i = guids.erase(i, iend);
                continue;
            }

            auto cellToWorld = cellIterator->_cellToWorld;
            auto* placements = GetPlacements(*cellIterator, *editorPimpl->_cellSet, *editorPimpl->_placementsCache);
            if (!placements) {
				// If we didn't get an actual "placements" object, it means that nothing has been created
				// in this cell yet (and maybe the original asset is invalid/uncreated).
				// We should treat this the same as if the object didn't exists previously.
				for (; i != iend; ++i) {
					ObjTransDef def;
					def._localToWorld = Identity<decltype(def._localToWorld)>();
					def._transaction = ObjTransDef::Error;
					originalState.push_back(def);
				}
				continue; 
			}

            if (transactionFlags & PlacementsEditor::TransactionFlags::IgnoreIdTop32Bits) {
                    //  Sometimes we want to ignore the top 32 bits of the id. It works, but it's
                    //  much less efficient, because we can't take advantage of the sorting.
                    //  Ideally we should avoid this path
                for (;i != iend; ++i) {
                    uint32 comparison = uint32(i->second);
                    auto pend = &placements->GetObjectReferences()[placements->GetObjectReferenceCount()];
                    auto pIterator = std::find_if(
                        placements->GetObjectReferences(), pend,
                        [=](const Placements::ObjectReference& obj) { return uint32(obj._guid) == comparison; });
                    if (pIterator!=pend) {
                        i->second = pIterator->_guid;       // set the recorded guid to the full guid

                        ObjTransDef def;
                        def._localToWorld = Combine(pIterator->_localToCell, cellToWorld);
                        def._model = (const ResChar*)PtrAdd(placements->GetFilenamesBuffer(), sizeof(uint64_t) + pIterator->_modelFilenameOffset);
                        def._material = (const ResChar*)PtrAdd(placements->GetFilenamesBuffer(), sizeof(uint64_t) + pIterator->_materialFilenameOffset);
                        def._supplements = SupplementsGuidsToString(AsSupplements(placements->GetSupplementsBuffer(), pIterator->_supplementsOffset));
                        def._transaction = ObjTransDef::Unchanged;
                        originalState.push_back(def);
                    } else {
                            // we couldn't find an original for this object. It's invalid
                        ObjTransDef def;
                        def._localToWorld = Identity<decltype(def._localToWorld)>();
                        def._transaction = ObjTransDef::Error;
                        originalState.push_back(def);
                    }
                }
            } else {
                auto pIterator = placements->GetObjectReferences();
                auto pEnd = &placements->GetObjectReferences()[placements->GetObjectReferenceCount()];
                for (;i != iend; ++i) {
                        //  Here, we're assuming everything is sorted, so we can just march forward
                        //  through the destination placements list
                    pIterator = std::lower_bound(pIterator, pEnd, i->second, CompareObjectId());
                    if (pIterator != pEnd && pIterator->_guid == i->second) {
                            // Build a ObjTransDef object from this object, and record it
                        ObjTransDef def;
                        def._localToWorld = Combine(pIterator->_localToCell, cellToWorld);
                        def._model = (const ResChar*)PtrAdd(placements->GetFilenamesBuffer(), sizeof(uint64_t) + pIterator->_modelFilenameOffset);
                        def._material = (const ResChar*)PtrAdd(placements->GetFilenamesBuffer(), sizeof(uint64_t) + pIterator->_materialFilenameOffset);
                        def._supplements = SupplementsGuidsToString(AsSupplements(placements->GetSupplementsBuffer(), pIterator->_supplementsOffset));
                        def._transaction = ObjTransDef::Unchanged;
                        originalState.push_back(def);
                    } else {
                            // we couldn't find an original for this object. It's invalid
                        ObjTransDef def;
                        def._localToWorld = Identity<decltype(def._localToWorld)>();
                        def._transaction = ObjTransDef::Error;
                        originalState.push_back(def);
                    }
                }
            }
        }

        _objects = originalState;
        _originalState = std::move(originalState);
        _originalGuids = guids;
        _pushedGuids = std::move(guids);
        _editorPimpl = editorPimpl;
        _state = Active;
    }

    Transaction::~Transaction()
    {
        if (_state == Active) {
            Cancel();
        }
    }

    uint64_t PlacementsEditor::CreateCell(
        const ::Assets::ResChar name[],
        const Float2& mins, const Float2& maxs)
    {
            //  The implementation here is not great. Originally, PlacementsManager
            //  was supposed to be constructed with all of it's cells already created.
            //  But we need create/delete for the interface with the editor
        PlacementCell newCell;
        XlCopyString(newCell._filename, name);
        newCell._filenameHash = Hash64(newCell._filename);
        newCell._cellToWorld = Identity<decltype(newCell._cellToWorld)>();
        newCell._aabbMin = Expand(mins, -10000.f);
        newCell._aabbMax = Expand(maxs,  10000.f);
        newCell._captureMins = mins;
        newCell._captureMaxs = maxs;
        _pimpl->_cellSet->_pimpl->_cells.push_back(newCell);
        return newCell._filenameHash;
    }

    bool PlacementsEditor::RemoveCell(uint64_t id)
    {
        auto i = std::lower_bound(_pimpl->_cellSet->_pimpl->_cells.begin(), _pimpl->_cellSet->_pimpl->_cells.end(), id, CompareFilenameHash());
        if (i != _pimpl->_cellSet->_pimpl->_cells.end() && i->_filenameHash == id) {
            _pimpl->_cellSet->_pimpl->_cells.erase(i);
            return true;
        }
        return false;
    }

    uint64_t PlacementsEditor::GenerateObjectGUID()
    {
        return uint64_t(BuildGuid32());
    }

	void PlacementsEditor::PerformGUIDFixup(PlacementGUID* begin, PlacementGUID* end) const
	{
		std::sort(begin, end);

		auto ci = _pimpl->_cellSet->_pimpl->_cells.begin();
		for (auto i = begin; i != end;) {
			auto i2 = i + 1;
			for (; i2 != end && i2->first == i->first; ++i2) {}

			while (ci != _pimpl->_cellSet->_pimpl->_cells.end() && ci->_filenameHash < i->first) { ++ci; }

			if (ci != _pimpl->_cellSet->_pimpl->_cells.end() && ci->_filenameHash == i->first) {

				// The ids will usually have their
				// top 32 bit zeroed out. We must fix them by finding the match placements
				// in our cached placements, and fill in the top 32 bits...
				const auto* cachedPlacements = GetPlacements(*ci, *_pimpl->_cellSet, *_pimpl->_placementsCache);
                if (!cachedPlacements) { i = i2; continue; }

				auto count = cachedPlacements->GetObjectReferenceCount();
				const auto* placements = cachedPlacements->GetObjectReferences();

				for (auto i3 = i; i3 < i2; ++i3) {
					auto p = std::find_if(placements, &placements[count],
						[=](const Placements::ObjectReference& obj) { return uint32(obj._guid) == uint32(i3->second); });
                    if (p != &placements[count])
                        i3->second = p->_guid;
				}

			}
            
            i = i2;
		}

			// re-sort again
		std::sort(begin, end);
	}

    std::pair<Float3, Float3> PlacementsEditor::CalculateCellBoundary(uint64_t cellId) const
    {
            // Find the given cell within our list, and calculate the true boundary
            // of all the placements within it
        std::pair<Float3, Float3> result(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));
        const Placements* p = nullptr;
        for (auto i = _pimpl->_dynPlacements.begin(); i!=_pimpl->_dynPlacements.end(); ++i)
            if (i->first == cellId) { p = i->second.get(); break; }
        
        if (!p) 
        {
            auto* c = _pimpl->GetCell(cellId);
            if (c)
                p = GetPlacements(*c, *_pimpl->_cellSet, *_pimpl->_placementsCache);
        }
        if (!p) return result;

        auto*r = p->GetObjectReferences();
        for (unsigned c=0; c<p->GetObjectReferenceCount(); ++c) {
            result.first[0] = std::min(result.first[0], r[c]._cellSpaceBoundary.first[0]);
            result.first[1] = std::min(result.first[1], r[c]._cellSpaceBoundary.first[1]);
            result.first[2] = std::min(result.first[2], r[c]._cellSpaceBoundary.first[2]);
            result.second[0] = std::max(result.second[0], r[c]._cellSpaceBoundary.second[0]);
            result.second[1] = std::max(result.second[1], r[c]._cellSpaceBoundary.second[1]);
            result.second[2] = std::max(result.second[2], r[c]._cellSpaceBoundary.second[2]);
        }

        return result;
    }
    
    static void SavePlacements(const ResChar outputFilename[], Placements& placements)
    {
        placements.Write(outputFilename);
        ConsoleRig::Console::GetInstance().Print(
            StringMeld<256>() << "Writing placements to: " << outputFilename << "\n");
    }

    void PlacementsEditor::WriteAllCells()
    {
            //  Save all of the placement files that have changed. 
            //
            //  Changed placement cells will have a "dynamic" placements object associated.
            //  These should get flushed to disk. Then we can delete the dynamic placements,
            //  because the changed static placements should get automatically reloaded from
            //  disk (making the dynamic placements cells now redundant)
            //
            //  We may need to commit or cancel any active transaction. How do we know
            //  if we need to commit or cancel them?

        for (auto i = _pimpl->_dynPlacements.begin(); i!=_pimpl->_dynPlacements.end(); ++i) {
            auto cellGuid = i->first;
            auto& placements = *i->second;

            const auto* cell = _pimpl->GetCell(cellGuid);
            if (cell) {
                SavePlacements(cell->_filename, placements);

                    // clear the renderer links
                _pimpl->_cellSet->_pimpl->SetOverride(cellGuid, nullptr);
            }
        }

        _pimpl->_dynPlacements.clear();
    }

    void PlacementsEditor::WriteCell(uint64_t cellId, const Assets::ResChar destinationFile[]) const
    {
            // Save a single placement cell file
            // This function is intended for tools, so we will aggressively throw exceptions on errors

        for (auto i=_pimpl->_dynPlacements.begin(); i!=_pimpl->_dynPlacements.end(); ++i) {
            if (i->first != cellId)
                continue;

            auto& placements = *i->second;
            placements.Write(destinationFile);
            return;
        }

        Throw(
            ::Exceptions::BasicLabel("Could not find cell with given id (0x%08x%08x). Saving cancelled",
                uint32(cellId>>32), uint32(cellId)));
    }

    std::string PlacementsEditor::GetMetricsString(uint64_t cellId) const
    {
        auto* cell = _pimpl->GetCell(cellId);
        if (!cell) return "Placements not found";
        auto* placements = GetPlacements(*cell, *_pimpl->_cellSet, *_pimpl->_placementsCache);
        if (!placements) return "Placements not found";

            // create a breakdown of the contents of the placements, showing 
            // some important metrics
        std::stringstream result;
        result << "[Model Name] [Material Name] Count" << std::endl;
        auto count = placements->GetObjectReferenceCount();
        auto* refs = placements->GetObjectReferences();
        const auto* fns = (const char*)placements->GetFilenamesBuffer();
        for (unsigned c=0; c<count;) {
            auto model = refs[c]._modelFilenameOffset;
            auto material = refs[c]._materialFilenameOffset;
            auto supp = refs[c]._supplementsOffset;
            unsigned cend = c+1;
            while (cend < count 
                && refs[cend]._modelFilenameOffset == model && refs[cend]._materialFilenameOffset == material
                && refs[cend]._supplementsOffset == supp)
                ++cend;

            result << "[" << PtrAdd(fns, model + sizeof(uint64_t)) << "] [" << PtrAdd(fns, material + sizeof(uint64_t)) << "] " << (cend - c) << std::endl;
            c = cend;
        }
        
        auto boundary = CalculateCellBoundary(cellId);

        result << std::endl;
        result << "Cell Mins: (" << boundary.first[0] << ", " << boundary.first[1] << ", " << boundary.first[2] << ")" << std::endl;
        result << "Cell Maxs: (" << boundary.second[0] << ", " << boundary.second[1] << ", " << boundary.second[2] << ")" << std::endl;

        return result.str();
    }

    std::pair<Float3, Float3> PlacementsEditor::GetModelBoundingBox(const ResChar modelName[]) const
    {
        Placements::BoundingBox boundingBox;
        auto assetState = TryGetBoundingBox(boundingBox, *_pimpl->_modelCache, modelName, 0, true);
        if (assetState != ::Assets::AssetState::Ready)
            return std::make_pair(Float3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()), Float3(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()));

        return boundingBox;
    }

    auto PlacementsEditor::Transaction_Begin(
        const PlacementGUID* placementsBegin, 
        const PlacementGUID* placementsEnd,
        TransactionFlags::BitField transactionFlags) -> std::shared_ptr<ITransaction>
    {
        return std::make_shared<Transaction>(_pimpl.get(), placementsBegin, placementsEnd, transactionFlags);
    }

    std::shared_ptr<PlacementsManager> PlacementsEditor::GetManager() { return _pimpl->_manager; }
    const PlacementCellSet& PlacementsEditor::GetCellSet() const { return *_pimpl->_cellSet; }

    PlacementsEditor::PlacementsEditor(
        std::shared_ptr<PlacementCellSet> cellSet,
        std::shared_ptr<PlacementsManager> manager,
        std::shared_ptr<PlacementsCache> placementsCache,
        std::shared_ptr<PlacementsModelCache> modelCache)
    {
        _pimpl = std::make_unique<Pimpl>();
        _pimpl->_cellSet = std::move(cellSet);
        _pimpl->_placementsCache = std::move(placementsCache);
        _pimpl->_modelCache = std::move(modelCache);
        _pimpl->_manager = std::move(manager);
    }

    PlacementsEditor::~PlacementsEditor()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    WorldPlacementsConfig::WorldPlacementsConfig(
        InputStreamFormatter<utf8>& formatter,
        const ::Assets::DirectorySearchRules& searchRules,
		const ::Assets::DependencyValidation& depVal)
    : WorldPlacementsConfig()
    {
        StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
        for (auto c:doc.RootElement().children()) {
            Cell cell;
            cell._offset = c.Attribute("Offset", Float3(0,0,0));
            cell._mins = c.Attribute("Mins", Float3(0,0,0));
            cell._maxs = c.Attribute("Maxs", Float3(0,0,0));

            auto baseFile = c.Attribute("NativeFile").Value().AsString();
            searchRules.ResolveFile(
                cell._file, dimof(cell._file),
                baseFile.c_str());
            _cells.push_back(cell);
        }
		_depVal = depVal;
    }

    WorldPlacementsConfig::WorldPlacementsConfig()
    {
    }

}


// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../RenderCore/Metal/Forward.h"
#include "../Math/Vector.h"
#include <vector>
#include <memory>
#include <functional>

namespace ConsoleRig { class IProgress; }

namespace SceneEngine
{
    class TerrainUberSurfaceGeneric;

    class ShortCircuitUpdate
    {
    public:
        std::unique_ptr<RenderCore::Metal::ShaderResourceView> _srv;
        Int2 _cellMinsInResource;      // the minimum corner of the cell in resource coordinates
        Int2 _cellMaxsInResource;      // the maximum corner of the cell in resource coordinates

        ShortCircuitUpdate();
        ~ShortCircuitUpdate();
        ShortCircuitUpdate(ShortCircuitUpdate&&) never_throws;
        ShortCircuitUpdate& operator=(ShortCircuitUpdate&&) never_throws;
    };

    class IShortCircuitSource
    {
    public:
        virtual ShortCircuitUpdate GetShortCircuit(UInt2 uberMins, UInt2 uberMaxs) = 0;
        virtual TerrainUberSurfaceGeneric& GetSurface() = 0;
        virtual ~IShortCircuitSource();
    };

    class ShortCircuitBridge
    {
    public:
        class CellRegion 
        { 
        public:
            uint64_t _cellHash;
            Float2 _cellMins, _cellMaxs;
        };
        std::vector<CellRegion>		GetPendingUpdates();
        std::vector<CellRegion>		GetPendingAbandons();

        ShortCircuitUpdate GetShortCircuit(uint64_t cellHash, Float2 cellMins, Float2 cellMaxs);

        void QueueShortCircuit(UInt2 uberMins, UInt2 uberMaxs);
        void QueueAbandon(UInt2 uberMins, UInt2 uberMaxs);
        void WriteCells(UInt2 uberMins, UInt2 uberMaxs, ConsoleRig::IProgress* progress);

        using WriteCellsFn = std::function<void()>;
        void RegisterCell(uint64_t cellHash, UInt2 uberMins, UInt2 uberMaxs, WriteCellsFn&& writeCells);

        ShortCircuitBridge(const std::shared_ptr<IShortCircuitSource>& source);
        ~ShortCircuitBridge();
    private:
        std::weak_ptr<IShortCircuitSource> _source;

        class RegisteredCell
        {
        public:
            UInt2 _uberMins, _uberMaxs;
            WriteCellsFn _writeCells;
        };
        std::vector<std::pair<uint64_t, RegisteredCell>> _cells;

        std::vector<CellRegion> _pendingAbandons;
        std::vector<CellRegion> _pendingUpdates;
    };
}

// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/AssetsCore.h"
#include "../Math/Matrix.h"
#include <vector>
#include <string>
#include <memory>

namespace OSServices    { class BasicFile; }
namespace Assets        { class DependencyValidation; }

namespace SceneEngine
{
    class TerrainCell
    {
    public:
        TerrainCell();
        ~TerrainCell();

        const std::string & SourceFile() const          { return _sourceFileName; }
        const std::string & SecondaryCacheFile() const  { return _secondaryCacheName; }
        const ::Assets::DependencyValidation& GetDependencyValidation() const   { return _validationCallback; }
        bool EncodedGradientFlags() const               { return _encodedGradientFlags; }

        //////////////////////////////////////////////////////////////////
        class Node
        {
        public:
            Float4x4    _localToCell;
            size_t      _heightMapFileOffset;
            size_t      _heightMapFileSize;
            unsigned    _widthInElements;
            Node(const Float4x4& localToCell, size_t heightMapFileOffset, size_t heightMapFileSize, unsigned widthInElements);

                //  Note -- hack here for 32x32 tiles!
            unsigned    GetOverlapWidth() const { return (_widthInElements==33)?1:2; }
        };

        //////////////////////////////////////////////////////////////////
        class NodeField
        {
        public:
            unsigned    _widthInNodes, _heightInNodes;
            unsigned    _nodeBegin, _nodeEnd;       // these are indices into the "_nodes" array of the TerrainCell

            NodeField(  unsigned widthInNodes, unsigned heightInNodes, 
                        unsigned nodeBegin, unsigned nodeEnd);
        };

        //////////////////////////////////////////////////////////////////
            //      each "nodeField" represents a different level of detail
            //      the first is the lowest quality, the last is the highest
            //      So the first has the fewest nodes
        std::vector<NodeField>              _nodeFields;
        std::vector<std::unique_ptr<Node>>  _nodes;

    protected:
        std::string         _sourceFileName;
        std::string         _secondaryCacheName;
        bool                _encodedGradientFlags;

        ::Assets::DependencyValidation  _validationCallback

    private:
        TerrainCell(const TerrainCell&);
        TerrainCell& operator=(const TerrainCell&);
        friend class TerrainCellRenderer;
    };

    class TerrainCellTexture
    {
    public:
        const std::string & SourceFile() const  { return _sourceFileName; }
        const ::Assets::DependencyValidation& GetDependencyValidation() const   { return _validationCallback; }

        TerrainCellTexture();
        ~TerrainCellTexture();

    protected:
        std::vector<unsigned>   _nodeFileOffsets;
        unsigned                _nodeTextureByteCount;
        unsigned                _fieldCount;
        std::string             _sourceFileName;

        ::Assets::DependencyValidation  _validationCallback

        friend class TerrainCellRenderer;
    };

    unsigned CompressedHeightMask(bool encodedGradientFlags);
}


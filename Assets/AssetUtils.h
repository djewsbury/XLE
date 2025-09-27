// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Utility/StringUtils.h"

namespace Assets
{
    class DirectorySearchRules
    {
    public:
        void AddSearchDirectory(StringSection<ResChar> dir);
        void AddSearchDirectoryFromFilename(StringSection<ResChar> filename);
        std::string AnySearchDirectory() const;
		void SetBaseFile(StringSection<ResChar> file);
        StringSection<> GetBaseFile() const;

        void ResolveFile(
            ResChar destination[], unsigned destinationCount, 
            StringSection<ResChar> baseName) const;
        void ResolveDirectory(
            ResChar destination[], unsigned destinationCount, 
            StringSection<ResChar> baseName) const;
        bool HasDirectory(StringSection<ResChar> dir);
		std::vector<std::basic_string<ResChar>> FindFiles(StringSection<char> wildcardSearch) const;

        template<int Count>
            void ResolveFile(ResChar (&destination)[Count], StringSection<ResChar> baseName) const
                { ResolveFile(destination, Count, baseName); }

        void Merge(const DirectorySearchRules& mergeFrom);

        DirectorySearchRules();
        DirectorySearchRules(const DirectorySearchRules&);
        DirectorySearchRules& operator=(const DirectorySearchRules&);
		DirectorySearchRules(DirectorySearchRules&&) never_throws;
        DirectorySearchRules& operator=(DirectorySearchRules&&) never_throws;

        static DirectorySearchRules Deserialize(IteratorRange<const void*>);
        Blob Serialize() const;
    protected:
        ResChar _buffer[256];
        std::vector<ResChar> _bufferOverflow;
    
        unsigned _startOffsets[8];
        unsigned _startPointCount;
        unsigned _baseFileOffset;

        unsigned _bufferUsed;

        unsigned AddString(StringSection<ResChar>);
    };

    DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar> baseFile);

}


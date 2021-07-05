// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "../Utility/UTFUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Streams/Stream.h"

namespace Assets
{
    class DirectorySearchRules
    {
    public:
        void AddSearchDirectory(StringSection<ResChar> dir);
        void AddSearchDirectoryFromFilename(StringSection<ResChar> filename);
        std::string AnySearchDirectory() const;
		void SetBaseFile(StringSection<ResChar> file);
        StringSection<> GetBaseFile() const { return _baseFile; }

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
    protected:
        ResChar _buffer[512];
        std::vector<ResChar> _bufferOverflow;
        unsigned _startOffsets[8];
    
        unsigned _bufferUsed;
        unsigned _startPointCount;

		ResChar _baseFile[MaxPath];
    };

    DirectorySearchRules DefaultDirectorySearchRules(StringSection<ResChar> baseFile);

        ////////////////////////////////////////////////////////////////////////////////////////////////

    class IFileInterface;
    class FileOutputStream : public OutputStream
    {
    public:
        virtual size_type Tell() override;
        virtual void Write(const void* p, size_type len) override;
        virtual void WriteChar(char ch) override;
        virtual void Write(StringSection<utf8> s) override;
        virtual void Flush() override;
        FileOutputStream(const std::shared_ptr<IFileInterface>& file);
        FileOutputStream(std::unique_ptr<IFileInterface>&& file);

        FileOutputStream(FileOutputStream&&) = default;
        FileOutputStream& operator=(FileOutputStream&&) = default;
    private:
        std::shared_ptr<IFileInterface> _file;
    };
}


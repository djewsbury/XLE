// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IArtifact.h"
#include "AssetsCore.h"
#include "../Utility/IteratorUtils.h"
#include <functional>
#include <memory>

namespace Assets
{
    /// <summary>Utility for building asset objects that load from chunk files (sometimes asychronously)</summary>
    /// Some simple assets simply want to load some raw data from a chunk in a file, or
    /// perhaps from a few chunks in the same file. This is a base class to take away some
    /// of the leg-work involved in implementing that class.
    class ChunkFileContainer
    {
    public:
        const rstring& Filename() const						{ return _filename; }
		const DepValPtr& GetDependencyValidation() const	{ return _validationCallback; }

		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const;
        std::vector<ArtifactRequestResult> ResolveRequests(IFileInterface& file, IteratorRange<const ArtifactRequest*> requests) const;

		std::shared_ptr<IFileInterface> OpenFile() const;

		ChunkFileContainer(StringSection<ResChar> assetTypeName);
		ChunkFileContainer(const Blob& blob, const DepValPtr& depVal, StringSection<ResChar>);
		ChunkFileContainer();
        ~ChunkFileContainer();

		ChunkFileContainer(const ChunkFileContainer&) = default;
		ChunkFileContainer& operator=(const ChunkFileContainer&) = default;
		ChunkFileContainer(ChunkFileContainer&&) never_throws = default;
		ChunkFileContainer& operator=(ChunkFileContainer&&) never_throws = default;
    private:
        rstring			_filename;
		Blob			_blob;
		DepValPtr		_validationCallback;
    };

}




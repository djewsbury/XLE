// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <memory>

namespace Assets
{
    class IntermediatesStore;
	class IIntermediateCompilers;
    class IFileSystem;

    class CompileAndAsyncManager
    {
    public:
		IIntermediateCompilers& GetIntermediateCompilers();

        const std::shared_ptr<IntermediatesStore>&	GetIntermediateStore();
		const std::shared_ptr<IntermediatesStore>&	GetShadowingStore();

        CompileAndAsyncManager(std::shared_ptr<IFileSystem> intermediatesFilesystem);
        ~CompileAndAsyncManager();
    protected:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
    };

}


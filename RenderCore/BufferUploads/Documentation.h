// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

namespace RenderCore { 

/*!
    \brief Interface for uploading data to the GPU

    The buffer uploads interface helps initialise buffer objects on the GPU
    in a asynchronous manner.

    Buffer uploads are usually one of the following types:
        \li Textures
        \li Vertex or Index buffers
        \li StructuredBuffers for compute shaders

    But they can represent any large block of data.

    The system also supports batching and defragmentation of memory within
    batches. This is important when uploading many small buffers. Some terrain
    implementations may frequently need to upload many
    small buffers per frame. But this can be done more efficiently by batching 
    uploads into larger buffers.

    For more information, start with RenderCore::BufferUploads::IManager.

    \sa RenderCore::BufferUploads::IManager
*/
namespace BufferUploads {}

}

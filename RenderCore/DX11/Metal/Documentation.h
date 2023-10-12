// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

namespace RenderCore
{
    /*!
        \brief This is the implementation of RenderCore::Metal for DX11 platforms

        When DX11 is enabled, RenderCore::Metal is renamed to mean RenderCore::Metal_DX11
        \code
            namespace RendeCore {
                using Metal = Metal_DX11;
            }
        \endcode

        This means that DX11 implementations of the "metal" interfaces can be accessed via
        RenderCore::Metal:...

        For example, RenderCore::Metal::DeviceContext will be a rename for 
        RenderCore::Metal_DX11::DeviceContext. This allows compile-time polymorphism: client
        code isn't strongly bound to any single implementation of RenderCore::Metal, and we
        can change which implementation is used with compile time switches.

        ## Important classes

        The classes RenderCore::Metal_DX11 implement a thin layer over an underlying graphics API.
        The architecture is designed to be roughly in-line with modern thinking about GPU hardware,
        striking a balance between dominant APIs (like DX11, DX12, OpenGL4, OpenGLES) as well as
        providing flexibility for future APIs.

        Here are some example classes;
        \par RenderCore::Metal_DX11::DeviceContext
            This is very similar to the DX11 DeviceContext, and closely tied with the higher 
            level RenderCore::IThreadContext. It is strongly associated with a single CPU thread. 
            And for that thread, it represents the GPU state.
            Important operations are: "Bind" (which changes the GPU state), and "Draw" 
            (which executes an operation on the GPU.

        \par RenderCore::Metal_DX11::ShaderProgram
            This represents a fully configured set of shaders, for use with a draw operation. 
            ShaderPrograms can be used with the Assets::GetAssetFuture API, or stored independently.
            While we can interact with VertexShader and PixelShader objects individually, the
            ShaderProgram interface is easiest to use across multiple platforms.

        \par RenderCore::Metal_DX11::BoundUniforms
            This interface allows the CPU to associate resources and constants with the input
            interface of a shader. This interface uses shader reflection to find the correct
            shader input slots for resources (so the CPU code is does not have to know them).
            But querying shader reflection is expensive. So generally BoundUniforms should only
            be constructed during loading events (or in a background thread), and not every
            frame.

        \par RenderCore::Metal_DX11::ShaderResourceView
            A ShaderResourceView configures the way a shader will see a part of GPU memory.
            Typically the GPU memory in question is a texture. ShaderResourceView tells
            the shader the dimensions of the texture, pixel format, mipmap counts, etc.
            We call a block of GPU memory a "resource." In modern graphics API designs,
            resources are mostly opaque. It's the view that determines how we interpret
            that memory.
            We can have multiple views per resource -- and this is important when we want
            to be able to different pixel formats for the same memory (or different mipmap
            settings, etc).
        
        ## DirectX11 specific code

        This namespace is designed to encourage platform-generic coding practices. But it
        also provides access to allow platform-specific clients. Some effects and methods
        require special implementations for certain platforms. And some platform concepts
        are difficult to create truly generic wrappers for.

        This is a advantage of the polymorphism method used for RenderCore::Metal.
        RenderCore::Metal_DX11::DeviceContext and RenderCore::Metal_OpenGLES::DeviceContext
        are actually unrelated types (they have no common base classes). So we can extend
        the base functionality with platform specific addition when needed.

    */
    namespace Metal_DX11 {}
}
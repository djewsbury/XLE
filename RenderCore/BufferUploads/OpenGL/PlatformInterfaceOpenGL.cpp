// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../../Core/Prefix.h"
#include "../../RenderCore/Metal/Metal.h"

#if GFXAPI_TARGET == GFXAPI_OPENGLES

    #include "../PlatformInterface.h"
    #include "../../RenderCore/Format.h"
    #include <GLES2/gl2.h>

    namespace BufferUploads { namespace PlatformInterface
    {

        void                Query_End(const DeviceContext& , UnderlyingQuery )
        {
        }

        bool                Query_IsEventTriggered(const DeviceContext& , UnderlyingQuery )
        {
            return false;
        }
        
        UnderlyingQuery     Query_CreateEvent(const DeviceContext&)
        {
            return UnderlyingQuery();
        }

        static GLuint AsUsage(CPUAccess::BitField cpuAccess)
        {
            if (cpuAccess & CPUAccess::WriteDynamic) {
                return GL_DYNAMIC_DRAW;
            } else if (cpuAccess & CPUAccess::Write) {
                return GL_STREAM_DRAW;
            } else {
                return GL_STATIC_DRAW;
            }
        }

        static CPUAccess::BitField AsCPUAccess(GLuint usage)
        {
            if (usage == GL_DYNAMIC_DRAW) {
                return CPUAccess::Read|CPUAccess::WriteDynamic;
            } else if (usage == GL_STREAM_DRAW) {
                return CPUAccess::Read|CPUAccess::Write;
            } else {
                return CPUAccess::Read;
            }
        }

            ///////////////////////////////////////

        template <typename Type>
            class DeviceBinding
            {
            public:
                DeviceBinding(const Type* bindObject, GLenum bindTarget) never_throws {}
            };

        template <>
            class DeviceBinding<OpenGL::Buffer>
            {
            public:
                GLint   GetParameter(GLenum parameter) const never_throws;
                void    BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage) const never_throws;
                void    BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid * data) const never_throws;
                        DeviceBinding(const OpenGL::Buffer* bindObject, GLenum bindTarget) never_throws;

                static const GLenum DefaultBinding = GL_ARRAY_BUFFER;

            private:
                GLenum _bindTarget;
            };

        template <>
            class DeviceBinding<OpenGL::RenderBuffer>
            {
            public:
                void    RenderBufferStorage(GLenum internalformat, GLsizei width, GLsizei height) const never_throws;
                GLint   GetParameter(GLenum parameter) const never_throws;
                        DeviceBinding(const OpenGL::RenderBuffer* bindObject, GLenum bindTarget) never_throws;

                static const GLenum DefaultBinding = GL_RENDERBUFFER;

            private:
                GLenum _bindTarget;
            };

        template <>
            class DeviceBinding<OpenGL::Texture>
            {
            public:
                GLint   GetParameter(GLenum parameter) const never_throws;
                void    SetParameter(GLenum parameter, GLint value) const never_throws;
                void    SetParameter(GLenum parameter, GLfloat value) const never_throws;

                void    Image2D(    GLint level, GLint internalformat,
 	                                GLsizei width, GLsizei height,
 	                                GLint border, GLenum format, GLenum type,
 	                                const GLvoid * data) const never_throws;
                void    SubImage2D( GLint level, GLint xoffset, GLint yoffset,
 	                                GLsizei width, GLsizei height,
 	                                GLenum format, GLenum type,
 	                                const GLvoid * data) const never_throws;
                void    CompressedImage2D(
                                    GLint level,
 	                                GLenum internalformat,
 	                                GLsizei width,
 	                                GLsizei height,
 	                                GLint border,
 	                                GLsizei imageSize,
 	                                const GLvoid * data);
                void    CompressedSubImage2D(
 	                                GLint level,
 	                                GLint xoffset,
 	                                GLint yoffset,
 	                                GLsizei width,
 	                                GLsizei height,
 	                                GLenum format,
 	                                GLsizei imageSize,
 	                                const GLvoid * data);

                        DeviceBinding(const OpenGL::Texture* bindObject, GLenum bindTarget) never_throws;

                static const GLenum DefaultBinding = GL_TEXTURE_2D;

            private:
                GLenum _bindTarget;
            };

        template <typename GlObject_Type>
            DeviceBinding<GlObject_Type>     MakeDeviceBinding(const GlObject_Type* bindObject, GLenum bindTarget = DeviceBinding<GlObject_Type>::DefaultBinding)
        {
            return DeviceBinding<GlObject_Type>(bindObject, bindTarget);
        }

        template <typename GlObject_Type>
            DeviceBinding<GlObject_Type>     MakeDeviceBinding(intrusive_ptr<GlObject_Type>& bindObject, GLenum bindTarget = DeviceBinding<GlObject_Type>::DefaultBinding)
        {
            return DeviceBinding<GlObject_Type>(bindObject.get(), bindTarget);
        }

            ///////////////////////////////////////

        void   DeviceBinding<OpenGL::RenderBuffer>::RenderBufferStorage(GLenum internalformat, GLsizei width, GLsizei height) const never_throws
        {
            glRenderbufferStorage(_bindTarget, internalformat, width, height);
        }

        GLint   DeviceBinding<OpenGL::RenderBuffer>::GetParameter(GLenum parameter) const never_throws
        {
            GLint result = 0;
            assert(     parameter == GL_RENDERBUFFER_WIDTH
                    ||  parameter == GL_RENDERBUFFER_HEIGHT
                    ||  parameter == GL_RENDERBUFFER_INTERNAL_FORMAT
                    ||  parameter == GL_RENDERBUFFER_RED_SIZE
                    ||  parameter == GL_RENDERBUFFER_GREEN_SIZE
                    ||  parameter == GL_RENDERBUFFER_BLUE_SIZE
                    ||  parameter == GL_RENDERBUFFER_ALPHA_SIZE
                    ||  parameter == GL_RENDERBUFFER_DEPTH_SIZE
                    ||  parameter == GL_RENDERBUFFER_STENCIL_SIZE);
            glGetRenderbufferParameteriv(_bindTarget, parameter, &result);
            return result;
        }

        DeviceBinding<OpenGL::RenderBuffer>::DeviceBinding(const OpenGL::RenderBuffer* bindObject, GLenum bindTarget) never_throws
        {
            assert(bindTarget == GL_RENDERBUFFER);
            assert(glIsRenderbuffer((GLuint)bindObject));
            glBindRenderbuffer(bindTarget, (GLuint)bindObject);
        }

            ///////////////////////////////////////

        GLint  DeviceBinding<OpenGL::Buffer>::GetParameter(GLenum parameter) const never_throws
        {
            GLint result = 0;
            assert(parameter == GL_BUFFER_SIZE || parameter == GL_BUFFER_USAGE);
            glGetBufferParameteriv(_bindTarget, parameter, &result);
            return result;
        }

        void    DeviceBinding<OpenGL::Buffer>::BufferData(GLsizeiptr size, const GLvoid* data, GLenum usage) const never_throws
        {
            glBufferData(_bindTarget, size, data, usage);
        }

        void    DeviceBinding<OpenGL::Buffer>::BufferSubData(GLintptr offset, GLsizeiptr size, const GLvoid * data) const never_throws
        {
            glBufferSubData(_bindTarget, offset, size, data);
        }

        DeviceBinding<OpenGL::Buffer>::DeviceBinding(const OpenGL::Buffer* bindObject, GLenum bindTarget) never_throws
        :           _bindTarget(bindTarget)
        {
            assert(bindTarget == GL_ARRAY_BUFFER || bindTarget == GL_ELEMENT_ARRAY_BUFFER);
            assert(glIsBuffer((GLuint)bindObject));
            glBindBuffer(bindTarget, (GLuint)bindObject);
        }

            ///////////////////////////////////////

        GLint   DeviceBinding<OpenGL::Texture>::GetParameter(GLenum parameter) const never_throws
        {
            GLint result = 0;
            assert( parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER ||
                    parameter ==  GL_TEXTURE_WRAP_S || parameter == GL_TEXTURE_WRAP_T);
            glGetTexParameteriv(_bindTarget, parameter, &result);
            return result;
        }
        void    DeviceBinding<OpenGL::Texture>::SetParameter(GLenum parameter, GLint value) const never_throws
        {
            assert( parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER ||
                    parameter ==  GL_TEXTURE_WRAP_S || parameter == GL_TEXTURE_WRAP_T);
            glTexParameteri(_bindTarget, parameter, value);
        }

        void    DeviceBinding<OpenGL::Texture>::SetParameter(GLenum parameter, GLfloat value) const never_throws
        {
            assert( parameter == GL_TEXTURE_MIN_FILTER || parameter == GL_TEXTURE_MAG_FILTER ||
                    parameter ==  GL_TEXTURE_WRAP_S || parameter == GL_TEXTURE_WRAP_T);
            glTexParameterf(_bindTarget, parameter, value);
        }

        void    DeviceBinding<OpenGL::Texture>::Image2D(    GLint level, GLint internalformat,
 	                        GLsizei width, GLsizei height,
 	                        GLint border, GLenum format, GLenum type,
 	                        const GLvoid * data) const never_throws
        {
            glTexImage2D(_bindTarget, level, internalformat, width, height, border, format, type, data);
        }

        void    DeviceBinding<OpenGL::Texture>::SubImage2D( GLint level, GLint xoffset, GLint yoffset,
 	                        GLsizei width, GLsizei height,
 	                        GLenum format, GLenum type,
 	                        const GLvoid * data) const never_throws
        {
                //
                //   DavidJ --      this is a little different from the normal pattern.
                //                  when using cubemaps, the target parameter for texture functions
                //                  can be one of the directions in the cubemap. In other words, it's
                //                  not the same value as "_bindTarget"... Interface changes are required
                //                  to support that
                //
            glTexSubImage2D(
                _bindTarget, level, xoffset, yoffset,
                width, height, format, type, data);
        }

        void    DeviceBinding<OpenGL::Texture>::CompressedImage2D(
                            GLint level,
 	                        GLenum internalformat,
 	                        GLsizei width,
 	                        GLsizei height,
 	                        GLint border,
 	                        GLsizei imageSize,
 	                        const GLvoid * data)
        {
            glCompressedTexImage2D(
                _bindTarget, level, internalformat, 
                width, height, border, imageSize, data);
        }

        void    DeviceBinding<OpenGL::Texture>::CompressedSubImage2D(
 	                        GLint level,
 	                        GLint xoffset,
 	                        GLint yoffset,
 	                        GLsizei width,
 	                        GLsizei height,
 	                        GLenum format,
 	                        GLsizei imageSize,
 	                        const GLvoid * data)
        {
            glCompressedTexSubImage2D(
                _bindTarget, level, xoffset, yoffset, 
                width, height, format, imageSize, data);
        }

        DeviceBinding<OpenGL::Texture>::DeviceBinding(const OpenGL::Texture* bindObject, GLenum bindTarget) never_throws
        {
            assert(bindTarget == GL_TEXTURE_2D || bindTarget == GL_TEXTURE_CUBE_MAP);
            assert(glIsTexture((GLuint)bindObject));
            glBindTexture(bindTarget, (GLuint)bindObject);
            _bindTarget = bindTarget;
        }

            ///////////////////////////////////////

        intrusive_ptr<Underlying::Resource>    CreateResource( ObjectFactory&, const ResourceDesc& bufferDesc, 
                                                            DataPacket* initialisationData)
        {
            switch (bufferDesc._type) {
            case ResourceDesc::Type::Texture:
                {
                        //
                        //      Create either a "texture" or a "render buffer" object
                        //      based on the bind flags in the buffer desc.
                        //
                    const GLint formatComponents          = AsGLComponents(GetComponents(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat)));
                    const GLint formatComponentWidths     = AsGLComponentWidths(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat));

                    if (bufferDesc._bindFlags & BindFlag::ShaderResource) {
                        auto texture = CreateTexture();
                        auto binding = MakeDeviceBinding(texture);
                        for (unsigned c=0; c<bufferDesc._textureDesc._mipCount; ++c) {
                            binding.Image2D( 
                                c, 
                                formatComponents,
                                bufferDesc._textureDesc._width,
                                bufferDesc._textureDesc._height,
                                0,
                                formatComponents, formatComponentWidths,
                                initialisationData ? initialisationData->GetData(c, 0) : nullptr);
                        }

                        return intrusive_ptr<Underlying::Resource>(texture->As<GlObject_Type::Resource>());
                    } else {
                        auto renderBuffer = CreateRenderBuffer();
                        MakeDeviceBinding(renderBuffer).RenderBufferStorage(
                            formatComponents, bufferDesc._textureDesc._width, bufferDesc._textureDesc._height);
                        return intrusive_ptr<Underlying::Resource>(renderBuffer->As<GlObject_Type::Resource>());
                    }

                }
                break;


            case ResourceDesc::Type::LinearBuffer:
                {
                    auto buffer = CreateBuffer();
                    // assert((bufferDesc._bindFlags & (BindFlag::VertexBuffer|BindFlag::IndexBuffer)) != (BindFlag::VertexBuffer|BindFlag::IndexBuffer));
                        // (does it really matter if we bind it as a vertex or index buffer now?)
                    GLenum bufferType = (bufferDesc._bindFlags & BindFlag::IndexBuffer) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
                    MakeDeviceBinding(buffer, bufferType).BufferData(
                        bufferDesc._linearBufferDesc._sizeInBytes,
                        initialisationData ? initialisationData->GetData(0,0) : nullptr, 
                        AsUsage(bufferDesc._cpuAccess));
                    return intrusive_ptr<Underlying::Resource>(buffer->As<GlObject_Type::Resource>());
                }
                break;
            }

            return intrusive_ptr<Underlying::Resource>();
        }

        ResourceDesc                      ExtractDesc(const Underlying::Resource& resource)
        {
                //
                //      Get the "ResourceDesc" description based on the underlying
                //      object in "resource"
                //
            const OpenGL::Buffer* buffer = resource.As<GlObject_Type::Buffer>();
            if (buffer != RawGLHandle_Invalid) {
                ResourceDesc result;
                result._type            = ResourceDesc::Type::LinearBuffer;
                result._bindFlags       = BindFlag::VertexBuffer | BindFlag::IndexBuffer;  // (the buffer itself doesn't know if it's vertices or indices)

                auto binding            = MakeDeviceBinding(buffer);
                result._cpuAccess       = AsCPUAccess(binding.GetParameter(GL_BUFFER_USAGE));
                result._gpuAccess       = GPUAccess::Read;
                result._allocationRules = 0;
                result._linearBufferDesc._sizeInBytes = binding.GetParameter(GL_BUFFER_SIZE);
                result._name[0]         = '\0';
                return result;
            } else {
                const OpenGL::Texture* texture = resource.As<GlObject_Type::Texture>();
                if (texture != RawGLHandle_Invalid) {
                    ResourceDesc result;
                    result._type        = ResourceDesc::Type::Texture;
                    result._bindFlags   = BindFlag::ShaderResource;
                    
                    auto binding                = MakeDeviceBinding(texture);
                    result._cpuAccess           = CPUAccess::Write;
                    result._gpuAccess           = GPUAccess::Read;
                    result._allocationRules     = 0;
                    result._textureDesc._width  = 0;                // can't query the width and height;
                    result._textureDesc._height = 0;                // 
                    result._textureDesc._nativePixelFormat = 0;     // .. or format
                    result._textureDesc._dimensionality = TextureDesc::Dimensionality::T2D;
                    result._textureDesc._mipCount = 0;              // .. or anything, really
                    result._textureDesc._arrayCount = 0;
                    return result;
                }
            }

            ResourceDesc desc;
            XlZeroMemory(desc);
            desc._type = ResourceDesc::Type::Unknown;
            return desc;
        }

        void ResourceUploadHelper::PushToResource(
                                            const Underlying::Resource& resource, const ResourceDesc& bufferDesc, unsigned resourceOffsetValue,
                                            const void* data, size_t dataSize,
                                            std::pair<unsigned,unsigned> rowAndSlicePitch,
                                            const Box2D& box, unsigned lodLevel, unsigned arrayIndex)
        {
            if (!data) {
                return;
            }

            switch (bufferDesc._type) {
            case ResourceDesc::Type::Texture:
                {
                        // (can't push to a render buffer object, it seems. Maybe copy from staging texture
                        //  would be ok?)
                    auto texture = resource.As<GlObject_Type::Texture>();
                    assert(texture != RawGLHandle_Invalid);

                    const bool isFullUpdate = box == Box2D();
                    const bool isCompressed = GetCompressionType(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat)) != FormatCompressionType::None;

                    auto binding         = MakeDeviceBinding(texture);
                    if (isCompressed) {
                        const GLint compressedFormat = AsGLCompressionType(GetCompressionType(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat)));
                            //
                            //      When pushing to a compressed texture; assume the input data is already in the 
                            //      compressed format. This is normally true -- we typically just load compressed
                            //      data off disk, and push directly to the texture.
                            //          -- todo -- validate the "dataSize" parameter, make sure it matches our expectations.
                            //
                        if (isFullUpdate) {
                            binding.CompressedImage2D(
                                lodLevel, 
                                compressedFormat,
                                bufferDesc._textureDesc._width,
                                bufferDesc._textureDesc._height,
                                0, dataSize, data);
                        } else {
                            binding.CompressedSubImage2D(
                                lodLevel, 
                                box._left, box._top,
                                box._right - box._left, box._bottom - box._top,
                                compressedFormat, dataSize, data);
                        }
                    } else {
                        const GLint formatComponents          = AsGLComponents(GetComponents(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat)));
                        const GLint formatComponentWidths     = AsGLComponentWidths(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat));

                        const unsigned bpp = BitsPerPixel(NativeFormat::Enum(bufferDesc._textureDesc._nativePixelFormat));
                        (void)bpp;
                        if (isFullUpdate) {
                                //
                                //      Spacing between the rows isn't supported. Make sure the row pitch fits exactly.
                                //
                            TextureDesc mipDesc = CalculateMipMapDesc(bufferDesc._textureDesc, lodLevel);
                            (void)mipDesc;
                            assert((mipDesc._width * bpp / 8) == rowAndSlicePitch.first);
                            binding.Image2D(
                                lodLevel,
                                formatComponents,
                                bufferDesc._textureDesc._width,
                                bufferDesc._textureDesc._height,
                                0,
                                formatComponents,
                                formatComponentWidths,
                                data);
                        } else {
                            assert(((box._right - box._left) * bpp / 8) == rowAndSlicePitch.first);
                            binding.SubImage2D(
                                lodLevel, 
                                box._left, box._top,
                                box._right - box._left, box._bottom - box._top,
                                formatComponents, formatComponentWidths,
                                data);
                        }
                    }
                }
                break;

            case ResourceDesc::Type::LinearBuffer:
                {
                    auto buffer = resource.As<GlObject_Type::Buffer>();
                    auto binding = MakeDeviceBinding(buffer);
                    const bool isFullUpdate = resourceOffsetValue == 0 && dataSize == bufferDesc._linearBufferDesc._sizeInBytes;
                    if (isFullUpdate) {
                        binding.BufferData(dataSize, data, AsUsage(bufferDesc._cpuAccess));
                    } else {
                        assert((resourceOffsetValue + dataSize) <= bufferDesc._linearBufferDesc._sizeInBytes);
                        binding.BufferSubData(resourceOffsetValue, dataSize, data);
                    }
                }
                break;
            }
        }

        void ResourceUploadHelper::PushToStagingResource(
                                            const Underlying::Resource& resource, const ResourceDesc& desc, unsigned resourceOffsetValue,
                                            const void* data, size_t dataSize, std::pair<unsigned,unsigned> rowAndSlicePitch,
                                            const Box2D& box, unsigned lodLevel, unsigned arrayIndex)
        {
            assert(0);  // unimplemented currently
        }

        void ResourceUploadHelper::UpdateFinalResourceFromStaging(
                                            const Underlying::Resource& finalResource, const Underlying::Resource& staging, 
                                            const ResourceDesc& destinationDesc, unsigned lodLevelMin, unsigned lodLevelMax, unsigned stagingLODOffset)
        {
            assert(0);  // unimplemented currently
        }

        void ResourceUploadHelper::ResourceCopy_DefragSteps(const Underlying::Resource& destination, const Underlying::Resource& source, const std::vector<DefragStep>& steps)
        {
            assert(0);  // unimplemented currently
        }

        void ResourceUploadHelper::ResourceCopy(const Underlying::Resource& destination, const Underlying::Resource& source)
        {
            assert(0);  // unimplemented currently
        }

        ResourceUploadHelper::MappedBuffer ResourceUploadHelper::Map(const Underlying::Resource& resource, MapType::Enum mapType, unsigned lodLevel, unsigned arrayIndex)
        {
            assert(0);  // unimplemented currently
            return MappedBuffer();
        }

        ResourceUploadHelper::MappedBuffer ResourceUploadHelper::MapPartial(const Underlying::Resource& resource, MapType::Enum mapType, unsigned offset, unsigned size, unsigned lodLevel, unsigned arrayIndex)
        {
            assert(0);  // unimplemented currently
            return MappedBuffer();
        }

        std::shared_ptr<CommandList> ResourceUploadHelper::ResolveCommandList()
        {
            // assert(0);  // unimplemented currently
            return std::shared_ptr<CommandList>();
        }

        void                        ResourceUploadHelper::BeginCommandList()
        {
            _devContext->BeginCommandList();
        }

        ResourceUploadHelper::ResourceUploadHelper(RenderCore::IDevice* device, DeviceContext* context)
        : _device(device)
        , _devContext(context ? intrusive_ptr<DeviceContext>(context) : DeviceContext::GetImmediateContext(device))
        {
        }

        void ResourceUploadHelper::Unmap(const Underlying::Resource&, unsigned) 
        {
            assert(0);
        }

    }}

#endif


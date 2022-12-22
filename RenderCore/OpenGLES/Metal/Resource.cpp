
#include "Resource.h"
#include "Format.h"
#include "DeviceContext.h"
#include "ExtensionFunctions.h"
#include "../../ResourceUtils.h"
#include "../../../OSServices/Log.h"
#include "../../../Utility/BitUtils.h"
#include "IncludeGLES.h"
#include <sstream>
#include <atomic>

#include "GLWrappers.h"

namespace RenderCore { namespace ImplOpenGLES {
    void CheckContextIntegrity();
}}

namespace RenderCore { namespace Metal_OpenGLES
{

////////////////////////////////////////////////////////////////////////////////////////////////////

    GLenum AsBufferTarget(BindFlag::BitField bindFlags)
    {
        // Valid "target" values for glBindBuffer:
        // GL_ARRAY_BUFFER, GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_PIXEL_PACK_BUFFER, GL_PIXEL_UNPACK_BUFFER, GL_TRANSFORM_FEEDBACK_BUFFER, or GL_UNIFORM_BUFFER
        // These are not accessible from here:
        // GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, GL_PIXEL_PACK_BUFFER & GL_PIXEL_UNPACK_BUFFER
        // Also note that this function will always 0 when multiple flags are set in 'bindFlags'

        switch (bindFlags) {
        case BindFlag::VertexBuffer:
        {
            // We have to be very careful about using the "GL_ARRAY_BUFFER" binding while a
            // VAO is bound. That will change the VAO contents specifically, which will can later
            // on result in clients of the VAO using this new binding. It's dangerous and difficult
            // to debug
            #if defined(_DEBUG)
                GLint currentVAO = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
                assert(currentVAO==0);
            #endif
            return GL_ARRAY_BUFFER;
        }
        case BindFlag::IndexBuffer: return GL_ELEMENT_ARRAY_BUFFER;
        case BindFlag::ConstantBuffer: return GL_UNIFORM_BUFFER;
        case BindFlag::StreamOutput: return GL_TRANSFORM_FEEDBACK_BUFFER;
        }
        return 0;
    }

    GLenum AsUsageMode( CPUAccess::BitField cpuAccess,
                        GPUAccess::BitField gpuAccess)
    {
        // GL_STREAM_DRAW, GL_STREAM_READ, GL_STREAM_COPY, GL_STATIC_DRAW, GL_STATIC_READ, GL_STATIC_COPY, GL_DYNAMIC_DRAW, GL_DYNAMIC_READ, or GL_DYNAMIC_COPY
        // These values are not accessible here:
        //  GL_STREAM_COPY, GL_STATIC_COPY, GL_DYNAMIC_COPY

        if (cpuAccess == 0) {
            return GL_STATIC_DRAW;
        } else if ((cpuAccess & CPUAccess::WriteDynamic) == CPUAccess::WriteDynamic) {
            if (cpuAccess & CPUAccess::Read) return GL_DYNAMIC_READ;
            else return GL_DYNAMIC_DRAW;
        } else if (cpuAccess & CPUAccess::Write) {
            if (cpuAccess & CPUAccess::Read) return GL_STREAM_READ;
            else return GL_STREAM_DRAW;
        }

        return 0;
    }

    static std::pair<CPUAccess::BitField, GPUAccess::BitField> AsAccessBitFields(GLenum glAccess)
    {
        switch (glAccess) {
        case GL_STATIC_DRAW: return {0, GPUAccess::Read};
        case GL_STATIC_READ: return {CPUAccess::Read, GPUAccess::Read};
        case GL_DYNAMIC_READ: return {CPUAccess::WriteDynamic | CPUAccess::Read, GPUAccess::Read};
        case GL_DYNAMIC_DRAW: return {CPUAccess::WriteDynamic, GPUAccess::Read};
        case GL_STREAM_READ: return {CPUAccess::Write | CPUAccess::Read, GPUAccess::Read};
        case GL_STREAM_DRAW: return {CPUAccess::Write, GPUAccess::Read};
        default: return {0,0};
        }
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    void* Resource::QueryInterface(size_t guid)
    {
        if (guid == TypeHashCode<Resource>)
            return this;
        return nullptr;
    }

    uint64_t Resource::GetGUID() const
    {
        return _guid;
    }

    std::vector<uint8_t> Resource::ReadBack(IThreadContext& context, SubResourceId subRes) const
    {
        if (_underlyingBuffer) {
            auto bindTarget = GetDesc()._type != ResourceDesc::Type::Unknown ? AsBufferTarget(GetDesc()._bindFlags) : GL_ARRAY_BUFFER;
            glBindBuffer(bindTarget, GetBuffer()->AsRawGLHandle());

            GLint bufferSize = 0;
            glGetBufferParameteriv(bindTarget, GL_BUFFER_SIZE, &bufferSize);

            void* mappedData = glMapBufferRange(bindTarget, 0, bufferSize, GL_MAP_READ_BIT);
            std::vector<uint8_t> result(bufferSize);
            std::memcpy(result.data(), mappedData, bufferSize);
            glUnmapBuffer(bindTarget);

            return result;
        } else if (_underlyingRenderBuffer || _underlyingTexture) {

            if (subRes._arrayLayer != 0 || subRes._mip != 0)
                Throw(std::runtime_error("Can only get the first subresource from a renderbuffer in OpenGLES"));

            if (    _desc._textureDesc._dimensionality != TextureDesc::Dimensionality::T2D
                &&  _desc._textureDesc._dimensionality != TextureDesc::Dimensionality::T1D)
                Throw(std::runtime_error("Only 1D and 2D textures supported in readback operations in OpenGLES"));

            //
            // In OpenGLES, glReadPixels transforms the format of the texture into some simpler
            // format. However the Resource::ReadBack() interface is designed to return the same
            // pixel format as the format that was used when the resource was created. For simplicity,
            // let's only allow some fixed set of formats, where we know the conversion behaviour
            // in glReadPixels isn't going to actually change the pixels.
            //
            auto pixFmt = AsTexelFormatType(AsLinearFormat(_desc._textureDesc._format));
            if (pixFmt._format != GL_RGBA && pixFmt._format != GL_RGBA_INTEGER)
                Throw(std::runtime_error("Can only read back from textures with simple pixel formats in OpenGLES"));

            if (pixFmt._type != GL_UNSIGNED_BYTE && pixFmt._type != GL_UNSIGNED_INT && pixFmt._type != GL_INT && pixFmt._type != GL_FLOAT)
                Throw(std::runtime_error("Can only read back from textures with simple pixel formats in OpenGLES"));

            std::vector<uint8_t> result(_desc._textureDesc._width * _desc._textureDesc._height * BitsPerPixel(_desc._textureDesc._format) / 8);

            GLint prevFrameBuffer;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFrameBuffer);

            auto& factory = GetObjectFactory();
            auto fb = factory.CreateFrameBuffer();
            glBindFramebuffer(GL_FRAMEBUFFER, fb->AsRawGLHandle());
            if (_underlyingRenderBuffer) {
                glFramebufferRenderbuffer(
                    GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0,
                    GL_RENDERBUFFER, _underlyingRenderBuffer->AsRawGLHandle());
            } else {
                if (_desc._textureDesc._arrayCount > 1u) {
                    glFramebufferTextureLayer(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0,
                        _underlyingTexture->AsRawGLHandle(),
                        0,
                        0);
                } else {
                    glFramebufferTexture2D(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + 0, GL_TEXTURE_2D,
                        _underlyingTexture->AsRawGLHandle(),
                        0);
                }
            }

            GLenum b[] = { GL_NONE, GL_NONE, GL_NONE, GL_NONE };
            glDrawBuffers(dimof(b), b);
            glReadBuffer(GL_COLOR_ATTACHMENT0);

            #if defined(_DEBUG)
                auto fbComplete = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (fbComplete != GL_FRAMEBUFFER_COMPLETE) {
                    Log(Warning) << "glCheckFramebufferStatus check failed in Resource ReadBack operation: " << CheckFramebufferStatusToString(fbComplete) << std::endl;
                    assert(fbComplete == GL_FRAMEBUFFER_COMPLETE);
                }
            #endif

            GLint packAlignment, packRowLength;
            glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
            glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ROW_LENGTH, 0);

            glReadPixels(
                0, 0, _desc._textureDesc._width, _desc._textureDesc._height,
                pixFmt._format, pixFmt._type,
                result.data());

            glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
            glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
            glBindFramebuffer(GL_FRAMEBUFFER, prevFrameBuffer);

            CheckGLError("After resource readback");
            return result;

        } else if (_isBackBuffer) {
            Throw(std::runtime_error("Cannot read back from the back buffer in OpenGLES using Resource::ReadBack"));
        }

        return _constantBuffer;
    }

    static std::atomic<uint64_t> s_nextResourceGUID;
    static uint64_t GetNextResourceGUID()
    {
        auto beforeIncrement = s_nextResourceGUID.fetch_add(1);
        return beforeIncrement+1;
    }

    Resource::Resource(
        ObjectFactory& factory, const Desc& desc,
        const SubResourceInitData& initData)
    : Resource(factory, desc,
        initData._data.size()
        ? [&initData](SubResourceId subRes) {
            assert(subRes._mip == 0 && subRes._arrayLayer == 0);
            return initData;
        }
        : IDevice::ResourceInitializer{})
    {}

    /*
    static GLenum AsBindingQuery(GLenum binding)
    {
        switch (binding) {
        case GL_TEXTURE_2D:         return GL_TEXTURE_BINDING_2D;
        case GL_TEXTURE_3D:         return GL_TEXTURE_BINDING_3D;
        case GL_TEXTURE_CUBE_MAP:   return GL_TEXTURE_BINDING_CUBE_MAP;
        default:                    return 0;
        }
    }
    */

    void CheckContext();
    void GLUberReset();

    Resource::Resource(
        ObjectFactory& factory, const Desc& desc,
        const IDevice::ResourceInitializer& initializer)
    : _desc(desc)
    , _isBackBuffer(false)
    , _guid(GetNextResourceGUID())
    {
        #if defined(_DEBUG) && (PLATFORMOS_TARGET == PLATFORMOS_IOS)
            ImplOpenGLES::CheckContextIntegrity();
        #endif

        if (desc._type == ResourceDesc::Type::LinearBuffer) {

            SubResourceInitData initData;
            if (initializer)
                initData = initializer({0,0});

            bool supportsUniformBuffers = (factory.GetFeatureSet() & FeatureSet::GLES300);
            if (desc._bindFlags & BindFlag::ConstantBuffer) {
                // If we request a constant buffer, we must always keep a CPU size copy of the buffer
                // this is because we can bind the constant buffer to a global uniforms in the shader
                // using underscore syntax. When that happens, a true device uniform buffer doesn't
                // really help us. We need access to the CPU data
                auto size = std::min(initData._data.size(), (size_t)desc._linearBufferDesc._sizeInBytes);
                _constantBuffer.insert(_constantBuffer.end(), (const uint8_t*)initData._data.begin(), (const uint8_t*)initData._data.begin() + size);
                if (size < desc._linearBufferDesc._sizeInBytes)
                    _constantBuffer.resize(desc._linearBufferDesc._sizeInBytes, 0);

                if (!(desc._cpuAccess & CPUAccess::Write))
                    _constantBufferHash = Hash64(AsPointer(_constantBuffer.begin()), AsPointer(_constantBuffer.end()));
            }

            if ((desc._bindFlags != BindFlag::ConstantBuffer) || supportsUniformBuffers) {
                _underlyingBuffer = factory.CreateBuffer();
                assert(_underlyingBuffer->AsRawGLHandle() != 0); // "Failed to allocate buffer name in Magnesium::Buffer::Buffer");

                auto bindTarget = AsBufferTarget(desc._bindFlags);
                assert(bindTarget != 0); // "Could not resolve buffer binding target for bind flags (0x%x)", bindFlags);

                auto usageMode = AsUsageMode(desc._cpuAccess, desc._gpuAccess);
                assert(usageMode != 0); // "Could not resolve buffer usable mode for cpu access flags (0x%x) and gpu access flags (0x%x)", cpuAccess, gpuAccess);

                // upload data to opengl buffer...
                glBindBuffer(bindTarget, _underlyingBuffer->AsRawGLHandle());
                GL_WRAP(BufferData)(bindTarget, std::max((GLsizeiptr)initData._data.size(), (GLsizeiptr)desc._linearBufferDesc._sizeInBytes), initData._data.data(), usageMode);

                if (ObjectFactory::WriteObjectLabels() && (factory.GetFeatureSet() & FeatureSet::Flags::LabelObject) && desc._name[0] && OpenGL::g_labelObject)
                    (*OpenGL::g_labelObject)(GL_BUFFER_OBJECT_EXT, _underlyingBuffer->AsRawGLHandle(), 0, desc._name);
            }

            CheckGLError("Creating linear buffer resource");

        } else {

            // write only textures can become render buffers
            auto fmt = AsTexelFormatType(desc._textureDesc._format);

            bool canBeRenderBuffer = desc._cpuAccess == 0 && desc._gpuAccess == GPUAccess::Write && (!(desc._bindFlags & BindFlag::ShaderResource)) && !initializer;
            if (!canBeRenderBuffer) {

                _underlyingTexture = factory.CreateTexture();
                if (    desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D
                    ||  desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D
                    ||  desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D
                    ||  desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {

                    auto bindTarget = GL_TEXTURE_2D;

                    #if APPORTABLE // glTexStorage2D is not yet supported in Apportable
                        bool useTexStorage = false;
                    #else
                        bool useTexStorage = factory.GetFeatureSet() & FeatureSet::GLES300;
                    #endif
                    if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T1D) {
                        assert(desc._textureDesc._height == 1);
                        assert(desc._textureDesc._arrayCount <= 1);
                    } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T2D) {
                        assert(desc._textureDesc._arrayCount <= 1);
                    } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::T3D) {
                        assert(desc._textureDesc._arrayCount <= 1);
                        bindTarget = GL_TEXTURE_3D;
                    } else if (desc._textureDesc._dimensionality == TextureDesc::Dimensionality::CubeMap) {
                        assert(desc._textureDesc._arrayCount == 6);
                        bindTarget = GL_TEXTURE_CUBE_MAP;
                    }

                    // GLint prevTexture;
                    // glGetIntegerv(AsBindingQuery(bindTarget), &prevTexture);
                    glBindTexture(bindTarget, _underlyingTexture->AsRawGLHandle());

                    if (!initializer && useTexStorage) {
                        GL_WRAP(TexStorage2D)(bindTarget, desc._textureDesc._mipCount, fmt._internalFormat, desc._textureDesc._width, desc._textureDesc._height);
                    } else {

                        // OpenGLES2.0 doesn't understand the "internal formats" when using glTexImage2D
                        // It requires that the internalFormat parameter matches the format parameter
                        // However, OpenGLES2.0 does use the internal formats in the glRenderbufferStorage
                        // call.
                        if (!(factory.GetFeatureSet() & FeatureSet::GLES300) && fmt._type != 0) {
                            fmt._internalFormat = fmt._format;
                        }

                        // If we're uploading a cubemap, we must iterate through all of the faces
                        // and use a binding target for each face.
                        // Otherwise we only have a single face, which is going to be the main
                        // binding target.
                        GLenum faceBinding[] = {
                            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                            GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                            GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                            GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                            GL_TEXTURE_CUBE_MAP_NEGATIVE_Z };
                        unsigned faceCount = dimof(faceBinding);

                        if (desc._textureDesc._dimensionality != TextureDesc::Dimensionality::CubeMap) {
                            faceBinding[0] = bindTarget;
                            faceCount = 1;
                        }

                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                        for (unsigned f=0; f<faceCount; ++f) {
                            if (initializer) {
                                for (unsigned m=0; m<desc._textureDesc._mipCount; ++m) {
                                    auto mipWidth  = std::max(desc._textureDesc._width >> m, 1u);
                                    auto mipHeight = std::max(desc._textureDesc._height >> m, 1u);
                                    auto subRes = initializer({m, f});
                                    if (fmt._type != 0) {
                                        GL_WRAP(TexImage2D)(
                                            faceBinding[f],
                                            m, fmt._internalFormat,
                                            mipWidth, mipHeight, 0,
                                            fmt._format, fmt._type,
                                            subRes._data.begin());
                                    } else {
                                        GL_WRAP(CompressedTexImage2D)(
                                            faceBinding[f],
                                            m, fmt._internalFormat,
                                            mipWidth, mipHeight, 0,
                                            (GLsizei)subRes._data.size(),
                                            subRes._data.begin());
                                    }
                                }
                            } else  {
                                assert(fmt._type != 0);
                                for (unsigned m=0; m<desc._textureDesc._mipCount; ++m) {
                                    auto mipWidth  = std::max(desc._textureDesc._width >> m, 1u);
                                    auto mipHeight = std::max(desc._textureDesc._height >> m, 1u);
                                    GL_WRAP(TexImage2D)(
                                        faceBinding[f],
                                        m, fmt._internalFormat,
                                        mipWidth, mipHeight, 0,
                                        fmt._format, fmt._type,
                                        nullptr);
                                }
                            }
                        }
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    }
                    // glBindTexture(bindTarget, prevTexture);
                }

                if (ObjectFactory::WriteObjectLabels() && (factory.GetFeatureSet() & FeatureSet::Flags::LabelObject) && desc._name[0] && OpenGL::g_labelObject)
                    (*OpenGL::g_labelObject)(GL_TEXTURE, _underlyingTexture->AsRawGLHandle(), 0, desc._name);

                CheckGLError("Creating texture resource");
            } else {
                _underlyingRenderBuffer = factory.CreateRenderBuffer();
                glBindRenderbuffer(GL_RENDERBUFFER, _underlyingRenderBuffer->AsRawGLHandle());

                // apportable doesn't properly wrap glRenderbufferStorageMultisample()
                #if defined(PGDROID)
                    bool supportsMultisampleRenderbuffer = false;
                // in "fake GLES" mode (ex. in OSX), the feature is backed by OpenGL
                #elif !defined(GL_ES_VERSION_2_0) && !defined(GL_ES_VERSION_3_0)
                    bool supportsMultisampleRenderbuffer = true;
                #else
                    bool supportsMultisampleRenderbuffer = factory.GetFeatureSet() & FeatureSet::GLES300;
                #endif

                if (supportsMultisampleRenderbuffer && desc._textureDesc._samples._sampleCount > 1) {
                     glRenderbufferStorageMultisample(GL_RENDERBUFFER, desc._textureDesc._samples._sampleCount, fmt._internalFormat, desc._textureDesc._width, desc._textureDesc._height);
                } else {
                    GL_WRAP(RenderbufferStorage)(GL_RENDERBUFFER, fmt._internalFormat, desc._textureDesc._width, desc._textureDesc._height);
                }

                if (ObjectFactory::WriteObjectLabels() && (factory.GetFeatureSet() & FeatureSet::Flags::LabelObject) && desc._name[0] && OpenGL::g_labelObject)
                    (*OpenGL::g_labelObject)(GL_RENDERBUFFER, _underlyingRenderBuffer->AsRawGLHandle(), 0, desc._name);

                CheckGLError("Creating render buffer resource");
            }
        }
    }

    Resource::Resource(const intrusive_ptr<OpenGL::Texture>& texture, const ResourceDesc& desc)
    : _underlyingTexture(texture)
    , _desc(desc)
    , _guid(GetNextResourceGUID())
    {
        if (!glIsTexture(texture->AsRawGLHandle()))
            Throw(::Exceptions::BasicLabel("Binding non-texture as texture resource"));
        _isBackBuffer = false;
    }

    Resource::Resource(const intrusive_ptr<OpenGL::RenderBuffer>& renderbuffer)
    : _underlyingRenderBuffer(renderbuffer)
    , _guid(GetNextResourceGUID())
    {
        if (!glIsRenderbuffer(renderbuffer->AsRawGLHandle()))
            Throw(::Exceptions::BasicLabel("Binding non-render buffer as render buffer resource"));

        _desc = ExtractDesc(renderbuffer.get());
        _isBackBuffer = false;
    }

    Resource::Resource(const intrusive_ptr<OpenGL::Buffer>& buffer)
    : _underlyingBuffer(buffer)
    , _guid(GetNextResourceGUID())
    {
        if (!glIsBuffer(buffer->AsRawGLHandle()))
            Throw(::Exceptions::BasicLabel("Binding non-render buffer as render buffer resource"));

        _desc = ExtractDesc(buffer.get());
        _isBackBuffer = false;
    }

    Resource::Resource() : _isBackBuffer(false), _guid(GetNextResourceGUID()) {}
    Resource::~Resource() {}

    Resource Resource::CreateBackBuffer(const Desc& desc)
    {
        Resource result;
        result._isBackBuffer = true;
        result._desc = desc;
        return result;
    }

    ResourceDesc ExtractDesc(const IResource& input)
    {
        auto* res = (Resource*)const_cast<IResource&>(input).QueryInterface(TypeHashCode<Resource>);
        if (res)
            return res->GetDesc();
        return ResourceDesc{};
    }

    IResourcePtr CreateResource(
        ObjectFactory& factory, const ResourceDesc& desc,
        const IDevice::ResourceInitializer& initData)
    {
        return std::make_shared<Resource>(factory, desc, initData);
    }

    ResourceDesc ExtractDesc(OpenGL::RenderBuffer* renderbuffer)
    {
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer->AsRawGLHandle());
        GLint width = 0, height = 0, internalFormat = 0, samples = 0;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_INTERNAL_FORMAT, &internalFormat);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &samples);

        auto fmt = SizedInternalFormatAsRenderCoreFormat(internalFormat);
        auto components = GetComponents(fmt);
        bool isDepthStencilBuffer =
                components == FormatComponents::Depth
            ||  components == FormatComponents::DepthStencil
            ||  components == FormatComponents::Stencil;

        return CreateDesc(
            isDepthStencilBuffer ? BindFlag::DepthStencil : BindFlag::RenderTarget,
            0, GPUAccess::Write,
            TextureDesc::Plain2D(width, height, fmt, 1, 0, TextureSamples::Create(samples)),
            "");
    }

    ResourceDesc ExtractDesc(OpenGL::Texture* texture)
    {
        assert(0);  // can't access information about the texture using opengles (until 3.1)
        return ResourceDesc{};
    }

    ResourceDesc ExtractDesc(OpenGL::Buffer* buffer)
    {
        GLenum bindingTarget = GL_ARRAY_BUFFER;
        glBindBuffer(bindingTarget, buffer->AsRawGLHandle());
        GLint size = 0, usage = 0;
        glGetBufferParameteriv(bindingTarget, GL_BUFFER_SIZE, &size);
        glGetBufferParameteriv(bindingTarget, GL_BUFFER_USAGE, &usage);

        auto accessFlags = AsAccessBitFields(usage);
        return CreateDesc(
            BindFlag::VertexBuffer | BindFlag::IndexBuffer | BindFlag::ConstantBuffer,
            accessFlags.first, accessFlags.second,
            LinearBufferDesc::Create(size),
            "");
    }

    std::string DescribeUnknownObject(unsigned glName)
    {
        std::stringstream str;
        bool started = false;
        if (glIsBuffer(glName)) {
            auto desc = ExtractDesc((OpenGL::Buffer*)(size_t)glName);
            str << "[Buffer] Bytes: " << desc._linearBufferDesc._sizeInBytes << " BindFlags: 0x" << std::hex << desc._bindFlags << std::dec;
            started = true;
        }
        if (glIsFramebuffer(glName)) {
            if (started) str << std::endl;
            str << "[FrameBuffer]";
            started = true;
        }
        if (glIsProgram(glName)) {
            if (started) str << std::endl;
            str << "[Program]";
            started = true;
        }
        if (glIsQuery(glName)) {
            if (started) str << std::endl;
            str << "[Query]";
            started = true;
        }
        if (glIsRenderbuffer(glName)) {
            if (started) str << std::endl;
            auto desc = ExtractDesc((OpenGL::RenderBuffer*)(size_t)glName);
            str << "[Renderbuffer] " << desc._textureDesc._width << "x" << desc._textureDesc._height;
            started = true;
        }
        if (glIsSampler(glName)) {
            if (started) str << std::endl;
            str << "[Sampler]";
            started = true;
        }
        if (glIsShader(glName)) {
            if (started) str << std::endl;
            str << "[Shader]";
            started = true;
        }
        if (glIsTexture(glName)) {
            if (started) str << std::endl;
            str << "[Texture] ";
            started = true;
        }
        if (glIsTransformFeedback(glName)) {
            if (started) str << std::endl;
            str << "[TransformFeedback]";
            started = true;
        }
        if (glIsVertexArray(glName)) {
            if (started) str << std::endl;
            str << "[VertexArray]";
            started = true;
        }
        return str.str();
    }


    void BlitPass::Write(
        const CopyPartial_Dest& dst,
        const RenderCore::SubResourceInitData& srcData,
        RenderCore::Format srcDataFormat,
        VectorPattern<unsigned, 3> srcDataDimensions)
    {
        auto texelFormatType = RenderCore::Metal_OpenGLES::AsTexelFormatType(srcDataFormat);
        auto oglesContentRes = (RenderCore::Metal_OpenGLES::Resource*)dst._resource->QueryInterface(TypeHashCode<RenderCore::Metal_OpenGLES::Resource>);
        assert(oglesContentRes);

        if (dst._leftTopFront[2] != 0 || srcDataDimensions[2] != 1 || dst._subResource._arrayLayer != 0)
            Throw(std::runtime_error("Only first depth slice and array slice supported for OpenGLES WritePixel operations"));

        auto desc = oglesContentRes->GetDesc();
        if (desc._type != RenderCore::ResourceDesc::Type::Texture)
            Throw(std::runtime_error("Non-texture resource type used with OGLES WritePixel operation"));

        if (dst._subResource._mip >= desc._textureDesc._mipCount)
            Throw(std::runtime_error("Mipmap index used in OGLES WritePixel operation is too high"));

        if ((dst._leftTopFront[0]+srcDataDimensions[0]) > desc._textureDesc._width || (dst._leftTopFront[1]+srcDataDimensions[1]) > desc._textureDesc._height)
            Throw(std::runtime_error("Rectangle dimensions used with OGLES WritePixel operation are outside of the destination texture area"));

        glBindTexture(GL_TEXTURE_2D, oglesContentRes->GetTexture()->AsRawGLHandle());
        if (!_boundTexture) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
        _boundTexture = true;

        auto rowPitch = srcData._pitches._rowPitch;
        if (!rowPitch)
            rowPitch = srcDataDimensions[0] * BitsPerPixel(srcDataFormat) / 8;

        if (rowPitch != CeilToMultiple(srcDataDimensions[0] * BitsPerPixel(srcDataFormat) / 8, 4))
            Throw(std::runtime_error("Row pitch unexpected for GLES Write operation. Expecting densely packed rows."));

        assert((size_t(srcData._data.begin()) % 4) == 0);
        assert((rowPitch % 4) == 0);

        glTexSubImage2D(
            GL_TEXTURE_2D, dst._subResource._mip,
            dst._leftTopFront[0], dst._leftTopFront[1], srcDataDimensions[0], srcDataDimensions[1],
            texelFormatType._format, texelFormatType._type,
            srcData._data.begin());

        CheckGLError("After glTexSubImage2D() upload in BlitPassInstance::Write");
    }

    void BindToFramebuffer(
        GLenum frameBufferTarget,
        GLenum attachmentSlot,
        Resource& res, const TextureViewDesc& viewWindow);

    void BlitPass::Copy(
        const CopyPartial_Dest& dst,
        const CopyPartial_Src& src)
    {
        GLint prevFrameBuffer = ~0u;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFrameBuffer);

        if (src._leftTopFront[2] != 0 || src._rightBottomBack[2] != 1 || dst._leftTopFront[2] != 0)
            Throw(std::runtime_error("Only the first depth slice is supported in BlitPass::Copy for GL. To copy other depth slices of a 3D texture you must use shader based copies."));

        if (src._subResource._mip != 0 || src._subResource._arrayLayer != 0 || dst._subResource._mip != 0 || dst._subResource._arrayLayer != 0)
            Throw(std::runtime_error("Only the first mip level and array layer is supported in BlitPass::Copy for GL."));

        // Using BlitPass::Copy on GL is slow / inefficient. It's not recommended for per-frame use

        GLuint fbs[2];
        glGenFramebuffers(dimof(fbs), fbs);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbs[0]);
        BindToFramebuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *checked_cast<Resource*>(src._resource), {});

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbs[1]);
        BindToFramebuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, *checked_cast<Resource*>(dst._resource), {});

        auto width = src._rightBottomBack[0] - src._leftTopFront[0];
        auto height = src._rightBottomBack[1] - src._leftTopFront[1];

        glBlitFramebuffer(
            src._leftTopFront[0], src._leftTopFront[1], width, height,
            dst._leftTopFront[0], dst._leftTopFront[1], width, height,
            GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, prevFrameBuffer);
        glDeleteFramebuffers(dimof(fbs), fbs);
    }

    BlitPass::BlitPass(RenderCore::IThreadContext& genericContext)
    {
        auto* context = DeviceContext::Get(genericContext).get();
        if (!context)
            Throw(std::runtime_error("Unexpected thread context type passed to BltPassInstance constructor (expecting GLES thread context)"));
        if (context->InRenderPass())
            Throw(::Exceptions::BasicLabel("BlitPassInstance begun while inside of a render pass. This can only be called outside of render passes."));

        _boundTexture = false;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &_prevTextureBinding);

        GLint packRowLength = 0;
        GLint packAlignment = 0;
        glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
        glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
        assert(packRowLength == 0);

        #if !PGDROID
            GLint packSkipRows = 0, packSkipPixels = 0;
            glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
            glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);

            GLint unpackRowLength = 0, unpackSkipRows = 0, unpackSkipPixels = 0, unpackSkipImages =0;
            GLint unpackImageHeight = 0, unpackAlignment = 0;
            glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
            glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
            glGetIntegerv(GL_UNPACK_SKIP_IMAGES, &unpackSkipImages);
            glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &unpackImageHeight);
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);

            GLint unpackBuffer = 0;
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
            assert(unpackBuffer == 0);

            assert(packSkipRows == 0);
            assert(packSkipPixels == 0);

            assert(unpackSkipRows == 0);
            assert(unpackSkipPixels == 0);
            assert(unpackSkipImages == 0);
            assert(unpackImageHeight == 0);

            _prevUnpackAlignment = unpackAlignment;
            _prevUnpackRowLength = unpackRowLength;
        #else
            _prevUnpackAlignment = 0;
            _prevUnpackRowLength = 0;
        #endif
    }

    BlitPass::~BlitPass()
    {
        if (_boundTexture) {
            glBindTexture(GL_TEXTURE_2D, _prevTextureBinding);
            #if !PGDROID
                glPixelStorei(GL_UNPACK_ROW_LENGTH, _prevUnpackRowLength);
                glPixelStorei(GL_UNPACK_ALIGNMENT, _prevUnpackAlignment);
            #endif
        }
    }


}}



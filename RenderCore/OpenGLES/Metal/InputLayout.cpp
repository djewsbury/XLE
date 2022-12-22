// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "InputLayout.h"
#include "Shader.h"
#include "ShaderIntrospection.h"
#include "Format.h"
#include "TextureView.h"
#include "PipelineLayout.h"
#include "State.h"
#include "Resource.h"
#include "DeviceContext.h"
#include "GPUSyncedAllocator.h"
#include "ExtensionFunctions.h"
#include "../../Types.h"
#include "../../Format.h"
#include "../../BufferView.h"
#include "../../../OSServices/Log.h"
#include "../../../Core/SelectConfiguration.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/PtrUtils.h"
#include "../../../Utility/ArithmeticUtils.h"
#include "IncludeGLES.h"
#include <set>
#include <unordered_set>
#include <cctype>

// #define EXTRA_INPUT_LAYOUT_LOGGING

namespace RenderCore { namespace Metal_OpenGLES
{
    std::unordered_set<std::string> g_whitelistedAttributesForBinding;

    bool BoundInputLayout::_warnOnMissingVertexAttribute = true;

    BoundInputLayout::BoundInputLayout(IteratorRange<const InputElementDesc*> layout, const ShaderProgram& program)
    {
            //
            //      For each entry in "layout", we need to compare its name
            //      with the names of the attributes in "shader".
            //
            //      When we find a match, write the details into a binding object.
            //      The binding object will be used to call glVertexAttribPointer.
            //
        const InputElementDesc* elements = layout.begin();
        size_t elementsCount = layout.size();
        _bindings.reserve(elementsCount);
        _attributeState = 0;
        _allAttributesBound = false;
        _vaoBindingHash = 0;

        unsigned vbMax = 0;
        for (const auto&l:layout)
            vbMax = std::max(l._inputSlot, vbMax);

        auto programIndex = program.GetUnderlying();
        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, (GLint*)&_maxVertexAttributes);

        for (unsigned vbIndex = 0; vbIndex <= vbMax; ++vbIndex) {
            auto bindingStart = _bindings.size();
            size_t vertexStride = 0;
            {
                unsigned lastElementEnd = 0;
                for (size_t c=0; c<elementsCount; ++c) {
                    if (elements[c]._inputSlot != vbIndex) continue;

                    const unsigned elementSize  = BitsPerPixel(elements[c]._nativeFormat) / 8;
                    const unsigned elementStart =
                        (elements[c]._alignedByteOffset != ~unsigned(0x0))?elements[c]._alignedByteOffset:lastElementEnd;
                    vertexStride = std::max(vertexStride, size_t(elementStart + elementSize));
                    lastElementEnd = elementStart + elementSize;
                }
            }

            unsigned lastElementEnd = 0;
            for (size_t c=0; c<elementsCount; ++c) {
                if (elements[c]._inputSlot != vbIndex) continue;

                char buffer[64];
                XlCopyString(buffer, elements[c]._semanticName.c_str());
                char append[] { char('0' + elements[c]._semanticIndex), '\0' };
                XlCatString(buffer, dimof(buffer), append);
                GLint attribute = glGetAttribLocation(programIndex->AsRawGLHandle(), buffer);

                const unsigned elementSize      = BitsPerPixel(elements[c]._nativeFormat) / 8;
                const unsigned elementStart     = (elements[c]._alignedByteOffset != ~unsigned(0x0)) ? elements[c]._alignedByteOffset : lastElementEnd;

                    //
                    //  The index can be left off the string for the first
                    //  one
                    //
                if (attribute == -1 && elements[c]._semanticIndex == 0) {
                    attribute = glGetAttribLocation(programIndex->AsRawGLHandle(), elements[c]._semanticName.c_str());
                }

                if (attribute < 0) {
                    //  Binding failure! Write a warning, but ignore it. The binding is
                    //  still valid even if one or more attributes fail
                    Log(Warning) << "Failure during vertex attribute binding. Attribute (" << buffer << ") cannot be found in the program. Ignoring" << std::endl;
                } else {
                    const auto componentType = GetComponentType(elements[c]._nativeFormat);
                    _bindings.push_back({
                            unsigned(attribute),
                            GetComponentCount(GetComponents(elements[c]._nativeFormat)), AsGLVertexComponentType(elements[c]._nativeFormat),
                            (componentType == FormatComponentType::UNorm) || (componentType == FormatComponentType::SNorm) || (componentType == FormatComponentType::UNorm_SRGB),
                            unsigned(vertexStride),
                            elementStart,
                            elements[c]._inputSlotClass == InputDataRate::PerVertex ? 0 : elements[c]._instanceDataStepRate
                        });

                    assert(!(_attributeState & 1<<unsigned(attribute)));
                    _attributeState |= 1<<unsigned(attribute);
                }

                lastElementEnd = elementStart + elementSize;
            }

            if (bindingStart != (unsigned)_bindings.size())
                _bindingsByVertexBuffer.push_back(unsigned(_bindings.size() - bindingStart));
        }

        _allAttributesBound = CalculateAllAttributesBound(program);

        CheckGLError("Construct BoundInputLayout");
    }

    BoundInputLayout::BoundInputLayout(IteratorRange<const SlotBinding*> layouts, const ShaderProgram& program)
    {
        _attributeState = 0;
        _allAttributesBound = false;
        _vaoBindingHash = 0;

        auto programHandle = program.GetUnderlying()->AsRawGLHandle();

        glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, (GLint*)&_maxVertexAttributes);
        
        int activeAttributeCount = 0, activeAttributeMaxLength = 0;
        glGetProgramiv(programHandle, GL_ACTIVE_ATTRIBUTES, &activeAttributeCount);
        glGetProgramiv(programHandle, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &activeAttributeMaxLength);

        GLchar buffer[activeAttributeMaxLength];

        if (!layouts.empty()) {
            std::vector<Binding> workingBindings[layouts.size()];
            unsigned vertexStrides[layouts.size()];
            for (unsigned c=0; c<layouts.size(); ++c)
                vertexStrides[c] = CalculateVertexStride(layouts[c]._elements, false);

            for (int attrIndex=0; attrIndex<activeAttributeCount; ++attrIndex) {
                GLint size; GLenum type;
                GLsizei nameLen;
                glGetActiveAttrib(programHandle, attrIndex, activeAttributeMaxLength, &nameLen, &size, &type, buffer);
                if (!nameLen) continue;
                
                // ignore "gl" system attributes
                if (!strncmp(buffer, "gl_", 3)) continue;

                GLint attrLoc = glGetAttribLocation(programHandle, buffer);

                auto semanticIdx = nameLen;
                while (std::isdigit(buffer[semanticIdx-1])) --semanticIdx;
                uint64_t hash = Hash64(buffer, &buffer[semanticIdx]);
                hash += std::atoi(&buffer[semanticIdx]);

                bool foundBinding = false;
                for (auto l=layouts.begin(); l!=layouts.end(); ++l) {
                    auto i = std::find_if(l->_elements.begin(), l->_elements.end(), [hash](const MiniInputElementDesc&e) { return e._semanticHash == hash; });
                    if (i == l->_elements.end())
                        continue;

                    const auto elementStart = CalculateVertexStride(MakeIteratorRange(l->_elements.begin(), i), false);
                    unsigned slotIdx = (unsigned)std::distance(layouts.begin(), l);

                    const auto componentType = GetComponentType(i->_nativeFormat);
                    workingBindings[slotIdx].push_back({
                        unsigned(attrLoc),
                        GetComponentCount(GetComponents(i->_nativeFormat)), AsGLVertexComponentType(i->_nativeFormat),
                        (componentType == FormatComponentType::UNorm) || (componentType == FormatComponentType::SNorm) || (componentType == FormatComponentType::UNorm_SRGB),
                        vertexStrides[slotIdx],
                        elementStart,
                        l->_instanceStepDataRate
                    });

                    foundBinding = true;
                    break;
                }

                if (foundBinding) {
                    assert(!(_attributeState & 1 << unsigned(attrLoc)));
                    _attributeState |= 1 << unsigned(attrLoc);
                }
            }

            for (unsigned c=0; c<layouts.size(); ++c) {
                _bindings.insert(_bindings.end(), workingBindings[c].begin(), workingBindings[c].end());
                _bindingsByVertexBuffer.push_back(unsigned(workingBindings[c].size()));
            }
            assert(_bindings.size() <= _maxVertexAttributes);
        }

        _allAttributesBound = CalculateAllAttributesBound(program);

        CheckGLError("Construct BoundInputLayout");
    }

    bool BoundInputLayout::CalculateAllAttributesBound(const ShaderProgram& program)
    {
        #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
            _unboundAttributesNames = std::vector<std::string>();
        #endif
        auto programHandle = program.GetUnderlying()->AsRawGLHandle();

        int activeAttributeCount = 0, activeAttributeMaxLength = 0;
        glGetProgramiv(programHandle, GL_ACTIVE_ATTRIBUTES, &activeAttributeCount);
        glGetProgramiv(programHandle, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &activeAttributeMaxLength);
        GLchar buffer[activeAttributeMaxLength];

        for (unsigned attrIndex = 0; attrIndex < activeAttributeCount; ++attrIndex) {
            GLint size; GLenum type;
            GLsizei nameLen;
            glGetActiveAttrib(programHandle, attrIndex, activeAttributeMaxLength, &nameLen, &size, &type, buffer);

            // ignore "gl" system attributes
            if (!strncmp(buffer, "gl_", 3)) continue;
            // ignore whitelisted attributes
            if (g_whitelistedAttributesForBinding.find(std::string(buffer)) != g_whitelistedAttributesForBinding.end()) {
                continue;
            }

            auto location = glGetAttribLocation(programHandle, buffer);

            bool hasBoundAttribute = false;
            for (const auto&b:_bindings) {
                if (b._attributeLocation == location) {
                    hasBoundAttribute = true;
                    break;
                }
            }

            if (!hasBoundAttribute) {
                if (_warnOnMissingVertexAttribute) {
                    Log(Warning) << "Failure during vertex attribute binding. Attribute (" << (const char*)buffer << ") cannot be found in the input binding." << std::endl;
                }
                #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                    _unboundAttributesNames.emplace_back(std::string(buffer));
                #else
                    // return early if not keeping track of unbound attributes
                    return false;
                #endif
            }
        }
        #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
            return _unboundAttributesNames.empty();
        #else
            // since it didn't return early, no attributes are missing
            return true;
        #endif
    }

    #if defined(_DEBUG) && PLATFORMOS == PLATFORMOS_IOS
        static void ArrayBufferCanaryCheck()
        {
            //
            // On IOS, we've run into a case where array buffers created in background contexts
            // will not be valid for use with glVertexAttribPointer. It appears to be a particular
            // bug or restriction within the driver. When it happens, we just get an error code
            // back from glVertexAttribPointer.
            //
            // However, in this case, we will also get a crash if we try to call glMapBufferRange
            // on the buffer in question. Given than IOS has a shared memory model, we are normally
            // always able to map a buffer for reading. So we can check for this condition by
            // calling glMapBufferRange() before the call to glVertexAttribPointer(). If we hit
            // the particular driver bug, we will crash there. Even though it's upgrading a GL
            // error code to a crash, it's makes it easier to distinguish this particular issue
            // from other errors in the glVertexAttribPointer() and can help highlight the
            // problem sooner.
            //
            static unsigned canaryCheckCount = 32;
            if (canaryCheckCount != 32) {
                ++canaryCheckCount;
                return;
            }
            canaryCheckCount = 0;

            GLint arrayBufferBinding0 = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBufferBinding0);
            assert(glIsBuffer(arrayBufferBinding0));

            GLint bufferSize = 0, bufferMapped = 0, bufferUsage = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &bufferMapped);
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &bufferUsage);

            void* mappedData = glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, GL_MAP_READ_BIT);
            assert(mappedData);
            (void)mappedData;
            glUnmapBuffer(GL_ARRAY_BUFFER);
        }
    #else
        inline void ArrayBufferCanaryCheck() {}
    #endif

    uint32_t BoundInputLayout::UnderlyingApply(DeviceContext& devContext, IteratorRange<const VertexBufferView*> vertexBuffers, bool cancelOnError) const never_throws
    {
        if (cancelOnError) {
            auto error = glGetError();
            if (error != GL_NO_ERROR) {
                return error;
            }
        }

        auto featureSet = devContext.GetFeatureSet();

        uint32_t instanceFlags = 0u;
        uint32_t instanceDataRate[32];

        unsigned attributeIterator = 0;
        for (unsigned b=0; b<unsigned(_bindingsByVertexBuffer.size()); ++b) {
            auto bindingCount = _bindingsByVertexBuffer[b];
            assert(b < vertexBuffers.size());
            if (!bindingCount) continue;
            const auto& vb = vertexBuffers[b];
            glBindBuffer(GL_ARRAY_BUFFER, GetBufferRawGLHandle(*vb._resource));
            ArrayBufferCanaryCheck();
            for(auto ai=attributeIterator; ai<(attributeIterator+bindingCount); ++ai) {
                const auto& i = _bindings[ai];
                glVertexAttribPointer(
                    i._attributeLocation, i._size, i._type, i._isNormalized,
                    i._stride,
                    (const void*)(size_t)(vb._offset + i._offset));

                instanceFlags |= ((i._instanceDataRate!=0)<<i._attributeLocation);
                instanceDataRate[i._attributeLocation] = i._instanceDataRate;
            }
            attributeIterator += bindingCount;
        }

        if (cancelOnError) {
            auto error = glGetError();
            if (error != GL_NO_ERROR) {
                return error;
            }
        }

        auto* capture = devContext.GetCapturedStates();
        if (capture) {
            if (featureSet & FeatureSet::GLES300) {
                auto differences = (capture->_instancedVertexAttrib & _attributeState) | instanceFlags;
                if (differences) {
                    int firstActive = xl_ctz4(differences);
                    int lastActive = 32u - xl_clz4(differences);
                    for (int c=firstActive; c<lastActive; ++c)
                        if (_attributeState & (1<<c))
                            glVertexAttribDivisor(c, instanceDataRate[c]);
                    capture->_instancedVertexAttrib = (capture->_instancedVertexAttrib & ~_attributeState) | instanceFlags;
                }
            } else {
                auto differences = (capture->_instancedVertexAttrib & _attributeState) | instanceFlags;
                if (differences) {
                    #if GL_ARB_instanced_arrays
                        int firstActive = xl_ctz4(differences);
                        int lastActive = 32u - xl_clz4(differences);
                        for (int c=firstActive; c<lastActive; ++c)
                            if (_attributeState & (1<<c))
                                glVertexAttribDivisorARB(c, instanceDataRate[c]);
                        capture->_instancedVertexAttrib = (capture->_instancedVertexAttrib & ~_attributeState) | instanceFlags;
                    #else
                        assert(0);  // no hardware support for variable rate input attributes
                    #endif
                }
            }

            // set enable/disable flags --
            // Note that this method cannot support more than 32 vertex attributes

            auto differences = capture->_activeVertexAttrib ^ _attributeState;
            if (differences) {
                int firstActive = xl_ctz4(differences);
                int lastActive = 32u - xl_clz4(differences);

                for (int c=firstActive; c<lastActive; ++c)
                    if (_attributeState & (1<<c)) {
                        glEnableVertexAttribArray(c);
                    } else {
                        glDisableVertexAttribArray(c);
                    }

                capture->_activeVertexAttrib = _attributeState;
            }
        } else {
            if (featureSet & FeatureSet::GLES300) {
                for (int c=0; c<std::min(32u, _maxVertexAttributes); ++c)
                    if (_attributeState & (1<<c))
                        glVertexAttribDivisor(c, instanceDataRate[c]);
            } else {
                #if GL_ARB_instanced_arrays
                    for (int c=0; c<std::min(32u, _maxVertexAttributes); ++c)
                        if (_attributeState & (1<<c))
                            glVertexAttribDivisorARB(c, instanceDataRate[c]);
                #else
                    for (int c=0; c<std::min(32u, _maxVertexAttributes); ++c)
                        if (_attributeState & (1<<c)) {
                            assert(instanceDataRate[c] == 0); // no hardware support for variable rate input attributes
                        }
                #endif
            }
            for (int c=0; c<std::min(32u, _maxVertexAttributes); ++c)
                if (_attributeState & (1<<c)) {
                    glEnableVertexAttribArray(c);
                } else {
                    glDisableVertexAttribArray(c);
                }
        }

        if (cancelOnError) {
            auto error = glGetError();
            if (error != GL_NO_ERROR) {
                return error;
            }
        }

        CheckGLError("Apply BoundInputLayout");
        return GL_NO_ERROR;
    }

    static void UnderlyingBindVAO(DeviceContext& devContext, RawGLHandle vao)
    {
        auto featureSet = devContext.GetFeatureSet();
        if (featureSet & FeatureSet::GLES300) {
            glBindVertexArray(vao);
        } else {
            #if GL_APPLE_vertex_array_object
                glBindVertexArrayAPPLE(vao);
            #else
                assert(OpenGL::g_bindVertexArray);
                (*OpenGL::g_bindVertexArray)(vao);
            #endif
        }
    }

    static void BindVAO(DeviceContext& devContext, RawGLHandle vao)
    {
        auto* capture = devContext.GetCapturedStates();
        if (capture) {
            capture->VerifyIntegrity();
            if (capture->_boundVAO == vao) return;
            capture->_boundVAO = vao;
        }

        UnderlyingBindVAO(devContext, vao);
    }

    static uint64_t Hash(IteratorRange<const VertexBufferView*> vertexBuffers)
    {
        auto hash = DefaultSeed64;
        for (auto& vbv:vertexBuffers) {
            hash = HashCombine(hash, vbv._resource->GetGUID());
            if (vbv._offset)
                hash = HashCombine(hash, vbv._offset);
        }
        return hash;
    }

    #if defined(_DEBUG) && !PGDROID
        void BoundInputLayout::ValidateVAO(DeviceContext& devContext, IteratorRange<const VertexBufferView*> vertexBuffers) const
        {
            GLint prevVAO = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);

            auto featureSet = devContext.GetFeatureSet();
            if (featureSet & FeatureSet::GLES300) {
                assert(glIsVertexArray(_vao->AsRawGLHandle()));
            } else {
                #if GL_APPLE_vertex_array_object
                    assert(glIsVertexArrayAPPLE(_vao->AsRawGLHandle()));
                #else
                    assert(OpenGL::g_isVertexArray);
                    assert((*OpenGL::g_isVertexArray)(_vao->AsRawGLHandle()));
                #endif
            }
            UnderlyingBindVAO(devContext, _vao->AsRawGLHandle());

            for (unsigned c=0; c<_maxVertexAttributes; ++c) {
                GLint isEnabled = 0;
            glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &isEnabled);
                assert(!!(_attributeState & (1<<c)) == isEnabled);
                if (!isEnabled) continue;

                auto i = std::find_if(
                    _bindings.begin(), _bindings.end(),
                    [c](const Binding& binding) {
                        return binding._attributeLocation == c;
                    });
                assert(i!=_bindings.end());
                size_t bindingIdx = std::distance(_bindings.begin(), i);
                auto vb=_bindingsByVertexBuffer.begin();
                for (; vb!=_bindingsByVertexBuffer.end(); ++i) {
                    if (bindingIdx < *vb)
                        break;
                    bindingIdx -= *vb;
                }
                assert(vb != _bindingsByVertexBuffer.end());
                size_t vbIdx = std::distance(_bindingsByVertexBuffer.begin(), vb);
                assert(vbIdx < vertexBuffers.size());

                GLint arrayBufferBinding = 0;
                glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &arrayBufferBinding);
                assert(arrayBufferBinding == GetBufferRawGLHandle(*vertexBuffers[vbIdx]._resource));

                GLint size = 0, stride = 0, type = 0, normalized = 0;
                glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
                glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
                glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
                glGetVertexAttribiv(c, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized);
                assert(size == i->_size);
                assert(stride == i->_stride);
                assert(type == i->_type);
                assert(normalized == i->_isNormalized);

                GLvoid *pointer = nullptr;
                glGetVertexAttribPointerv(c, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);
                assert(pointer == (const void*)(size_t)(vertexBuffers[vbIdx]._offset + i->_offset));
            }

            UnderlyingBindVAO(devContext, prevVAO);
        }
    #endif

    void BoundInputLayout::Apply(DeviceContext& devContext, IteratorRange<const VertexBufferView*> vertexBuffers) const never_throws
    {
        if (_vao) {
            // The "vao" binds this input layout to a specific set of vertex buffers (passed to CreateVAO())
            // If you hit this assert, it means that the vertex buffers passed to CreateVAO don't match what's
            // passed to this function. That won't work; you need to either clone the BoundInputLayout for
            // each set of vertex buffers you want to use, or just don't call CreateVAO at all.
            assert(_vaoBindingHash == Hash(vertexBuffers));
            #if defined(_DEBUG) && !PGDROID
                ValidateVAO(devContext, vertexBuffers);
            #endif
            BindVAO(devContext, _vao->AsRawGLHandle());
        } else {
            BindVAO(devContext, 0);
            UnderlyingApply(devContext, vertexBuffers, false);
        }
    }

    void BoundInputLayout::CreateVAO(DeviceContext& devContext, IteratorRange<const VertexBufferView*> vertexBuffers)
    {
        _vao = nullptr;

        _vao = GetObjectFactory(devContext).CreateVAO();
        if (!_vao) return;

        auto* originalCapture = devContext.GetCapturedStates();
        if (originalCapture)
            devContext.EndStateCapture();

        BindVAO(devContext, _vao->AsRawGLHandle());
        auto error = UnderlyingApply(devContext, vertexBuffers, true);
        if (error != GL_NO_ERROR) {
            Log(Warning) << "Encountered OpenGL error (0x" << std::hex << error << std::dec << ") while attempt to create VAO. Dropping back to non-VAO path. Note that without VAOs performance will be severely impacted." << std::endl;
            _vao = nullptr;
            _vaoBindingHash = 0;
        } else {
            _vaoBindingHash = Hash(vertexBuffers);
        }

        // Reset cached state in devContext
        // When a vao other than 0 is bound, it's unclear to me how calls to glEnableVertexAttribArray, etc,
        // affect VAO 0. Let's just play safe, and reset everything to default, and clear out the cached values
        // in DeviceContext
        BindVAO(devContext, 0);
        if (originalCapture)
            devContext.BeginStateCapture(*originalCapture);
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    class ShaderProgramCapturedState
    {
    public:
        struct Structs
        {
            uint64_t _cbNameHash;
            uint64_t _boundContents;
            unsigned _deviceContextCaptureGUID;
        };
        std::vector<Structs> _structs;

        struct UniformBuffers
        {
            uint64_t _cbNameHash;
            unsigned _boundBuffer;
            unsigned _deviceContextCaptureGUID;
            unsigned _rangeBegin = 0;
            unsigned _rangeEnd = 0;
            uint64_t _boundContents;
        };
        std::vector<UniformBuffers> _uniformBuffers;

        unsigned GetCBIndex(uint64_t hashName)
        {
            auto i = std::find_if(
                _structs.begin(), _structs.end(),
                [hashName](const Structs& cb) { return cb._cbNameHash == hashName; });
            if (i == _structs.end()) return ~0u;
            return (unsigned)std::distance(_structs.begin(), i);
        }

        ShaderProgramCapturedState(const ShaderIntrospection& introspection)
        {
            _structs.reserve(introspection.GetStructs().size());
            for (const auto&s:introspection.GetStructs()) {
                _structs.push_back(Structs{s.first, 0, 0});
            }

            _uniformBuffers.reserve(introspection.GetUniformBlocks().size());
            for (const auto&s:introspection.GetUniformBlocks()) {
                _uniformBuffers.push_back(UniformBuffers{s.first, 0, 0});
            }
        }
    };

////////////////////////////////////////////////////////////////////////////////////////////////////

    unsigned BoundUniforms::s_uniformSetAccumulator = 0;
    unsigned BoundUniforms::s_redundantUniformSetAccumulator = 0;
    bool BoundUniforms::s_doRedundantUniformSetReduction = true;

    void BoundUniforms::Apply(
        DeviceContext& context,
        unsigned streamIdx,
        const UniformsStream& stream) const
    {
        // this could be improved by ordering _cbs, _srvs by stream idx -- that way we wouldn't
        // have to spend so much time searching through for the right bindings
        for (const auto&cb:_cbs) {
            if (cb._stream != streamIdx) continue;

            #if defined(_DEBUG)
                if (cb._slot >= stream._constantBuffers.size())
                    Throw(::Exceptions::BasicLabel("Uniform stream does not include Constant Buffer for bound resource. Expected CB bound at index (%u) of stream (%u). Only (%u) CBs were provided in the UniformsStream passed to BoundUniforms::Apply", cb._slot, cb._stream, stream._constantBuffers.size()));
            #endif

            assert(!cb._commandGroup._commands.empty());

            const auto& cbv = stream._constantBuffers[cb._slot];
            const auto& pkt = cbv._packet;
            IteratorRange<const void*> pktData;
            uint64_t pktHash = 0;
            if (pkt.size() != 0) {
                pktHash = pkt.GetHash();
                pktData = MakeIteratorRange(pkt.begin(), pkt.end());
                assert(cbv._prebuiltRangeBegin == 0 && cbv._prebuiltRangeEnd == 0);     // we don't support using partial parts of the constant buffer in this case
            } else {
                pktHash = ((Resource*)cbv._prebuiltBuffer)->GetConstantBufferHash();
                pktData = ((Resource*)cbv._prebuiltBuffer)->GetConstantBuffer();
                pktData.first = std::min(pktData.end(), PtrAdd(pktData.begin(), cbv._prebuiltRangeBegin));
                pktData.second = std::min(pktData.end(), PtrAdd(pktData.begin(), cbv._prebuiltRangeEnd));
            }

            if (!pktData.empty()) {
                assert(cb._capturedStateIndex < _capturedState->_structs.size());
                auto& capturedState = _capturedState->_structs[cb._capturedStateIndex];
                bool redundantSet = false;
                if (pktHash != 0 && context.GetCapturedStates() && s_doRedundantUniformSetReduction) {
                    redundantSet = capturedState._boundContents == pktHash && capturedState._deviceContextCaptureGUID == context.GetCapturedStates()->_captureGUID;
                    capturedState._boundContents = pktHash;
                    capturedState._deviceContextCaptureGUID = context.GetCapturedStates()->_captureGUID;
                }
                if (!redundantSet) {
                    s_uniformSetAccumulator += Bind(context, cb._commandGroup, pktData);
                } else {
                    s_redundantUniformSetAccumulator += (unsigned)cb._commandGroup._commands.size();
                }
            }
        }

        auto* capture = context.GetCapturedStates();

        for (const auto&srv:_srvs) {
            if (srv._stream != streamIdx) continue;

            #if defined(_DEBUG)
                if (srv._slot >= stream._resources.size())
                    Throw(::Exceptions::BasicLabel("Uniform stream does not include SRV for bound resource. Expected SRV bound at index (%u) of stream (%u). Only (%u) SRVs were provided in the UniformsStream passed to BoundUniforms::Apply", srv._slot, srv._stream, stream._resources.size()));
            #endif

            const auto& res = *(ShaderResourceView*)stream._resources[srv._slot];
            const auto& sampler = *(SamplerState*)stream._samplers[srv._slot];

            if (res.GetResource()) {
                if (capture) {
                    if (capture->_activeTextureIndex != srv._textureUnit) {
                        glActiveTexture(GL_TEXTURE0 + srv._textureUnit);
                        capture->_activeTextureIndex = srv._textureUnit;
                    }
                    glBindTexture(srv._dimensionality, res.GetUnderlying()->AsRawGLHandle());
                    sampler.Apply(*capture, srv._textureUnit, srv._dimensionality, res.GetResource().get(), res.HasMipMaps());
                } else {
                    glActiveTexture(GL_TEXTURE0 + srv._textureUnit);
                    glBindTexture(srv._dimensionality, res.GetUnderlying()->AsRawGLHandle());
                    sampler.Apply(srv._textureUnit, srv._dimensionality, res.HasMipMaps());
                }
            } else {
                #if 0 // defined(_DEBUG)
                    Log(Warning) << "Null resource while binding SRV to texture uniform (" << srv._name << ")" << std::endl;
                #endif
            }
        }

        #if defined(GL_ES_VERSION_3_0) && !PGDROID
            if (GetObjectFactory().GetFeatureSet() & FeatureSet::GLES300) {
                auto& reusableTemporarySpace = GetObjectFactory().GetReusableCBSpace();
                for (const auto&uniformBuffer:_uniformBuffer) {
                    if (uniformBuffer._stream != streamIdx) continue;
                    assert(uniformBuffer._slot < stream._constantBuffers.size());

                    assert(uniformBuffer._uniformBlockIdx < _capturedState->_uniformBuffers.size());
                    auto& capturedState = _capturedState->_uniformBuffers[uniformBuffer._uniformBlockIdx];

                    const auto& cbv = stream._constantBuffers[uniformBuffer._slot];
                    const auto& pkt = cbv._packet;
                    if (pkt.size() != 0) {
                        unsigned offset = reusableTemporarySpace.Write(context, pkt.AsIteratorRange());
                        if (offset != ~0u) {
                            assert(pkt.size() >= uniformBuffer._blockSize);        // the provided buffer must be large enough, or we can get GPU crashes on IOS

                            auto hash = pkt.GetHash();
                            bool redundantSet = false;
                            if (hash != 0 && context.GetCapturedStates() && s_doRedundantUniformSetReduction) {
                                redundantSet = capturedState._boundContents == hash && capturedState._deviceContextCaptureGUID == context.GetCapturedStates()->_captureGUID;
                                capturedState._boundContents = hash;
                                capturedState._deviceContextCaptureGUID = context.GetCapturedStates()->_captureGUID;
                            }

                            if (!redundantSet) {
                                glBindBufferRange(
                                    GL_UNIFORM_BUFFER,
                                    uniformBuffer._uniformBlockIdx, // mapped 1:1 with uniform buffer binding points
                                    reusableTemporarySpace.GetBuffer().GetUnderlying()->AsRawGLHandle(),
                                    offset, pkt.size());
                                capturedState._boundBuffer = 0;
                            } else {
                                s_redundantUniformSetAccumulator += (unsigned)pkt.size();
                            }
                        } else {
                            Log(Error) << "Allocation failed on dynamic CB buffer. Cannot write uniform values." << std::endl;
                        }
                    } else {
                        assert(((IResource*)cbv._prebuiltBuffer)->QueryInterface(TypeHashCode<Resource>));
                        auto* res = checked_cast<const Resource*>(cbv._prebuiltBuffer);

                        if (!res->GetConstantBuffer().empty()) {

                            // note -- we don't do redundant change checking in this case. The wrapping
                            // behaviour in the DynamicBuffer class makes it more difficult -- because
                            // we can overwrite the part of the buffer that was previously bound and
                            // thereby mark some updates as incorrectly redundant.

                            auto data = res->GetConstantBuffer();
                            if (cbv._prebuiltRangeBegin != 0 || cbv._prebuiltRangeEnd != 0) {
                                auto base = data;
                                data.first = std::min(base.second, PtrAdd(base.first, cbv._prebuiltRangeBegin));
                                data.second = std::min(base.second, PtrAdd(base.first, cbv._prebuiltRangeEnd));
                            }
                            unsigned offset = reusableTemporarySpace.Write(context, data);
                            assert(data.size() >= uniformBuffer._blockSize);        // the provided buffer must be large enough, or we can get GPU crashes on IOS
                            glBindBufferRange(
                                GL_UNIFORM_BUFFER,
                                uniformBuffer._uniformBlockIdx, // mapped 1:1 with uniform buffer binding points
                                reusableTemporarySpace.GetBuffer().GetUnderlying()->AsRawGLHandle(),
                                offset, data.size());
                            capturedState._boundBuffer = 0;
                            capturedState._boundContents = 0;

                        } else {
                            auto glHandle = res->GetBuffer()->AsRawGLHandle();
                            bool isRedundant = false;
                            if (context.GetCapturedStates()) {
                                isRedundant = capturedState._boundBuffer == glHandle
                                    &&  capturedState._deviceContextCaptureGUID == context.GetCapturedStates()->_captureGUID
                                    &&  capturedState._rangeBegin == cbv._prebuiltRangeBegin
                                    &&  capturedState._rangeEnd == cbv._prebuiltRangeEnd;

                                if (!isRedundant) {
                                    capturedState._boundBuffer = res->GetBuffer()->AsRawGLHandle();
                                    capturedState._deviceContextCaptureGUID = context.GetCapturedStates()->_captureGUID;
                                    capturedState._rangeBegin = cbv._prebuiltRangeBegin;
                                    capturedState._rangeEnd = cbv._prebuiltRangeEnd;
                                    capturedState._boundContents = 0;
                                }
                            }

                            if (!isRedundant) {
                                if (cbv._prebuiltRangeBegin != 0 || cbv._prebuiltRangeEnd != 0) {
                                    assert((cbv._prebuiltRangeEnd-cbv._prebuiltRangeBegin) >= uniformBuffer._blockSize);        // the provided buffer must be large enough, or we can get GPU crashes on IOS
                                    glBindBufferRange(
                                        GL_UNIFORM_BUFFER,
                                        uniformBuffer._uniformBlockIdx,
                                        glHandle,
                                        cbv._prebuiltRangeBegin, cbv._prebuiltRangeEnd-cbv._prebuiltRangeBegin);
                                } else {
                                    assert(res->GetDesc()._linearBufferDesc._sizeInBytes >= uniformBuffer._blockSize);        // the provided buffer must be large enough, or we can get GPU crashes on IOS
                                    glBindBufferBase(GL_UNIFORM_BUFFER, uniformBuffer._uniformBlockIdx, glHandle);
                                }
                            }
                        }
                    }
                }

                if (streamIdx == 0) {
                    for (const auto&block:_unboundUniformBuffers) {
                        assert(block._uniformBlockIdx < _capturedState->_uniformBuffers.size());
                        auto& capturedState = _capturedState->_uniformBuffers[block._uniformBlockIdx];

                        std::vector<uint8_t> tempBuffer(block._blockSize, 0);
                        unsigned offset = reusableTemporarySpace.Write(context, MakeIteratorRange(tempBuffer));
                        if (offset != ~0u) {
                            glBindBufferRange(
                                GL_UNIFORM_BUFFER,
                                block._uniformBlockIdx, // mapped 1:1 with uniform buffer binding points
                                reusableTemporarySpace.GetBuffer().GetUnderlying()->AsRawGLHandle(),
                                offset, tempBuffer.size());
                            capturedState._boundBuffer = 0;
                        } else {
                            Log(Error) << "Allocation failed on dynamic CB buffer. Cannot write uniform values." << std::endl;
                        }
                    }
                }
            }
        #endif

        // Commit changes to texture uniforms
        // This must be done separately to the texture binding, because when using array uniforms,
        // a single uniform set operation can be used for multiple texture bindings
        if (streamIdx == 0) {
            if (!_textureAssignmentCommands._commands.empty()) {
                Bind(context, _textureAssignmentCommands, MakeIteratorRange(_textureAssignmentByteData));

                if (_standInTexture2DUnit != ~0u) {
                    if (capture) {
                        if (capture->_activeTextureIndex != _standInTexture2DUnit) {
                            glActiveTexture(GL_TEXTURE0 + _standInTexture2DUnit);
                            capture->_activeTextureIndex = _standInTexture2DUnit;
                        }
                        glBindTexture(GL_TEXTURE_2D, GetObjectFactory(context).StandIn2DTexture());
                        SamplerState().Apply(*capture, _standInTexture2DUnit, GL_TEXTURE_2D, nullptr, false);
                    } else {
                        glActiveTexture(GL_TEXTURE0 + _standInTexture2DUnit);
                        glBindTexture(GL_TEXTURE_2D, GetObjectFactory(context).StandIn2DTexture());
                        SamplerState().Apply(_standInTexture2DUnit, GL_TEXTURE_2D, false);
                    }
                }

                if (_standInTextureCubeUnit != ~0u) {
                    if (capture) {
                        if (capture->_activeTextureIndex != _standInTextureCubeUnit) {
                            glActiveTexture(GL_TEXTURE0 + _standInTextureCubeUnit);
                            capture->_activeTextureIndex = _standInTextureCubeUnit;
                        }
                        glBindTexture(GL_TEXTURE_CUBE_MAP, GetObjectFactory(context).StandInCubeTexture());
                        SamplerState().Apply(*capture, _standInTextureCubeUnit, GL_TEXTURE_2D, nullptr, false);
                    } else {
                        glActiveTexture(GL_TEXTURE0 + _standInTextureCubeUnit);
                        glBindTexture(GL_TEXTURE_CUBE_MAP, GetObjectFactory(context).StandInCubeTexture());
                        SamplerState().Apply(_standInTextureCubeUnit, GL_TEXTURE_CUBE_MAP, false);
                    }
                }
            }
        }

        CheckGLError("Apply BoundUniforms");
    }

    static GLenum DimensionalityForUniformType(GLenum uniformType)
    {
        switch (uniformType) {
        case GL_SAMPLER_2D:
        case GL_SAMPLER_2D_SHADOW:
        case GL_INT_SAMPLER_2D:
        case GL_UNSIGNED_INT_SAMPLER_2D:
            return GL_TEXTURE_2D;

        case GL_SAMPLER_CUBE:
        case GL_SAMPLER_CUBE_SHADOW:
        case GL_INT_SAMPLER_CUBE:
        case GL_UNSIGNED_INT_SAMPLER_CUBE:
            return GL_TEXTURE_CUBE_MAP;

        // array types:
        // case GL_SAMPLER_2D_ARRAY:
        // case GL_SAMPLER_2D_ARRAY_SHADOW:
        // case GL_INT_SAMPLER_2D_ARRAY:
        // case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:

        // 3D texture types:
        case GL_SAMPLER_3D:
        case GL_INT_SAMPLER_3D:
        case GL_UNSIGNED_INT_SAMPLER_3D:

        default:
            return GL_NONE;
        }
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    static bool HasCBBinding(IteratorRange<const UniformsStreamInterface**> interfaces, uint64_t hash)
    {
        for (auto interf:interfaces) {
            auto i = std::find_if(
                interf->_cbBindings.begin(),
                interf->_cbBindings.end(),
                [hash](const UniformsStreamInterface::RetainedCBBinding& b) {
                    return b._hashName == hash;
                });
            if (i!=interf->_cbBindings.end())
                return true;
        }
        return false;
    }

    static bool HasSRVBinding(IteratorRange<const UniformsStreamInterface**> interfaces, uint64_t hash)
    {
        for (auto interf:interfaces) {
            auto i = std::find(interf->_srvBindings.begin(), interf->_srvBindings.end(), hash);
            if (i!=interf->_srvBindings.end())
                return true;
        }
        return false;
    }

    static std::string AdaptNameForIndex(const std::string& name, unsigned elementIdx, unsigned uniformElementCount)
    {
        // If the input uniform name already has [], then modify the string to insert the particular
        // element idx we're actually interested in... 
        // Otherwise we will just append the indexor to the end of the string name
        if (name.size() >= 2 && name[name.size()-1] == ']') {
            auto i = name.begin() + (name.size()-2);
            while (i > name.begin() && *i >= '0' && *i <= '9') --i;
            if (*i == '[')
                return std::string(name.begin(), i+1) + std::to_string(elementIdx) + "]";
        }

        if (elementIdx != 0 || uniformElementCount > 1)
            return name + "[" + std::to_string(elementIdx) + "]";
        return name;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    #if defined(_DEBUG)
        static const std::string s_memberNamedDisabled = "<<introspection named disabled>>";
        static void ValidateCBElements(
            IteratorRange<const ConstantBufferElementDesc*> elements,
            const ShaderIntrospection::UniformBlock& block)
        {
            // Every member of the struct must be in the "elements", and offsets and types must match
            for (const auto& member:block._uniforms) {

                std::string memberName;
                #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                    memberName = member._name;
                #else
                    memberName = s_memberNamedDisabled;
                #endif

                auto hashName = member._bindingName;
                auto i = std::find_if(
                    elements.begin(), elements.end(),
                    [hashName](const ConstantBufferElementDesc& t) { return t._semanticHash == hashName; });
                if (i == elements.end())
                    Throw(::Exceptions::BasicLabel("Missing CB binding for element name (%s)", memberName.c_str()));

                if (i->_offset != member._location)
                    Throw(::Exceptions::BasicLabel("CB element offset is incorrect for member (%s). It's (%i) in the shader, but (%i) in the binding provided",
                        memberName.c_str(), member._location, i->_offset));

                if (std::max(1u, i->_arrayElementCount) != std::max(1u, unsigned(member._elementCount)))
                    Throw(::Exceptions::BasicLabel("CB element array element count is incorrect for member (%s). It's (%i) in the shader, but (%i) in the binding provided",
                        memberName.c_str(), member._elementCount, i->_arrayElementCount));

                auto f = AsFormat(GLUniformTypeAsTypeDesc(member._type));
                if (i->_nativeFormat != f)
                    Throw(::Exceptions::BasicLabel("CB element type is incorrect for member (%s). It's (%s) in the shader, but (%s) in the binding provided",
                        memberName.c_str(), AsString(f), AsString(i->_nativeFormat)));
            }
        }
    #endif

////////////////////////////////////////////////////////////////////////////////////////////////////

    BoundUniforms::BoundUniforms(
        const ShaderProgram& shader,
        const PipelineLayoutConfig& pipelineLayout,
        const UniformsStreamInterface& interface0,
        const UniformsStreamInterface& interface1,
        const UniformsStreamInterface& interface2,
        const UniformsStreamInterface& interface3)
    {
        for (auto&v:_boundUniformBufferSlots) v = 0u;
        for (auto&v:_boundResourceSlots) v = 0u;

        auto introspection = ShaderIntrospection(shader);
        _capturedState = shader._capturedState;
        if (!_capturedState) {
            _capturedState = shader._capturedState = std::make_shared<ShaderProgramCapturedState>(introspection);
        }

        unsigned textureUnitAccumulator = 0;

        struct UniformSet { int _location; unsigned _index, _value; GLenum _type; int _elementCount; };
        std::vector<UniformSet> srvUniformSets;

        #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
            std::set<uint64_t> boundGlobalUniforms;
            std::set<uint64_t> boundUniformStructs;
        #endif

        const UniformsStreamInterface* inputInterface[] = { &interface0, &interface1, &interface2, &interface3 };
        auto streamCount = (unsigned)dimof(inputInterface);
        // We bind the 4 streams with reversed order, so that material textures can be bound before global textures
        // (ex. shadowmap). This can help avoid the situation where shadowmap end up being bound at texture unit 0,
        // which is prone to GL errors caused by shader bugs (one example is when a shader mistakenly sample from
        // unbound texture samplers, which has default binding 0, and shadowmap happens to be bound at texture unit 0
        // but with a comparison sampler).
        for (int s=streamCount-1; s>=0; --s) {
            const auto& interf = *inputInterface[s];
            for (unsigned slot=0; slot<interf._cbBindings.size(); ++slot) {
                const auto& binding = interf._cbBindings[slot];

                // Ensure it's not shadowed by bindings from streams with higher index
                // Note that we don't enforce this for bindings to global uniforms. This allows the client to
                // provide multiple bindings for global, wherein each only binds a subset of all of the global uniforms
                if (binding._hashName != 0) {
                    if (HasCBBinding(MakeIteratorRange(&inputInterface[s+1], &inputInterface[dimof(inputInterface)]), binding._hashName)) continue;

                    #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                        assert(boundUniformStructs.find(binding._hashName) == boundUniformStructs.end());
                        boundUniformStructs.insert(binding._hashName);
                    #endif
                }

                // Try binding either as a command group or as a uniform block
                if (!binding._elements.empty()) {
                    auto cmdGroup = introspection.MakeBinding(binding._hashName, MakeIteratorRange(binding._elements));
                    if (!cmdGroup._commands.empty()) {
                        _cbs.emplace_back(CB{(unsigned)s, slot, std::move(cmdGroup),
                            _capturedState->GetCBIndex(binding._hashName)
                            #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                                , cmdGroup._name
                            #endif
                            });
                        _boundUniformBufferSlots[s] |= (1ull << uint64_t(slot));
                    }
                }

                auto block = introspection.FindUniformBlock(binding._hashName);
                if (block._blockIdx != ~0u) {
                    _uniformBuffer.emplace_back(UniformBuffer{
                        (unsigned)s, slot, block._blockIdx, block._blockSize
                        #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                            , block._name
                        #endif
                        });
                    _boundUniformBufferSlots[s] |= (1ull << uint64_t(slot));

                    #if defined(_DEBUG)
                        ValidateCBElements(MakeIteratorRange(binding._elements), block);
                    #endif
                }
            }

            for (unsigned slot=0; slot<interf._srvBindings.size(); ++slot) {
                auto binding = interf._srvBindings[slot];
                if (!binding) continue;

                // ensure it's not shadowed by bindings from streams with higher index
                if (HasSRVBinding(MakeIteratorRange(&inputInterface[s+1], &inputInterface[dimof(inputInterface)]), binding)) continue;

                auto uniform = introspection.FindUniform(binding);
                if (uniform._elementCount != 0) {
                    #if defined(_DEBUG)
                        boundGlobalUniforms.insert(uniform._bindingName);
                    #endif
                    // assign a texture unit for this binding
                    auto textureUnit = pipelineLayout.GetFixedTextureUnit(binding);
                    if (textureUnit == ~0u) {
                        textureUnit = pipelineLayout.GetFlexibleTextureUnit(textureUnitAccumulator);
                        textureUnitAccumulator++;
                    }
                    auto dim = DimensionalityForUniformType(uniform._type);
                    auto elementIndex = unsigned(binding - uniform._bindingName);
                    assert(dim != GL_NONE);
                    _srvs.emplace_back(SRV{(unsigned)s, slot, textureUnit, dim
                        #if defined(_DEBUG)
                            , AdaptNameForIndex(uniform._name, elementIndex, uniform._elementCount)
                        #endif
                        });

                    #if defined(_DEBUG) && defined(EXTRA_INPUT_LAYOUT_LOGGING)
                        Log(Verbose)
                            << "Selecting texunit (" << textureUnit << ") for uniform "
                            << AdaptNameForIndex(uniform._name, elementIndex, uniform._elementCount) << " in stream " << s << ", slot " << slot
                            << std::endl;
                    #endif

                    // Record the command to set the uniform. Note that this
                    // is made a little more complicated due to array uniforms.
                    assert((binding - uniform._bindingName) < uniform._elementCount);
                    srvUniformSets.push_back({uniform._location, elementIndex, textureUnit, uniform._type, uniform._elementCount});

                    _boundResourceSlots[s] |= (1ull << uint64_t(slot));
                }
            }
        }

        auto globalsStruct = introspection.FindStruct(0);
        #if defined(_DEBUG)
            for (const auto&u:globalsStruct._uniforms) {
                if (boundGlobalUniforms.find(u._bindingName) == boundGlobalUniforms.end()) {
                    Log(Verbose) << "Didn't get binding for global uniform (" << u._name << ") in BoundUniforms constructor" << std::endl;
                    _unboundUniforms.push_back(u);

                    /*if (DimensionalityForUniformType(u._type) != GL_NONE) {
                        auto textureUnit = pipelineLayout.GetFlexibleTextureUnit(textureUnitAccumulator);
                        textureUnitAccumulator++;
                        srvUniformSets.push_back({u._location, 0, textureUnit, u._type, u._elementCount});
                    }*/
                }
            }
            for (auto s:introspection.GetStructs()) {
                if (s.first != 0 && boundUniformStructs.find(s.first) == boundUniformStructs.end()) {
                    Log(Verbose) << "Didn't get binding for uniform struct (" << s.second._name << ") in BoundUniforms constructor" << std::endl;
                    _unboundUniforms.insert(_unboundUniforms.end(), s.second._uniforms.begin(), s.second._uniforms.end());
                }
            }
        #endif

        // Ensure all sampler uniforms got some assignment. If they don't have a valid assignment,
        // we should set them to a dummy resource. This helps avoid problems related to texture
        // bindings (maybe from previous draw calls) getting bound to incorrect sampler types
        // (which triggers a GL error and skips the draw)
        for (const auto&u:globalsStruct._uniforms) {
            auto dim = DimensionalityForUniformType(u._type);
            bool isSampler = dim != GL_NONE;
            if (!isSampler) continue;

            for (unsigned elementIndex=0; elementIndex<u._elementCount; ++elementIndex) {
                auto existing = std::find_if(
                    srvUniformSets.begin(), srvUniformSets.end(),
                    [&u, elementIndex](const UniformSet& us) {
                        return us._location == u._location && us._index == elementIndex;
                    });
                if (existing != srvUniformSets.end()) continue;

                // If we got here, there is no existing binding. We must bind a dummy texture here
                unsigned textureUnit = 0;
                if (dim == GL_TEXTURE_CUBE_MAP) {
                    if (_standInTextureCubeUnit == ~0u) {
                        _standInTextureCubeUnit = pipelineLayout.GetFlexibleTextureUnit(textureUnitAccumulator);
                        textureUnitAccumulator++;
                    }
                    textureUnit = _standInTextureCubeUnit;
                } else {
                    // assuming 2D, 3D textures not supported
                    if (_standInTexture2DUnit == ~0u) {
                        _standInTexture2DUnit = pipelineLayout.GetFlexibleTextureUnit(textureUnitAccumulator);
                        textureUnitAccumulator++;
                    }
                    textureUnit = _standInTexture2DUnit;
                }

                srvUniformSets.push_back({u._location, elementIndex, textureUnit, u._type, u._elementCount});
            }
        }

        for (const auto&uniformBlock:introspection.GetUniformBlocks()) {
            auto blockIdx = uniformBlock.second._blockIdx;
            auto existing = std::find_if(
                _uniformBuffer.begin(), _uniformBuffer.end(),
                [blockIdx](const UniformBuffer& ub) {
                    return ub._uniformBlockIdx == blockIdx;
                });
            if (existing != _uniformBuffer.end()) continue;

            _unboundUniformBuffers.push_back(UnboundUniformBuffer{blockIdx, uniformBlock.second._blockSize
                #if defined(EXTRA_INPUT_LAYOUT_PROPERTIES)
                    , uniformBlock.second._name
                #endif
                });
        }

        // sort the uniform sets to collect up sequential sets on the same uniforms
        std::sort(
            srvUniformSets.begin(), srvUniformSets.end(),
            [](const UniformSet& lhs, const UniformSet& rhs) {
                if (lhs._location < rhs._location) return true;
                if (lhs._location > rhs._location) return false;
                return lhs._index < rhs._index;
            });

        #if defined(_DEBUG) // ensure that we are not writing to the same uniform more than once
            if (!srvUniformSets.empty()) {
                for (auto i=srvUniformSets.begin(); (i+1)!=srvUniformSets.end(); ++i) {
                    assert(i->_location != (i+1)->_location || i->_index != (i+1)->_index);
                }
            }
        #endif

        // Now generate the set commands that will assign the uniforms as required
        for (auto i = srvUniformSets.begin(); i!=srvUniformSets.end();) {
            auto i2 = i+1;
            while (i2 != srvUniformSets.end() && i2->_location == i->_location) { ++i2; };

            // unsigned maxIndex = (i2-1)->_index;
            unsigned elementCount = i->_elementCount;
            auto dataOffsetStart = _textureAssignmentByteData.size();
            _textureAssignmentByteData.resize(dataOffsetStart+sizeof(GLuint)*elementCount, 0u);
            GLuint* dst = (GLuint*)&_textureAssignmentByteData[dataOffsetStart];
            for (auto q=i; q<i2; ++q) {
                assert(q->_index < elementCount);
                assert(q->_type == i->_type);
                dst[q->_index] = q->_value;
            }
            _textureAssignmentCommands._commands.push_back(
                SetUniformCommandGroup::SetCommand{i->_location, i->_type, elementCount, dataOffsetStart});
            i = i2;
        }

        CheckGLError("Construct BoundUniforms");

        #if defined(_DEBUG) && defined(EXTRA_INPUT_LAYOUT_LOGGING)
            {
                Log(Verbose) << "Building bound uniforms for program: " << shader << std::endl;
                for (auto c=_cbs.begin(); c!=_cbs.end(); ++c) {
                    Log(Verbose) << "CB[" << std::distance(_cbs.begin(), c) << "]: " << c->_name << " (" << c->_stream << ", " << c->_slot << "):" << std::endl;
                    Log(Verbose) << c->_commandGroup << std::endl;
                }
                Log(Verbose) << "Texture assignment:" << std::endl;
                Log(Verbose) << _textureAssignmentCommands << std::endl;
            }
        #endif
    }

    BoundUniforms::~BoundUniforms() {}

    BoundUniforms::BoundUniforms()
    {
        for (unsigned c=0; c<dimof(_boundUniformBufferSlots); ++c) {
            _boundUniformBufferSlots[c] = 0ull;
            _boundResourceSlots[c] = 0ull;
        }
    }

    BoundUniforms::BoundUniforms(BoundUniforms&& moveFrom) never_throws
    : _cbs(std::move(moveFrom._cbs))
    , _srvs(std::move(moveFrom._srvs))
    , _uniformBuffer(std::move(moveFrom._uniformBuffer))
    , _unboundUniformBuffers(std::move(moveFrom._unboundUniformBuffers))
    , _textureAssignmentCommands(std::move(moveFrom._textureAssignmentCommands))
    , _textureAssignmentByteData(std::move(moveFrom._textureAssignmentByteData))
    #if defined(_DEBUG)
    , _unboundUniforms(std::move(moveFrom._unboundUniforms))
    #endif
    , _capturedState(std::move(moveFrom._capturedState))
    , _standInTexture2DUnit(moveFrom._standInTexture2DUnit)
    , _standInTextureCubeUnit(moveFrom._standInTextureCubeUnit)
    {
        for (unsigned c=0; c<dimof(_boundUniformBufferSlots); ++c) {
            _boundUniformBufferSlots[c] = moveFrom._boundUniformBufferSlots[c];
            _boundResourceSlots[c] = moveFrom._boundResourceSlots[c];
            moveFrom._boundUniformBufferSlots[c] = 0ull;
            moveFrom._boundResourceSlots[c] = 0ull;
        }
    }

    BoundUniforms& BoundUniforms::operator=(BoundUniforms&& moveFrom) never_throws
    {
        _cbs = std::move(moveFrom._cbs);
        _srvs = std::move(moveFrom._srvs);
        _uniformBuffer = std::move(moveFrom._uniformBuffer);
        _unboundUniformBuffers = std::move(moveFrom._unboundUniformBuffers);
        _textureAssignmentCommands = std::move(moveFrom._textureAssignmentCommands);
        _textureAssignmentByteData = std::move(moveFrom._textureAssignmentByteData);
        for (unsigned c=0; c<dimof(_boundUniformBufferSlots); ++c) {
            _boundUniformBufferSlots[c] = moveFrom._boundUniformBufferSlots[c];
            _boundResourceSlots[c] = moveFrom._boundResourceSlots[c];
            moveFrom._boundUniformBufferSlots[c] = 0ull;
            moveFrom._boundResourceSlots[c] = 0ull;
        }
        #if defined(_DEBUG)
            _unboundUniforms = std::move(moveFrom._unboundUniforms);
        #endif
        _standInTexture2DUnit = moveFrom._standInTexture2DUnit;
        _standInTextureCubeUnit = moveFrom._standInTextureCubeUnit;
        _capturedState = std::move(moveFrom._capturedState);
        return *this;
    }

}}


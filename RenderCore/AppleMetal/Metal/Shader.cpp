// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Shader.h"
#include "ObjectFactory.h"
#include "../Device.h"
#include "../../ShaderService.h"
#include "../../RenderUtils.h"
#include "../../Types.h"
#include "../../../Assets/Assets.h"
#include "../../../Assets/DepVal.h"
#include "../../../OSServices/Log.h"
#include "../../../OSServices/RawFS.h"
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/Conversion.h"
#include "../../../Utility/Threading/ThreadingUtils.h"
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <regex>

#include "IncludeAppleMetal.h"
#import <Metal/MTLLibrary.h>
#import <Foundation/NSObject.h>
#include <atomic>

static id<MTLLibrary> s_defaultLibrary = nil;

namespace RenderCore { namespace Metal_AppleMetal
{
    using ::Assets::ResChar;

    struct AsyncCallbackData
    {
        bool _cancel = false;
        std::atomic_int _pendingAsyncCallbackCount;

        AsyncCallbackData()
        {
            std::atomic_init(&_pendingAsyncCallbackCount, 0);
        }
    };

    class ShaderCompiler : public ILowLevelCompiler
    {
    public:
        virtual void AdaptShaderModel(
            ResChar destination[],
            const size_t destinationCount,
            StringSection<ResChar> inputShaderModel) const;

        virtual bool DoLowLevelCompile(
            /*out*/ Payload& payload,
            /*out*/ Payload& errors,
            /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
            const void* sourceCode, size_t sourceCodeLength,
            const ILowLevelCompiler::ResId& shaderPath,
            StringSection<::Assets::ResChar> definesTable,
            IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers) const;

        virtual bool DoLowLevelCompile(
            CompletionFunction&& completionFunction,
            const void* sourceCode, size_t sourceCodeLength,
            const ResId& shaderPath,
            StringSection<::Assets::ResChar> definesTable,
            IteratorRange<const SourceLineMarker*> sourceLineMarkers) const;

        virtual bool SupportsCompletionFunctionCompile() { return true; }

        virtual std::string MakeShaderMetricsString(
            const void* byteCode, size_t byteCodeSize) const;

        ShaderCompiler() = delete;
        ShaderCompiler(id<MTLDevice> device);
        ~ShaderCompiler();

        IdPtr _device; // MTLDevice
        std::shared_ptr<AsyncCallbackData> _asyncCallbackData;
    };

    void ShaderCompiler::AdaptShaderModel(
        ResChar destination[],
        const size_t destinationCount,
        StringSection<ResChar> inputShaderModel) const
    {
        if (destination != inputShaderModel.begin())
            XlCopyString(destination, destinationCount, inputShaderModel);
    }

    static std::pair<std::string, unsigned> FindTranslatedSourceLine(
        unsigned inputLineNumber,
        IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers,
        unsigned preambleLineCount)
    {
        if (inputLineNumber < preambleLineCount)
            return {{}, 0};

        inputLineNumber -= preambleLineCount;

        if (!sourceLineMarkers.empty()) {
            auto m = sourceLineMarkers.end()-1;
            while (m >= sourceLineMarkers.begin()) {
                if (m->_processedSourceLine <= inputLineNumber)
                    return { m->_sourceName, unsigned(inputLineNumber - m->_processedSourceLine + m->_sourceLine) };
                --m;
            }
        }

        return {{}, inputLineNumber};
    }

    static std::string TranslateErrorMsgs(
        StringSection<> inputErrorMsgs,
        IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers,
        unsigned preambleLineCount)
    {
        std::regex translateableContent(R"--([^:]+:(\d+):(\d+):(.*))--");

        std::stringstream str;

        auto i = inputErrorMsgs.begin();
        while (i != inputErrorMsgs.end()) {
            auto startOfChunk = i;

            while (i != inputErrorMsgs.end() && (*i == ' ' || *i == '\t' || *i == '\n' || *i == '\r')) ++i;

            auto startOfLine = i;
            while (i != inputErrorMsgs.end() && *i != '\n' && *i != '\r') ++i;
            auto endOfLine = i;

            // Look for translatable content
            std::cmatch match;
            if (std::regex_match(startOfLine, endOfLine, match, translateableContent)) {

                // note some +1 and -1 here to convert between 1-based and 0-based indices
                auto line = Conversion::Convert<unsigned>(MakeStringSection(match[1].first, match[1].second)) - 1;

                auto translated = FindTranslatedSourceLine(line, sourceLineMarkers, preambleLineCount);
                if (!translated.first.empty()) {
                    str << MakeStringSection(startOfChunk, startOfLine);

                    str << translated.first
                        << ":" << (translated.second+1)
                        << ":" << MakeStringSection(match[2].first, match[2].second)
                        << ":" << MakeStringSection(match[3].first, match[3].second);

                } else {
                    str << MakeStringSection(startOfChunk, i);
                }
            } else {
                str << MakeStringSection(startOfChunk, i);
            }
        }

        return str.str();
    }

    static std::string CreateFinalShaderCode(
        unsigned& preambleLineCount,
        const void* sourceCode, size_t sourceCodeLength,
        const ILowLevelCompiler::ResId& shaderPath,
        StringSection<::Assets::ResChar> definesTable)
    {
        std::stringstream definesPreamble;
        preambleLineCount = 0;
        {
            auto p = definesTable.begin();
            while (p != definesTable.end()) {
                while (p != definesTable.end() && std::isspace(*p)) ++p;

                auto definition = std::find(p, definesTable.end(), '=');
                auto defineEnd = std::find(p, definesTable.end(), ';');

                auto endOfName = std::min(defineEnd, definition);
                while ((endOfName-1) > p && std::isspace(*(endOfName-1))) ++endOfName;

                if (definition < defineEnd) {
                    auto e = definition+1;
                    while (e < defineEnd && std::isspace(*e)) ++e;
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << " " << MakeStringSection(e, defineEnd).AsString() << std::endl;
                    ++preambleLineCount;
                    #if defined(_DEBUG)
                        // variantLabel << MakeStringSection(p, endOfName).AsString() << " " << MakeStringSection(e, defineEnd).AsString() << "; ";
                    #endif
                } else {
                    definesPreamble << "#define " << MakeStringSection(p, endOfName).AsString() << std::endl;
                    ++preambleLineCount;
                    #if defined(_DEBUG)
                        // variantLabel << MakeStringSection(p, endOfName).AsString() << "; ";
                    #endif
                }

                p = (defineEnd == definesTable.end()) ? defineEnd : (defineEnd+1);
            }
        }

        bool isFragmentShader = shaderPath._shaderModel[0] == 'p';
        const char* versionDecl = isFragmentShader ? "#define FRAGMENT_SHADER 1\n" : "";
        if (isFragmentShader) ++preambleLineCount;
        definesPreamble << versionDecl;
        definesPreamble << MakeStringSection((const char*)sourceCode, (const char*)PtrAdd(sourceCode, sourceCodeLength));

        auto finalShaderCode = definesPreamble.str();

        return finalShaderCode;
    }

    bool ShaderCompiler::DoLowLevelCompile(
        /*out*/ Payload& payload,
        /*out*/ Payload& errors,
        /*out*/ std::vector<::Assets::DependentFileState>& dependencies,
        const void* sourceCode, size_t sourceCodeLength,
        const ILowLevelCompiler::ResId& shaderPath,
        StringSection<::Assets::ResChar> definesTable,
        IteratorRange<const ILowLevelCompiler::SourceLineMarker*> sourceLineMarkers) const
    {
        @try {
            #if defined(_DEBUG)
                // std::stringstream variantLabel;
            #endif

            unsigned preambleLineCount = 0;
            auto finalShaderCode = CreateFinalShaderCode(preambleLineCount, sourceCode, sourceCodeLength, shaderPath, definesTable);

            MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
            options.languageVersion = MTLLanguageVersion1_2;

            NSError* error = NULL;
            id<MTLLibrary> newLibrary = [_device.get() newLibraryWithSource:[NSString stringWithUTF8String:finalShaderCode.c_str()]
                                                                    options:options
                                                                      error:&error];
            [options release];
            if (!newLibrary) {
                auto errorMsg = TranslateErrorMsgs(error.description.UTF8String, sourceLineMarkers, preambleLineCount);

                Log(Error) << "Failure during shader compile. Errors follow:" << std::endl;
                Log(Error) << errorMsg << std::endl;

                errors = std::make_shared<std::vector<uint8_t>>((const uint8_t*)AsPointer(errorMsg.begin()), (const uint8_t*)AsPointer(errorMsg.end()));
                return false;
            }

            if (!newLibrary.functionNames.count) {
                std::string errorMsg = "Shader compile failed because no functions were found in the compiled result";
                errors = std::make_shared<std::vector<uint8_t>>((const uint8_t*)AsPointer(errorMsg.begin()), (const uint8_t*)AsPointer(errorMsg.end()));
                [newLibrary release];
                return false;
            }

            #if defined(_DEBUG)
                // [newLibrary setLabel:[NSString stringWithCString:variantLabel.str().c_str() encoding:NSUTF8StringEncoding]];
            #endif

            auto& objectFactory = GetObjectFactory();
            uint64_t hashCode = Hash64(finalShaderCode);
            {
                ScopedLock(objectFactory._compiledShadersLock);
                while (objectFactory._compiledShaders.find(hashCode) != objectFactory._compiledShaders.end())
                    ++hashCode;     // uniquify this hash. There are some edge cases where we can end up compiling the same shader twice; it's better to tread safely here
                objectFactory._compiledShaders.emplace(std::make_pair(hashCode, moveptr(newLibrary)));
            }

            struct OutputBlob
            {
                ShaderService::ShaderHeader _hdr;
                uint64_t _hashCode;
            };
            payload = std::shared_ptr<std::vector<uint8>>(
                new std::vector<uint8>(sizeof(OutputBlob)),
                [](std::vector<uint8>* obj) {
                    if (!obj) return;
                    OutputBlob& blob = *(OutputBlob*)obj->data();
                    {
                        auto& objectFactory = GetObjectFactory();
                        ScopedLock(objectFactory._compiledShadersLock);
                        objectFactory._compiledShaders.erase(blob._hashCode);
                    }
                    delete obj;
                });
            OutputBlob& output = *(OutputBlob*)payload->data();
            StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
            identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "-" << std::hex << hashCode;
            output._hdr = ShaderService::ShaderHeader { identifier.AsStringSection(), shaderPath._shaderModel, shaderPath._entryPoint, false };
            output._hashCode = hashCode;
            return true;

        } @catch (NSException *exception) {
            Throw(::Exceptions::BasicLabel("Caught Obj-C exception (%s) during shader compile. This is dangerous, avoid Obj-C exceptions here", exception.description.UTF8String));
        }
    }

    bool ShaderCompiler::DoLowLevelCompile(
        CompletionFunction&& completionFunction,
        const void* sourceCode, size_t sourceCodeLength,
        const ResId& shaderPath,
        StringSection<::Assets::ResChar> definesTable,
        IteratorRange<const SourceLineMarker*> sourceLineMarkers) const
    {
        @try {
            unsigned preambleLineCount = 0;
            auto finalShaderCode = CreateFinalShaderCode(preambleLineCount, sourceCode, sourceCodeLength, shaderPath, definesTable);

            MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
            options.languageVersion = MTLLanguageVersion1_2;

            CompletionFunction completionFunctionCopy = std::move(completionFunction);

            auto asyncCallbackDataCopy = _asyncCallbackData;
            ++asyncCallbackDataCopy->_pendingAsyncCallbackCount;
            std::vector<SourceLineMarker> sourceLineMarkersCopy(sourceLineMarkers.begin(), sourceLineMarkers.end());
            uint64_t hashCode = Hash64(finalShaderCode);

            StringMeld<dimof(ShaderService::ShaderHeader::_identifier)> identifier;
            identifier << shaderPath._filename << "-" << shaderPath._entryPoint << "-" << std::hex << hashCode;
            ShaderService::ShaderHeader shaderHeader { identifier.AsStringSection(), shaderPath._shaderModel, shaderPath._entryPoint, false };

            [_device.get() newLibraryWithSource:[NSString stringWithUTF8String:finalShaderCode.c_str()]
                                        options:options
                              completionHandler:^(id<MTLLibrary> newLibrary, NSError * error) {

                                Payload payload;
                                Payload errors;
                                std::vector<::Assets::DependentFileState> dependencies;

                                if (asyncCallbackDataCopy->_cancel) {
                                    // We can hit this case if the ShaderCompiler object has begun
                                    // shutdown. Normally this only happens during the game shutdown.
                                    // We don't even call our "completionFunction" callback in this case. We're just going to
                                    // early out
                                    --asyncCallbackDataCopy->_pendingAsyncCallbackCount;
                                    return;
                                }

                                if (!newLibrary) {
                                    auto errorMsg = TranslateErrorMsgs(error.description.UTF8String, MakeIteratorRange(sourceLineMarkersCopy), preambleLineCount);

                                    Log(Error) << "Failure during shader compile. Errors follow:" << std::endl;
                                    Log(Error) << errorMsg << std::endl;

                                    errors = std::make_shared<std::vector<uint8_t>>((const uint8_t*)AsPointer(errorMsg.begin()), (const uint8_t*)AsPointer(errorMsg.end()));
                                    completionFunctionCopy(false, payload, errors, dependencies);
                                    --asyncCallbackDataCopy->_pendingAsyncCallbackCount;
                                    return;
                                }

                                if (!newLibrary.functionNames.count) {
                                    std::string errorMsg = "Shader compile failed because no functions were found in the compiled result";
                                    errors = std::make_shared<std::vector<uint8_t>>((const uint8_t*)AsPointer(errorMsg.begin()), (const uint8_t*)AsPointer(errorMsg.end()));
                                    completionFunctionCopy(false, payload, errors, dependencies);
                                    --asyncCallbackDataCopy->_pendingAsyncCallbackCount;
                                    return;
                                }

                                auto uniqueHashCode = hashCode;
                                {
                                    auto& objectFactory = GetObjectFactory();
                                    ScopedLock(objectFactory._compiledShadersLock);
                                    while (objectFactory._compiledShaders.find(uniqueHashCode) != objectFactory._compiledShaders.end())
                                        ++uniqueHashCode;     // uniquify this hash. There are some edge cases where we can end up compiling the same shader twice; it's better to tread safely here
                                    objectFactory._compiledShaders.emplace(std::make_pair(uniqueHashCode, newLibrary));   // increase reference count here. Note the difference between this and the non-async case, which must *move* the pointer, without a reference count increase
                                }

                                struct OutputBlob
                                {
                                    ShaderService::ShaderHeader _hdr;
                                    uint64_t _hashCode;
                                };

                                payload = std::shared_ptr<std::vector<uint8>>(
                                    new std::vector<uint8>(sizeof(OutputBlob)),
                                    [](std::vector<uint8>* obj) {
                                        if (!obj) return;
                                        OutputBlob& blob = *(OutputBlob*)obj->data();
                                        {
                                            auto& objectFactory = GetObjectFactory();
                                            ScopedLock(objectFactory._compiledShadersLock);
                                            objectFactory._compiledShaders.erase(blob._hashCode);
                                        }
                                        delete obj;
                                    });
                                OutputBlob& output = *(OutputBlob*)payload->data();
                                output._hdr = shaderHeader;
                                output._hashCode = uniqueHashCode;

                                completionFunctionCopy(true, payload, errors, dependencies);

                                --asyncCallbackDataCopy->_pendingAsyncCallbackCount;

                            }];
            [options release];

            return true;

        } @catch (NSException *exception) {
            Throw(::Exceptions::BasicLabel("Caught Obj-C exception (%s) during shader compile. This is dangerous, avoid Obj-C exceptions here", exception.description.UTF8String));
        }
    }

    std::string ShaderCompiler::MakeShaderMetricsString(const void* byteCode, size_t byteCodeSize) const { return std::string(); }

    ShaderCompiler::ShaderCompiler(id<MTLDevice> device)
    : _device(device)
    , _asyncCallbackData(std::make_shared<AsyncCallbackData>())
    {}

    ShaderCompiler::~ShaderCompiler()
    {
        // We should "join" with any pending async compiles. Typically this is called during
        // shutdown, and it's possible that some shader compile operations could still be inflight.
        // If we just go ahead and ignore them, they might hit a crash while accessing the global
        // GetObjectFactory(). So let's just stall here waiting for them to complete
        _asyncCallbackData->_cancel = true;
        unsigned c=0;
        while (_asyncCallbackData->_pendingAsyncCallbackCount.load() > 0) {
            if (c >= 500) {
                Log(Warning) << "Dropping out of Async Callback Join because of timeout." << std::endl;
                break;
            }
            Threading::Sleep(10);
            ++c;
        }
    }

    std::shared_ptr<ILowLevelCompiler> CreateLowLevelShaderCompiler(IDevice& device)
    {
        // KenD -- Metal HACK -- holding on to a static Metal library for now.  We should cache it, but we might consider a different approach to construction
        auto* dev = (ImplAppleMetal::Device*)device.QueryInterface(typeid(ImplAppleMetal::Device).hash_code());
        id<MTLDevice> metalDevice = dev->GetUnderlying();
        assert(metalDevice);
        /* default library may be useful for some things still... quick testing of ad-hoc functions, perhaps */
        s_defaultLibrary = [metalDevice newDefaultLibrary];

        return std::make_shared<ShaderCompiler>(metalDevice);
    }

    static uint32_t g_nextShaderProgramGUID = 0;

    ShaderProgram::ShaderProgram(   ObjectFactory&,
                                    const CompiledShaderByteCode& vertexShader,
                                    const CompiledShaderByteCode& fragmentShader)
    {
        /* Get the vertex and fragment function from the MTLLibraries */

        auto vsByteCode = vertexShader.GetByteCode();
        auto fsByteCode = fragmentShader.GetByteCode();

        assert(vsByteCode.size() == sizeof(uint64_t) && fsByteCode.size() == sizeof(uint64_t));

        IdPtr vs, fs;
        auto& objectFactory = GetObjectFactory();
        {
            ScopedLock(objectFactory._compiledShadersLock);
            vs = objectFactory._compiledShaders[*(uint64_t*)vsByteCode.first];
            fs = objectFactory._compiledShaders[*(uint64_t*)fsByteCode.first];
            assert(vs && fs);
        }

        _depVal = ::Assets::GetDepValSys().Make();
        _depVal.RegisterDependency(vertexShader.GetDependencyValidation());
        _depVal.RegisterDependency(fragmentShader.GetDependencyValidation());

        id<MTLLibrary> vertexLibrary = vs.get();
        id<MTLLibrary> fragmentLibrary = fs.get();

        /*
            Look for the entrypoints by the following names.
            Ideally we should get these entrypoint names from the original shader request (ie, the ShaderService::ResId)
            But we've lost that information by now (it's not carried in the CompiledShaderByteCode)
        */
        _vf = moveptr([vertexLibrary newFunctionWithName:@"vs_framework_entry"]);
        _ff = moveptr([fragmentLibrary newFunctionWithName:@"fs_framework_entry"]);

        // METAL_TODO: use consistent rules for entry point functions
        if (!_vf)
            _vf = moveptr([vertexLibrary newFunctionWithName:[[vertexLibrary functionNames] firstObject]]);
        if (!_ff)
            _ff = moveptr([fragmentLibrary newFunctionWithName:[[fragmentLibrary functionNames] firstObject]]);

#if defined(_DEBUG)
        [_vf setLabel:[[_vf.get() name] stringByAppendingFormat:@" (%@)", vertexLibrary.label]];
        [_ff setLabel:[[_ff.get() name] stringByAppendingFormat:@" (%@)", fragmentLibrary.label]];

        std::stringstream str;
        str << "[VS:" << vertexShader.GetIdentifier() << "][FS:" << fragmentShader.GetIdentifier() << "]";
        _sourceIdentifiers = str.str();
#endif

        _guid = g_nextShaderProgramGUID++;
    }

    ShaderProgram::~ShaderProgram()
    {
    }

    ShaderProgram::ShaderProgram(const std::string& vertexFunctionName, const std::string& fragmentFunctionName)
    {
        /* this function can be useful on an ad-hoc basis, but otherwise, could remove it */
        assert(s_defaultLibrary);
        _vf = moveptr([s_defaultLibrary newFunctionWithName:[NSString stringWithCString:vertexFunctionName.c_str() encoding:NSUTF8StringEncoding]]);
        if (!_vf) {
            std::stringstream str;
            str << "Could not create ShaderProgram because vertex shader with name (" << vertexFunctionName << ") was not found in the shader library" << std::endl;
            str << "Known functions: " << std::endl;
            for (NSString* s in s_defaultLibrary.functionNames) {
                str << "\t" << s.UTF8String << std::endl;
            }
            Throw(std::runtime_error(str.str()));
        }

        _ff = moveptr([s_defaultLibrary newFunctionWithName:[NSString stringWithCString:fragmentFunctionName.c_str() encoding:NSUTF8StringEncoding]]);
        if (!_ff) {
            std::stringstream str;
            str << "Could not create ShaderProgram because fragment shader with name (" << fragmentFunctionName << ") was not found in the shader library" << std::endl;
            str << "Known functions: " << std::endl;
            for (NSString* s in s_defaultLibrary.functionNames) {
                str << "\t" << s.UTF8String << std::endl;
            }
            Throw(std::runtime_error(str.str()));
        }

        assert(_vf);
        assert(_ff);

        _guid = g_nextShaderProgramGUID++;
    }
}}

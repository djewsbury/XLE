// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeferredShaderResource.h"
#include "Services.h"
#include "../Assets/TextureLoaders.h"
#include "../Assets/TextureCompiler.h"
#include "../Format.h"
#include "../IDevice.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../Assets/Assets.h"
#include "../../Assets/Marker.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetServices.h"

#include "../../Utility/Streams/PathUtils.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/ParameterBox.h"
#include "../../ConsoleRig/ResourceBox.h"
#include <chrono>

#include "../Metal/Resource.h"
#include "../Metal/DeviceContext.h"

namespace RenderCore { namespace Techniques 
{
    using ResChar = ::Assets::ResChar;

    enum class SourceColorSpace { SRGB, Linear, Unspecified };

	class TextureMetaData
	{
	public:
		SourceColorSpace _colorSpace = SourceColorSpace::Unspecified;
		const ::Assets::DependencyValidation&				GetDependencyValidation() const     { return _depVal; }

		TextureMetaData(
			InputStreamFormatter<utf8>& input, 
			const ::Assets::DirectorySearchRules&, 
			const ::Assets::DependencyValidation& depVal);
        TextureMetaData() = default;
	private:
		::Assets::DependencyValidation _depVal;
	};

	TextureMetaData::TextureMetaData(
		InputStreamFormatter<utf8>& input, 
		const ::Assets::DirectorySearchRules&, 
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal)
	{
		StreamDOM<InputStreamFormatter<utf8>> dom(input);
        if (!dom.RootElement().children().empty()) {
            auto colorSpace = dom.RootElement().children().begin()->Attribute("colorSpace");
            if (colorSpace) {
                if (!XlCompareStringI(colorSpace.Value(), "srgb")) { _colorSpace = SourceColorSpace::SRGB; }
                else if (!XlCompareStringI(colorSpace.Value(), "linear")) { _colorSpace = SourceColorSpace::Linear; }
            }
        }
	}

    class DecodedInitializer
    {
    public:
        SourceColorSpace    _colSpaceRequestString;
        SourceColorSpace    _colSpaceDefault;
        bool                _generateMipmaps;

        DecodedInitializer(const FileNameSplitter<ResChar>& initializer);
    };

    DecodedInitializer::DecodedInitializer(const FileNameSplitter<ResChar>& initializer)
    {
        _generateMipmaps = true;
        _colSpaceRequestString = SourceColorSpace::Unspecified;
        _colSpaceDefault = SourceColorSpace::Unspecified;

        for (auto c:initializer.Parameters()) {
            if (c == 'l' || c == 'L') { _colSpaceRequestString = SourceColorSpace::Linear; }
            if (c == 's' || c == 'S') { _colSpaceRequestString = SourceColorSpace::SRGB; }
            if (c == 't' || c == 'T') { _generateMipmaps = false; }
        }

        if (_colSpaceRequestString == SourceColorSpace::Unspecified) {
            if (XlFindStringI(initializer.File(), "_ddn")) {
                _colSpaceDefault = SourceColorSpace::Linear;
            } else {
                _colSpaceDefault = SourceColorSpace::SRGB;
            }
        }
    }

    static TextureViewDesc MakeTextureViewDesc(const TextureDesc& tDesc, const DecodedInitializer& init, const TextureMetaData* actualizedMetaData = nullptr)
    {
            // calculate the color space to use (resolving the defaults, request string and metadata)
        auto colSpace = SourceColorSpace::Unspecified;
        auto fmtComponentType = GetComponentType(tDesc._format);
        if (fmtComponentType == FormatComponentType::UNorm_SRGB) {
            colSpace = SourceColorSpace::SRGB;
        } else if (fmtComponentType != FormatComponentType::Typeless) {
            colSpace = SourceColorSpace::Linear;
        } else if (init._colSpaceRequestString != SourceColorSpace::Unspecified) {
            colSpace = init._colSpaceRequestString;
        } else if (actualizedMetaData) {
            if (actualizedMetaData->_colorSpace != SourceColorSpace::Unspecified)
                colSpace = actualizedMetaData->_colorSpace;
        }

        if (colSpace == SourceColorSpace::Unspecified && init._colSpaceDefault != SourceColorSpace::Unspecified)
            colSpace = init._colSpaceDefault;

        TextureViewDesc viewDesc{}; 
        if (colSpace == SourceColorSpace::SRGB) viewDesc._format._aspect = TextureViewDesc::Aspect::ColorSRGB;
        else if (colSpace == SourceColorSpace::Linear) viewDesc._format._aspect = TextureViewDesc::Aspect::ColorLinear;
        return viewDesc;
    }

	static BufferUploads::TransactionID ConstructToPromiseImageFile(
		std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
		const FileNameSplitter<char>& splitter)
    {
        DecodedInitializer init(splitter);
		assert(!splitter.File().IsEmpty());

		std::shared_ptr<::Assets::Marker<TextureMetaData>> metaDataFuture;
		{
            ::Assets::ResChar filename[MaxPath];
                // Some resources might have a little xml ".metadata" file attached. This 
                // can contain a setting that can tell us the intended source color format.
                //
                // Some textures use "_ddn" to mark them as normal maps... So we'll take this
                // as a hint that they are in linear space. 
            XlCopyString(filename, splitter.AllExceptParameters());
            XlCatString(filename, ".metadata");
			if (::Assets::MainFileSystem::TryGetDesc(filename)._state == ::Assets::FileDesc::State::Normal)
				metaDataFuture = ::Assets::MakeAsset<TextureMetaData>(filename);
        }

        using namespace BufferUploads;
        Assets::TextureLoaderFlags::BitField flags = init._generateMipmaps ? Assets::TextureLoaderFlags::GenerateMipmaps : 0;

		auto pkt = Services::GetInstance().CreateTextureDataSource(splitter.AllExceptParameters(), flags);
        if (!pkt) {
            promise.set_exception(std::make_exception_ptr(std::runtime_error("Could not find matching texture loader")));
			return BufferUploads::TransactionID_Invalid;
        }

        auto transactionMarker = RenderCore::Techniques::Services::GetBufferUploads().Begin(pkt, BindFlag::ShaderResource);
		if (!transactionMarker.IsValid()) {
			promise.set_exception(std::make_exception_ptr(std::runtime_error("Could not begin buffer uploads transaction")));
			return BufferUploads::TransactionID_Invalid;
		}

        struct Captures
        {
            std::future<BufferUploads::ResourceLocator> _futureLocator;
            std::promise<std::shared_ptr<DeferredShaderResource>> _promise;
            std::shared_ptr<::Assets::Marker<TextureMetaData>> _metaDataFuture;
        };
        auto cap = std::make_shared<Captures>();
        cap->_futureLocator = std::move(transactionMarker._future);
        cap->_promise = std::move(promise);
        cap->_metaDataFuture = std::move(metaDataFuture);
        auto id = transactionMarker._transactionID;
        RenderCore::Techniques::Services::GetBufferUploads().OnCompletion(
            MakeIteratorRange(&id, &id+1),
            [cap, init, initializerStr = splitter.FullFilename().AsString(), depVal = pkt->GetDependencyValidation()]() {
                TRY {

                    BufferUploads::ResourceLocator locator;
                    TRY {
                        assert(cap->_futureLocator.wait_for(std::chrono::seconds(0)) == std::future_status::ready);       // must be ready here
                        locator = cap->_futureLocator.get();
                    } CATCH(const std::exception& e) {
                        Throw(::Assets::Exceptions::ConstructionError(e, depVal));
                    } CATCH_END
                    
                    auto desc = locator.GetContainingResource()->GetDesc();
                    TextureViewDesc viewDesc;
                    auto finalDepVal = depVal;

                    if (cap->_metaDataFuture) {
                        cap->_metaDataFuture->StallWhilePending();      // we're stalling in the buffer uploads thread here, so we need this to be quick!
                        auto metaData = cap->_metaDataFuture->Actualize();
                        if (metaData.GetDependencyValidation()) {
                            auto parentDepVal = ::Assets::GetDepValSys().Make();
                            parentDepVal.RegisterDependency(finalDepVal);
                            parentDepVal.RegisterDependency(metaData.GetDependencyValidation());
                            finalDepVal = parentDepVal;
                        }
                    } else {
                        viewDesc = MakeTextureViewDesc(desc._textureDesc, init);
                    }

                    auto view = locator.CreateTextureView(BindFlag::ShaderResource, viewDesc);
                    if (!view) {
                        Throw(::Assets::Exceptions::ConstructionError(
                            ::Assets::Exceptions::ConstructionError::Reason::Unknown, 
                            finalDepVal, ::Assets::AsBlob("Buffer upload transaction completed, but with invalid resource")));
                    }

                    cap->_promise.set_value(std::make_shared<DeferredShaderResource>(
                        std::move(view), initializerStr,
                        locator.GetCompletionCommandList(), finalDepVal));
                    
                } CATCH (...) {
                    cap->_promise.set_exception(std::current_exception());
                } CATCH_END
            });

        return transactionMarker._transactionID;
    }

    static void ConstructToPromiseArtifact(
		std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
		const RenderCore::Assets::TextureArtifact& artifact,
        std::string originalRequest)
    {
        using namespace BufferUploads;
		auto pkt = artifact.BeginDataSource();
        if (!pkt) {
            promise.set_exception(std::make_exception_ptr(::Assets::Exceptions::InvalidAsset{{}, artifact.GetDependencyValidation(), ::Assets::AsBlob("Could not find matching texture loader")}));
			return;
        }

        auto transactionMarker = RenderCore::Techniques::Services::GetBufferUploads().Begin(pkt, BindFlag::ShaderResource);
		if (!transactionMarker.IsValid()) {
			promise.set_exception(std::make_exception_ptr(::Assets::Exceptions::InvalidAsset{{}, artifact.GetDependencyValidation(), ::Assets::AsBlob("Could not begin buffer uploads transaction")}));
			return;
		}

        ::Assets::WhenAll(std::move(transactionMarker._future)).ThenConstructToPromiseWithFutures(
            std::move(promise),
			[originalRequest, depVal=artifact.GetDependencyValidation()](std::future<BufferUploads::ResourceLocator> futureLocator) {

                BufferUploads::ResourceLocator locator;
                TRY {
                    locator = futureLocator.get();
                } CATCH(const std::exception& e) {
                    Throw(::Assets::Exceptions::ConstructionError(e, depVal));
                } CATCH_END

                auto desc = locator.GetContainingResource()->GetDesc();
				auto viewDesc = MakeTextureViewDesc(desc._textureDesc, DecodedInitializer{originalRequest});
                auto view = locator.CreateTextureView(BindFlag::ShaderResource, viewDesc);
				if (!view) {
					Throw(::Assets::Exceptions::ConstructionError(
                        ::Assets::Exceptions::ConstructionError::Reason::Unknown, 
                        depVal, ::Assets::AsBlob("Buffer upload transaction completed, but with invalid resource")));
				}

				return std::make_shared<DeferredShaderResource>(
                    std::move(view), originalRequest,
                    locator.GetCompletionCommandList(), depVal);
			});
    }

    static void ConstructToPromiseTextureCompile(
		std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
		const FileNameSplitter<char>& splitter)
    {
        auto containerInitializer = splitter.AllExceptParameters();
		auto containerFuture = ::Assets::MakeFuturePtr<Assets::TextureArtifact>(containerInitializer);
        ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
            std::move(promise),
            [originalRequest=splitter.FullFilename().AsString()](std::promise<std::shared_ptr<DeferredShaderResource>>&& thatPromise, auto containerActual) mutable {
                ConstructToPromiseArtifact(std::move(thatPromise), *containerActual, originalRequest);
            });
    }

    void DeferredShaderResource::ConstructToPromise(
        std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
        const Assets::TextureCompilationRequest& compileRequest)
    {
        auto containerFuture = ::Assets::MakeFuturePtr<Assets::TextureArtifact>(compileRequest);
        ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
            std::move(promise),
            [originalRequest=compileRequest._srcFile](std::promise<std::shared_ptr<DeferredShaderResource>>&& thatPromise, auto containerActual) mutable {
                ConstructToPromiseArtifact(std::move(thatPromise), *containerActual, originalRequest);
            });
    }

    void DeferredShaderResource::ConstructToPromise(
		std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
		StringSection<> initializer)
    {
        auto splitter = MakeFileNameSplitter(initializer);
        if (XlEqStringI(splitter.Extension(), "texture")) {
            ConstructToPromiseTextureCompile(std::move(promise), splitter);
        } else {
            ConstructToPromiseImageFile(std::move(promise), splitter);
        }
    }

    BufferUploads::TransactionID DeferredShaderResource::ConstructToTrackablePromise(
        std::promise<std::shared_ptr<DeferredShaderResource>>&& promise,
        StringSection<> initializer)
    {
        auto splitter = MakeFileNameSplitter(initializer);
        if (XlEqStringI(splitter.Extension(), "texture")) {
            ConstructToPromiseTextureCompile(std::move(promise), splitter);
            return BufferUploads::TransactionID_Invalid;
        } else {
            return ConstructToPromiseImageFile(std::move(promise), splitter);
        }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

#if 0
    static TextureViewDesc ResolveTextureViewDescImmediate(const DecodedInitializer& init, const FileNameSplitter<ResChar>& splitter)
    {
        TextureViewDesc result{};

        auto finalColSpace = init._colSpaceRequestString;
        if (finalColSpace == SourceColorSpace::Unspecified) {
                // need to load the metadata file to get SRGB settings!
            ::Assets::ResChar metadataFile[MaxPath];
            XlCopyString(metadataFile, splitter.AllExceptParameters());
            XlCatString(metadataFile, ".metadata");

            auto res = ::Assets::MakeAsset<TextureMetaData>(metadataFile);
            res->StallWhilePending();
            auto actual = res->TryActualize();
            if (actual)
                finalColSpace = (*actual)->_colorSpace;

            if (finalColSpace == SourceColorSpace::Unspecified)
                finalColSpace = (init._colSpaceDefault != SourceColorSpace::Unspecified) ? init._colSpaceDefault : SourceColorSpace::SRGB;
        }

        if (finalColSpace == SourceColorSpace::SRGB) result._format._aspect = TextureViewDesc::Aspect::ColorSRGB;
        else if (finalColSpace == SourceColorSpace::Linear) result._format._aspect = TextureViewDesc::Aspect::ColorLinear;

        return result;
    }

    std::shared_ptr<IResourceView> DeferredShaderResource::LoadImmediately(
        IThreadContext& threadContext,
        StringSection<::Assets::ResChar> initializer)
    {
		auto splitter = MakeFileNameSplitter(initializer);
        DecodedInitializer init(splitter);

        using namespace BufferUploads;
        TextureLoaderFlags::BitField flags = init._generateMipmaps ? TextureLoaderFlags::GenerateMipmaps : 0;

		std::shared_ptr<BufferUploads::IAsyncDataSource> pkt;
		/*const bool checkForShadowingFile = CheckShadowingFile(splitter);
		if (checkForShadowingFile) {
			::Assets::ResChar filename[MaxPath];
			BuildRequestString(filename, splitter);
			pkt = CreateStreamingTextureSource(RenderCore::Techniques::Services::GetInstance().GetTexturePlugins(), MakeStringSection(filename), flags);
		} else*/
			pkt = CreateStreamingTextureSource(RenderCore::Techniques::Services::GetInstance().GetTexturePlugins(), splitter.AllExceptParameters(), flags);

        auto result = Services::GetBufferUploads().Transaction_Immediate(threadContext, *pkt, BindFlag::ShaderResource);

            //  We don't have to change the SRGB modes here -- the caller should select the
            //  right srgb mode when creating a shader resource view

        if (!result)
            Throw(::Assets::Exceptions::ConstructionError(::Assets::Exceptions::ConstructionError::Reason::Unknown, nullptr, "Failure while attempting to load texture immediately"));

        auto view = ResolveTextureViewDescImmediate(init, splitter);
        return result.CreateTextureView(BindFlag::ShaderResource, view);
    }
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

    class CachedTextureFormats
    {
    public:
        class Desc {};

        CachedTextureFormats(const Desc&);
        ~CachedTextureFormats();

        typedef std::pair<uint64, Format> Entry;
        OSServices::MemoryMappedFile _cache;

        class Header
        {
        public:
            unsigned _count;
        };

        static const unsigned MaxCachedTextures = 10*1024;
    };

    CachedTextureFormats::CachedTextureFormats(const Desc&)
    {
        unsigned entrySize = sizeof(Entry);
            
            //  use a memory mapped file for this. This way, we never have to 
            //  worry about flushing out to disk... The OS will take care of 
            //  committing the results to disk on exit
		auto size = entrySize * MaxCachedTextures + sizeof(Header);
		auto ioResult = ::Assets::MainFileSystem::TryOpen(
			_cache,
            "int/TextureFormatCache.dat", size,
			"r+", 0u);
		if (ioResult != ::Assets::IFileSystem::IOReason::Success) {
			_cache = ::Assets::MainFileSystem::OpenMemoryMappedFile("int/TextureFormatCache.dat", size, "w", 0u);
			XlClearMemory(_cache.GetData().begin(), size);
		}
    }

    CachedTextureFormats::~CachedTextureFormats() {}

#if 0
    static bool IsDXTNormalMapFormat(Format format)
    {
        return unsigned(format) >= unsigned(RenderCore::Format::BC1_TYPELESS)
            && unsigned(format) <= unsigned(RenderCore::Format::BC1_UNORM_SRGB);
    }

    static Format LoadFormat(StringSection<::Assets::ResChar> initializer)
    {
		auto splitter = MakeFileNameSplitter(initializer);
        DecodedInitializer init(splitter);
		return LoadTextureFormat(splitter.AllExceptParameters())._format;
    }

    bool DeferredShaderResource::IsDXTNormalMap(StringSection<::Assets::ResChar> textureName)
    {
        if (textureName.IsEmpty()) return false;

        auto& cache = ConsoleRig::FindCachedBox<CachedTextureFormats>(
            CachedTextureFormats::Desc());

        typedef CachedTextureFormats::Header Hdr;
        typedef CachedTextureFormats::Entry Entry;
        auto* data = cache._cache.GetData().begin();
        if (!data) {
            static bool firstTime = true;
            if (firstTime) {
                Log(Error) << "Failed to open TextureFormatCache.dat! DXT normal map queries will be inefficient." << std::endl;
                firstTime = false;
            }
            return IsDXTNormalMapFormat(LoadFormat(textureName));
        }

        auto& hdr = *(Hdr*)data;
        auto* start = (Entry*)PtrAdd(data, sizeof(Hdr));
        auto* end = (Entry*)PtrAdd(data, sizeof(Hdr) + sizeof(Entry) * hdr._count);

        auto hashName = Hash64(textureName);
        auto* i = std::lower_bound(start, end, hashName, CompareFirst<uint64, Format>());
        if (i == end || i->first != hashName) {
            if ((hdr._count+1) > CachedTextureFormats::MaxCachedTextures) {
                assert(0);  // cache has gotten too big
                return false;
            }

            std::move_backward(i, end, end+1);
            i->first = hashName;
            TRY {
                i->second = LoadFormat(textureName);
            } CATCH (const ::Assets::Exceptions::InvalidAsset&) {
                i->second = Format::Unknown;
            } CATCH_END
            ++hdr._count;
            return IsDXTNormalMapFormat(i->second);
        }

        return IsDXTNormalMapFormat(i->second);
    }
#endif

	DeferredShaderResource::DeferredShaderResource(
		const std::shared_ptr<IResourceView>& srv,
		const std::string& initializer,
        BufferUploads::CommandListID completionCommandList,
		const ::Assets::DependencyValidation& depVal)
	: _srv(srv), _initializer(initializer), _depVal(depVal), _completionCommandList(completionCommandList)
	{}

	DeferredShaderResource::~DeferredShaderResource()
    {
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IResource> CreateResourceImmediately(
        IThreadContext& threadContext,
        BufferUploads::IAsyncDataSource& pkt,
        BindFlag::BitField bindFlags)
    {
        auto descFuture = pkt.GetDesc();
        descFuture.wait();
        auto desc = descFuture.get();
        auto device = threadContext.GetDevice();
        std::vector<uint8_t> data(ByteCount(desc._textureDesc));
        auto arrayCount = ActualArrayLayerCount(desc._textureDesc), mipCount = (unsigned)desc._textureDesc._mipCount;
        BufferUploads::IAsyncDataSource::SubResource srs[arrayCount*mipCount];
        for (unsigned a=0; a<arrayCount; ++a)
            for (unsigned m=0; m<mipCount; ++m) {
                auto srOffset = GetSubResourceOffset(desc._textureDesc, m, a);
                srs[m+a*mipCount]._destination = MakeIteratorRange(PtrAdd(AsPointer(data.begin()), srOffset._offset), PtrAdd(AsPointer(data.begin()), srOffset._offset+srOffset._size));
                srs[m+a*mipCount]._id = {m, a};
                srs[m+a*mipCount]._pitches = srOffset._pitches;
            }
        auto dataFuture = pkt.PrepareData({srs, &srs[arrayCount*mipCount]});
        dataFuture.wait();

        auto stagingDesc = desc;
        stagingDesc._allocationRules = AllocationRules::HostVisibleSequentialWrite;
        stagingDesc._bindFlags = BindFlag::TransferSrc;
        auto stagingResource = device->CreateResource(
            stagingDesc,
            [data=std::move(data), textureDesc=desc._textureDesc](auto sr) {
                auto srOffset = GetSubResourceOffset(textureDesc, sr._mip, sr._arrayLayer);
                return SubResourceInitData {
                    MakeIteratorRange(PtrAdd(AsPointer(data.begin()), srOffset._offset), PtrAdd(AsPointer(data.begin()), srOffset._offset+srOffset._size)),
                    srOffset._pitches
                };
            });
        desc._bindFlags |= bindFlags | BindFlag::TransferDst;
        auto finalResource = device->CreateResource(desc);
        auto& devContext = *Metal::DeviceContext::Get(threadContext);
        Metal::CompleteInitialization(devContext, {stagingResource.get(), finalResource.get()});
        devContext.BeginBlitEncoder().Copy(*finalResource, *stagingResource);
        return finalResource;
    }

    std::shared_ptr<IResource> DestageResource(
        IThreadContext& threadContext,
        const std::shared_ptr<IResource>& input)
    {
        auto inputDesc = input->GetDesc();
        if (inputDesc._allocationRules & AllocationRules::HostVisibleRandomAccess)
            return input;

        auto destagingDesc = inputDesc;
        destagingDesc._allocationRules = AllocationRules::HostVisibleRandomAccess;
        destagingDesc._bindFlags = BindFlag::TransferDst;
        auto destagingResource = threadContext.GetDevice()->CreateResource(destagingDesc);
        auto& devContext = *Metal::DeviceContext::Get(threadContext);
        Metal::CompleteInitialization(devContext, {destagingResource.get()});
        devContext.BeginBlitEncoder().Copy(*destagingResource, *input);

        // "7.9. Host Write Ordering Guarantees" suggests we shouldn't need a transfer -> host barrier here

        threadContext.CommitCommands(CommitCommandsFlags::WaitForCompletion);
        return destagingResource;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	ParameterBox TechParams_SetResHas(
        const ParameterBox& inputMatParameters, const ParameterBox& resBindings,
        const ::Assets::DirectorySearchRules& searchRules)
    {
        static const auto DefaultNormalsTextureBindingHash = ParameterBox::MakeParameterNameHash("NormalsTexture");
            // The "material parameters" ParameterBox should contain some "RES_HAS_..."
            // settings. These tell the shader what resource bindings are available
            // (and what are missing). We need to set these parameters according to our
            // binding list
        ParameterBox result = inputMatParameters;
        for (const auto& param:resBindings) {
            result.SetParameter(StringMeld<64, utf8>() << "RES_HAS_" << param.Name(), 1);
#if 0
            if (param.HashName() == DefaultNormalsTextureBindingHash) {
                auto resourceName = resBindings.GetParameterAsString(DefaultNormalsTextureBindingHash);
                if (resourceName.has_value()) {
                    ::Assets::ResChar resolvedName[MaxPath];
                    searchRules.ResolveFile(resolvedName, dimof(resolvedName), resourceName.value().c_str());
                    result.SetParameter(
                        (const utf8*)"RES_HAS_NormalsTexture_DXT", 
                        DeferredShaderResource::IsDXTNormalMap(resolvedName));
                }
            }
#endif
        }
        return std::move(result);
    }
}}

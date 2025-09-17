// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "TerrainUberSurface.h"
#include "TerrainScaffold.h"
#include "Terrain.h"
#include "TerrainConfig.h"
#include "SceneEngineUtils.h"
#include "Noise.h"
#include "Erosion.h"
#include "SurfaceHeightsProvider.h"
#include "MetalStubs.h"

#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/BufferView.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/ResourceLocator.h"
#include "../BufferUploads/DataPacket.h"

#include "../ConsoleRig/ResourceBox.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/Assets.h"
#include "../OSServices/RawFS.h"
#include "../Utility/PtrUtils.h"
#include "../Utility/StringFormat.h"
#include "../xleres/FileList.h"

namespace SceneEngine
{
    using namespace RenderCore;

///////////////////////////////////////////////////////////////////////////////////////////////////

    void* TerrainUberSurfaceGeneric::GetData(UInt2 coord)
    {
        assert(_mappedFile.IsGood() && _dataStart);
        if (coord[0] >= _width || coord[1] >= _height) return nullptr;
        auto stride = _width * _sampleBytes;
        return PtrAdd(_dataStart, coord[1] * stride + coord[0] * _sampleBytes);
    }

    unsigned TerrainUberSurfaceGeneric::GetStride() const 
    { 
        return _width * _sampleBytes;
    }

    ImpliedTyping::TypeDesc TerrainUberSurfaceGeneric::Format() const { return _format; }

    TerrainUberSurfaceGeneric::TerrainUberSurfaceGeneric(StringSection<::Assets::ResChar> filename)
    {
        _width = _height = 0;
        _dataStart = nullptr;
        _sampleBytes = 0;

            //  Load the file as a Win32 "mapped file"
            //  the format is very simple.. it's just a basic header, and then
            //  a huge 2D array of height values
        auto mappedFile = ::Assets::MainFileSystem::OpenMemoryMappedFile(filename, 0, "r+", OSServices::FileShareMode::Read);
        
        auto& hdr = *(TerrainUberHeader*)mappedFile.GetData().begin();
        if (hdr._magic != TerrainUberHeader::Magic)
            Throw(::Exceptions::BasicLabel(
                "Uber surface file appears to be corrupt (%s)", filename.AsString().c_str()));

        _width = hdr._width;
        _height = hdr._height;
        _dataStart = PtrAdd(mappedFile.GetData().begin(), sizeof(TerrainUberHeader));
        _format = ImpliedTyping::TypeDesc{
            ImpliedTyping::TypeCat(hdr._typeCat), 
            (uint16)hdr._typeArrayCount};
        _sampleBytes = _format.GetSize();

        if (mappedFile.GetSize() < (sizeof(TerrainUberHeader) + hdr._width * hdr._height * _sampleBytes))
            Throw(::Exceptions::BasicLabel(
                "Uber surface file appears to be corrupt (it is smaller than it should be) (%s)", filename.AsString().c_str()));

        _mappedFile = std::move(mappedFile);
    }

    TerrainUberSurfaceGeneric::TerrainUberSurfaceGeneric()
    {
        _width = _height = 0;
        _dataStart = nullptr;
    }

    TerrainUberSurfaceGeneric::~TerrainUberSurfaceGeneric()
    {}

    TerrainUberSurfaceGeneric::TerrainUberSurfaceGeneric(TerrainUberSurfaceGeneric&& moveFrom)
    : _mappedFile(std::move(moveFrom._mappedFile))
    , _dataStart(moveFrom._dataStart)
    , _width(moveFrom._width)
    , _height(moveFrom._height)
    {
        moveFrom._dataStart = nullptr;
        moveFrom._width = moveFrom._height = 0;
    }

    TerrainUberSurfaceGeneric& TerrainUberSurfaceGeneric::operator=(TerrainUberSurfaceGeneric&& moveFrom)
    {
        _mappedFile = std::move(moveFrom._mappedFile);
        _width = moveFrom._width;
        _height = moveFrom._height;
        _dataStart = moveFrom._dataStart;
        moveFrom._dataStart = nullptr;
        moveFrom._width = moveFrom._height = 0;
        return *this;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template <typename Type>
        TerrainUberSurface<Type>::TerrainUberSurface(StringSection<::Assets::ResChar> filename)
            : TerrainUberSurfaceGeneric(filename)
        {
            if (!(_format == ImpliedTyping::TypeOf<Type>()))
                Throw(::Exceptions::BasicLabel(
                    "Uber surface format doesn't match expected type (%s)", filename.AsString().c_str()));
        }

    template <typename Type>
        TerrainUberSurface<Type>::TerrainUberSurface() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////

    namespace Internal { class SurfaceHeightsProvider; }

    class GenericUberSurfaceInterface::Pimpl
    {
    public:
        TerrainUberSurfaceGeneric*      _uberSurface;

        UInt2                           _gpuCacheMins, _gpuCacheMaxs;

        intrusive_ptr<BufferUploads::ResourceLocator>  _gpucache[2];

        std::unique_ptr<ErosionSimulation>  _erosionSim;
        UInt2                               _erosionSimGPUCacheOffset;
        Float2                              _erosionWorldSpaceOffset;

        std::shared_ptr<ShortCircuitBridge>   _bridge;

        Pimpl() : _uberSurface(nullptr) {}
    };

    namespace Internal
    {
        static BufferUploads::BufferDesc BuildCacheDesc(UInt2 dims, RenderCore::Format format)
        {
            ResourceDesc desc;
            desc._type = ResourceDesc::Type::Texture;
            desc._bindFlags = BindFlag::UnorderedAccess | BindFlag::ShaderResource;
            desc._cpuAccess = 0;
            desc._gpuAccess = GPUAccess::Read|GPUAccess::Write;
            desc._allocationRules = 0;
            desc._textureDesc = TextureDesc::Plain2D(dims[0], dims[1], format);
            XlCopyString(desc._name, "TerrainWorkingCache");
            return desc;
        }

        class UberSurfacePacket : public BufferUploads::DataPacket
        {
        public:
            virtual void* GetData(SubResourceId subRes);
            virtual size_t GetDataSize(SubResourceId subRes) const;
            virtual BufferUploads::TexturePitches GetPitches(SubResourceId subRes) const;

            virtual std::shared_ptr<Marker> BeginBackgroundLoad();

            UberSurfacePacket(void* sourceData, unsigned stride, UInt2 dims);

        private:
            void* _sourceData;
            unsigned _stride;
            UInt2 _dims;
        };

        void* UberSurfacePacket::GetData(SubResourceId subRes)
        {
            assert(subRes._mip==0 && subRes._arrayLayer==0);
            return _sourceData;
        }

        size_t UberSurfacePacket::GetDataSize(SubResourceId subRes) const
        {
            assert(subRes._mip==0 && subRes._arrayLayer==0);
            return _stride*_dims[1];
        }

        BufferUploads::TexturePitches UberSurfacePacket::GetPitches(SubResourceId subRes) const
        {
            assert(subRes._mip==0 && subRes._arrayLayer==0);
            return BufferUploads::TexturePitches{_stride, _stride*_dims[1]};
        }

        auto UberSurfacePacket::BeginBackgroundLoad() -> std::shared_ptr<Marker> { return nullptr; }

        UberSurfacePacket::UberSurfacePacket(void* sourceData, unsigned stride, UInt2 dims)
        {
            _sourceData = sourceData;
            _stride = stride;
            _dims = dims;
        }

        class SurfaceHeightsProvider : public ISurfaceHeightsProvider
        {
        public:
            virtual SRV         GetSRV();
            virtual Addressing  GetAddress(Float2 minCoordWorld, Float2 maxCoordWorld);
            virtual bool        IsFloatFormat() const;

            SurfaceHeightsProvider(Float2 worldSpaceCacheMin, Float2 worldSpaceCacheMax, UInt2 cacheDims, RenderCore::IResourcePtr gpuCache);
            ~SurfaceHeightsProvider();
        private:
            Float2 _worldSpaceCacheMin, _worldSpaceCacheMax;
            UInt2 _cacheDims;
			RenderCore::IResourcePtr _gpuCache;
            SRV _srv;
        };

        SurfaceHeightsProvider::SurfaceHeightsProvider(Float2 worldSpaceCacheMin, Float2 worldSpaceCacheMax, UInt2 cacheDims, RenderCore::IResourcePtr gpuCache)
        : _worldSpaceCacheMin(worldSpaceCacheMin), _worldSpaceCacheMax(worldSpaceCacheMax)
        , _cacheDims(cacheDims), _gpuCache(std::move(gpuCache)), _srv(_gpuCache)
        {}

        SurfaceHeightsProvider::~SurfaceHeightsProvider() {}

        auto SurfaceHeightsProvider::GetSRV() -> SRV { return _srv; }
        auto SurfaceHeightsProvider::GetAddress(Float2 minCoordWorld, Float2 maxCoordWorld) -> Addressing
        {
            Addressing result;
            result._baseCoordinate = Int3(0,0,0);
            result._heightScale = 1.f;
            result._heightOffset = 0.f;

            if (    minCoordWorld[0] >= _worldSpaceCacheMin[0] &&  maxCoordWorld[0] <= _worldSpaceCacheMax[0]
                &&  minCoordWorld[1] >= _worldSpaceCacheMin[1] &&  maxCoordWorld[1] <= _worldSpaceCacheMax[1]) {

                result._minCoordOffset[0] = (int)XlFloor(float(_cacheDims[0]) * (minCoordWorld[0] - _worldSpaceCacheMin[0]) / (_worldSpaceCacheMax[0] - _worldSpaceCacheMin[0]));
                result._minCoordOffset[1] = (int)XlFloor(float(_cacheDims[1]) * (minCoordWorld[1] - _worldSpaceCacheMin[1]) / (_worldSpaceCacheMax[1] - _worldSpaceCacheMin[1]));

                result._maxCoordOffset[0] = (int)XlFloor(float(_cacheDims[0]) * (maxCoordWorld[0] - _worldSpaceCacheMin[0]) / (_worldSpaceCacheMax[0] - _worldSpaceCacheMin[0]));
                result._maxCoordOffset[1] = (int)XlFloor(float(_cacheDims[1]) * (maxCoordWorld[1] - _worldSpaceCacheMin[1]) / (_worldSpaceCacheMax[1] - _worldSpaceCacheMin[1]));

                result._valid = true;
            } else {
                result._minCoordOffset = result._maxCoordOffset = UInt2(0,0);
                result._valid = false;
            }

            return result;
        }

        bool SurfaceHeightsProvider::IsFloatFormat() const { return true; }
    }

	std::pair<UInt2, UInt2> GenericUberSurfaceInterface::GetLock() const
	{
		if (_pimpl->_gpucache[0])
			return std::make_pair(_pimpl->_gpuCacheMins, _pimpl->_gpuCacheMaxs);
		return std::make_pair(UInt2(0,0), UInt2(0,0));
	}

	void	GenericUberSurfaceInterface::AbandonLock()
	{
		if (_pimpl->_gpucache[0]) {
			auto oldCacheMins = _pimpl->_gpuCacheMins;
			auto oldCacheMaxs = _pimpl->_gpuCacheMaxs;
			_pimpl->_gpucache[0].reset();
			_pimpl->_gpucache[1].reset();
			_pimpl->_gpuCacheMins = _pimpl->_gpuCacheMaxs = UInt2(0,0);

			// Our rendered probably has short-curcuit updates for the cells intersecting the
			// lock area. We need to reload these cells from disk to abandon the updated data.
            if (_pimpl->_bridge)
                _pimpl->_bridge->QueueAbandon(oldCacheMins, oldCacheMaxs);
		}
	}

    void    GenericUberSurfaceInterface::FlushLockToDisk(ConsoleRig::IProgress* progress)
    {
        if (_pimpl->_gpucache[0]) {
                // readback data from the gpu asset (often requires a staging-style resource)
            {
                using namespace BufferUploads;
                auto& bufferUploads = GetBufferUploads();

                auto readback = bufferUploads.Resource_ReadBack(*_pimpl->_gpucache[0].get());
                assert(readback.get());

                auto readbackStride = readback->GetPitches()._rowPitch;
                auto readbackData = readback->GetData();

                auto dstStart = _pimpl->_uberSurface->GetData(_pimpl->_gpuCacheMins);
                auto dstStride = _pimpl->_uberSurface->GetStride();
                auto bytesPerSample = _pimpl->_uberSurface->Format().GetSize();

                UInt2 dims( _pimpl->_gpuCacheMaxs[0]-_pimpl->_gpuCacheMins[0]+1, 
                            _pimpl->_gpuCacheMaxs[1]-_pimpl->_gpuCacheMins[1]+1);
                for (unsigned y = 0; y<dims[1]; ++y)
                    XlCopyMemory(
                        PtrAdd(dstStart, y*dstStride),
                        PtrAdd(readbackData, y*readbackStride),
                        dims[0] * bytesPerSample);
            }

                // Destroy the gpu cache
            _pimpl->_gpucache[0].reset();
            _pimpl->_gpucache[1].reset();

                // Look for all of the cells that intersect with the area we've changed.
                // we have to rebuild the entire cell
            if (_pimpl->_bridge) {
                // Note that we abandon changes first (to flush out any queued change events)
                // Afterwards, we should just write the cells, and they will be reloaded by the
                // terrain renderer in the normal way.
                _pimpl->_bridge->QueueAbandon(_pimpl->_gpuCacheMins, _pimpl->_gpuCacheMaxs);
                _pimpl->_bridge->WriteCells(_pimpl->_gpuCacheMins, _pimpl->_gpuCacheMaxs, progress);
            }

            _pimpl->_gpuCacheMins = _pimpl->_gpuCacheMaxs = UInt2(0,0);
        }
    }

    void    GenericUberSurfaceInterface::BuildGPUCache(UInt2 mins, UInt2 maxs)
    {
            //  if there's an existing GPU cache, we need to flush it out, and copy
            //  the results back to system memory
        if (_pimpl->_gpucache[0]) {
            FlushLockToDisk();
        }

        auto& bufferUploads = GetBufferUploads();

        UInt2 dims(maxs[0]-mins[0]+1, maxs[1]-mins[1]+1);
        auto desc = Internal::BuildCacheDesc(dims, AsFormat(_pimpl->_uberSurface->Format()));
        auto pkt = make_intrusive<Internal::UberSurfacePacket>(
            _pimpl->_uberSurface->GetData(mins), _pimpl->_uberSurface->GetStride(), dims);

            // create a texture on the GPU with some cached data from the uber surface.
            //      we need 2 copies of the gpu cache for update operations
        auto gpucache0 = bufferUploads.Transaction_Immediate(desc, pkt.get());
        auto gpucache1 = bufferUploads.Transaction_Immediate(desc, pkt.get());
        assert(gpucache0.get() && gpucache1.get());

        _pimpl->_gpucache[0] = std::move(gpucache0);
        _pimpl->_gpucache[1] = std::move(gpucache1);
        _pimpl->_gpuCacheMins = mins;
        _pimpl->_gpuCacheMaxs = maxs;
    }

    intrusive_ptr<BufferUploads::ResourceLocator> GenericUberSurfaceInterface::CopyToGPU(
        UInt2 topLeft, UInt2 bottomRight)
    {
        using namespace BufferUploads;
        auto& bufferUploads = GetBufferUploads();

        UInt2 dims = bottomRight - topLeft;
        auto desc = Internal::BuildCacheDesc(dims, AsFormat(_pimpl->_uberSurface->Format()));
        auto pkt = make_intrusive<Internal::UberSurfacePacket>(
            _pimpl->_uberSurface->GetData(topLeft), _pimpl->_uberSurface->GetStride(), dims);

        return bufferUploads.Transaction_Immediate(desc, pkt.get());
    }

    bool GenericUberSurfaceInterface::PrepareCache(UInt2 adjMins, UInt2 adjMaxs)
    {
        unsigned fieldWidth = _pimpl->_uberSurface->GetWidth();
        unsigned fieldHeight = _pimpl->_uberSurface->GetHeight();

            // see if this fits within the existing GPU cache
        if (_pimpl->_gpucache[0]) {
            bool within =   adjMins[0] >= _pimpl->_gpuCacheMins[0] && adjMins[1] >= _pimpl->_gpuCacheMins[1]
                        &&  adjMaxs[0] <= _pimpl->_gpuCacheMaxs[0] && adjMaxs[1] <= _pimpl->_gpuCacheMaxs[1];
            if (!within)
                return false;
        }

        if (!_pimpl->_gpucache[0]) {
            UInt2 editCenter = (adjMins + adjMaxs) / 2;
            const UInt2 cacheSize(512, 512);    // take a big part of the surface and upload to the gpu

            UInt2 cacheMins(
                (unsigned)std::max(0, std::min(signed(adjMins[0]), signed(editCenter[0]) - signed(cacheSize[0]))),
                (unsigned)std::max(0, std::min(signed(adjMins[1]), signed(editCenter[1]) - signed(cacheSize[1]))));

            UInt2 cacheMaxs(
                std::min(fieldWidth-1, std::max(adjMaxs[0], editCenter[0] + cacheSize[0])),
                std::min(fieldHeight-1, std::max(adjMaxs[1], editCenter[1] + cacheSize[1])));

            BuildGPUCache(cacheMins, cacheMaxs);
        }

		return true;
    }

    void GenericUberSurfaceInterface::CancelActiveOperations()
    {}

	class ApplyToolResources
	{
	public:
		class Desc 
		{
		public:
			const ::Assets::ResChar* _shaderName;
			using Pkt = std::tuple<uint64_t, void*, size_t>;
			IteratorRange<Pkt*> _extraPackets; 

			Desc(const ::Assets::ResChar shaderName[], IteratorRange<Pkt*> extraPackets)
			: _shaderName(shaderName), _extraPackets(extraPackets) {}
		};

		const Metal::ComputeShader* _cs;
		Metal::BoundUniforms _uniforms;
		bool _needsInputHash;

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		ApplyToolResources(const Desc& desc);
		~ApplyToolResources();
	private:
		::Assets::DependencyValidation _depVal;
	};
}

namespace ConsoleRig
{
	template <> uint64_t CalculateCachedBoxHash(const SceneEngine::ApplyToolResources::Desc& desc)
	{
		auto h = Hash64(desc._shaderName);
		for (const auto& p:desc._extraPackets)
			h = HashCombine(std::get<0>(p), h);
		return h;
	}
}

namespace SceneEngine
{
	ApplyToolResources::ApplyToolResources(const Desc& desc)
	{
		StringMeld<256, ::Assets::ResChar> fullShaderName;
		fullShaderName << desc._shaderName << ":cs_*";

		_cs = &::Assets::Legacy::GetAssetDep<Metal::ComputeShader>(fullShaderName.get());

		UniformsStreamInterface usi;
		usi.BindConstantBuffer(0, {Hash64("Parameters")});
		for (unsigned c = 0; c<desc._extraPackets.size(); ++c)
			usi.BindConstantBuffer(1 + c, {std::get<0>(desc._extraPackets[c])});
		usi.BindShaderResource(0, Hash64("InputSurface"));

		_uniforms = Metal::BoundUniforms(
			*_cs,
			Metal::PipelineLayoutConfig{},
			UniformsStreamInterface{},
			usi);

		_needsInputHash = (_uniforms._boundResourceSlots[1] != 0);

		_depVal = _cs->GetDependencyValidation();
	}

	ApplyToolResources::~ApplyToolResources() {}

    auto GenericUberSurfaceInterface::ApplyTool(
        RenderCore::IThreadContext& threadContext,
        UInt2 adjMins, UInt2 adjMaxs, const char shaderName[],
        Float2 center, float radius, float adjustment, 
        std::tuple<uint64_t, void*, size_t> extraPackets[], unsigned extraPacketCount) -> TerrainToolResult
    {
        CancelActiveOperations();
        TRY 
        {
                // might be able to do this in a deferred context?
            auto context = Metal::DeviceContext::Get(threadContext);

            Metal::UnorderedAccessView uav(_pimpl->_gpucache[0]->GetUnderlying());
            context->GetNumericUniforms(ShaderStage::Compute).Bind(RenderCore::MakeResourceList(uav));

            auto& perlinNoiseRes = ConsoleRig::FindCachedBox<PerlinNoiseResources>(PerlinNoiseResources::Desc());
            context->GetNumericUniforms(ShaderStage::Compute).Bind(RenderCore::MakeResourceList(12, perlinNoiseRes._gradShaderResource, perlinNoiseRes._permShaderResource));
            
            struct Parameters
            {
                Float2 _center; float _radius; float _adjustment;
                UInt2 _cacheMins; UInt2 _cacheMaxs;
                UInt2 _adjMins; int _dummy[2];
            } parameters = { 
                center, radius, adjustment, 
                _pimpl->_gpuCacheMins, _pimpl->_gpuCacheMaxs, 
                adjMins, { 0, 0 }
            };

			auto& box = ConsoleRig::FindCachedBoxDep2<ApplyToolResources>(
				shaderName, 
				MakeIteratorRange(extraPackets, &extraPackets[extraPacketCount]));
            
            Metal::ShaderResourceView cacheCopySRV;
            if (box._needsInputHash) {
                Metal::Copy(*context, Metal::AsResource(*_pimpl->_gpucache[1]->GetUnderlying()), Metal::AsResource(*_pimpl->_gpucache[0]->GetUnderlying()));
                cacheCopySRV = Metal::ShaderResourceView(_pimpl->_gpucache[1]->GetUnderlying());
            }

            const Metal::ShaderResourceView* resources[] = { &cacheCopySRV };
            std::vector<ConstantBufferView> pkts;
            pkts.push_back(RenderCore::MakeSharedPkt(parameters));
            for (unsigned c=0; c<extraPacketCount; ++c) {
                auto start = std::get<1>(extraPackets[c]);
                auto size = std::get<2>(extraPackets[c]);
                pkts.push_back(RenderCore::MakeSharedPkt(start, PtrAdd(start, size)));
            }

            box._uniforms.Apply(
				*context, 1,
				UniformsStream{
					MakeIteratorRange(pkts), 
					UniformsStream::MakeResources(MakeIteratorRange(resources))});
            context->Bind(*box._cs);
            const unsigned threadGroupDim = 16;
            context->Dispatch(
                (adjMaxs[0] - adjMins[0] + 1 + threadGroupDim - 1) / threadGroupDim,
                (adjMaxs[1] - adjMins[1] + 1 + threadGroupDim - 1) / threadGroupDim);
            MetalStubs::UnbindCS<Metal::UnorderedAccessView>(*context, 0, 1);

            QueueShortCircuitUpdate(adjMins, adjMaxs);
        }
		CATCH (::Assets::Exceptions::InvalidAsset&) { return TerrainToolResult::InvalidAsset; }
        CATCH (::Assets::Exceptions::PendingAsset&) { return TerrainToolResult::PendingAsset; }
		CATCH (...) { return TerrainToolResult::Error; }
        CATCH_END

		return TerrainToolResult::Success;
    }

    void    GenericUberSurfaceInterface::QueueShortCircuitUpdate(UInt2 adjMins, UInt2 adjMaxs)
    {
        if (_pimpl->_bridge)
            _pimpl->_bridge->QueueShortCircuit(adjMins, adjMaxs);
    }

    void    GenericUberSurfaceInterface::BuildEmptyFile(
        const ::Assets::ResChar destinationFile[], 
        unsigned width, unsigned height, const ImpliedTyping::TypeDesc& type)
    {
		auto outputFile = ::Assets::MainFileSystem::OpenBasicFile(destinationFile, "wb");

        TerrainUberHeader hdr;
        hdr._magic = TerrainUberHeader::Magic;
        hdr._width = width;
        hdr._height = height;
        hdr._typeCat = (unsigned)type._type;
        hdr._typeArrayCount = type._arrayCount;
        hdr._dummy[0] = hdr._dummy[1] = hdr._dummy[2] = 0;
        outputFile.Write(&hdr, sizeof(hdr), 1);

        unsigned lineSize = width*type.GetSize();
        auto lineOfSamples = std::make_unique<uint8[]>(lineSize);
        std::fill(lineOfSamples.get(), &lineOfSamples[lineSize], 0ui8);
        for (int y=0; y<int(height); ++y) {
            outputFile.Write(lineOfSamples.get(), 1, lineSize);
        }
    }

    void    GenericUberSurfaceInterface::RenderDebugging(RenderCore::IThreadContext& context, RenderCore::Techniques::ParsingContext& parserContext)
    {
        if (!_pimpl->_gpucache[0])
            return;

        auto& metalContext = *RenderCore::Metal::DeviceContext::Get(context);

        CATCH_ASSETS_BEGIN
            using namespace RenderCore;
            Metal::ShaderResourceView gpuCacheSRV(_pimpl->_gpucache[0]->GetUnderlying());
            metalContext.GetNumericUniforms(ShaderStage::Pixel).Bind(MakeResourceList(5, gpuCacheSRV));
            auto& debuggingShader = ::Assets::Legacy::GetAssetDep<Metal::ShaderProgram>(
                BASIC2D_VERTEX_HLSL ":fullscreen:vs_*", 
                "xleres/ui/terrainmodification.hlsl:GpuCacheDebugging:ps_*",
                "");
            metalContext.Bind(debuggingShader);
            metalContext.Bind(Techniques::CommonResources()._blendStraightAlpha);
            SetupVertexGeneratorShader(metalContext);
            metalContext.Draw(4);
        CATCH_ASSETS_END(parserContext)

        MetalStubs::UnbindPS<RenderCore::Metal::ShaderResourceView>(metalContext, 5, 1);
    }

    void GenericUberSurfaceInterface::SetShortCircuitBridge(const std::shared_ptr<ShortCircuitBridge>& bridge)
    {
        _pimpl->_bridge = bridge;
    }

    ShortCircuitUpdate GenericUberSurfaceInterface::GetShortCircuit(UInt2 uberMins, UInt2 uberMaxs)
    {
		if (!_pimpl->_gpucache[0]) return ShortCircuitUpdate();
		if (	uberMins[0] >= _pimpl->_gpuCacheMaxs[0] || uberMins[1] >= _pimpl->_gpuCacheMaxs[1]
			||	uberMaxs[0] <  _pimpl->_gpuCacheMins[0] || uberMaxs[1] <  _pimpl->_gpuCacheMins[1])
			return ShortCircuitUpdate();

        ShortCircuitUpdate result;
        result._srv = std::make_unique<Metal::ShaderResourceView>(_pimpl->_gpucache[0]->GetUnderlying());
        result._cellMinsInResource = Int2(uberMins) - Int2(_pimpl->_gpuCacheMins);
        result._cellMaxsInResource = Int2(uberMaxs) - Int2(_pimpl->_gpuCacheMins);
        return std::move(result);
    }

    TerrainUberSurfaceGeneric& GenericUberSurfaceInterface::GetSurface()
    {
        return *_pimpl->_uberSurface;
    }

    GenericUberSurfaceInterface::GenericUberSurfaceInterface(TerrainUberSurfaceGeneric& uberSurface)
    {
        auto pimpl = std::make_unique<Pimpl>();
        pimpl->_uberSurface = &uberSurface;     // no protection on this pointer (assuming it's coming from a resource)
        _pimpl = std::move(pimpl);
    }

    GenericUberSurfaceInterface::~GenericUberSurfaceInterface() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    #define baseShader SCENE_ENGINE_RES "/Terrain/terrainmodification.hlsl:"

    auto HeightsUberSurfaceInterface::AdjustHeights(RenderCore::IThreadContext& threadContext, Float2 center, float radius, float adjustment, float powerValue) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(center[0] + radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(center[1] + radius)));

        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need
        struct RaiseLowerParameters
        {
            float _powerValue;
            float dummy[3];
        } raiseLowerParameters = { powerValue, 0.f, 0.f, 0.f };

        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("RaiseLowerParameters"), &raiseLowerParameters, sizeof(RaiseLowerParameters))
        };

        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "RaiseLower", center, radius, adjustment, extraPackets, dimof(extraPackets));
    }

    auto HeightsUberSurfaceInterface::AddNoise(RenderCore::IThreadContext& threadContext, Float2 center, float radius, float adjustment) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(center[0] + radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(center[1] + radius)));

        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need

        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "AddNoise", center, radius, adjustment, nullptr, 0);
    }

    auto HeightsUberSurfaceInterface::CopyHeight(RenderCore::IThreadContext& threadContext, Float2 center, Float2 source, float radius, float adjustment, float powerValue, unsigned flags) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(center[0] + radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(center[1] + radius)));

        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need
        struct CopyHeightParameters
        {
            Float2 _source;
            float _powerValue;
            unsigned _flags;
        } copyHeightParameters = { source, powerValue, flags };

        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("CopyHeightParameters"), &copyHeightParameters, sizeof(CopyHeightParameters))
        };

        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "CopyHeight", center, radius, adjustment, extraPackets, dimof(extraPackets));
    }

    auto HeightsUberSurfaceInterface::FineTune(
        RenderCore::IThreadContext& threadContext, 
        Float2 center, float radius, float newHeight) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(center[0] + radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(center[1] + radius)));

        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need

        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "FineTune", center, radius, newHeight, nullptr, 0);
    }

    auto HeightsUberSurfaceInterface::Rotate(RenderCore::IThreadContext& threadContext, Float2 center, float radius, Float3 rotationAxis, float rotationAngle) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        const float extend = 1.2f;
        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - extend * radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - extend * radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(center[0] + extend * radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(center[1] + extend * radius)));

        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need
        assert(rotationAxis[2] == 0.f);
        struct RotateParamaters
        {
            Float2 _rotationAxis;
            float _angle;
            float _dummy;
        } rotateParameters = { Truncate(rotationAxis), rotationAngle, 0.f };

        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("RotateParameters"), &rotateParameters, sizeof(RotateParamaters))
        };

        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "Rotate", center, radius, 1.f, extraPackets, dimof(extraPackets));
    }

    auto HeightsUberSurfaceInterface::Smooth(RenderCore::IThreadContext& threadContext, Float2 center, float radius, unsigned filterRadius, float standardDeviation, float strength, unsigned flags) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        auto fieldWidth = _pimpl->_uberSurface->GetWidth()-1;
        auto fieldHeight = _pimpl->_uberSurface->GetHeight()-1;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(center[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(center[1] - radius)));
        UInt2 adjMaxs(  std::min(fieldWidth, (unsigned)XlCeil(center[0] + radius)),
                        std::min(fieldHeight, (unsigned)XlCeil(center[1] + radius)));

        if (!PrepareCache(
            UInt2(std::max(0u, adjMins[0]-filterRadius), std::max(0u, adjMins[1]-filterRadius)),
            UInt2(std::min(fieldWidth, adjMaxs[0]+filterRadius), std::min(fieldHeight, adjMins[1]+filterRadius))))
			return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need

        struct BlurParameters
        {
            Float4 _weights[33];
            unsigned _filterSize;
            unsigned _flags;
            unsigned _dummy[2];
        } blurParameters;
        unsigned filterSize = std::min(33u, 1 + filterRadius * 2);
        blurParameters._filterSize = filterSize;
        blurParameters._flags = flags;
        blurParameters._dummy[0] = blurParameters._dummy[1] = 0;
        float temp[dimof(((BlurParameters*)nullptr)->_weights)];
        BuildGaussianFilteringWeights(temp, standardDeviation, filterSize);
        for (unsigned c=0; c<filterSize; ++c)
            blurParameters._weights[c] = Float4(temp[c], temp[c], temp[c], temp[c]);
    
        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("GaussianParameters"), &blurParameters, sizeof(BlurParameters))
        };
            
        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "Smooth", center, radius, strength, extraPackets, dimof(extraPackets));
    }

    auto HeightsUberSurfaceInterface::FillWithNoise(RenderCore::IThreadContext& threadContext, Float2 mins, Float2 maxs, float baseHeight, float noiseHeight, float roughness, float fractalDetail) -> TerrainToolResult
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        auto fieldWidth = _pimpl->_uberSurface->GetWidth()-1;
        auto fieldHeight = _pimpl->_uberSurface->GetHeight()-1;

        UInt2 adjMins((unsigned)std::max(0.f, mins[0]), (unsigned)std::max(0.f, mins[1]));
        UInt2 adjMaxs(std::min(fieldWidth, (unsigned)maxs[0]), std::min(fieldHeight, (unsigned)maxs[1]));
        if (!PrepareCache(adjMins, adjMaxs))
			return TerrainToolResult::OutsideLock;

        struct FillWithNoiseParameters
        {
            Float2 _mins, _maxs;
            float _baseHeight, _noiseHeight, _roughness, _fractalDetail;
        } fillWithNoiseParameters = { mins, maxs, baseHeight, noiseHeight, roughness, fractalDetail };
    
        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("FillWithNoiseParameters"), &fillWithNoiseParameters, sizeof(FillWithNoiseParameters))
        };
            
        return ApplyTool(threadContext, adjMins, adjMaxs, baseShader "FillWithNoise", LinearInterpolate(mins, maxs, 0.5f), 1.f, 1.f, extraPackets, dimof(extraPackets));
    }

    void    HeightsUberSurfaceInterface::Erosion_Begin(
        RenderCore::IThreadContext& context,
        Float2 mins, Float2 maxs, const TerrainConfig& cfg)
    {
        Erosion_End();

        auto tileSize = ErosionSimulation::DefaultTileSize();
        unsigned gridsX = (unsigned)XlCeil((maxs[0] - mins[0])/tileSize[0]);
        unsigned gridsY = (unsigned)XlCeil((maxs[1] - mins[1])/tileSize[1]);

        Float2 center(XlFloor((mins[0] + maxs[0])/2.f), XlFloor((mins[1] + maxs[1])/2.f));
        UInt2 size(gridsX * tileSize[0], gridsY * tileSize[1]);

            // adjust "mins" and "maxs"
            // (size should be even now, so this is safe)
        mins = center - 0.5f * Float2(size);
        maxs = center + 0.5f * Float2(size);
            // but we need to shift the grid if we end up straddling zero
        if (mins[0] < 0.f) { maxs[0] -= mins[0]; mins[0] = 0.f; }
        if (mins[1] < 0.f) { maxs[1] -= mins[1]; mins[1] = 0.f; }
        assert(mins[0] >= 0 && mins[1] >= 0);

            // prepare our new GPU cache
        UInt2 finalCacheMin = UInt2(unsigned(mins[0]), unsigned(mins[1]));
        UInt2 finalCacheMax = UInt2(unsigned(maxs[0]), unsigned(maxs[1]));
        PrepareCache(finalCacheMin, finalCacheMax);

        Int2 gpuCacheOffset = UInt2(unsigned(mins[0]), unsigned(mins[1])) - _pimpl->_gpuCacheMins;

            // Build a SurfaceHeightsProvider that can map from world space coordinates
            // into the coordinates of the GPU cache. In the coordinate system of the 
            // water simulation, the origin is the minimum point of the first tile we're
            // simulating. In other words, there is a offset between the water sim world
            // coords and the real world coords
        float terrainScale = cfg.ElementSpacing();
        // auto surfaceHeightsProvider = std::make_unique<Internal::SurfaceHeightsProvider>(
        //     (Float2(_pimpl->_gpuCacheMins) - Float2(finalCacheMin)) * terrainScale, (Float2(_pimpl->_gpuCacheMaxs + UInt2(1,1)) - Float2(finalCacheMin)) * terrainScale,
        //     _pimpl->_gpuCacheMaxs - _pimpl->_gpuCacheMins + UInt2(1,1), 
        //     _pimpl->_gpucache[0]);

        auto metalContext = RenderCore::Metal::DeviceContext::Get(context);

        UInt2 erosionSimSize = finalCacheMax - finalCacheMin + UInt2(1,1);
        auto erosionSim = std::make_unique<ErosionSimulation>(erosionSimSize, terrainScale);

        Metal::ShaderResourceView gpuCacheSRV(_pimpl->_gpucache[0]->GetUnderlying());
        erosionSim->InitHeights(
            *metalContext, gpuCacheSRV,
            gpuCacheOffset, gpuCacheOffset + erosionSimSize);

        _pimpl->_erosionSim = std::move(erosionSim);
        _pimpl->_erosionSimGPUCacheOffset = gpuCacheOffset;
        _pimpl->_erosionWorldSpaceOffset = terrainScale * Float2(_pimpl->_erosionSimGPUCacheOffset + _pimpl->_gpuCacheMins);
    }

    void    HeightsUberSurfaceInterface::Erosion_Tick(
        RenderCore::IThreadContext& context,
        const ErosionSimulation::Settings& params)
    {
        if (!Erosion_IsPrepared()) {
            return;     // no active sim
        }

        auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
        _pimpl->_erosionSim->Tick(*metalContext, params);

        auto gpuCacheOffset = _pimpl->_erosionSimGPUCacheOffset;
        auto size = _pimpl->_erosionSim->GetDimensions();

        Metal::UnorderedAccessView gpuCacheUAV(_pimpl->_gpucache[0]->GetUnderlying());
        _pimpl->_erosionSim->GetHeights(
            *metalContext, gpuCacheUAV,
            gpuCacheOffset, gpuCacheOffset + size);

            //  Update the mesh with the changes
        QueueShortCircuitUpdate(
            _pimpl->_gpuCacheMins + gpuCacheOffset, 
            _pimpl->_gpuCacheMins + gpuCacheOffset + size);
    }

    void    HeightsUberSurfaceInterface::Erosion_End()
    {
        _pimpl->_erosionSimGPUCacheOffset = UInt2(0,0);
        _pimpl->_erosionWorldSpaceOffset = Float2(0.f, 0.f);
        _pimpl->_erosionSim.reset();
    }

    bool    HeightsUberSurfaceInterface::Erosion_IsPrepared() const
    {
        return _pimpl->_erosionSim != nullptr;
    }

    void    HeightsUberSurfaceInterface::Erosion_RenderDebugging(
        RenderCore::IThreadContext& context,
        RenderCore::Techniques::ParsingContext& parserContext,
        const TerrainCoordinateSystem& coords)
    {
        if (!Erosion_IsPrepared()) return;

        auto metalContext = RenderCore::Metal::DeviceContext::Get(context);
        _pimpl->_erosionSim->RenderDebugging(
            *metalContext, parserContext, 
            ErosionSimulation::RenderDebugMode::WaterVelocity3D,
            _pimpl->_erosionWorldSpaceOffset + Truncate(coords.TerrainOffset()));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void HeightsUberSurfaceInterface::CancelActiveOperations()
    {
            // (if we're currently running erosion, cancel it now...)
        Erosion_End();
    }

    TerrainUberHeightsSurface* HeightsUberSurfaceInterface::GetUberSurface() { return _uberSurface; }

    HeightsUberSurfaceInterface::HeightsUberSurfaceInterface(TerrainUberHeightsSurface& uberSurface)
    : GenericUberSurfaceInterface(uberSurface)
    {
        _uberSurface = &uberSurface;
    }

    HeightsUberSurfaceInterface::~HeightsUberSurfaceInterface()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    TerrainToolResult CoverageUberSurfaceInterface::Paint(RenderCore::IThreadContext& context, Float2 centre, float radius, unsigned paintValue)
    {
        if (!_pimpl || !_pimpl->_uberSurface)
            return TerrainToolResult::Error;

        UInt2 adjMins(  (unsigned)std::max(0.f, XlFloor(centre[0] - radius)),
                        (unsigned)std::max(0.f, XlFloor(centre[1] - radius)));
        UInt2 adjMaxs(  std::min(_pimpl->_uberSurface->GetWidth()-1, (unsigned)XlCeil(centre[0] + radius)),
                        std::min(_pimpl->_uberSurface->GetHeight()-1, (unsigned)XlCeil(centre[1] + radius)));

        if (!PrepareCache(adjMins, adjMaxs))
            return TerrainToolResult::OutsideLock;

            //  run a shader that will modify the gpu-cached part of the uber surface as we need
        struct PaintParameters
        {
            unsigned _newValue;
            float dummy[3];
        } params = { paintValue, 0.f, 0.f, 0.f };

        std::tuple<uint64_t, void*, size_t> extraPackets[] = 
        {
            std::make_tuple(Hash64("PaintParameters"), &params, sizeof(params))
        };

        return ApplyTool(context, adjMins, adjMaxs, SCENE_ENGINE_RES "Terrain/terrainmodification_int.hlsl:Paint", centre, radius, 0.f, extraPackets, dimof(extraPackets));
    }

    void CoverageUberSurfaceInterface::CancelActiveOperations()
    {
    }

    CoverageUberSurfaceInterface::CoverageUberSurfaceInterface(TerrainUberSurfaceGeneric& uberSurface)
    : GenericUberSurfaceInterface(uberSurface)
    {
    }
    CoverageUberSurfaceInterface::~CoverageUberSurfaceInterface() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template TerrainUberHeightsSurface;
    template TerrainUberSurface<ShadowSample>;
    template TerrainUberSurface<uint8>;
}


namespace Utility { namespace ImpliedTyping
{
    template<>
        TypeDesc TypeOf<SceneEngine::ShadowSample>()
        {
            return TypeDesc{TypeCat::UInt16, 2};
        }
}}
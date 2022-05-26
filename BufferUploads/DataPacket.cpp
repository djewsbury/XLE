// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceUploadHelper.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/ResourceUtils.h"
#include "../Assets/IFileSystem.h"
#include "../Assets/IAsyncMarker.h"
#include "../Assets/AssetsCore.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../Utility/Threading/CompletionThreadPool.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Conversion.h"
#include "../Utility/StringUtils.h"
#include "../OSServices/RawFS.h"
#include <queue>
#include <thread>

#if PLATFORMOS_TARGET == PLATFORMOS_WINDOWS
    #include "../OSServices/WinAPI/IncludeWindows.h"
#endif

// Important character set note!
// We're using some "wchar_t" conversions in this file. This is because we're using some Windows API functions that take WCHAR strings
// (indirectly via the DirectXTex library).
// However, Windows should technically expect UTF16 (not UCS2) encoding for WCHAR. Maybe in our usage patterns it's not a big
// deal... But we should be careful!

namespace BufferUploads
{

    class BasicRawDataPacket : public IDataPacket
    {
    public:
        virtual IteratorRange<void*> GetData(SubResourceId subRes = {}) override;
        virtual TexturePitches GetPitches(SubResourceId subRes = {}) const override;

        BasicRawDataPacket(size_t size, IteratorRange<const void*> data = {}, TexturePitches pitches = TexturePitches());
        virtual ~BasicRawDataPacket();
    protected:
        std::unique_ptr<uint8_t, PODAlignedDeletor> _data; 
        size_t _dataSize;
        TexturePitches _pitches;

        BasicRawDataPacket(const BasicRawDataPacket&);
        BasicRawDataPacket& operator=(const BasicRawDataPacket&);
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    BasicRawDataPacket::BasicRawDataPacket(size_t size, IteratorRange<const void*> data, TexturePitches pitches)
    : _dataSize(size), _pitches(pitches)    
    {
            // note --  prefer sending aligned data as input! Just makes it
            //          more convenient for copying
        _data.reset((uint8_t*)XlMemAlign(_dataSize, 16));
        if (!data.empty()) {
            assert(data.size() == _dataSize);
            if ((size_t(data.begin()) & 0xf)==0x0 && (_dataSize & 0xf)==0x0) {
                XlCopyMemoryAlign16(_data.get(), data.begin(), _dataSize);
            } else {
                XlCopyMemory(_data.get(), data.begin(), _dataSize);
            }
        }
    }

    BasicRawDataPacket::~BasicRawDataPacket()
    {}

    IteratorRange<void*> BasicRawDataPacket::GetData(SubResourceId subRes)
    {
        assert(subRes._mip == 0 && subRes._arrayLayer == 0);
        return MakeIteratorRange(_data.get(), PtrAdd(_data.get(), _dataSize)); 
    }
    
    TexturePitches BasicRawDataPacket::GetPitches(SubResourceId subRes) const
    {
        assert(subRes._mip == 0 && subRes._arrayLayer == 0);
        return _pitches; 
    }

    std::shared_ptr<IDataPacket> CreateBasicPacket(
        IteratorRange<const void*> data, TexturePitches rowAndSlicePitch)
    {
        return std::make_shared<BasicRawDataPacket>(data.size(), data, rowAndSlicePitch);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class VectorRawDataPacket : public IDataPacket
    {
    public:
        virtual IteratorRange<void*> GetData(SubResourceId subRes = {}) override;
        virtual TexturePitches GetPitches(SubResourceId subRes = {}) const override;

        VectorRawDataPacket(std::vector<uint8_t>&& data, TexturePitches pitches = TexturePitches());
        virtual ~VectorRawDataPacket();
    protected:
        std::vector<uint8_t> _data; 
        TexturePitches _pitches;

        VectorRawDataPacket(const VectorRawDataPacket&) = delete;
        VectorRawDataPacket& operator=(const VectorRawDataPacket&) = delete;
    };


    VectorRawDataPacket::VectorRawDataPacket(std::vector<uint8_t>&& data, TexturePitches pitches)
    : _data(std::move(data)), _pitches(pitches)    
    {}

    VectorRawDataPacket::~VectorRawDataPacket()
    {}

    IteratorRange<void*> VectorRawDataPacket::GetData(SubResourceId subRes)
    {
        assert(subRes._mip == 0 && subRes._arrayLayer == 0);
        return MakeIteratorRange(_data); 
    }
    
    TexturePitches VectorRawDataPacket::GetPitches(SubResourceId subRes) const
    {
        assert(subRes._mip == 0 && subRes._arrayLayer == 0);
        return _pitches; 
    }

    std::shared_ptr<IDataPacket> CreateBasicPacket(std::vector<uint8_t>&& data, TexturePitches rowAndSlicePitch)
    {
        return std::make_shared<VectorRawDataPacket>(std::move(data), rowAndSlicePitch);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::shared_ptr<IDataPacket> CreateEmptyPacket(const ResourceDesc& desc)
    {
            // Create an empty packet of the appropriate size for the given desc
            // Linear buffers are simple, but textures need a little more detail...
        if (desc._type == ResourceDesc::Type::LinearBuffer) {
            auto size = RenderCore::ByteCount(desc);
            return std::make_shared<BasicRawDataPacket>(size, IteratorRange<const void*>{}, TexturePitches{size, size});
        } else if (desc._type == ResourceDesc::Type::Texture) {
                //  currently not supporting textures with multiple mip-maps
                //  or multiple array slices
            assert(desc._textureDesc._mipCount <= 1);
            assert(desc._textureDesc._arrayCount <= 1);

            auto pitches = RenderCore::MakeTexturePitches(desc._textureDesc);
            return std::make_shared<BasicRawDataPacket>(pitches._slicePitch, IteratorRange<const void*>{}, pitches);
        }

        return nullptr;
    }

    std::shared_ptr<IDataPacket> CreateEmptyLinearBufferPacket(size_t size)
    {
        return std::make_shared<BasicRawDataPacket>(size, IteratorRange<const void*>{}, TexturePitches{(unsigned)size, (unsigned)size});
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
            //      S T R E A M I N G   P A C K E T

    using Marker = ::Assets::GenericFuture;

    class FileDataSource : public IDataPacket, public std::enable_shared_from_this<FileDataSource>
    {
    public:
        virtual IteratorRange<void*>    GetData         (SubResourceId subRes) override;
        virtual TexturePitches          GetPitches      (SubResourceId subRes) const override;

        virtual std::shared_ptr<Marker>     BeginBackgroundLoad();

        FileDataSource(const void* fileHandle, size_t offset, size_t dataSize, TexturePitches pitches);
        virtual ~FileDataSource();

    protected:
        HANDLE      _fileHandle;
        size_t      _dataSize;
        size_t      _offset;

        struct SpecialOverlapped
        {
            OVERLAPPED                      _internal;
            std::weak_ptr<FileDataSource>   _returnPointer;
        };
        SpecialOverlapped _overlappedStatus;
        std::unique_ptr<byte[], PODAlignedDeletor> _pkt;
        std::shared_ptr<Marker> _marker;

        TexturePitches  _pitches;

        static void CALLBACK CompletionRoutine(
            DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
            LPOVERLAPPED lpOverlapped);
    };

    IteratorRange<void*> FileDataSource::GetData(SubResourceId subRes)
    {
        assert(subRes._mip == 0 && subRes._arrayLayer == 0);
        return MakeIteratorRange(_pkt.get(), PtrAdd(_pkt.get(), _dataSize));
    }

    TexturePitches FileDataSource::GetPitches(SubResourceId subRes) const    { /*assert(subRes == 0);*/ return _pitches; }

    void CALLBACK FileDataSource::CompletionRoutine(
        DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered,
        LPOVERLAPPED lpOverlapped)
    {
        auto* o = (SpecialOverlapped*)lpOverlapped;
        assert(o);
        auto returnPointer = o->_returnPointer.lock();
        if (!returnPointer) return;
        assert(returnPointer->_marker);
        assert(returnPointer->_marker->GetAssetState() == ::Assets::AssetState::Pending);

            // We don't have to do any extra processing right now. Just mark the asset as ready
            // or invalid, based on the result...
        returnPointer->_marker->SetState(
            (dwErrorCode == ERROR_SUCCESS) ? ::Assets::AssetState::Ready : ::Assets::AssetState::Invalid);

            // we can reset the "_returnPointer", which will also decrease the reference
            // count on the FileDataSource object
        returnPointer.reset();
    }

    auto FileDataSource::BeginBackgroundLoad() -> std::shared_ptr < Marker >
    {
        assert(!_marker);
        assert(_fileHandle && _fileHandle != INVALID_HANDLE_VALUE);

        // Queue read operation begin (it will happen asynchronously)...

        _marker = std::make_shared<Marker>();
        
        XlSetMemory(&_overlappedStatus._internal, 0, sizeof(_overlappedStatus._internal));
        _overlappedStatus._internal.Pointer = (void*)_offset;
        _overlappedStatus._returnPointer = weak_from_this();

        auto* o = &_overlappedStatus;
            // this should be a very quick operation -- it might be best to put it in a
            // separate thread pool from the long operations
        ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
            [o]()
            {
                auto pkt = o->_returnPointer.lock();
                if (!pkt) Throw(std::runtime_error("FileDataSource has expired"));

                    // We allocate the buffer here, to remove malloc costs from the caller thread
                pkt->_pkt.reset((byte*)XlMemAlign(pkt->_dataSize, 16));

                auto result = ReadFileEx(
                    pkt->_fileHandle, pkt->_pkt.get(), (DWORD)pkt->_dataSize, 
                    &pkt->_overlappedStatus._internal, &CompletionRoutine);

                if (!result) {
                    auto lastError = GetLastError();
                    (void)lastError;

                    pkt->_marker->SetState(Assets::AssetState::Invalid);
                    o->_returnPointer.reset();
                }
            });
        
        return _marker;
    }

    FileDataSource::FileDataSource(const void* fileHandle, size_t offset, size_t dataSize, TexturePitches pitches)
    {
        assert(dataSize);
        assert(fileHandle != INVALID_HANDLE_VALUE);

            //  duplicate the file handle so we get our own reference count on this
            //  file object.
        ::DuplicateHandle(
            GetCurrentProcess(), (HANDLE)fileHandle, GetCurrentProcess(),
            &_fileHandle, 0, FALSE, DUPLICATE_SAME_ACCESS);

        _dataSize = dataSize;
        _pitches = pitches;
        _offset = offset;
    }

    FileDataSource::~FileDataSource()
    {
        if (_fileHandle && _fileHandle!=INVALID_HANDLE_VALUE) {
            CloseHandle(_fileHandle);
        }
    }

    std::shared_ptr<IDataPacket> CreateFileDataSource(const void* fileHandle, size_t offset, size_t dataSize, TexturePitches pitches)
    {
        return std::make_shared<FileDataSource>(fileHandle, offset, dataSize, pitches);
    }

    IDataPacket::~IDataPacket() {}
    IAsyncDataSource::~IAsyncDataSource() {}
    
}

// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "GeneralCompiler.h"
#include "../../Assets/CompilationThread.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/CompilerHelper.h"
#include "../../Assets/InvalidAssetManager.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/NascentChunkArray.h"
#include "../../Assets/CompilerLibrary.h"
#include "../../Assets/IFileSystem.h"
#include "../../ConsoleRig/AttachableLibrary.h"
#include "../../ConsoleRig/Log.h"
#include "../../Utility/Threading/LockFree.h"
#include "../../Utility/Threading/ThreadObject.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Streams/FileUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/SystemUtils.h"

namespace Converter 
{
	typedef std::shared_ptr<::Assets::ICompilerDesc> GetCompilerDescFn();
	typedef std::shared_ptr<::Assets::ICompileOperation> CreateCompileOperationFn(StringSection<::Assets::ResChar> identifier);

	class CompilerLibrary
	{
	public:
		void PerformCompile(
			uint64 typeCode, StringSection<::Assets::ResChar> initializer, 
			::Assets::PendingCompileMarker& compileMarker,
			const ::Assets::IntermediateAssets::Store& destinationStore);
		void AttachLibrary();

		bool IsKnownExtension(StringSection<::Assets::ResChar> ext)
		{
			AttachLibrary();
			for (const auto& e:_knownExtensions)
				if (XlEqStringI(MakeStringSection(e), ext)) return true;
			return false;
		}

		CompilerLibrary(StringSection<::Assets::ResChar> libraryName)
			: _library(libraryName.AsString().c_str())
		{
			_createCompileOpFunction = nullptr;
			_isAttached = _attemptedAttach = false;
		}

		CompilerLibrary(CompilerLibrary&&) never_throws = default;
		CompilerLibrary& operator=(CompilerLibrary&&) never_throws = default;

	private:
		// ---------- interface to DLL functions ----------
		CreateCompileOperationFn* _createCompileOpFunction;

		std::vector<::Assets::rstring> _knownExtensions;

		::Assets::rstring _libraryName;
		ConsoleRig::AttachableLibrary _library;
		bool _isAttached;
		bool _attemptedAttach;
	};

    class GeneralCompiler::Pimpl
    {
    public:
		std::vector<CompilerLibrary>		_compilers;
		bool								_discoveryDone;
		::Assets::DirectorySearchRules		_librarySearchRules;

        Threading::Mutex					_threadLock;   // (used while initialising _thread for the first time)
        std::unique_ptr<::Assets::CompilationThread>	_thread;

		void DiscoverLibraries();
		void PerformCompile(::Assets::QueuedCompileOperation& op);

		Pimpl() : _discoveryDone(false) {}
		Pimpl(const Pimpl&) = delete;
		Pimpl& operator=(const Pimpl&) = delete;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////

    static void BuildChunkFile(
        BasicFile& file,
        IteratorRange<::Assets::NascentChunk*>& chunks,
        const ConsoleRig::LibVersionDesc& versionInfo,
        std::function<bool(const ::Assets::NascentChunk&)> predicate)
    {
        unsigned chunksForMainFile = 0;
		for (const auto& c:chunks)
            if (predicate(c))
                ++chunksForMainFile;

        using namespace Serialization::ChunkFile;
        auto header = MakeChunkFileHeader(
            chunksForMainFile, 
            versionInfo._versionString, versionInfo._buildDateString);
        file.Write(&header, sizeof(header), 1);

        unsigned trackingOffset = unsigned(file.TellP() + sizeof(ChunkHeader) * chunksForMainFile);
        for (const auto& c:chunks)
            if (predicate(c)) {
                auto hdr = c._hdr;
                hdr._fileOffset = trackingOffset;
                file.Write(&hdr, sizeof(c._hdr), 1);
                trackingOffset += hdr._size;
            }

        for (const auto& c:chunks)
            if (predicate(c))
                file.Write(AsPointer(c._data.begin()), c._data.size(), 1);
    }

    static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;

    static void SerializeToFile(
		IteratorRange<::Assets::NascentChunk*> chunks,
        const char destinationFilename[],
        const ConsoleRig::LibVersionDesc& versionInfo)
    {
            // Create the directory if we need to...
        RawFS::CreateDirectoryRecursive(MakeFileNameSplitter(destinationFilename).DriveAndPath());

            // We need to separate out chunks that will be written to
            // the main output file from chunks that will be written to
            // a metrics file.

        {
            auto outputFile = ::Assets::MainFileSystem::OpenBasicFile(destinationFilename, "wb");
            BuildChunkFile(outputFile, chunks, versionInfo,
                [](const ::Assets::NascentChunk& c) { return c._hdr._type != ChunkType_Metrics; });
        }

        for (const auto& c:chunks)
            if (c._hdr._type == ChunkType_Metrics) {
                auto outputFile = ::Assets::MainFileSystem::OpenBasicFile(
                    StringMeld<MaxPath>() << destinationFilename << "-" << c._hdr._name,
                    "wb");
                outputFile.Write((const void*)AsPointer(c._data.cbegin()), 1, c._data.size());
            }
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    void CompilerLibrary::PerformCompile(
		uint64 typeCode, StringSection<::Assets::ResChar> initializer, 
		::Assets::PendingCompileMarker& compileMarker,
		const ::Assets::IntermediateAssets::Store& destinationStore)
    {
        TRY
        {
            AttachLibrary();

            ConsoleRig::LibVersionDesc libVersionDesc;
            _library.TryGetVersion(libVersionDesc);

            TRY 
            {
                // const auto* destinationFile = compileMarker.GetLocator()._sourceID0;

                auto model = (*_createCompileOpFunction)(initializer);
						
				// look for the first target of the correct type
				auto targetCount = model->TargetCount();
				bool foundTarget = false;
				for (unsigned t=0; t<targetCount; ++t) {
					auto target = model->GetTarget(t);
					if (target._type == typeCode) {
						auto chunks = model->SerializeTarget(t);

						::Assets::ResChar destinationFile[MaxPath];
						XlFormatString(destinationFile, dimof(destinationFile), "%s-%s", compileMarker.GetLocator()._sourceID0, target._name);

						SerializeToFile(MakeIteratorRange(*chunks), destinationFile, libVersionDesc);
						foundTarget = true;
					}
				}

				if (!foundTarget)
					Throw(::Exceptions::BasicLabel("Could not find target of the requested type in compile operation for (%s)", initializer.AsString().c_str()));

                    // write new dependencies
                // std::vector<::Assets::DependentFileState> deps;
                // deps.push_back(destinationStore.GetDependentFileState(initializer));
				// auto splitName = MakeFileNameSplitter(initializer);
				// compileMarker.GetLocator()._dependencyValidation = destinationStore.WriteDependencies(destinationFile, splitName.DriveAndPath(), MakeIteratorRange(deps));
        
				compileMarker.SetState(::Assets::AssetState::Ready);

            } CATCH(...) {
                if (!compileMarker.GetLocator()._dependencyValidation) {
					compileMarker.GetLocator()._dependencyValidation = std::make_shared<::Assets::DependencyValidation>();
                    ::Assets::RegisterFileDependency(compileMarker.GetLocator()._dependencyValidation, initializer);
                }
                throw;
            } CATCH_END


        } CATCH(const std::exception& e) {
            LogAlwaysError << "Caught exception while performing general compiler conversion. Exception details as follows:";
            LogAlwaysError << e.what();
            if (::Assets::Services::GetInvalidAssetMan())
                ::Assets::Services::GetInvalidAssetMan()->MarkInvalid(initializer, e.what());
			compileMarker.SetState(::Assets::AssetState::Invalid);
        } CATCH(...) {
            if (::Assets::Services::GetInvalidAssetMan())
                ::Assets::Services::GetInvalidAssetMan()->MarkInvalid(initializer, "Unknown error");
			compileMarker.SetState(::Assets::AssetState::Invalid);
        } CATCH_END
    }

	void CompilerLibrary::AttachLibrary()
	{
		if (!_attemptedAttach && !_isAttached) {
			_attemptedAttach = true;

			_isAttached = _library.TryAttach();
			if (_isAttached) {
				_createCompileOpFunction    = _library.GetFunction<decltype(_createCompileOpFunction)>("CreateCompileOperation");

				auto compilerDescFn = _library.GetFunction<GetCompilerDescFn*>("GetCompilerDesc");
				if (compilerDescFn) {
					auto compilerDesc = (*compilerDescFn)();
					auto targetCount = compilerDesc->FileKindCount();
					for (unsigned c=0; c<targetCount; ++c) {
						_knownExtensions.push_back(compilerDesc->GetFileKind(c)._extension);
					}
				}
			}
		}

		// check for problems (missing functions or bad version number)
		if (!_isAttached)
			Throw(::Exceptions::BasicLabel("Error while linking asset conversion DLL. Could not find DLL (%s)", _libraryName.c_str()));

		if (!_createCompileOpFunction)
			Throw(::Exceptions::BasicLabel("Error while linking asset conversion DLL. Some interface functions are missing"));
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    class GeneralCompiler::Marker : public ::Assets::ICompileMarker
    {
    public:
        ::Assets::IntermediateAssetLocator GetExistingAsset() const;
        std::shared_ptr<::Assets::PendingCompileMarker> InvokeCompile() const;
        StringSection<::Assets::ResChar> Initializer() const;

        Marker(
            StringSection<::Assets::ResChar> requestName, uint64 typeCode,
            const ::Assets::IntermediateAssets::Store& store,
            std::shared_ptr<GeneralCompiler> compiler);
        ~Marker();
    private:
        std::weak_ptr<GeneralCompiler> _compiler;
        ::Assets::rstring _requestName;
        uint64 _typeCode;
        const ::Assets::IntermediateAssets::Store* _store;

        void MakeIntermediateName(::Assets::ResChar destination[], size_t count) const;
    };

    void GeneralCompiler::Marker::MakeIntermediateName(::Assets::ResChar destination[], size_t count) const
    {
        _store->MakeIntermediateName(destination, (unsigned)count, _requestName.c_str());
		XlCatString(destination, count, "-res");
    }

    ::Assets::IntermediateAssetLocator GeneralCompiler::Marker::GetExistingAsset() const
    {
        ::Assets::IntermediateAssetLocator result;
        // MakeIntermediateName(result._sourceID0, dimof(result._sourceID0));
        // result._dependencyValidation = _store->MakeDependencyValidation(result._sourceID0);
        return result;
    }

    std::shared_ptr<::Assets::PendingCompileMarker> GeneralCompiler::Marker::InvokeCompile() const
    {
        auto c = _compiler.lock();
        if (!c) return nullptr;

		c->_pimpl->DiscoverLibraries();

		auto splitRequest = MakeFileNameSplitter(_requestName);

		unsigned compilerIndex = 0;
			// Find the compiler that can handle this asset type (just by looking at the extension)
		for (; compilerIndex < c->_pimpl->_compilers.size(); ++compilerIndex)
			if (c->_pimpl->_compilers[compilerIndex].IsKnownExtension(splitRequest.Extension()))
				break;

		if (compilerIndex >= c->_pimpl->_compilers.size())
			Throw(::Exceptions::BasicLabel("Could not find compiler to handle request (%s)", _requestName.c_str()));

        auto backgroundOp = std::make_shared<::Assets::QueuedCompileOperation>();
        backgroundOp->SetInitializer(_requestName.c_str());
        XlCopyString(backgroundOp->_initializer0, _requestName);
        MakeIntermediateName(backgroundOp->GetLocator()._sourceID0, dimof(backgroundOp->GetLocator()._sourceID0));
        backgroundOp->_destinationStore = _store;
        backgroundOp->_typeCode = _typeCode;
		backgroundOp->_compilerIndex = compilerIndex;

        {
            ScopedLock(c->_pimpl->_threadLock);
            if (!c->_pimpl->_thread) {
                auto* p = c->_pimpl.get();
                c->_pimpl->_thread = std::make_unique<::Assets::CompilationThread>(
                    [p](::Assets::QueuedCompileOperation& op) { p->PerformCompile(op); });
            }
        }
        c->_pimpl->_thread->Push(backgroundOp);
        
        return std::move(backgroundOp);
    }

    StringSection<::Assets::ResChar> GeneralCompiler::Marker::Initializer() const
    {
        return MakeStringSection(_requestName);
    }

	GeneralCompiler::Marker::Marker(
        StringSection<::Assets::ResChar> requestName, uint64 typeCode,
        const ::Assets::IntermediateAssets::Store& store,
        std::shared_ptr<GeneralCompiler> compiler)
    : _compiler(std::move(compiler)), _requestName(requestName.AsString()), _typeCode(typeCode), _store(&store)
    {}

	GeneralCompiler::Marker::~Marker() {}

    std::shared_ptr<::Assets::ICompileMarker> GeneralCompiler::PrepareAsset(
        uint64 typeCode, 
        const StringSection<::Assets::ResChar> initializers[], unsigned initializerCount, 
        const ::Assets::IntermediateAssets::Store& destinationStore)
    {
        return std::make_shared<Marker>(initializers[0], typeCode, destinationStore, shared_from_this());
    }

    void GeneralCompiler::StallOnPendingOperations(bool cancelAll)
    {
        {
            ScopedLock(_pimpl->_threadLock);
            if (!_pimpl->_thread) return;
        }
        _pimpl->_thread->StallOnPendingOperations(cancelAll);
    }

	void GeneralCompiler::AddLibrarySearchDirectories(const ::Assets::DirectorySearchRules& directories)
	{
		assert(!_pimpl->_discoveryDone);
		_pimpl->_librarySearchRules.Merge(directories);
	}

	GeneralCompiler::GeneralCompiler()
	{ 
		_pimpl = std::make_shared<Pimpl>(); 

		// Default search path for libraries is just the process path.
		// In some cases (eg, for unit tests where the process path points to an internal visual studio path), 
		// we have to include extra paths
		char processPath[MaxPath];
		XlGetProcessPath((utf8*)processPath, dimof(processPath));
		_pimpl->_librarySearchRules.AddSearchDirectory(
			MakeFileNameSplitter(processPath).DriveAndPath());
	}
	GeneralCompiler::~GeneralCompiler() {}

	void GeneralCompiler::Pimpl::PerformCompile(::Assets::QueuedCompileOperation& op)
	{
		assert(op._compilerIndex < _compilers.size());
		_compilers[op._compilerIndex].PerformCompile(op._typeCode, op._initializer0, op, *op._destinationStore);
	}

	void GeneralCompiler::Pimpl::DiscoverLibraries()
	{
		if (_discoveryDone) return;

		// Look for attachable libraries that can compile raw assets
		// We're expecting to find them in the same directory as the executable with the form "*Conversion.dll" 
		auto candidateCompilers = _librarySearchRules.FindFiles(MakeStringSection("*Conversion.dll"));
		for (auto& c : candidateCompilers) {
			TRY{
				CompilerLibrary library(c);
				library.AttachLibrary();
				_compilers.emplace_back(std::move(library));
			} CATCH (const std::exception& e) {
				LogWarning << "Failed while attempt to attach library: " << e.what();
			} CATCH_END
		}

		_discoveryDone = true;
	}

}

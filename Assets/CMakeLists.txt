set(Src
    ArchiveCache.cpp
    Assets.cpp
    AssetServices.cpp
    AssetSetManager.cpp
    AssetTraits.cpp
    AssetUtils.cpp
    AsyncMarkerGroup.cpp
    BlockSerializer.cpp
    ChunkFileContainer.cpp
    ChunkFile.cpp
    CompilationThread.cpp
    CompileAndAsyncManager.cpp
    ConfigFileContainer.cpp
    IntermediatesStore.cpp
    IntermediateCompilers.cpp
    MainFileSystem.cpp
    MemoryFile.cpp
    MountingTree.cpp
    NascentChunk.cpp
    OSFileSystem.cpp
    RawFileAsset.cpp
    Legacy/AssetsLegacy.cpp)

add_library(Assets STATIC ${Src})
xle_configure_library(Assets)
target_link_libraries(Assets PUBLIC ConsoleRig OSServices Utility ForeignMisc)
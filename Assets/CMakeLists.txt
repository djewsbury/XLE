set(Src
    ArchiveCache.cpp
    ArtifactCollectionFuture.cpp
    AssetHeapNew.cpp
    Assets.cpp
    AssetServices.cpp
    AssetSetManager.cpp
    AssetTraits.cpp
    AssetUtils.cpp
    AsyncMarkerGroup.cpp
    BlockSerializer.cpp
    ChunkFileContainer.cpp
    ChunkFileWriter.cpp
    CompoundAsset.cpp
    ConfigFileContainer.cpp
    DepVal.cpp
    IntermediateCompilers.cpp
    IntermediatesStore.cpp
    LooseFilesCache.cpp
    MainFileSystem.cpp
    MemoryFile.cpp
    MountingTree.cpp
    NascentChunk.cpp
    OperationContext.cpp
    OSFileSystem.cpp
    PreprocessorIncludeHandler.cpp
    RawFileAsset.cpp
    XPak.cpp
    Legacy/AssetsLegacy.cpp
    ../Foreign/FastLZ/fastlz.c)

add_library(Assets STATIC ${Src})
xle_configure_library(Assets)
target_link_libraries(Assets PUBLIC ConsoleRig Formatters OSServices Utility ForeignMisc)
if (XLE_FILE_SYSTEM_MONITORING_ENABLE)
    target_compile_definitions(Assets PRIVATE -DXLE_FILE_SYSTEM_MONITORING_ENABLE=1)
endif()
if (XLE_ATTACHABLE_LIBRARIES_ENABLE)
    target_compile_definitions(Assets PRIVATE -DXLE_ATTACHABLE_LIBRARIES_ENABLE=1)
endif()
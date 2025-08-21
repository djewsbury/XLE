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
    ChunkFileWriter.cpp
    ArtifactCollectionFuture.cpp
    ConfigFileContainer.cpp
    DepVal.cpp
    IntermediatesStore.cpp
    IntermediateCompilers.cpp
    MainFileSystem.cpp
    MemoryFile.cpp
    MountingTree.cpp
    NascentChunk.cpp
    OSFileSystem.cpp
    RawFileAsset.cpp
    LooseFilesCache.cpp
    PreprocessorIncludeHandler.cpp
    OperationContext.cpp
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
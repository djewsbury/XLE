
option(XLE_GET_MODULE_FILE_TIME_ENABLE "Enables OS calls that query the filetime of the current module" ON)
option(XLE_GET_MODULE_PATH_ENABLE "Enables use of GetModuleFileNameA to get the module path name" ON) 

set(Src
    Socket.cpp
    RawFS.cpp
    Log.cpp
    LegacyFileStreams.cpp
    InputSnapshot.cpp
    )

if (WIN32)
    set(WinAPISrc WinAPI/System_WinAPI.cpp WinAPI/FileSystemMonitor_WinAPI.cpp WinAPI/RawFS_WinAPI.cpp WinAPI/PollingThread_WinAPI.cpp WinAPI/AttachableLibrary_WinAPI.cpp WinAPI/DisplaySettings_WinAPI.cpp WinAPI/InputTranslator.cpp WinAPI/OverlappedWindow.cpp WinAPI/RunLoop_WinAPI.cpp)
elseif (NOT APPLE)
    set(LinuxSrc Linux/System_Linux.cpp Linux/FileSystemMonitor_Linux.cpp Linux/PollingThread_epoll.cpp RawFS_Posix.cpp RawFS_stdio.cpp Posix/AttachableLibrary_Posix.cpp)
elseif (APPLE)
    set(AppleSrc Apple/System_Apple.cpp Apple/PollingThread_kqueue.cpp RawFS_Posix.cpp RawFS_stdio.cpp)
endif()

add_library(OSServices STATIC ${Src} ${WinAPISrc} ${LinuxSrc} ${AppleSrc})
xle_configure_library(OSServices)
target_link_libraries(OSServices PRIVATE ForeignMisc Utility)

if (WIN32)
    target_link_libraries(OSServices PRIVATE avrt.lib winmm.lib)
elseif (NOT APPLE)
    target_link_libraries(OSServices PRIVATE pthread)
endif ()

if (XLE_ATTACHABLE_LIBRARIES_ENABLE)
    target_compile_definitions(OSServices PRIVATE -DXLE_ATTACHABLE_LIBRARIES_ENABLE=1)
endif()
if (XLE_FILE_SYSTEM_MONITORING_ENABLE)
    target_compile_definitions(OSServices PRIVATE -DXLE_FILE_SYSTEM_MONITORING_ENABLE=1)
endif()
if (XLE_GET_MODULE_FILE_TIME_ENABLE)
    target_link_libraries(OSServices PRIVATE imagehlp.lib)
    target_compile_definitions(OSServices PRIVATE -DXLE_GET_MODULE_FILE_TIME_ENABLE=1)
endif()
if (XLE_GET_MODULE_PATH_ENABLE)
    target_link_libraries(OSServices PRIVATE imagehlp.lib)
    target_compile_definitions(OSServices PRIVATE -DXLE_GET_MODULE_PATH_ENABLE=1)
endif()
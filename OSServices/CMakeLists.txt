
set(Src
    Socket.cpp
    RawFS.cpp
    Log.cpp
    LegacyFileStreams.cpp
    )
set(Headers
    RawFS.h
    TimeUtils.h
    )

if (WIN32)
    set(WinAPISrc WinAPI/System_WinAPI.cpp WinAPI/FileSystemMonitor_WinAPI.cpp WinAPI/RawFS_WinAPI.cpp WinAPI/PollingThread_WinAPI.cpp WinAPI/AttachableLibrary_WinAPI.cpp)
elseif (NOT APPLE)
    set(LinuxSrc Linux/System_Linux.cpp Linux/FileSystemMonitor_Linux.cpp Linux/PollingThread_epoll.cpp RawFS_Posix.cpp RawFS_stdio.cpp Posix/AttachableLibrary_Posix.cpp)
elseif (APPLE)
    set(AppleSrc Apple/System_Apple.cpp Apple/PollingThread_kqueue.cpp RawFS_Posix.cpp RawFS_stdio.cpp)
endif()

add_library(OSServices STATIC ${Src} ${Headers} ${WinAPISrc} ${LinuxSrc} ${AppleSrc})
xle_configure_library(OSServices)
target_link_libraries(OSServices PRIVATE ForeignMisc Utility)

if (WIN32)
    target_link_libraries(OSServices PRIVATE wsock32.lib ws2_32.lib imagehlp.lib)
elseif (NOT APPLE)
    target_link_libraries(OSServices PRIVATE pthread)    
endif ()

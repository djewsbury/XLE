
set(Src
    Console.cpp
    DebugUtil.cpp
    GlobalServices.cpp
    OutputStream.cpp
    Plugins.cpp
    AttachablePtr.cpp
    ${CMAKE_BINARY_DIR}/ConsoleRig/Version.cpp)

add_library(ConsoleRig STATIC ${Src})
xle_configure_library(ConsoleRig)

target_link_libraries(ConsoleRig PUBLIC OSServices Utility ForeignMisc)
target_link_libraries(ConsoleRig PRIVATE Assets)    # todo -- this creates a dependency cycle; find a better way!

# Posix platforms should link in "dl", which is used for interacting with shared libraries
if (NOT WIN32)
  target_link_libraries(ConsoleRig PRIVATE dl)
endif ()

##################################################################################################################################

find_package(Git)
if(NOT Git_FOUND)
  message("Did not find git for version number checks. Build commit and tag information won't be correctly assigned")
endif()

# the commit's SHA1, and whether the building workspace was dirty or not
execute_process(
  COMMAND "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh --always --abbrev=40 --dirty
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_VARIABLE GIT_SHA1
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  
# Get the current working branch
execute_process(
  COMMAND "${GIT_EXECUTABLE}" rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Get the latest abbreviated commit hash of the working branch
execute_process(
  COMMAND "${GIT_EXECUTABLE}" log -1 --format=%h
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND "${GIT_EXECUTABLE}" describe
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
  OUTPUT_VARIABLE GIT_TAG
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M")

configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/Version.in.cpp
  ${CMAKE_BINARY_DIR}/ConsoleRig/Version.cpp
)

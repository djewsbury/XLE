add_subdirectory(freetype freetype)
add_subdirectory(Catch2 catch2)
add_subdirectory(wildcards wildcards)

if (WIN32)
    add_subdirectory(DirectXTex DirectXTex)
    add_subdirectory(HLSLCC/mk HLSLCC)
endif ()

#############################################################################################

if(NOT CLIENT_HAS_LUA)
    file(GLOB LuaSrc "Lua/src/*.c" "Lua/src/*.h")
endif()
file(GLOB HashSrc "Hash/*.cpp" "Hash/*.h")
file(GLOB AntlrSrc "Antlr-3.4/libantlr3c-3.4/src/*.c")
file(GLOB FmtSrc "fmt/*.cc")
if (WIN32)
	file(GLOB StackWalkerSrc "StackWalker/*.cpp")
endif()
file(GLOB PlustacheSrc "plustache/*.cpp")
if (NOT APPLE)
    file(GLOB ZLibSrc "zlib/*.c" "zlib/*.h")
endif ()
set(StamSimpleFluidSrc StamSimpleFluid/solver.c)
set(CParseSource cparse/containers.cpp cparse/functions.cpp cparse/packToken.cpp cparse/shunting-yard.cpp)
set(CUTFSource cutf/cutf.cpp)

add_library(ForeignMisc STATIC ${LuaSrc} ${HashSrc} ${FmtSrc} ${StackWalkerSrc} ${PlustacheSrc} ${StamSimpleFluidSrc} ${ZLibSrc} ${AntlrSrc} ${CParseSource} ${CUTFSource})
xle_configure_library(ForeignMisc)

if (APPLE)
    target_link_libraries(ForeignMisc PUBLIC z)
endif ()

# Ensure we export the necessary include folders from this target
# Most other targets list this target as a dependency, so they should inherit these include folders
target_include_directories(ForeignMisc PUBLIC Antlr-3.4/libantlr3c-3.4 Antlr-3.4/libantlr3c-3.4/include)
target_include_directories(ForeignMisc PUBLIC ${CMAKE_CURRENT_LIST_DIR})        # required to be public because of the way plustache includes it's headers
target_include_directories(ForeignMisc INTERFACE half-1.9.2/include eigen)
target_include_directories(ForeignMisc INTERFACE cml-2_1_2)
target_include_directories(ForeignMisc INTERFACE LuaBridge/Source/LuaBridge Lua/src)
target_include_directories(ForeignMisc INTERFACE json/include)
target_include_directories(ForeignMisc INTERFACE future/include)
target_include_directories(ForeignMisc INTERFACE thousandeyes-futures/include)
target_include_directories(ForeignMisc INTERFACE wildcards/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ForeignMisc PRIVATE -Wno-parentheses-equality -Wno-incompatible-pointer-types-discards-qualifiers)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    target_compile_options(ForeignMisc PRIVATE /wd4100 /wd4244 /wd4996 /wd4706 /wd4127 /wd4267 /wd4018 /wd4702 /wd4324 /wd4310 /wd4701 /wd4703 /wd4099 /wd4068 /wd4101 /wd4180)
endif()

#############################################################################################

file(GLOB_RECURSE yogacore_SRC "yoga/yoga/*.cpp")
add_library(yogacore STATIC ${yogacore_SRC})

target_compile_options(
    yogacore
    PRIVATE
    -fno-omit-frame-pointer
    -fexceptions
    -fvisibility=hidden
    -ffunction-sections
    -fdata-sections
    -Wall
    -std=c++11)

target_include_directories(yogacore PUBLIC "yoga/")
set_target_properties(yogacore PROPERTIES CXX_STANDARD 11)

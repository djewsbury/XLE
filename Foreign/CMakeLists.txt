if (XLE_FREETYPE_ENABLE)
    add_subdirectory(freetype freetype)
    xle_internal_configure_compiler(freetype)
endif()

add_subdirectory(Catch2 catch2)
add_subdirectory(wildcards wildcards)

if (WIN32)

    add_library(
        DirectXTexMinimal STATIC
        DirectXTex/DirectXTex/DirectXTexDDS.cpp
        DirectXTex/DirectXTex/DirectXTexImage.cpp
        DirectXTex/DirectXTex/DirectXTexMipmaps.cpp
        DirectXTex/DirectXTex/DirectXTexConvert.cpp
        DirectXTex/DirectXTex/DirectXTexUtil.cpp
        DirectXTex/DirectXTex/DirectXTexTGA.cpp
        DirectXTex/DirectXTex/DirectXTexWIC.cpp
        DirectXTexStubs.cpp)
    xle_configure_library(DirectXTexMinimal)

endif ()

#############################################################################################

if(NOT CLIENT_HAS_LUA)
    set(LuaSrc 
        Lua/src/lapi.c
        Lua/src/lauxlib.c
        Lua/src/lbaselib.c
        Lua/src/lbitlib.c
        Lua/src/lcode.c
        # Lua/src/lcorolib.c
        Lua/src/lctype.c
        # Lua/src/ldblib.c
        Lua/src/ldebug.c
        Lua/src/ldo.c
        Lua/src/ldump.c
        Lua/src/lfunc.c
        Lua/src/lgc.c
        Lua/src/linit.c
        # Lua/src/liolib.c
        Lua/src/llex.c
        Lua/src/lmathlib.c
        Lua/src/lmem.c
        # Lua/src/loadlib.c
        Lua/src/lobject.c
        Lua/src/lopcodes.c
        # Lua/src/loslib.c
        Lua/src/lparser.c
        Lua/src/lstate.c
        Lua/src/lstring.c
        Lua/src/lstrlib.c
        Lua/src/ltable.c
        Lua/src/ltablib.c
        Lua/src/ltm.c
        Lua/src/lundump.c
        Lua/src/lutf8lib.c
        Lua/src/lvm.c
        Lua/src/lzio.c
        Lua/lstub.c
        )
endif()
file(GLOB HashSrc Hash/MurmurHash2.cpp Hash/MurmurHash3.cpp)
if (XLE_ANTLR_ENABLE)
    file(GLOB AntlrSrc "Antlr-3.4/libantlr3c-3.4/src/*.c")
endif()
# file(GLOB FmtSrc "fmt/*.cc")
# if (WIN32)
# 	file(GLOB StackWalkerSrc "StackWalker/*.cpp")
# endif()
# file(GLOB PlustacheSrc "plustache/*.cpp")
# set(StamSimpleFluidSrc StamSimpleFluid/solver.c)
set(CParseSource cparse/containers.cpp cparse/functions.cpp cparse/packToken.cpp cparse/shunting-yard.cpp)
set(CUTFSource cutf/cutf.cpp)

add_library(ForeignMisc STATIC ${LuaSrc} ${HashSrc} ${FmtSrc} ${StackWalkerSrc} ${PlustacheSrc} ${StamSimpleFluidSrc} ${ZLibSrc} ${AntlrSrc} ${CParseSource} ${CUTFSource})
xle_configure_library(ForeignMisc)

if (MSVC)
    target_compile_options(ForeignMisc PRIVATE /FI ${CMAKE_CURRENT_LIST_DIR}/ForeignPrefix.h)
else()
    target_compile_options(ForeignMisc PRIVATE -include "ForeignPrefix.h")
endif()

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
target_include_directories(ForeignMisc INTERFACE ctti/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ForeignMisc PRIVATE -Wno-parentheses-equality -Wno-incompatible-pointer-types-discards-qualifiers)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    target_compile_options(ForeignMisc PRIVATE /wd4100 /wd4244 /wd4996 /wd4706 /wd4127 /wd4267 /wd4018 /wd4702 /wd4324 /wd4310 /wd4701 /wd4703 /wd4099 /wd4068 /wd4101 /wd4180)
endif()

#############################################################################################

file(GLOB_RECURSE yogacore_SRC "yoga/yoga/*.cpp")
add_library(yogacore STATIC ${yogacore_SRC})

target_include_directories(yogacore PUBLIC "yoga/")
target_include_directories(yogacore SYSTEM PUBLIC "yoga/")
xle_internal_configure_compiler(yogacore)


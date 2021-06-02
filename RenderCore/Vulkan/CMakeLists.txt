
set(Src
	Annotator.cpp
	DeviceVulkan.cpp
	Metal/AsyncTracker.cpp
	Metal/CmdListAttachedStorage.cpp
	Metal/CommandList.cpp
	Metal/CompiledShaderByteCode.cpp
	Metal/DescriptorSet.cpp
	Metal/DeviceContext.cpp
	Metal/ExtensionFunctions.cpp
	Metal/Format.cpp
	Metal/FrameBuffer.cpp
	Metal/InputLayout.cpp
	Metal/NumericInterface.cpp
	Metal/ObjectFactory.cpp
	Metal/PipelineBuilder.cpp
	Metal/PipelineLayout.cpp
	Metal/Pools.cpp
	Metal/QueryPool.cpp
	Metal/Resource.cpp
	Metal/Shader.cpp
	Metal/ShaderReflection.cpp
	Metal/State.cpp
	Metal/TextureView.cpp
	../DX11/Metal/CompiledShaderByteCode.cpp
	)

add_library(RenderCore_Vulkan STATIC ${Src} ${Headers})
xle_configure_library(RenderCore_Vulkan)
target_link_libraries(RenderCore_Vulkan PRIVATE ForeignMisc)
target_sources(RenderCore_Vulkan INTERFACE ${CMAKE_CURRENT_LIST_DIR}/RegisterVulkan.cpp)
target_link_libraries(RenderCore_Vulkan PRIVATE libHLSLcc)

############################################################################################################################################
set(XLE_VULKAN_SDK_DIR "" CACHE PATH "Vulkan SDK directory")

macro(find_vulkan)

	if (NOT INTERNAL_LAST_XLE_VULKAN_SDK_DIR STREQUAL XLE_VULKAN_SDK_DIR)
		message("Detected change in Vulkan configuration, searching again")
		set(INTERNAL_FOUND_VULKAN_INCLUDE_DIR)
		set(INTERNAL_FOUND_VULKAN_LIBRARY)
		set(INTERNAL_LAST_XLE_VULKAN_SDK_DIR ${XLE_VULKAN_SDK_DIR} CACHE STRING INTERNAL FORCE)
	endif ()

	# attempt to use "find_package(Vulkan)" to locate Vulkan
	if (NOT CMAKE_VERSION VERSION_LESS 3.7.0)
		find_package(Vulkan)
	endif()

	if (Vulkan_FOUND)
		set(INTERNAL_FOUND_VULKAN_INCLUDE_DIR ${Vulkan_INCLUDE_DIRS})
		set(INTERNAL_FOUND_VULKAN_LIBRARY ${Vulkan_LIBRARY})
		message("Found Vulkan via cmake module: ${Vulkan_INCLUDE_DIRS}, ${Vulkan_LIBRARY}")
	else ()
		find_path(INTERNAL_FOUND_VULKAN_INCLUDE_DIR vulkan/vulkan.h HINT "${XLE_VULKAN_SDK_DIR}/Include")
		if (CMAKE_SIZEOF_VOID_P EQUAL 8)
			find_library(INTERNAL_FOUND_VULKAN_LIBRARY vulkan-1 HINTS "${XLE_VULKAN_SDK_DIR}/Lib")
		elseif ()
			find_library(INTERNAL_FOUND_VULKAN_LIBRARY vulkan-1 HINTS "${XLE_VULKAN_SDK_DIR}/Lib32")
		endif ()
	endif ()

	if (INTERNAL_FOUND_VULKAN_INCLUDE_DIR STREQUAL INTERNAL_FOUND_VULKAN_INCLUDE_DIR-NOTFOUND 
		OR INTERNAL_FOUND_VULKAN_LIBRARY STREQUAL INTERNAL_FOUND_VULKAN_LIBRARY-NOTFOUND)

		# Most of the time, the remedy will be to just set XLE_VULKAN_SDK_DIR in the cmake cache
		# You'll usually grab that from https://www.lunarg.com/vulkan-sdk/
		message("INTERNAL_FOUND_VULKAN_INCLUDE_DIR: ${INTERNAL_FOUND_VULKAN_INCLUDE_DIR}")
		message("INTERNAL_FOUND_VULKAN_LIBRARY: ${INTERNAL_FOUND_VULKAN_LIBRARY}")
		message("XLE_VULKAN_SDK_DIR: ${XLE_VULKAN_SDK_DIR}")
		message(FATAL_ERROR "Could not find Vulkan headers and libraries. Either compile without this target included, or set the XLE_VULKAN_SDK_DIR cmake cache variables to the correct directories.")
	endif ()

	mark_as_advanced(INTERNAL_FOUND_VULKAN_INCLUDE_DIR)
	mark_as_advanced(INTERNAL_FOUND_VULKAN_LIBRARY)
	mark_as_advanced(INTERNAL_LAST_XLE_VULKAN_SDK_DIR)

endmacro ()
############################################################################################################################################

find_vulkan()
target_link_libraries(RenderCore_Vulkan PUBLIC ${INTERNAL_FOUND_VULKAN_LIBRARY})
target_include_directories(RenderCore_Vulkan PUBLIC ${INTERNAL_FOUND_VULKAN_INCLUDE_DIR})

message("todo -- nicer way to find vulkan files!")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_link_libraries(
		RenderCore_Vulkan PUBLIC 
			${Vulkan_INCLUDE_DIRS}/../Lib/glslangd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/GenericCodeGend.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/MachineIndependentd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/OGLCompilerd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/OSDependentd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRVd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Toolsd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-optd.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-reduced.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-sharedd.lib
		)
else ()
	target_link_libraries(
		RenderCore_Vulkan PUBLIC 
			${Vulkan_INCLUDE_DIRS}/../Lib/glslang.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/GenericCodeGen.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/MachineIndependent.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/OGLCompiler.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/OSDependent.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-opt.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-reduce.lib
			${Vulkan_INCLUDE_DIRS}/../Lib/SPIRV-Tools-shared.lib
		)
endif ()

target_link_libraries(RenderCore_Vulkan PUBLIC dxguid.lib)

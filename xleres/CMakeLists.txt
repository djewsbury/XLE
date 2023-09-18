cmake_minimum_required (VERSION 3.3)

add_custom_command(
	OUTPUT ${XLE_BINARY_OUTPUT_DIR_${CMAKE_BUILD_TYPE}}/xleres.pak
	COMMAND Archiver "i=${CMAKE_CURRENT_SOURCE_DIR}/core;o=${XLE_BINARY_OUTPUT_DIR_${CMAKE_BUILD_TYPE}}/xleres.pak"
	COMMENT "Building xleres.pak"
	VERBATIM
	)

add_custom_target(
	xleres
	ALL
	DEPENDS ${XLE_BINARY_OUTPUT_DIR_${CMAKE_BUILD_TYPE}}/xleres.pak
	)
xle_configure_binary_output(xleres)

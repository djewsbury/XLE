
add_executable(
    Archiver
    Archiver.cpp)

xle_configure_executable(Archiver)
xle_configure_binary_output(Archiver)
target_link_libraries(Archiver PRIVATE Utility OSServices Assets Formatters)

file(GLOB Src "*.cpp")
file(GLOB Headers "*.h")

add_executable(ShaderScan ${Src} ${Headers})
xle_configure_executable(ShaderScan)

add_dependencies(ShaderScan Utility Math Assets ConsoleRig RenderCore ShaderParser ForeignMisc)
target_link_libraries(ShaderScan PRIVATE Utility Math Assets ConsoleRig RenderCore ShaderParser ForeignMisc)

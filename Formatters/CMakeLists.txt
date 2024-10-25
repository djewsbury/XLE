add_library(
	Formatters
	STATIC
	BinaryFormatter.cpp
	BinarySchemata.cpp
	StreamDOM.cpp
	TextFormatter.cpp
	XmlFormatter.cpp
	CommandLineFormatter.cpp)
xle_configure_library(Formatters)

target_link_libraries(Formatters PRIVATE Utility ForeignMisc)

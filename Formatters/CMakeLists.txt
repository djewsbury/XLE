add_library(
	Formatters
	STATIC
	BinaryFormatter.cpp
	BinarySchemata.cpp
	StreamDOM.cpp
	TextFormatter.cpp
	XmlFormatter.cpp)
xle_configure_library(Math)

target_link_libraries(Formatters PRIVATE Utility ForeignMisc)

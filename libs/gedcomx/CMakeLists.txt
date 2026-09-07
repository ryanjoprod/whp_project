find_package(nlohmann_json CONFIG REQUIRED)

add_library(gedcomx STATIC
	src/gedcomx.cpp
	src/parser.cpp
)

target_include_directories(gedcomx PUBLIC include)
target_link_libraries(gedcomx PUBLIC nlohmann_json::nlohmann_json)
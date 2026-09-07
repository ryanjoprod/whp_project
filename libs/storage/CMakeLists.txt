find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(storage STATIC
	src/database.cpp
)

target_include_directories(storage PUBLIC include)

target_link_libraries(storage
	PUBLIC gedcomx
	PRIVATE unofficial::sqlite3::sqlite3
)
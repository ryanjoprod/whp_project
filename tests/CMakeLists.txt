find_package(doctest CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(whp_tests
	test_main.cpp
	gedcomx/test_person_parser.cpp
	gedcomx/test_fact_serializer.cpp
	gedcomx/test_person_serializer.cpp
	gedcomx/test_relationship_serializer.cpp
	gedcomx/test_source_description_serializer.cpp
	gedcomx/test_source_export.cpp
	gedcomx/test_parent_child.cpp
	gedcomx/test_event_serializer.cpp
	storage/test_person_storage.cpp
	storage/test_aggregate_storage.cpp
	storage/test_relationship_storage.cpp
	storage/test_places_storage.cpp
	storage/test_citation_storage.cpp
	storage/test_event_storage.cpp
	storage/test_references_storage.cpp
	storage/test_document_storage.cpp
	storage/test_schema_version.cpp
	storage/test_migrations.cpp
)

target_include_directories(whp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(whp_tests PRIVATE gedcomx storage doctest::doctest unofficial::sqlite3::sqlite3)
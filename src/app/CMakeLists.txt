find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(app WIN32 
	main.cpp
	app.cpp
	app_paths.cpp
	camera.cpp
	project.cpp
	logger.cpp
	window.cpp
	window_titlebar.cpp
	window_canvas.cpp
	window_dialogs.cpp
	window_person_editor.cpp
	window_relationship_editor.cpp
	window_sources.cpp
	app_settings.cpp
	id_generator.cpp
	window_citations.cpp
	window_date_editor.cpp
	window_places.cpp
	window_events.cpp
	window_state.cpp
)

target_link_libraries(app PRIVATE storage SDL3::SDL3-shared imgui::imgui)
target_compile_definitions(app PRIVATE WHP_VERSION="${PROJECT_VERSION}")

if (MSVC)
	target_link_options(app PRIVATE /ENTRY:mainCRTStartup)
endif()

install(TARGETS app RUNTIME DESTINATION .)

if (WIN32)
	# The DLLs the app needs next to it (SDL3.dll, sqlite3.dll)
	install(FILES $<TARGET_RUNTIME_DLLS:app> DESTINATION .)
endif()
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(app 
	main.cpp
	app.cpp
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
)

target_link_libraries(app PRIVATE storage SDL3::SDL3-shared imgui::imgui)
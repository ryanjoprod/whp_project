find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(app 
	main.cpp
	app.cpp
	window.cpp
	window_titlebar.cpp
	window_canvas.cpp
	window_dialogs.cpp
	window_person_editor.cpp
	window_relationship_editor.cpp
	camera.cpp
	project.cpp
	logger.cpp
	app_settings.cpp
	id_generator.cpp
)

target_link_libraries(app PRIVATE storage SDL3::SDL3-shared imgui::imgui)
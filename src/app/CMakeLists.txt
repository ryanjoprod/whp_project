find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(app main.cpp app.cpp window.cpp camera.cpp project.cpp)

target_link_libraries(app PRIVATE storage SDL3::SDL3-shared imgui::imgui)
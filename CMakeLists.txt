cmake_minimum_required(VERSION 3.28)
project(whp_program VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
	add_compile_options(/we4715 /we4716)  # "not all control paths return a value" / "must return a value"
else()
	add_compile_options(-Werror=return-type)
endif()

enable_testing()
add_subdirectory(tests)

add_subdirectory(libs/gedcomx)
add_subdirectory(libs/storage)
add_subdirectory(src/app)

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "WHP-Program")
set(CPACK_PACKAGE_FILE_NAME "WHP-Program-${PROJECT_VERSION}-win64")
include(CPack)
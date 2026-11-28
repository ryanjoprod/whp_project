cmake_minimum_required(VERSION 3.28)
project(whp_program CXX)

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
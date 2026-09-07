cmake_minimum_required(VERSION 3.28)
project(whp_program CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/gedcomx)
add_subdirectory(libs/storage)
add_subdirectory(src/app)
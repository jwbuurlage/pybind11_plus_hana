cmake_minimum_required(VERSION 3.0.0)

project(hanapy)

find_package(Boost 1.61 REQUIRED)
add_subdirectory("ext/pybind11")

set(HANAPY_FLAGS "-Wall" "-Wfatal-errors" "-Wextra" "-g" "-O3" "-std=c++14")
set(LIB_NAMES "Boost::boost")

pybind11_add_module(hanapy hanapy.cpp)
target_link_libraries(hanapy PRIVATE ${LIB_NAMES})
target_compile_options(hanapy PRIVATE ${HANAPY_FLAGS})

cmake_minimum_required(VERSION 3.16)
project(mkico LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Gui)

add_executable(mkico main.cpp)
target_link_libraries(mkico PRIVATE Qt6::Gui)

# Default the tool to this repo so `mkico` with no arguments just works; an
# explicit path argument still overrides it.
target_compile_definitions(mkico PRIVATE
    SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../..")

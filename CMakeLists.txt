cmake_minimum_required(VERSION 3.10)
project(Tinywebserver LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test)

file(GLOB_RECURSE TINYWEBSERVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

list(FILTER TINYWEBSERVER_SOURCES EXCLUDE REGEX "/build/")

if(TINYWEBSERVER_SOURCES STREQUAL "")
    message(FATAL_ERROR "No source files found under ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_executable(main ${TINYWEBSERVER_SOURCES})
set_target_properties(main PROPERTIES OUTPUT_NAME Tinywebserver)

target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/Pool
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
)

target_link_libraries(main PRIVATE
    pthread
    mysqlclient
    argon2
)

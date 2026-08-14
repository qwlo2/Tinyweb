# cmake_minimum_required(VERSION 3.10)
# project(Tinywebserver)


# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# SET(CMAKE_CXX_FLAGS "-g -Wall -Werror -std=c++20")
# set(CMAKE_CXX_COMPILER "g++")


# SET(CMAKE_CXX_FLAGS "-O2 -Wall -std=c++14")
# set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/test/)
# aux_source_directory(src SRC_LIST1)

# add_executable(Tinywebserver  ${SRC_LIST1} )
# target_include_directories(Tinywebserver PRIVATE  
#       ${PROJECT_SOURCE_DIR}/buffer 
#       ${PROJECT_SOURCE_DIR}/log
#       ${PROJECT_SOURCE_DIR}/Pool 
#       ${PROJECT_SOURCE_DIR}/server
#       ${PROJECT_SOURCE_DIR}/http
#        ${PROJECT_SOURCE_DIR}/timer
#       )
#       target_link_libraries(Tinywebserver PRIVATE
#     pthread  # 线程库
#     mysqlclient  # MySQL客户端库，解决mysql_*函数未定义
#     argon2  # Argon2id密码哈希
# )
cmake_minimum_required(VERSION 3.10)
project(Tinywebserver)

find_package(OpenSSL REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB SRC_LIST
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

message(STATUS "CMAKE_CURRENT_SOURCE_DIR = ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "SRC_LIST = ${SRC_LIST}")

if(NOT SRC_LIST)
    message(FATAL_ERROR "No source files found in ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_executable(Tinywebserver ${SRC_LIST}
    src/Auth.cpp
    http/upload.h
    src/upload.cpp
    server/session.h
    src/session.cpp
    http/download.h)

target_include_directories(Tinywebserver PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer
    ${CMAKE_CURRENT_SOURCE_DIR}/log
    ${CMAKE_CURRENT_SOURCE_DIR}/Pool
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
)

target_link_libraries(Tinywebserver PRIVATE
    pthread
    mysqlclient
    argon2
    hiredis
    OpenSSL::Crypto
)
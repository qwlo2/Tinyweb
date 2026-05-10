cmake_minimum_required(VERSION 3.10)
project(Tinywebserver)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
SET(CMAKE_CXX_FLAGS "-g -Wall -Werror -std=c++14")
set(CMAKE_CXX_COMPILER "g++")

#set(CMAKE_CXX_FLAGS_DEBUG "-O0")
SET(CMAKE_CXX_FLAGS "-O2 -Wall -std=c++14")
set (EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/test/)
aux_source_directory(src SRC_LIST1)

add_executable(Tinywebserver  ${SRC_LIST1} )
target_include_directories(Tinywebserver PRIVATE  
      ${PROJECT_SOURCE_DIR}/buffer 
      ${PROJECT_SOURCE_DIR}/log
      ${PROJECT_SOURCE_DIR}/Pool 
      ${PROJECT_SOURCE_DIR}/server
      ${PROJECT_SOURCE_DIR}/http
       ${PROJECT_SOURCE_DIR}/timer
      )
      target_link_libraries(Tinywebserver PRIVATE
    pthread  # 线程库
    mysqlclient  # MySQL客户端库，解决mysql_*函数未定义
)
cmake_minimum_required(VERSION 3.2)
project(webserver)
set(CMAKE_BUILD_TYPE DEBUG_CONFIGURATIONS)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})
FIND_PACKAGE(Threads)
include_directories(
    ${PROJECT_SOURCE_DIR}/CGImysql
    ${PROJECT_SOURCE_DIR}/http
    ${PROJECT_SOURCE_DIR}/lock
    ${PROJECT_SOURCE_DIR}/log
    ${PROJECT_SOURCE_DIR}/threadpool
    ${PROJECT_SOURCE_DIR}/timer
    ${PROJECT_SOURCE_DIR}/config
    ${PROJECT_SOURCE_DIR}/webfile
) 
add_executable(webserver main.cpp webfile/webserver.cpp config/config.cpp CGImysql/sql_connection_pool.cpp 
http/http_conn.cpp log/log.cpp timer/lst_timer.cpp)
link_directories(/usr/lib/x86_64-linux-gnu)
target_link_libraries(webserver ${CMAKE_THREAD_LIBS_INIT}  libmysqlclient.so)


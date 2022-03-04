cmake_minimum_required (VERSION 2.8)

project (server)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

find_package(Threads)
set(CMAKE_CXX_STANDARD 14)

aux_source_directory (src SRC_LIST)

include_directories (include)

add_executable (main ${SRC_LIST})
target_link_libraries(main ${CMAKE_THREAD_LIBS_INIT})




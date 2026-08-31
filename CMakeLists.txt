cmake_minimum_required(VERSION 3.13)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()

project(http-server-starter-cpp)
set(CMAKE_CXX_STANDARD 20) # Enable the C++20 standard

set(SOURCE_FILES src/server.cpp src/request_handler.cpp src/event_loop.cpp src/connection.cpp)

find_package(ZLIB REQUIRED)

add_executable(server ${SOURCE_FILES})

# Include zlib headers
target_include_directories(server PRIVATE ${ZLIB_INCLUDE_DIRS})

# Link platform-specific libraries
if(WIN32)
    target_link_libraries(server ws2_32 ZLIB::ZLIB)
else()
    target_link_libraries(server pthread ZLIB::ZLIB)
endif()


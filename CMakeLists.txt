cmake_minimum_required(VERSION 3.20)
project(TapLan)
include(ExternalProject)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

include_directories(inc)
set(THIRD_INSTALL_DIR "${CMAKE_BINARY_DIR}/third_party")
set(THIRD_INCLUDE_DIR "${THIRD_INSTALL_DIR}/include")
if(NOT EXISTS "${THIRD_INCLUDE_DIR}")
    file(MAKE_DIRECTORY "${THIRD_INCLUDE_DIR}")
endif()
include_directories(${THIRD_INCLUDE_DIR})

string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type_lower)
if(build_type_lower STREQUAL "debug")
    unset(CMAKE_EXE_LINKER_FLAGS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif()

# link libsodium
add_library(DataSec src/DataSec.cpp)
add_library(sodium UNKNOWN IMPORTED GLOBAL)
find_path(SODIUM_INCLUDE_DIR sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium libsodium)
if(NOT SODIUM_LIBRARY OR NOT SODIUM_INCLUDE_DIR)
    message(STATUS "libsodium not found")
    ExternalProject_Add(
        ext_libsodium
        GIT_REPOSITORY      https://github.com/jedisct1/libsodium
        GIT_TAG             stable

        PREFIX              ${CMAKE_BINARY_DIR}/ext_libsodium
        STAMP_DIR           ${CMAKE_BINARY_DIR}/ext_libsodium/stamp
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ext_libsodium/src
        BUILD_BYPRODUCTS    ${THIRD_INSTALL_DIR}/lib/libsodium.a

        BUILD_IN_SOURCE     1
        CONFIGURE_COMMAND   sh ./configure --prefix=${THIRD_INSTALL_DIR}
        BUILD_COMMAND       make -j
        INSTALL_COMMAND     make install
        UPDATE_COMMAND      ""
    )
    add_dependencies(sodium ext_libsodium)
    set_target_properties(sodium PROPERTIES
        IMPORTED_LOCATION               ${THIRD_INSTALL_DIR}/lib/libsodium.a
        INTERFACE_INCLUDE_DIRECTORIES   ${THIRD_INCLUDE_DIR}
    )
else()
    set_target_properties(sodium PROPERTIES
        IMPORTED_LOCATION               ${SODIUM_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES   ${SODIUM_INCLUDE_DIR}
    )
endif()
target_link_libraries(DataSec PUBLIC sodium)

if(WIN32)
    add_library(TapDev src/TapDevWin.cpp)
    add_library(UioIntf src/UioIntf.cpp src/UioIntfWin.cpp)

    add_library(BsdSock src/BsdSock.cpp src/BsdSockWin.cpp)
    target_link_libraries(BsdSock PUBLIC ws2_32)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_library(UioIntf src/UioIntf.cpp src/UioIntfLinux.cpp)
    add_library(uring UNKNOWN IMPORTED GLOBAL)
    find_path(URING_INCLUDE_DIR liburing.h)
    find_library(URING_LIBRARY NAMES uring liburing)
    if(NOT URING_LIBRARY OR NOT URING_INCLUDE_DIR)
        message(STATUS "liburing not found")
        ExternalProject_Add(
            ext_liburing
            GIT_REPOSITORY      https://github.com/axboe/liburing.git
            GIT_TAG             master

            PREFIX              ${CMAKE_BINARY_DIR}/ext_liburing
            STAMP_DIR           ${CMAKE_BINARY_DIR}/ext_liburing/stamp
            SOURCE_DIR          ${CMAKE_BINARY_DIR}/ext_liburing/src
            BUILD_BYPRODUCTS    ${THIRD_INSTALL_DIR}/lib/liburing.a

            BUILD_IN_SOURCE     1
            CONFIGURE_COMMAND   ./configure --prefix=${THIRD_INSTALL_DIR}
            BUILD_COMMAND       make -j
            INSTALL_COMMAND     make install
            UPDATE_COMMAND      ""
        )
        add_dependencies(uring ext_liburing)
        set_target_properties(uring PROPERTIES
            IMPORTED_LOCATION               ${THIRD_INSTALL_DIR}/lib/liburing.a
            INTERFACE_INCLUDE_DIRECTORIES   ${THIRD_INCLUDE_DIR}
        )
    else()
        set_target_properties(uring PROPERTIES
            IMPORTED_LOCATION               ${URING_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES   ${URING_INCLUDE_DIR}
        )
    endif()
    target_link_libraries(UioIntf PUBLIC uring)

    add_library(TapDev src/TapDevLinux.cpp)
    add_library(BsdSock src/BsdSock.cpp src/BsdSockLinux.cpp)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

add_library(NodeMgr src/NodeMgr.cpp)
add_library(LogMgr src/LogMgr.cpp)
add_library(Config src/Config.cpp)
add_library(Common src/Common.cpp)

add_executable(TapLan src/main.cpp src/TapLan.cpp)
target_link_libraries(TapLan NodeMgr TapDev BsdSock LogMgr UioIntf Config DataSec Common)

find_package(Threads REQUIRED)
target_link_libraries(TapLan Threads::Threads)

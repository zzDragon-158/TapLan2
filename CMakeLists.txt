cmake_minimum_required(VERSION 3.20)
project(TapLan)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")

include_directories(inc)

if(WIN32)
    add_library(TapDev src/TapDevWin.cpp)
    add_library(BsdSock src/BsdSock.cpp src/BsdSockWin.cpp)
    add_library(UioIntf src/UioIntf.cpp src/UioIntfWin.cpp)

    target_link_libraries(BsdSock PUBLIC ws2_32)

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(URING_INSTALL_DIR "${CMAKE_BINARY_DIR}/third_party")
    set(URING_INCLUDE_DIR "${URING_INSTALL_DIR}/include")
    if(NOT EXISTS "${URING_INCLUDE_DIR}")
        file(MAKE_DIRECTORY "${URING_INCLUDE_DIR}")
    endif()
    include(ExternalProject)
    ExternalProject_Add(
        ext_liburing
        GIT_REPOSITORY      https://github.com/axboe/liburing.git
        GIT_TAG             master

        PREFIX              ${CMAKE_BINARY_DIR}/ext_liburing
        STAMP_DIR           ${CMAKE_BINARY_DIR}/ext_liburing/stamp
        SOURCE_DIR          ${CMAKE_BINARY_DIR}/ext_liburing/src

        BUILD_IN_SOURCE     1
        CONFIGURE_COMMAND   ./configure --prefix=${URING_INSTALL_DIR}
        BUILD_COMMAND       make -j
        INSTALL_COMMAND     make install
        UPDATE_COMMAND      ""
    )
    add_library(uring STATIC IMPORTED GLOBAL)
    add_dependencies(uring ext_liburing)
    set_target_properties(uring PROPERTIES
        IMPORTED_LOCATION               ${URING_INSTALL_DIR}/lib/liburing.a
        INTERFACE_INCLUDE_DIRECTORIES   ${URING_INCLUDE_DIR}
    )

    add_library(TapDev src/TapDevLinux.cpp)
    add_library(BsdSock src/BsdSock.cpp src/BsdSockLinux.cpp)
    add_library(UioIntf src/UioIntf.cpp src/UioIntfLinux.cpp)

    target_link_libraries(UioIntf PUBLIC uring)

else()
    message(FATAL_ERROR "Unsupported platform")

endif()

add_library(NodeMgr src/NodeMgr.cpp)
add_library(LogMgr src/LogMgr.cpp)

add_executable(TapLan src/main.cpp src/TapLan.cpp)
target_link_libraries(TapLan NodeMgr TapDev BsdSock LogMgr UioIntf)

find_package(Threads REQUIRED)
target_link_libraries(TapLan Threads::Threads)

cmake_minimum_required(VERSION 3.20)
project(TapLan)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# use c++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXE_LINKER_FLAGS "-static")
include_directories(inc)
# PUBLIC: 自己用，别人也要用
# PRIVATE: 就自己用，别人不用
# INTERFACE: 给别人用，自己不用
add_library(NodeMgr src/NodeMgr.cpp)
add_library(LogMgr src/LogMgr.cpp)

if(WIN32)
    add_library(TapDev src/TapDevWin.cpp)
    add_library(BsdSock src/BsdSockWin.cpp)
    add_library(AioIntf src/AioIntfWin.cpp)
    add_library(SioIntf src/SioIntfWin.cpp)

    target_link_libraries(BsdSock PUBLIC ws2_32)

elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_library(TapDev src/TapDevLinux.cpp)
    add_library(BsdSock src/BsdSockLinux.cpp)
    add_library(AioIntf src/AioIntfLinux.cpp)
    add_library(SioIntf src/SioIntfLinux.cpp)

else()
    message(FATAL_ERROR "Unsupported platform")

endif()

add_executable(TapLan src/main.cpp src/TapLan.cpp)
target_link_libraries(TapLan NodeMgr TapDev BsdSock LogMgr AioIntf SioIntf)

find_package(Threads REQUIRED)
target_link_libraries(TapLan Threads::Threads)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(MY_URING_STATIC_PATH "/usr/local/lib/liburing.a")
    add_library(uring_static UNKNOWN IMPORTED)
    set_target_properties(uring_static PROPERTIES
        IMPORTED_LOCATION "${MY_URING_STATIC_PATH}"
        # 如果有头文件路径也可以在这里指定
        # INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    )
    target_link_libraries(TapLan uring_static)
endif()
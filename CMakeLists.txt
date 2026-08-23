cmake_minimum_required(VERSION 3.20)
project(cougarLCD.cpp VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

if(WIN32)
    add_subdirectory(tools/windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(src/linux)
else()
    message(FATAL_ERROR "Supported targets are Windows and Linux/WSL2")
endif()


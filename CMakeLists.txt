cmake_minimum_required(VERSION 3.16)
project(NanoDrawer CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nana QUIET)
if(NOT nana_FOUND)
    if(EXISTS "${CMAKE_SOURCE_DIR}/../nana/CMakeLists.txt")
        add_subdirectory(../nana nana_build)
        set(NANA_TARGET nana)
    else()
        message(FATAL_ERROR
            "Nana not found.\n"
            "  git clone https://github.com/cnjinhao/nana.git ../nana")
    endif()
else()
    set(NANA_TARGET nana::nana)
endif()

add_executable(nano_drawer nano_drawer.cpp)
target_link_libraries(nano_drawer PRIVATE ${NANA_TARGET})

if(UNIX AND NOT APPLE)
    target_link_libraries(nano_drawer PRIVATE X11 Xft fontconfig pthread)
endif()
if(WIN32)
    target_link_libraries(nano_drawer PRIVATE gdi32 comdlg32 imm32)
    set_target_properties(nano_drawer PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

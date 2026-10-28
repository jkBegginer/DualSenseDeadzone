cmake_minimum_required(VERSION 3.22.1)
project(dsdz C)

set(CMAKE_C_STANDARD 11)

add_library(dsdz SHARED
        jni.c
        bridge.c
        deadzone.c
        ds_report.c)

target_compile_options(dsdz PRIVATE -Wall -Wextra -O2)
target_link_libraries(dsdz log m)

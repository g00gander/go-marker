cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nav_display)

# Speed experiment (2026-09-29): build LVGL's software renderer at -O3 (the project default is -O2).
idf_component_get_property(nav_lvgl_lib lvgl__lvgl COMPONENT_LIB)
target_compile_options(${nav_lvgl_lib} PRIVATE -O3)

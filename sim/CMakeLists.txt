cmake_minimum_required(VERSION 3.16)
project(nav_sim C)

set(CMAKE_C_STANDARD 11)

# Panel size the UI is built for (412 = the Waveshare 1.46, 360 = the 1.85): cmake -DNAV_UI_SIZE=360 ..
set(NAV_UI_SIZE 412 CACHE STRING "UI panel size in pixels (412 or 360)")

# LVGL, configured by ./lv_conf.h
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
execute_process(COMMAND sdl2-config --prefix OUTPUT_VARIABLE SDL_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE)
include_directories(${SDL_PREFIX}/include)   # LVGL includes <SDL2/SDL.h>
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../lvgl ${CMAKE_BINARY_DIR}/lvgl)

# SDL2 (Homebrew's sdl2-compat provides sdl2-config)
execute_process(COMMAND sdl2-config --cflags OUTPUT_VARIABLE SDL_CFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND sdl2-config --libs OUTPUT_VARIABLE SDL_LIBS OUTPUT_STRIP_TRAILING_WHITESPACE)
separate_arguments(SDL_CFLAGS_LIST UNIX_COMMAND "${SDL_CFLAGS}")
separate_arguments(SDL_LIBS_LIST UNIX_COMMAND "${SDL_LIBS}")

if(NAV_UI_SIZE EQUAL 412)
    set(NAV_UI_SUFFIX "")
else()
    set(NAV_UI_SUFFIX "_${NAV_UI_SIZE}")
endif()

add_executable(nav_sim
    main.c
    feed.c
    ../ui/nav_ui.c
    ../ui/splash_logo${NAV_UI_SUFFIX}.c
    ../ui/nav_trip.c
    ../ui/nav_route_rx.c
    ../ui/nav_roads_rx.c
    ../ui/nav_areas_rx.c
    ../ui/nav_trip_store.c
    ../ui/nav_link.c
    ../ui/nav_battery.c
)
if(NAV_UI_SIZE EQUAL 360)
    target_sources(nav_sim PRIVATE
        ../ui/fonts/font_montserrat_bold_19.c ../ui/fonts/font_montserrat_bold_23.c
        ../ui/fonts/font_montserrat_bold_38.c ../ui/fonts/font_montserrat_bold_105.c ../ui/fonts/font_montserrat_bold_70.c)
else()
    target_sources(nav_sim PRIVATE
        ../ui/fonts/font_montserrat_bold_22.c ../ui/fonts/font_montserrat_bold_26.c
        ../ui/fonts/font_montserrat_bold_44.c ../ui/fonts/font_montserrat_bold_120.c ../ui/fonts/font_montserrat_bold_80.c)
endif()
target_include_directories(nav_sim PRIVATE ../ui ../proto .)
target_compile_definitions(nav_sim PRIVATE NAV_UI_ROUND_MASK NAV_UI_SIZE=${NAV_UI_SIZE})
target_compile_options(nav_sim PRIVATE ${SDL_CFLAGS_LIST} -Wall -Wextra)
target_compile_options(lvgl PRIVATE ${SDL_CFLAGS_LIST})
target_link_libraries(nav_sim PRIVATE lvgl ${SDL_LIBS_LIST} m)

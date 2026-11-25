cmake_minimum_required(VERSION 3.16)

project(retrogc-mister VERSION 0.1.0 LANGUAGES C CXX)

# OFF: the MiSTer binary (cross-compile with cmake/mister-armhf.cmake).
# ON: the desktop preview, an SDL2 window on the PC (same UI and card logic).
option(RETROGC_SDL "Build the desktop preview (SDL2 window)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ------------------------------------------------------------------ LVGL (pinned)
include(FetchContent)
set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.2.2
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)

if(RETROGC_SDL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_compile_definitions(lvgl PUBLIC RETROGC_SDL=1)
    target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)
endif()

# ------------------------------------------------------------------ fonts, built in
include(cmake/embed.cmake)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
embed_file(${GEN}/font_press_start_2p.c ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PressStart2P-Regular.ttf font_press_start_2p)
embed_file(${GEN}/font_vt323.c ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/VT323-Regular.ttf font_vt323)
embed_file(${GEN}/font_inter.c ${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf font_inter)

# ------------------------------------------------------------------ the launcher
add_executable(retrogc-mister
    src/main.cpp
    src/app.cpp
    src/card.cpp
    src/card_monitor.cpp
    src/image.cpp
    src/log.cpp
    src/mister.cpp
    src/settings.cpp
    src/ui.cpp
    ${GEN}/font_press_start_2p.c
    ${GEN}/font_vt323.c
    ${GEN}/font_inter.c)

target_include_directories(retrogc-mister PRIVATE src third_party)
# The release workflow passes -DRETROGC_VERSION=X.Y.Z
set(RETROGC_VERSION "${PROJECT_VERSION}" CACHE STRING "Version shown by the launcher")
target_compile_definitions(retrogc-mister PRIVATE RETROGC_VERSION="${RETROGC_VERSION}")
target_compile_options(retrogc-mister PRIVATE -Wall -Wextra)
target_link_libraries(retrogc-mister PRIVATE lvgl m)

if(CMAKE_CROSSCOMPILING)
    # One self-contained file to copy to the SD card
    target_link_options(retrogc-mister PRIVATE -static -s)
endif()

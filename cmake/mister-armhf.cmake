# MiSTer (DE10-Nano, Cortex-A9) cross toolchain: the same ARM GNU 10.2-2020.11 compiler that
# Main_MiSTer is built with, so the binary needs no newer glibc than the MiSTer image has.
#
#   cmake -B build-mister -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mister-armhf.cmake
#
# Override the location with -DMISTER_TOOLCHAIN_DIR=... or the MISTER_TOOLCHAIN_DIR variable.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT MISTER_TOOLCHAIN_DIR)
    if(DEFINED ENV{MISTER_TOOLCHAIN_DIR})
        set(MISTER_TOOLCHAIN_DIR $ENV{MISTER_TOOLCHAIN_DIR})
    else()
        set(MISTER_TOOLCHAIN_DIR /opt/mister/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf)
    endif()
endif()
set(MISTER_TOOLCHAIN_DIR ${MISTER_TOOLCHAIN_DIR} CACHE PATH "ARM GNU toolchain used for MiSTer")

set(TRIPLE arm-none-linux-gnueabihf)
set(CMAKE_C_COMPILER ${MISTER_TOOLCHAIN_DIR}/bin/${TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${MISTER_TOOLCHAIN_DIR}/bin/${TRIPLE}-g++)
set(CMAKE_STRIP ${MISTER_TOOLCHAIN_DIR}/bin/${TRIPLE}-strip)

# Cortex-A9 with NEON (same flags as Main_MiSTer)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cortex-a9 -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

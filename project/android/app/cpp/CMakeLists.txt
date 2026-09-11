cmake_minimum_required(VERSION 3.7)
project(krkr2yuri_android)

set(KRKR2YURI_DIR ${CMAKE_SOURCE_DIR}/../../../..)
# Upstream publishes its prebuilt dependency tree for aarch64 only, as
# thirdparty/build/arch_androida64. Both halves of this if/else pointed at that
# one directory, so building any other ABI would have linked aarch64 static
# archives into it and failed somewhere deep in the linker. Say what is missing
# instead: the dependency set has to be built for the ABI first (see
# script/_androida64.sh, which hardcodes aarch64 at every call site).
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(PORTBUILD_PATH ${KRKR2YURI_DIR}/thirdparty/build/arch_androida64)
else()
    message(FATAL_ERROR
        "No prebuilt dependency tree for ANDROID_ABI=${ANDROID_ABI}: upstream "
        "Kirikiroid2Yuri ships thirdparty/build/arch_androida64 (aarch64) only. "
        "Build the eighteen static dependencies for this ABI and add "
        "thirdparty/build/arch_<platform> before adding it to PROP_APP_ABI.")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
endif()

add_subdirectory(${KRKR2YURI_DIR} ${KRKR2YURI_DIR}/build_android)
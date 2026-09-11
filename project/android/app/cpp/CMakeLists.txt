cmake_minimum_required(VERSION 3.7)
project(krkr2yuri_android)

set(KRKR2YURI_DIR ${CMAKE_SOURCE_DIR}/../../../..)

# Upstream keeps its built dependencies in one directory per architecture under
# thirdparty/build, named the way script/cross_androida64.sh names its PLATFORM
# (and the way script/_fetch.sh pre-creates them). Resolve the directory from
# the ABI rather than hardcoding one, so an architecture whose tree has been
# built simply works without touching this file again. Upstream publishes
# arch_androida64 and nothing else today.
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(PORTBUILD_ARCH arch_androida64)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(PORTBUILD_ARCH arch_androida32)
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(PORTBUILD_ARCH arch_androidx64)
elseif(ANDROID_ABI STREQUAL "x86")
    set(PORTBUILD_ARCH arch_androidx32)
else()
    message(FATAL_ERROR "Unknown ANDROID_ABI=${ANDROID_ABI}")
endif()
set(PORTBUILD_PATH ${KRKR2YURI_DIR}/thirdparty/build/${PORTBUILD_ARCH})

# Without this the build would link whatever archives happened to be on the
# path -- previously both halves of an if/else pointed at the aarch64 tree, so a
# second ABI linked aarch64 static libraries and died deep in the linker with
# nothing readable to go on. Fail here instead, naming what is missing.
#
# Building that tree for x86_64 is not a matter of retargeting
# script/_androida64.sh. Seventeen of the eighteen dependencies would retarget
# mechanically (opencv needs its arm-only libtegra_hal.a dropped, see
# src/core/CMakeLists.txt:185, and ffmpeg's --arch=aarch64 --disable-asm
# restated). The eighteenth, cocos2d-x 3.17.2, does not build its own
# dependencies: external/<lib>/CMakeLists.txt in the cocos2d-x deps package
# imports each as a static library at prebuilt/android/${ANDROID_ABI}
# (cmake/CocosExternalConfig.cmake:33), and every tag of
# cocos2d-x-3rd-party-libs-bin on the v3-deps line -- from v3-deps-158, which
# cocos2d-x 3.17.2 pins in external/config.json, through the last one,
# v3-deps-172 -- carries armeabi-v7a, arm64-v8a and x86 only. Nine libraries
# this engine links come from there (z, png, webp, tiff, freetype, chipmunk,
# BulletCollision, BulletDynamics, LinearMath) and none of them exists for
# x86_64. Upstream's own builder for that set cannot produce them either:
# cocos2d-x-3rd-party-libs-src branch v3, build/android.ini, declares
# cfg_all_supported_arches=("armv7" "x86" "arm64" "mips") and defines no x86_64
# alias folder, host triple or API level. The route that remains is building
# those nine from their own upstream sources for android-x86_64 and staging
# them into cocos2d-x's prebuilt layout.
if(NOT EXISTS ${PORTBUILD_PATH}/lib)
    message(FATAL_ERROR
        "No built dependency tree at ${PORTBUILD_PATH} for ANDROID_ABI=${ANDROID_ABI}. "
        "Upstream Kirikiroid2Yuri's deps release publishes thirdparty/build/arch_androida64 "
        "(aarch64) alone. Build the eighteen static dependencies for this ABI into "
        "thirdparty/build/${PORTBUILD_ARCH} before adding it to PROP_APP_ABI; the comment "
        "above this check says which of them cocos2d-x 3.17.2 has no x86_64 answer for.")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
endif()

add_subdirectory(${KRKR2YURI_DIR} ${KRKR2YURI_DIR}/build_android)

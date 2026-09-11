cmake_minimum_required(VERSION 3.7)
project(krkr2yuri_android)

set(KRKR2YURI_DIR ${CMAKE_SOURCE_DIR}/../../../..)

# The built dependencies live in one directory per architecture under
# thirdparty/build, named the way script/cross_android.sh names its PLATFORM
# (and the way script/_fetch.sh pre-creates them). Resolve the directory from
# the ABI rather than hardcoding one. Upstream Kirikiroid2Yuri publishes
# arch_androida64 and nothing else; every other ABI's tree is built by the
# thirdparty job in .github/workflows/android-plugin.yml and restored here.
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
# x86_64 is the one ABI that is more than a retargeted build. cocos2d-x 3.17.2
# does not build nine of the libraries this engine links (z, png, webp, tiff,
# freetype, chipmunk, BulletCollision, BulletDynamics, LinearMath): each
# external/<lib>/CMakeLists.txt imports a static library at
# prebuilt/android/${ANDROID_ABI} (cmake/CocosExternalConfig.cmake:33), and the
# set cocos2d-x publishes -- every v3-deps tag of cocos2d-x-3rd-party-libs-bin,
# from the v3-deps-158 that 3.17.2 pins in external/config.json through the last
# one, v3-deps-172 -- carries armeabi-v7a, arm64-v8a and x86 and no more.
# Upstream's own builder agrees: cocos2d-x-3rd-party-libs-src branch v3,
# build/android.ini, declares cfg_all_supported_arches=("armv7" "x86" "arm64"
# "mips") and defines no x86_64 alias folder, host triple or API level. So
# script/build_cocos_deps.sh builds those archives from their own upstream
# sources, at the versions that builder pins, and stages them into cocos2d-x's
# prebuilt layout before the rest of the tree is built.
if(NOT EXISTS ${PORTBUILD_PATH}/lib)
    message(FATAL_ERROR
        "No built dependency tree at ${PORTBUILD_PATH} for ANDROID_ABI=${ANDROID_ABI}. "
        "Upstream Kirikiroid2Yuri's deps release publishes thirdparty/build/arch_androida64 "
        "(aarch64) alone; every other ABI is built by the thirdparty job in "
        ".github/workflows/android-plugin.yml, which runs script/cross_android.sh "
        "${PORTBUILD_ARCH#arch_}. Build that tree before adding this ABI to PROP_APP_ABI.")
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
endif()

add_subdirectory(${KRKR2YURI_DIR} ${KRKR2YURI_DIR}/build_android)

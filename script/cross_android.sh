#!/usr/bin/env bash
# Fetch and build the native dependency tree for one Android ABI, and then the
# project itself.
#
#   ./cross_android.sh androida64            # aarch64, what upstream publishes
#   ./cross_android.sh androidx32            # x86
#   ./cross_android.sh androidx64            # x86_64
#   PORTS_ONLY=yes ./cross_android.sh androidx32   # dependencies only (CI)
#   SKIP_PORTS=yes ./cross_android.sh androida64   # project only
#
# The platform names are upstream's own (thirdparty/build/arch_<platform>, which
# project/android/app/cpp/CMakeLists.txt resolves from ANDROID_ABI). This
# replaces cross_androida64.sh, which was the same script with aarch64 written
# into it; one script, one place the target is named.

PLATFORM=${1:-androida64}
shift 2>/dev/null
TARGETS=$@

case $PLATFORM in
    androida64) ABI=arm64-v8a   ; TRIPLE=aarch64-linux-android ; FFARCH=aarch64 ; IS_ARM=yes ;;
    androida32) ABI=armeabi-v7a ; TRIPLE=armv7a-linux-androideabi ; FFARCH=arm ; IS_ARM=yes ;;
    androidx32) ABI=x86         ; TRIPLE=i686-linux-android   ; FFARCH=x86     ; IS_ARM=no  ;;
    androidx64) ABI=x86_64      ; TRIPLE=x86_64-linux-android ; FFARCH=x86_64  ; IS_ARM=no  ;;
    *) echo "unknown platform $PLATFORM (androida64|androida32|androidx32|androidx64)" >&2; exit 1 ;;
esac
if [ -z "$API" ]; then API=21; fi

BUILD_PATH=./../build_${PLATFORM}
CMAKELISTS_PATH=$(pwd)/..
PORTBUILD_PATH=$CMAKELISTS_PATH/thirdparty/build/arch_$PLATFORM
ANDROID_THIRDPARTY_PATH=$CMAKELISTS_PATH/src/onsyuri_android/app/cpp/thirdparty
CORE_NUM=$(cat /proc/cpuinfo | grep -c ^processor)

function fetch_ports()
{
    # audio
    fetch_opus
    fetch_ogg
    fetch_vorbis
    fetch_opusfile
    fetch_oboe
    fetch_openal

    # video
    fetch_jpeg
    fetch_opencv
    fetch_ffmpeg

    # archive
    fetch_unrar
    fetch_lz4
    fetch_archive
    fetch_p7zip

    # others
    fetch_oniguruma
    fetch_syscall
    fetch_breakpad

    # framework
    fetch_sdl2
    fetch_cocos2dx
}

function build_ports()
{
    # audio
    build_opus
    build_ogg
    build_vorbis
    build_opusfile
    build_oboe
    build_openal

    # video
    build_jpeg
    build_opencv
    build_ffmpeg

    # archive
    build_unrar
    build_lz4
    build_archive
    build_p7zip

    # others
    build_oniguruma
    build_breakpad

    # framework
    build_sdl2
    build_cocos2dx
}

# prepare env, tested with ndk 25.2.9519653
if [ -z "$ANDROID_HOME" ]; then ANDROID_HOME=/d/Software/env/sdk/androidsdk; fi
if [ -z "$NDK_HOME" ]; then
    if [ -n "$ANDROID_NDK_HOME" ]; then
        NDK_HOME=$ANDROID_NDK_HOME
    else
        NDK_HOME=$ANDROID_HOME/ndk/$(ls -A $ANDROID_HOME/ndk | tail -n 1)
    fi
fi
PREBUILT_DIR=$NDK_HOME/toolchains/llvm/prebuilt
PREBUILT_DIR=$PREBUILT_DIR/$(ls -A $PREBUILT_DIR | tail -n 1)
PATH=$NDK_HOME/build:$PREBUILT_DIR/bin:$PATH
CC=$(which $TRIPLE$API-clang)
CXX=$(which $TRIPLE$API-clang++)
AR=$(which llvm-ar)
RANLIB=$(which llvm-ranlib)
NM=$(which llvm-nm)
STRIP=$(which llvm-strip)
NDKBUILD=$(which ndk-build)
SYSROOT=$PREBUILT_DIR/sysroot
echo "## PLATFORM=$PLATFORM ABI=$ABI TRIPLE=$TRIPLE API=$API"
echo "## ANDROID_HOME=$ANDROID_HOME"
echo "## NDK_HOME=$NDK_HOME"
echo "## NDK-BUILD=$NDKBUILD"
echo "## CC=$CC"
echo "## AR=$AR"

# fetch and build ports
# SKIP_PORTS="yes"
source ./_fetch.sh
source ./_android.sh
# The runtime assets belong to the project build, not to the dependencies.
if [ -z "$PORTS_ONLY" ]; then fetch_asset; fi
if [ -z "$SKIP_PORTS" ]; then
    fetch_ports
    # Upstream's thirdparty_port.tar.gz stores its files without the execute
    # bit, and tar restores what was stored, so every ./configure in it is
    # unrunnable ("Permission denied") until this. Upstream never hits it
    # because its own builds are done in a tree it checked out rather than
    # unpacked.
    find $CMAKELISTS_PATH/thirdparty/port -type f \( \
        -name configure -o -name config.sub -o -name config.guess -o \
        -name install-sh -o -name ltmain.sh -o -name missing -o \
        -name depcomp -o -name compile -o -name test-driver -o \
        -name mkinstalldirs -o -name 'configure.gnu' -o -name '*.sh' \) \
        -exec chmod +x {} +
    # cocos2d-x 3.17.2 builds nine of its dependencies for nobody: it imports
    # them as static libraries out of external/<lib>/prebuilt/android/$ABI, and
    # the set it publishes covers armeabi-v7a, arm64-v8a and x86 only. Say so
    # here rather than letting build_cocos2dx fail on a missing wildcard.
    if ! [ -d $COCOS2DX_SRC/external/zlib/prebuilt/android/$ABI ]; then
        echo "## cocos2d-x has no prebuilt dependency set for $ABI at $COCOS2DX_SRC/external/*/prebuilt/android/$ABI" >&2
        exit 1
    fi
    build_ports
fi
if [ -n "$PORTS_ONLY" ]; then exit 0; fi

# config and build project
if [ -z "$BUILD_TYPE" ]; then BUILD_TYPE=MinSizeRel; fi
if [ -z "$TARGETS" ]; then TARGETS=all; fi

cmake -B $BUILD_PATH -S $CMAKELISTS_PATH \
    -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
    -DPORTBUILD_PATH=$PORTBUILD_PATH
make -C $BUILD_PATH $TARGETS -j$CORE_NUM

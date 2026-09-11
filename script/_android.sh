# The dependency builds, one call site per library. Sourced by cross_android.sh
# after _fetch.sh and parameterized by the target: $ABI is the Android ABI name
# the NDK and cocos2d-x both use, $TRIPLE the toolchain triple, $API the minimum
# platform level, $FFARCH what ffmpeg's configure calls the architecture, and
# $IS_ARM whether this target is an ARM one. Upstream shipped this file as
# _androida64.sh with aarch64-linux-android and arm64-v8a written out at every
# call site; it is the same set of builds with the target named once instead.

CLANG=$TRIPLE$API-clang
CLANGXX=$TRIPLE$API-clang++

# A cross configure that fails says "see config.log" and CI throws the tree
# away, so the reason is gone. Run every one of them through this instead.
run_configure()
{
    "$@" && return 0
    echo "### configure failed: $* ###" >&2
    # The variable dump at the end of config.log is never the interesting
    # part; the failed test is above it, so print the failures themselves.
    echo "### config.log: the failed tests ###" >&2
    grep -n -B4 -A12 'error:\|failed program was\|configure: error' config.log | tail -n 200 >&2
    return 1
}

# audio
build_opus()
{
    if ! [ -d $OPUS_SRC/build_$PLATFORM ]; then mkdir -p $OPUS_SRC/build_$PLATFORM ;fi

    pushd $OPUS_SRC/build_$PLATFORM
    run_configure ../configure --host=$TRIPLE \
        CC=$CLANG  AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm STRIP=llvm-strip \
        CXX=$CLANGXX \
        --prefix=$PORTBUILD_PATH --with-pic
    make -j$CORE_NUM &&  make install
    popd
}

build_ogg()
{
    if ! [ -d $OGG_SRC/build_$PLATFORM ]; then mkdir -p $OGG_SRC/build_$PLATFORM ;fi

    pushd $OGG_SRC/build_$PLATFORM
    run_configure ../configure --host=$TRIPLE \
        CC=$CLANG  AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm STRIP=llvm-strip \
        CXX=$CLANGXX \
        --prefix=$PORTBUILD_PATH --with-pic
    make -j$CORE_NUM &&  make install
    popd
}

build_vorbis()
{
    if ! [ -d $VORBIS_SRC/build_$PLATFORM ]; then mkdir -p $VORBIS_SRC/build_$PLATFORM ;fi

    # libvorbis's configure writes its own CFLAGS per host, and for any
    # *86-*-linux* host that includes GCC's -mno-ieee-fp. clang has never had
    # that flag, so with NDK 25 every conftest after that line fails to compile
    # and configure reports the symptom instead of the cause: "Ogg >= 1.0
    # required !", with -lm and -lpthread also "not found". The flag only
    # appears in that one branch, so taking it out changes nothing for an ARM
    # target.
    sed -i 's/-mno-ieee-fp//g' $VORBIS_SRC/configure

    pushd $VORBIS_SRC/build_$PLATFORM
    run_configure ../configure --host=$TRIPLE \
        CC=$CLANG  AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm STRIP=llvm-strip \
        CXX=$CLANGXX \
        --prefix=$PORTBUILD_PATH --with-pic \
        --with-ogg=$PORTBUILD_PATH
    make -j$CORE_NUM &&  make install
    popd
}

build_opusfile() # after ogg, opus, vorbits
{
    if ! [ -d $OPUSFILE_SRC/build_$PLATFORM ]; then mkdir -p $OPUSFILE_SRC/build_$PLATFORM ;fi

    pushd $OPUSFILE_SRC/build_$PLATFORM
    run_configure ../configure --host=$TRIPLE \
        CC=$CLANG  AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm STRIP=llvm-strip \
        CXX=$CLANGXX \
        --prefix=$PORTBUILD_PATH --with-pic \
        DEPS_CFLAGS="-I$PORTBUILD_PATH/include -I$PORTBUILD_PATH/include/opus" \
        DEPS_LIBS="-L$PORTBUILD_PATH/lib -logg -lopus" \
        --disable-http --disable-examples
    make -j$CORE_NUM &&  make install

    cp -rf $CMAKELISTS_PATH/thirdparty/patch/opus/opusfile.h $PORTBUILD_PATH/include/opus/opusfile.h

    popd
}

build_oboe()
{
    if ! [ -d $OBOE_SRC/build_$PLATFORM ]; then mkdir -p $OBOE_SRC/build_$PLATFORM ;fi

    pushd $OBOE_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_C_FLAGS="-fPIC" -DCMAKE_CXX_FLAGS="-fPIC" \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH \
        -DLIBTYPE=STATIC
    make -j$CORE_NUM &&  make install

    mv -f $PORTBUILD_PATH/lib/$ABI/liboboe.a $PORTBUILD_PATH/lib/liboboe.a

    popd
}

build_openal()
{
    if ! [ -d $OPENAL_SRC/build_$PLATFORM ]; then mkdir -p $OPENAL_SRC/build_$PLATFORM ;fi

    pushd $OPENAL_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_C_FLAGS="-fPIC" -DCMAKE_CXX_FLAGS="-fPIC" \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH \
        -DLIBTYPE=STATIC
    make -j$CORE_NUM &&  make install
    popd
}

# video
build_jpeg()
{
    if ! [ -d $JPEG_SRC/build_$PLATFORM ]; then mkdir -p $JPEG_SRC/build_$PLATFORM ;fi

    pushd $JPEG_SRC/build_$PLATFORM
    NDK_PATH=$NDK_HOME
    TOOLCHAIN=clang
    # libjpeg-turbo's ARM SIMD is .S assembly the NDK's own clang assembles.
    # Its x86 SIMD is NASM syntax and wants a nasm or yasm the NDK does not
    # carry, so on an x86 target the C paths are built instead -- stated here
    # rather than left to whatever assembler happens to be on the machine.
    if [ "$IS_ARM" = yes ]; then SIMD_ARG=; else SIMD_ARG=-DWITH_SIMD=OFF; fi
    cmake .. -G "Unix Makefiles" \
        -DANDROID_ABI=$ABI \
        -DANDROID_ARM_MODE=arm \
        -DANDROID_PLATFORM=android-${API} \
        -DANDROID_TOOLCHAIN=${TOOLCHAIN} \
        -DCMAKE_ASM_FLAGS="--target=$TRIPLE${API}" \
        -DCMAKE_TOOLCHAIN_FILE=${NDK_PATH}/build/cmake/android.toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH $SIMD_ARG
    make -j$CORE_NUM &&  make install
    popd
}

build_opencv()
{
    if ! [ -d $OPENCV_SRC/build_$PLATFORM ]; then mkdir -p $OPENCV_SRC/build_$PLATFORM ;fi

    # libtegra_hal.a is opencv's ARM HAL and has no x86 counterpart; left to
    # itself a non-ARM build reaches for the IPP archives instead, which it
    # downloads at configure time. Turn IPP off and link neither: what the
    # engine asks of opencv is imgproc over core, which the C paths serve.
    if [ "$IS_ARM" = yes ]; then HAL_ARGS=; else HAL_ARGS="-DWITH_IPP=OFF -DBUILD_IPP_IW=OFF"; fi

    pushd $OPENCV_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH \
        -DWITH_CUDA=OFF -DWITH_MATLAB=OFF -DBUILD_ANDROID_EXAMPLES=OFF \
        -DBUILD_DOCS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_TESTS=OFF \
        -DBUILD_opencv_video=OFF -DBUILD_opencv_videoio=OFF -DBUILD_opencv_features2d=OFF \
        -DBUILD_opencv_flann=OFF -DBUILD_opencv_highgui=OFF -DBUILD_opencv_ml=OFF \
        -DBUILD_opencv_dnn=OFF -DBUILD_opencv_gapi=OFF -DBUILD_opencv_hal=ON \
        -DBUILD_opencv_photo=OFF -DBUILD_opencv_python=OFF -DBUILD_opencv_shape=OFF \
        -DBUILD_opencv_stitching=OFF -DBUILD_opencv_superres=OFF -DWITH_ITT=OFF \
        -DBUILD_opencv_ts=OFF -DBUILD_opencv_videostab=OFF -DBUILD_ANDROID_PROJECTS=OFF \
        $HAL_ARGS
    make -j$CORE_NUM &&  make install

    # opencv's install prefix is an Android SDK layout, not a lib directory:
    # its own modules land in sdk/native/staticlibs/$ABI and the third-party
    # archives it built for itself in sdk/native/3rdparty/libs/$ABI. Everything
    # the engine links out of opencv comes from those two -- libopencv_core,
    # libopencv_imgproc, and on an ARM target libtegra_hal, which is simply one
    # of the staticlibs rather than a case of its own.
    cp -rf  $PORTBUILD_PATH/sdk/native/3rdparty/libs/$ABI/*.a $PORTBUILD_PATH/lib
    cp -rf  $PORTBUILD_PATH/sdk/native/staticlibs/$ABI/*.a $PORTBUILD_PATH/lib

    popd
}

build_ffmpeg()
{
    if ! [ -d $FFMPEG_SRC/build_$PLATFORM ]; then mkdir -p $FFMPEG_SRC/build_$PLATFORM ;fi

    pushd $FFMPEG_SRC
    # The patch is against the source tree, not the build directory, so a
    # second ABI in the same checkout finds it already applied.
    if git apply --check $CMAKELISTS_PATH/thirdparty/patch/ffmpeg/android_ffmpeg.diff 2>/dev/null; then
        git apply $CMAKELISTS_PATH/thirdparty/patch/ffmpeg/android_ffmpeg.diff
    fi
    cd build_$PLATFORM
    run_configure ../configure --enable-cross-compile --cross-prefix=$TRIPLE- \
        --cc=$CLANG  --ar=llvm-ar \
        --cxx=$CLANGXX --ranlib=llvm-ranlib \
        --strip=llvm-strip --prefix=$PORTBUILD_PATH \
        --arch=$FFARCH --target-os=android --enable-pic --disable-asm \
        --enable-static --enable-shared --enable-small --enable-swscale \
        --disable-ffmpeg --disable-ffplay --disable-ffprobe \
        --disable-avdevice --disable-programs --disable-doc --enable-stripping

    # use sh directory is not available in windows (absolute path), must use msys2 shell
    make -j$CORE_NUM &&  make install
    popd
}

# archive
build_unrar()
{
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/unrar/android_ulinks.cpp $UNRAR_SRC/ulinks.cpp

    pushd $UNRAR_SRC
    make clean
    make lib -j$CORE_NUM \
        CXX=$CLANGXX \
        AR=llvm-ar STRIP=llvm-strip \
        DESTDIR=$PORTBUILD_PATH

    if ! [ -d $PORTBUILD_PATH/include/unrar ]; then mkdir -p $PORTBUILD_PATH/include/unrar ;fi
    cp -rf *.a $PORTBUILD_PATH/lib
    cp -rf *.hpp $PORTBUILD_PATH/include/unrar

    popd
}

build_lz4()
{
    pushd $LZ4_SRC
    make clean
    make lib -j$CORE_NUM \
        CC=$CLANG \
        CXX=$CLANGXX \
        AR=llvm-ar STRIP=llvm-strip \
        WINBASED=no

    if ! [ -d $PORTBUILD_PATH/include/lz4 ]; then mkdir -p $PORTBUILD_PATH/include/lz4 ;fi
    cp -rp lib/*.a $PORTBUILD_PATH/lib
    cp -rp lib/*.h $PORTBUILD_PATH/include/lz4

    popd
}

build_archive()
{
    if ! [ -d $ARCHIVE_SRC/build_$PLATFORM ]; then mkdir -p $ARCHIVE_SRC/build_$PLATFORM ;fi
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/android_android_lf.h  $ARCHIVE_SRC/libarchive/android_lf.h

    pushd $ARCHIVE_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DENABLE_OPENSSL=OFF -DENABLE_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH
    make -j$CORE_NUM &&  make install

    if ! [ -d $PORTBUILD_PATH/include/libarchive ]; then mkdir -p $PORTBUILD_PATH/include/libarchive ;fi
    mv -f $PORTBUILD_PATH/include/archive.h $PORTBUILD_PATH/include/libarchive
    mv -f $PORTBUILD_PATH/include/archive_entry.h $PORTBUILD_PATH/include/libarchive

    popd
}

build_p7zip()
{
    if ! [ -d $P7ZIP_SRC/build_$PLATFORM ]; then mkdir -p $P7ZIP_SRC/build_$PLATFORM ;fi
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/p7zip/7z* $P7ZIP_SRC/C
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/p7zip/android_p7zip.cmake $P7ZIP_SRC/CPP/ANDROID/7za/jni/CMakeLists.txt

    pushd $P7ZIP_SRC/build_$PLATFORM
    cmake ../CPP/ANDROID/7za/jni -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake
    make -j$CORE_NUM

    if ! [ -d $PORTBUILD_PATH/include/p7zip/C ]; then mkdir -p $PORTBUILD_PATH/include/p7zip/C ;fi
    if ! [ -d $PORTBUILD_PATH/include/p7zip/CPP ]; then mkdir -p $PORTBUILD_PATH/include/p7zip/CPP ;fi
    cp -rf lib7za.a $PORTBUILD_PATH/lib
    cp -rf ../C/*.h  $PORTBUILD_PATH/include/p7zip/C
    cp -rf ../CPP  $PORTBUILD_PATH/include/p7zip
    rm -rf $PORTBUILD_PATH/include/p7zip/CPP/**/*.cpp
    rm -rf $PORTBUILD_PATH/include/p7zip/CPP/**/**/*.cpp
    rm -rf $PORTBUILD_PATH/include/p7zip/CPP/**/**/**/*.cpp
    rm -rf $PORTBUILD_PATH/include/p7zip/CPP/**/**/**/**/*.cpp
    rm -rf $PORTBUILD_PATH/include/p7zip/CPP/ANDROID/7za/obj

    popd
}

# others
build_oniguruma()
{
    if ! [ -d $ONIGURUMA_SRC/build_$PLATFORM ]; then mkdir -p $ONIGURUMA_SRC/build_$PLATFORM ;fi
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/oniguruma/oniguruma.cmake $ONIGURUMA_SRC/CMakeLists.txt

    pushd $ONIGURUMA_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH
    make -j$CORE_NUM &&  make install
    popd
}

build_breakpad() # after linux-syscall
{
    if ! [ -d $BREAKPAD_SRC/build_$PLATFORM ]; then mkdir -p $BREAKPAD_SRC/build_$PLATFORM ;fi
    cp -rf $SYSCALL_SRC/lss $BREAKPAD_SRC/src/third_party/

    # src/common/android/testing/include exists to fill gaps in old 32-bit
    # bionic: its wchar.h defines a static wcscasecmp behind
    # "#if !defined(__aarch64__) && !defined(__x86_64__)", and NDK 25's own
    # wchar.h declares that function, so a 32-bit build stops at "static
    # declaration of 'wcscasecmp' follows non-static declaration" while arm64
    # and x86_64 never compile the shim at all. It is a testing shim and
    # --disable-tools builds no tests, so take the directory off the include
    # path where it does harm.
    case $ABI in
        armeabi*|x86)
            sed -i 's#-I$(top_srcdir)/src/common/android/testing/include##' \
                $BREAKPAD_SRC/Makefile.in
            ;;
    esac

    # libc++'s <fstream> calls fseeko and ftello, which 32-bit bionic declares
    # only from API 24; on a 64-bit ABI they are always present, which is why
    # arm64 and x86_64 build this and x86 stops at "use of undeclared
    # identifier 'fseeko'". The only sources that include <fstream> are
    # src/processor, the minidump ANALYSIS half, which does not run on the
    # device at all -- so the archive is compiled against API 24 on those ABIs.
    # Nothing quiet about it: if anything in there were actually reached, the
    # app's own link at API $API would fail on those same two symbols.
    case $ABI in
        armeabi*|x86) BREAKPAD_API=24 ;;
        *)            BREAKPAD_API=$API ;;
    esac

    pushd $BREAKPAD_SRC/build_$PLATFORM
    run_configure ../configure --host=$TRIPLE \
        CC=$TRIPLE$BREAKPAD_API-clang  AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm \
        CXX=$TRIPLE$BREAKPAD_API-clang++ STRIP=llvm-strip \
        --prefix=$PORTBUILD_PATH \
        --disable-tools
    make -j$CORE_NUM &&  make install-strip
    popd
}

# framework
build_sdl2()
{
    if ! [ -d $SDL2_SRC/build_$PLATFORM ]; then mkdir -p $SDL2_SRC/build_$PLATFORM ;fi
    cp -rf $CMAKELISTS_PATH/thirdparty/patch/sdl2/android_SDL_android.c  $SDL2_SRC/src/core/android/SDL_android.c

    pushd $SDL2_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DANDROID=ON -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH \
        -DHIDAPI=OFF -DHAVE_GCC_WDECLARATION_AFTER_STATEMENT=OFF
    make -j$CORE_NUM &&  make install
    popd
}

build_cocos2dx()
{
    if ! [ -d $COCOS2DX_SRC/platform/build_$PLATFORM ]; then mkdir -p $COCOS2DX_SRC/build_$PLATFORM ;fi
    cp $CMAKELISTS_PATH/thirdparty/patch/cocos2d-x/android_cocos2dx.cmake $COCOS2DX_SRC/CMakeLists.txt
    cp $CMAKELISTS_PATH/thirdparty/patch/cocos2d-x/android_CCFileUtils-android.h $COCOS2DX_SRC/cocos/platform/android/CCFileUtils-android.h
    cp $CMAKELISTS_PATH/thirdparty/patch/cocos2d-x/android_CCFileUtils-android.cpp $COCOS2DX_SRC/cocos/platform/android/CCFileUtils-android.cpp
    cp $CMAKELISTS_PATH/thirdparty/patch/cocos2d-x/android_Java_org_cocos2dx_lib_Cocos2dxHelper.h $COCOS2DX_SRC/cocos/platform/android/jni/Java_org_cocos2dx_lib_Cocos2dxHelper.h
    cp $CMAKELISTS_PATH/thirdparty/patch/cocos2d-x/android_Java_org_cocos2dx_lib_Cocos2dxHelper.cpp $COCOS2DX_SRC/cocos/platform/android/jni/Java_org_cocos2dx_lib_Cocos2dxHelper.cpp

    pushd $COCOS2DX_SRC/build_$PLATFORM
    cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE=$NDK_HOME/build/cmake/android.toolchain.cmake \
        -DANDROID_PLATFORM=$API -DANDROID_ABI=$ABI \
        -DCMAKE_INSTALL_PREFIX=$PORTBUILD_PATH \
        -DBUILD_TESTS=OFF -DBUILD_LUA_LIBS=OFF -DBUILD_JS_LIBS=OFF
    make -j$CORE_NUM

    cp -rf lib/libcocos2d.a $PORTBUILD_PATH/lib/
    cp -rf lib/libext_*.a $PORTBUILD_PATH/lib/
    cp -rf engine/cocos/android/libcpp_android_spec.a $PORTBUILD_PATH/lib/
    # cocos2d-x 3.17.2 builds none of these nine: each external/<lib> imports a
    # static library out of prebuilt/android/$ABI. The set cocos2d-x publishes
    # carries armeabi-v7a, arm64-v8a and x86 and nothing else, so any other ABI
    # has to have them staged into that same layout before this runs -- which
    # is what script/build_cocos_deps.sh does.
    for lib in zlib png tiff webp freetype2 chipmunk bullet; do
        cp -rf ../external/$lib/prebuilt/android/$ABI/*.a $PORTBUILD_PATH/lib/
    done

    popd
}

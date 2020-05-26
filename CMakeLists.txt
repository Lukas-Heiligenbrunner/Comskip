cmake_minimum_required(VERSION 3.16)
project(ComSkip)

set(CMAKE_CXX_STANDARD 20)

find_package(PkgConfig REQUIRED)
# add ffmpeg dependencies here
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
        )

include_directories(inc)

SET(SOURCES
        src/comskip.c
        src/mpeg2dec.c
        src/platform.c
        src/ccextratorwin/608.c
        src/ccextratorwin/ccextractor.c
        src/ccextratorwin/encoding.c
        src/ccextratorwin/general_loop.c
        src/ccextratorwin/myth.c

        src/main.c
        src/Settings.c)

add_executable(${PROJECT_NAME} ${SOURCES})


target_link_libraries(${PROJECT_NAME}
        PkgConfig::LIBAV
        -lpthread
        -lm
        /usr/lib64/libargtable2.so
        )
# todo link libartable2 dynamic here
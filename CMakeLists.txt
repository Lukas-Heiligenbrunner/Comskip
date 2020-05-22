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
        inc/comskip.h
        src/mpeg2dec.c
        src/platform.c
        inc/platform.h
        src/ccextratorwin/608.c
        inc/ccextratorwin/608.h
        src/ccextratorwin/ccextractor.c
        inc/ccextratorwin/ccextractor.h
        src/ccextratorwin/encoding.c
        src/ccextratorwin/general_loop.c
        src/ccextratorwin/myth.c)

add_executable(${PROJECT_NAME} ${SOURCES})


target_link_libraries(${PROJECT_NAME}
        PkgConfig::LIBAV
        -lpthread
        -lm
        /usr/lib64/libargtable2.so
        )
# todo link libartable2 dynamic here
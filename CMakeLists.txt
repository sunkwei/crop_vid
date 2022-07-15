cmake_minimum_required(VERSION 3.0)
project(crop_vid VERSION 1.0.0 DESCRIPTION "to crop multiple bbox from video")

set(CMAKE_LINK_WHAT_YOU_USE TRUE)
option(WITH_TEA OFF)

find_package(PkgConfig)
pkg_check_modules(CV opencv4 REQUIRED)
pkg_check_modules(AVFORMAT libavformat REQUIRED)
pkg_check_modules(AVCODEC libavcodec REQUIRED)
pkg_check_modules(SWSCALE libswscale REQUIRED)
pkg_check_modules(AVFILTER libavfilter REQUIRED)
pkg_check_modules(AVUTIL libavutil REQUIRED)

include_directories(
    ${CV_INCLUDE_DIRS} 
    ${AVCODEC_INCLUDE_DIRS} 
    ${AVFORMAT_INCLUDE_DIRS} 
    ${SWSCALE_INCLUDE_DIRS} 
    ${AVFILTER_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

if(${WITH_TEA})
    add_compile_definitions(WITH_TEA=1)
    include_directories(
        /home/sunkw/work/git/video_analyse_libtea/libtea/include/
    )
endif()

add_executable(crop_vid
    src/main.cpp
    src/media.cxx src/media.hxx
)

target_link_libraries(crop_vid PRIVATE
    ${CV_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

if(${WITH_TEA})
    target_link_directories(crop_vid PRIVATE
        /home/sunkw/work/git/video_analyse_libtea/libtea/build_x86
    )
    target_link_libraries(crop_vid PRIVATE
        tea_nopy
    )
endif()
cmake_minimum_required(VERSION 3.0)
project(zk_build_student_vid VERSION 1.0.0 DESCRIPTION "to build student personal videos")

# set(CMAKE_LINK_WHAT_YOU_USE TRUE)

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

add_executable(zk_build_student_vid
    src/main.cpp
    src/media.cxx src/media.hxx
)

target_link_libraries(zk_build_student_vid PRIVATE
    ${CV_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${AVFILTER_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)
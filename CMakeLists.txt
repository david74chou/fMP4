cmake_minimum_required(VERSION 2.6)
project(fMP4)

set( CMAKE_BUILD_TYPE Debug )

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(LIBAVCODEC REQUIRED libavcodec)
if(LIBAVCODEC_FOUND)
    include_directories(${LIBAVCODEC_INCLUDE_DIRS})
    link_directories(${LIBAVCODEC_LIBRARY_DIRS})
endif()

pkg_check_modules(LIBAVUTIL REQUIRED libavutil)
if(LIBAVUTIL_FOUND)
    include_directories(${LIBAVUTIL_INCLUDE_DIRS})
    link_directories(${LIBAVUTIL_LIBRARY_DIRS})
endif()

pkg_check_modules(LIBAVFORMAT REQUIRED libavformat)
if(LIBAVFORMAT_FOUND)
    include_directories(${LIBAVFORMAT_INCLUDE_DIRS})
    link_directories(${LIBAVFORMAT_LIBRARY_DIRS})
endif()

pkg_check_modules(LIBSWSCALE REQUIRED libswscale)
if(LIBSWSCALE_FOUND)
    include_directories(${LIBSWSCALE_INCLUDE_DIRS})
    link_directories(${LIBSWSCALE_LIBRARY_DIRS})
endif()

find_package(MP4V2 REQUIRED)
if(MP4V2_FOUND)
    include_directories(${MP4V2_INCLUDE_DIRS})
endif()

find_package(BENTO4 REQUIRED)
if(BENTO4_FOUND)
    include_directories(${BENTO4_INCLUDE_DIRS})
endif()

pkg_check_modules(GSTCODECPARSERLIB REQUIRED gstreamer-codecparsers-1.0)
if(GSTCODECPARSERLIB_FOUND)
    include_directories(${GSTCODECPARSERLIB_INCLUDE_DIRS})
    link_directories(${GSTCODECPARSERLIB_LIBRARY_DIRS})
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D__STDC_CONSTANT_MACROS")

add_executable(fMP4-sample1 sample1.cpp)
target_link_libraries(fMP4-sample1
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
)

add_executable(fMP4-sample2 sample2.cpp)
target_link_libraries(fMP4-sample2
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
)

add_executable(fMP4-sample3 sample3.cpp)
target_link_libraries(fMP4-sample3
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
)

add_executable(fMP4-sample4 sample4.cpp)
target_link_libraries(fMP4-sample4
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
)

add_executable(fMP4-sample5 sample5.cpp)
target_link_libraries(fMP4-sample5
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
)

add_executable(fMP4-sample6 sample6.cpp)
target_link_libraries(fMP4-sample6
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
        ${GSTCODECPARSERLIB_LIBRARIES}
)

add_executable(fMP4-sample7 sample7.cpp)
target_link_libraries(fMP4-sample7
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
        ${GSTCODECPARSERLIB_LIBRARIES}
)

add_executable(fMP4-sample8 sample8.cpp)
target_link_libraries(fMP4-sample8
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
        ${GSTCODECPARSERLIB_LIBRARIES}
)

add_executable(fMP4-sample9 sample9.cpp)
target_link_libraries(fMP4-sample9
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
        ${MP4V2_LIBRARY}
        ${BENTO4_LIBRARY}
        ${GSTCODECPARSERLIB_LIBRARIES}
)
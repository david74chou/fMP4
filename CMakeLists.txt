cmake_minimum_required(VERSION 2.6)
project(fMP4)

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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D__STDC_CONSTANT_MACROS")
set(SOURCE_FILES main.cpp)
add_executable(fMP4 ${SOURCE_FILES})
target_link_libraries(fMP4
        ${LIBAVCODEC_LIBRARIES}
        ${LIBAVUTIL_LIBRARIES}
        ${LIBAVFORMAT_LIBRARIES}
        ${LIBSWSCALE_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT}
)
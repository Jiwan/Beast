# Part of Beast

cmake_minimum_required (VERSION 3.2)

project (Beast)

set_property (GLOBAL PROPERTY USE_FOLDERS ON)

if (WIN32)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /W4 /wd4100 /wd4131 /wd4127 /wd4244 /D_SCL_SECURE_NO_WARNINGS=1 /D_CRT_SECURE_NO_WARNINGS=1")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
else()
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_MULTITHREADED ON)
    find_package(Boost REQUIRED COMPONENTS coroutine context thread filesystem program_options system)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIR})

    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads)

    set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -g -std=c++11 -Wall -Wpedantic")
endif()

message ("cxx Flags: " ${CMAKE_CXX_FLAGS})

function(DoGroupSources curdir rootdir folder)
    file(GLOB children RELATIVE ${PROJECT_SOURCE_DIR}/${curdir} ${PROJECT_SOURCE_DIR}/${curdir}/*)
    foreach(child ${children})
        if(IS_DIRECTORY ${PROJECT_SOURCE_DIR}/${curdir}/${child})
            DoGroupSources(${curdir}/${child} ${rootdir} ${folder})
        elseif(${child} STREQUAL "CMakeLists.txt")
            source_group("" FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        else()
            string(REGEX REPLACE ^${rootdir} ${folder} groupname ${curdir})
            #set(groupname ${curdir})
            string(REPLACE "/" "\\" groupname ${groupname})
            source_group(${groupname} FILES ${PROJECT_SOURCE_DIR}/${curdir}/${child})
        endif()
    endforeach()
endfunction()

function(GroupSources curdir folder)
    DoGroupSources(${curdir} ${curdir} ${folder})
endfunction()

include_directories (extras)
include_directories (include)

file(GLOB_RECURSE BEAST_INCLUDES
    ${PROJECT_SOURCE_DIR}/include/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/include/beast/*.ipp
    ${PROJECT_SOURCE_DIR}/extras/beast/*.hpp
    ${PROJECT_SOURCE_DIR}/extras/beast/*.ipp
)

set (ZLIB_SOURCES
    ${PROJECT_SOURCE_DIR}/test/core/zlib/zconf.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/zlib.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/crc32.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/deflate.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inffast.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inffixed.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inflate.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inftrees.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/trees.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/zutil.h
    ${PROJECT_SOURCE_DIR}/test/core/zlib/adler32.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/compress.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/crc32.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/deflate.cpp
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inflate.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/infback.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inftrees.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/inffast.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/trees.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/uncompr.c
    ${PROJECT_SOURCE_DIR}/test/core/zlib/zutil.c
)

add_subdirectory (examples)
add_subdirectory (test)
add_subdirectory (test/core)
add_subdirectory (test/http)
add_subdirectory (test/websocket)

#enable_testing()

cmake_minimum_required(VERSION 2.4.4...3.15.0)
set(CMAKE_ALLOW_LOOSE_LOOP_CONSTRUCTS ON)

project(zlib C)

set(VERSION "1.3.1.1")

set(target zlib)
add_app_libraries($<TARGET_FILE:${target}>)

set(Z_HAVE_UNISTD_H unistd.h)

set(ZLIB_PC ${CMAKE_CURRENT_BINARY_DIR}/zlib.pc)
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/zlib.pc.cmakein
        ${ZLIB_PC} @ONLY)
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/zconf.h.cmakein
        ${CMAKE_CURRENT_BINARY_DIR}/zconf.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR})

#============================================================================
# zlib
#============================================================================

add_definitions(-DIOAPI_NO_64)

set(ZLIB_PUBLIC_HDRS
    ${CMAKE_CURRENT_BINARY_DIR}/zconf.h
    zlib.h
)
set(ZLIB_PRIVATE_HDRS
    crc32.h
    deflate.h
    gzguts.h
    inffast.h
    inffixed.h
    inflate.h
    inftrees.h
    trees.h
    zutil.h
)
set(ZLIB_SRCS
    adler32.c
    compress.c
    crc32.c
    deflate.c
    gzclose.c
    gzlib.c
    gzread.c
    gzwrite.c
    inflate.c
    infback.c
    inftrees.c
    inffast.c
    trees.c
    uncompr.c
    zutil.c
    contrib/minizip/ioapi.c
    contrib/minizip/unzip.c
    contrib/minizip/zip.c
)

if(NOT MINGW)
    set(ZLIB_DLL_SRCS
        win32/zlib1.rc # If present will override custom build rule below.
    )
endif()

# parse the full version number from zlib.h and include in ZLIB_FULL_VERSION
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/zlib.h _zlib_h_contents)
string(REGEX REPLACE ".*#define[ \t]+ZLIB_VERSION[ \t]+\"([-0-9A-Za-z.]+)\".*"
    "\\1" ZLIB_FULL_VERSION ${_zlib_h_contents})

#add_library(zlib SHARED ${ZLIB_SRCS} ${ZLIB_DLL_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS})
#target_include_directories(zlib PUBLIC
#    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
#    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

add_library(zlibstatic STATIC ${ZLIB_SRCS} ${ZLIB_PUBLIC_HDRS} ${ZLIB_PRIVATE_HDRS})
target_include_directories(zlibstatic PUBLIC
    contrib/minizip
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

target_link_libraries(zlibstatic PRIVATE ${libc_file_name} ${libm_file_name} ${libgcc_file_name})

#set_target_properties(zlib PROPERTIES DEFINE_SYMBOL ZLIB_DLL)
#set_target_properties(zlib PROPERTIES SOVERSION 1)

if(NOT CYGWIN)
    # This property causes shared libraries on Linux to have the full version
    # encoded into their final filename.  We disable this on Cygwin because
    # it causes cygz-${ZLIB_FULL_VERSION}.dll to be created when cygz.dll
    # seems to be the default.
    #
    # This has no effect with MSVC, on that platform the version info for
    # the DLL comes from the resource file win32/zlib1.rc
    #set_target_properties(zlib PROPERTIES VERSION ${ZLIB_FULL_VERSION})
endif()

if(UNIX)
    # On unix-like platforms the library is almost always called libz
   set_target_properties(zlib zlibstatic PROPERTIES OUTPUT_NAME z)
   if(NOT APPLE AND NOT(CMAKE_SYSTEM_NAME STREQUAL AIX))
     set_target_properties(zlib PROPERTIES LINK_FLAGS "-Wl,--version-script,\"${CMAKE_CURRENT_SOURCE_DIR}/zlib.map\"")
   endif()
elseif(BUILD_SHARED_LIBS AND WIN32)
    # Creates zlib1.dll when building shared library version
    set_target_properties(zlib PROPERTIES SUFFIX "1.dll")
endif()

#============================================================================
# Example binaries
#============================================================================
#if(ZLIB_BUILD_EXAMPLES)
#    add_executable(example test/example.c)
#    target_link_libraries(example zlib)
#    add_test(example example)
#
#    add_executable(minigzip test/minigzip.c)
#    target_link_libraries(minigzip zlib)

#    if(HAVE_OFF64_T)
#        add_executable(example64 test/example.c)
#        target_link_libraries(example64 zlib)
#        set_target_properties(example64 PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64")
#        add_test(example64 example64)
#
#        add_executable(minigzip64 test/minigzip.c)
#        target_link_libraries(minigzip64 zlib)
#        set_target_properties(minigzip64 PROPERTIES COMPILE_FLAGS "-D_FILE_OFFSET_BITS=64")
#    endif()
#endif()

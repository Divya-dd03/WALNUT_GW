# All rights reserved.
#

# Configures CMake for using GCC

set(CMAKE_SYSTEM_NAME           Generic)
find_program(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
find_program(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
find_program(CMAKE_READELF      ${CROSS_COMPILE}readelf)

set(CMAKE_EXECUTABLE_SUFFIX_ASM .elf)
set(CMAKE_EXECUTABLE_SUFFIX_C .elf)
set(CMAKE_EXECUTABLE_SUFFIX_CXX .elf)

execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion OUTPUT_VARIABLE gcc_version OUTPUT_STRIP_TRAILING_WHITESPACE)

if(CONFIG_CPU_ARM_CA5)
    set(abi_options -mtune=cortex-r5 -march=armv7-r)
    set(partial_link_options)
    set(libc_file_name ${CMAKE_CURRENT_SOURCE_DIR}/components/newlib/armcr5/libc.a)
    set(libm_file_name ${CMAKE_CURRENT_SOURCE_DIR}/components/newlib/armcr5/libm.a)
endif()

if(CONFIG_ENABLE_GCC_LTO)
    set(lto_compile_option -flto -ffat-lto-objects)
    set(lto_link_option -flto)
endif()

add_compile_options(${abi_options} -g
    -Wall
    -fno-strict-aliasing
    -ffunction-sections -fdata-sections
    -Os
)
add_link_options(${abi_options} ${lto_link_option})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -mapcs-frame")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics")

# GNU ar will alreay create index
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")

if(WITH_WERROR)
    add_compile_options(-Werror)
endif()

if(WITH_LINK_CREF)
    set(link_cref_option -Wl,-cref)
endif()

#set(multilib_opions ${abi_options})
#if((${gcc_version} VERSION_GREATER 8) AND (CONFIG_CPU_ARM_CA5))
#    set(multilib_opions -mfloat-abi=hard)
#endif()
execute_process(COMMAND ${CMAKE_C_COMPILER} ${multilib_opions} --print-file-name libgcc.a
    OUTPUT_VARIABLE libgcc_file_name
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

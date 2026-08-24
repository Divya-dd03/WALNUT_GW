# All rights reserved.
#

function(print arg)
    message(STATUS "${arg}: ${${arg}}")
endfunction()

# Helper to set source with conditions
macro(target_sources_if) # <conditon> THEN <command>
    cmake_parse_arguments(MY "" "" "THEN" ${ARGN})
    if(${MY_UNPARSED_ARGUMENTS})
        target_sources(${MY_THEN})
    endif()
endmacro()

# Helper to link libraries with conditions
macro(target_link_libraries_if)  # <conditon> THEN <command>
    cmake_parse_arguments(MY "" "" "THEN" ${ARGN})
    if(${MY_UNPARSED_ARGUMENTS})
        target_link_libraries(${MY_THEN})
    endif()
endmacro()

# Helper to link libraries with conditions
macro(target_include_targets_if)  # <conditon> THEN <command>
    cmake_parse_arguments(MY "" "" "THEN" ${ARGN})
    if(${MY_UNPARSED_ARGUMENTS})
        target_include_targets(${MY_THEN})
    endif()
endmacro()

# Helper to set variable with conditions
macro(set_if var) # <condition> THEN <val_true> OTHERWISE <val_false>
    cmake_parse_arguments(MY "" "" "THEN;OTHERWISE" ${ARGN})
    if(${MY_UNPARSED_ARGUMENTS})
        if(MY_THEN)
            set(${var} ${MY_THEN})
        endif()
    else()
        if (MY_OTHERWISE)
            set(${var} ${MY_OTHERWISE})
        endif()
    endif()
endmacro()

function(add_subdirectory_if)
    cmake_parse_arguments(MY "" "" "THEN" ${ARGN})
    if(${MY_UNPARSED_ARGUMENTS})
        add_subdirectory(${MY_THEN})
    endif()
endfunction()

# Set a target to include all interface directories of depended targets.
# The arguments must be target.
function(target_include_targets target type)
    foreach(arg ${ARGN})
        target_include_directories(${target} ${type}
            $<TARGET_PROPERTY:${arg},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
endfunction()

# Include all interface directories of depended targets
# The arguments must be target.
function(include_targets)
    foreach(arg ${ARGN})
        include_directories($<TARGET_PROPERTY:${arg},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
endfunction()

# Set library files as application libraries
function(add_app_libraries)
    get_property(app_libraries GLOBAL PROPERTY app_libraries)
    set_property(GLOBAL PROPERTY app_libraries ${app_libraries} ${ARGN})
endfunction()

# Link targets with --whole-archive. PUBLIC/PRIVATE is required as parameter,
# but PRIVATE will be used forcedly.
function(target_link_whole_archive target signature)
    target_link_libraries(${target} PRIVATE -Wl,--whole-archive)
    foreach(arg ${ARGN})
        target_link_libraries(${target} PRIVATE ${arg})
    endforeach()
    target_link_libraries(${target} PRIVATE -Wl,--no-whole-archive)
endfunction()

# Link targets with --start-group. PUBLIC/PRIVATE is required as parameter,
# but PRIVATE will be used forcedly.
function(target_link_group target signature)
    target_link_libraries(${target} PRIVATE -Wl,--start-group)
    foreach(arg ${ARGN})
        target_link_libraries(${target} PRIVATE ${arg})
    endforeach()
    target_link_libraries(${target} PRIVATE -Wl,--end-group)
endfunction()

function(add_subdirectory_if_exist dir)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir})
        if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
            add_subdirectory(${dir})
        endif()
    endif()
endfunction()

# don't compile soure codes with lto, even lto is enabled by default
function(source_files_no_lto)
    set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "-fno-lto")
endfunction()

# don't compile target with lto, even lto is enabled by default
function(target_no_lto target)
    target_compile_options(${target} PRIVATE "-fno-lto")
endfunction()

function(cpp_only target file)
    add_library(${target} OBJECT ${file})
    set_source_files_properties(${file} PROPERTIES LANGUAGE C)
    target_compile_options(${target} PRIVATE -E -P -x c)
    foreach(dep ${ARGN})
        target_include_directories(${target}
            PRIVATE $<TARGET_PROPERTY:${dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endforeach()
    foreach(dep ${ARGN})
        target_compile_definitions(${target}
            PRIVATE $<TARGET_PROPERTY:${dep},INTERFACE_COMPILE_DEFINITIONS>)
    endforeach()
endfunction()

# Final link depends on generated linker script
function(target_depend_rom_ld ${target})
endfunction()

# Build appimg with specified link script.
function(add_appimg target ldscript) # <sources> LINK_LIBRARIES <libs> OUTPUT_DIR <dir>
    cmake_parse_arguments(MY "" "" "LINK_LIBRARIES;OUTPUT_DIR" ${ARGN})
    set(MY_SOURCES ${MY_UNPARSED_ARGUMENTS} ${core_stub_o})
    set(MY_LINK_LIBRARIES ${MY_LINK_LIBRARIES} ${libc_file_name} ${libm_file_name} ${libgcc_file_name})
    if(NOT MY_OUTPUT_DIR)
        set(MY_OUTPUT_DIR ${out_hex_dir})
    endif()

    set(gen_ldscript ${target}_ldscript)
    set(target_map_file ${MY_OUTPUT_DIR}/${target}.map)
    set(target_img_file ${MY_OUTPUT_DIR}/${target}.img)
    cpp_only(${gen_ldscript} ${ldscript})
    add_executable(${target} ${MY_SOURCES})
    set_source_files_properties(${core_stub_o} PROPERTIES GENERATED on)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${MY_OUTPUT_DIR})
    target_link_libraries(${target} PRIVATE -T $<TARGET_OBJECTS:${gen_ldscript}>)
    target_link_libraries(${target} PRIVATE -Wl,-Map=${target_map_file} -nostdlib -Wl,--gc-sections ${link_cref_option})
    target_link_libraries(${target} PRIVATE ${MY_LINK_LIBRARIES})
    target_depend_rom_ld(${target})

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${cmd_mkappimg} $<TARGET_FILE:${target}> ${target_img_file}
        BYPRODUCTS ${target_img_file} ${target_map_file}
    )
endfunction()

macro(relative_glob var)
    file(GLOB ${var} RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${ARGN})
endmacro()

function(beautify_c_code target)
    if(ARGN)
        set(beautify_target beautify_${target})
        if(NOT TARGET ${beautify_target})
            add_custom_target(${beautify_target})
            add_dependencies(beautify ${beautify_target})
        endif()
        add_custom_command(TARGET ${beautify_target} POST_BUILD
            COMMAND clang-format -i ${ARGN}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endfunction()

set(target libc_wrap)

add_app_libraries($<TARGET_FILE:${target}>)
add_library(${target} STATIC ${dummy_c_file})
target_link_libraries(libc_wrap PRIVATE ${libc_file_name} ${libm_file_name} ${libgcc_file_name})

target_sources(${target} PUBLIC
    c_wrap.c
)




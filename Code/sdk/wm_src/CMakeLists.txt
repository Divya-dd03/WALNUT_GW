# Define a target library named wm_lib_A
set(target _wmsrc)

# Add the target library to the application's libraries
add_app_libraries($<TARGET_FILE:${target}>)

# Create a static library named wm_lib_A with its source directory set to the current binary directory
add_library(${target} STATIC ${CMAKE_CURRENT_BINARY_DIR})

# Set the output directory for the compiled archive (.a or .lib)
set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${out_lib_dir})

# Include the current binary directory as a public include path for other targets using _wmsrc
target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

# Rename the output file of the target library to wm_libsrc
set_target_properties(${target} PROPERTIES OUTPUT_NAME _wmsrc)

# Link library dependencies 
target_include_directories(${target} PUBLIC ../components/mbedtls/include/mbedtls)
target_link_libraries(${target} PRIVATE zlibstatic libc_wrap)
target_link_libraries(${target} PUBLIC ${SOURCE_TOP_DIR}/wm_src/wm_lib/lib_wmsrc.a)
target_link_libraries(${target} PUBLIC ${SOURCE_TOP_DIR}/wm_src/wm_lib/lib_wmsrc_B.a)
target_link_libraries(${target} PUBLIC ${SOURCE_TOP_DIR}/wm_src/wm_lib/libhttpclient.a)
target_link_libraries(${target} PUBLIC ${SOURCE_TOP_DIR}/wm_src/wm_lib/libjson.a)
target_link_libraries(${target} PUBLIC ${SOURCE_TOP_DIR}/wm_src/wm_lib/libwm_mqtt.a)
# Add source files to the target library

#Demo APP inclusion
target_sources(${target} PRIVATE
    wm_lib/global/wm_global.c

    # if demo app needs to be included or excluded
    # wm_ui_app/wm_ui_app.c
    # wm_ui_app/wm_ui_https.c          # Exxclude files if HTTP DEMO is not required
    # wm_ui_app/wm_ui_ota.c           # Exxclude files if OTA DEMO is not required
    # wm_ui_app/wm_ui_tcp.c           # Exxclude files if TCP DEMO is not required
    # wm_ui_app/wm_ui_mqtt.c          # Exxclude files if MQTT is not required
    # wm_ui_app/wm_demo_certs.c       # Exxclude files if demo certs are not required

    # if weware app needs to be included or excluded
    #config
    weware_app/src/config/config.c
    #system infra
    weware_app/src/common/utils.c
    weware_app/src/common/event_manager.c
    weware_app/src/common/queue_manager.c
    weware_app/src/common/state_timeout.c
    weware_app/src/common/task_stats.c
    # module framework
    weware_app/src/module/module_manager.c
    weware_app/src/module/module_config.c
    # log
    weware_app/src/module/log/log_manager.c
    weware_app/src/module/log/log_config.c
    # network
    weware_app/src/module/network/network.c
    weware_app/src/module/network/network_config.c
    # sim
    weware_app/src/module/sim/sim.c
    # tcp
    weware_app/src/module/tcp/tcp.c
    weware_app/src/module/tcp/tcp_ops.c
    weware_app/src/module/tcp/tcp_config.c
    weware_app/src/module/tcp/login_packet.c
    weware_app/src/module/tcp/sdk_platform_tcp.c
    weware_app/src/module/tcp/sdk_walnut_tcp.c
    weware_app/src/module/tcp/sdk_walnut_socket_poll.c
    # gps
    weware_app/src/module/gps/gps_manager.c
    weware_app/src/module/gps/gps_ops.c
    weware_app/src/module/gps/gps_triggers.c
    weware_app/src/module/gps/gps_config.c
    weware_app/src/module/gps/gps_storage.c
    weware_app/src/module/gps/gps_packet.c
    # urc
    weware_app/src/module/urc/urc_processor.c
    # system framework
    weware_app/src/system/system_manager.c
    weware_app/src/system/system_config.c
    # https, ota
    weware_app/src/system/ota/sdk_functionality_https.c
    weware_app/src/system/ota/sdk_walnut_https.c
    weware_app/src/system/ota/ota_file_download.c
    weware_app/src/system/ota/ota_manager.c
    # flash - storage
    weware_app/src/system/storage/file_system.c
    weware_app/src/system/storage/flash_paths.c
    # power, reset and preboot handlers
    weware_app/src/system/reset/pre_boot_handler.c
    weware_app/src/system/reset/post_boot_handler.c
    weware_app/src/system/reset/reset_handler.c
    # adc
    weware_app/src/system/adc/adc_manager.c
    # gpio
    weware_app/src/system/gpio/gpio_manager.c
    #system vehicle state
    weware_app/src/system/vehicle_state.c
    # system utils
    weware_app/src/system/device_utils.c
    weware_app/src/system/time_utils.c
)
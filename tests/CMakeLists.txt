function(add_core_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE app_core_framework)
	enable_linux_api(${test_name})
    enable_project_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_core_test(test_channel test_channel.c)
add_core_test(test_channel_stress test_channel_stress.c)
add_core_test(test_event test_event.c)
add_core_test(test_event_shutdown test_event_shutdown.c)
add_core_test(test_lifecycle test_lifecycle.c)
add_core_test(test_lifecycle_failure test_lifecycle_failure.c)
add_core_test(test_lifecycle_start_failure test_lifecycle_start_failure.c)
add_core_test(test_lifecycle_optional test_lifecycle_optional.c)
add_core_test(test_log_types test_log_types.c)
add_core_test(test_service test_service.c)
add_core_test(test_thread_runtime test_thread_runtime.c)

add_executable(test_camera_rollback
    test_camera_rollback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/features/camera_mod.c
)
target_include_directories(test_camera_rollback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/features
)
target_link_libraries(test_camera_rollback PRIVATE app_core_framework)
target_link_libraries(test_camera_rollback PRIVATE app_module_config)
enable_linux_api(test_camera_rollback)
enable_project_warnings(test_camera_rollback)
add_test(NAME test_camera_rollback COMMAND test_camera_rollback)

if(TARGET app_core)
	add_test(NAME app_smoke COMMAND app_core)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME test_manifest COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_manifest.py)

if(APP_CORE_TEST_INSTALL_PACKAGE)
	configure_file(test_install.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake" @ONLY)
	add_test(NAME test_install_package COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/test_install.cmake")
endif()

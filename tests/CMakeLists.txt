function(add_core_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE app_core_framework)
    enable_project_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_core_test(test_channel test_channel.c)
add_core_test(test_channel_stress test_channel_stress.c)
add_core_test(test_event test_event.c)
add_core_test(test_lifecycle test_lifecycle.c)
add_core_test(test_lifecycle_failure test_lifecycle_failure.c)
add_core_test(test_service test_service.c)

add_executable(test_camera_rollback
    test_camera_rollback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/features/camera_mod.c
)
target_include_directories(test_camera_rollback PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../app
    ${CMAKE_CURRENT_SOURCE_DIR}/../app/features
)
target_link_libraries(test_camera_rollback PRIVATE app_core_framework)
enable_project_warnings(test_camera_rollback)
add_test(NAME test_camera_rollback COMMAND test_camera_rollback)

add_test(NAME app_smoke COMMAND app_core)

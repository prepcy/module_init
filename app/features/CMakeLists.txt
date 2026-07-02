# app/features/CMakeLists.txt

# 设置当前特性的公共头文件包含路径，以防 main.c 报头文件缺失
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 核心防线：只有在配置宏开启时，才将对应的 .c 文件送入编译器
if(CONFIG_ENABLE_WIFI)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/wifi_mod.c)
endif()

if(CONFIG_ENABLE_CAMERA)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/camera_mod.c)
endif()

if(CONFIG_ENABLE_GPS)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gps_module.c)
endif()

if(CONFIG_ENABLE_IMU)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imu_module.c)
endif()


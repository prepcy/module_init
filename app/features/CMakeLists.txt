target_include_directories(app_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CONFIG_ENABLE_WIFI)
    target_sources(app_core PRIVATE wifi_mod.c)
endif()

if(CONFIG_ENABLE_CAMERA)
    target_sources(app_core PRIVATE camera_mod.c)
endif()

if(CONFIG_ENABLE_GPS)
    target_sources(app_core PRIVATE gps_mod.c)
endif()

if(CONFIG_ENABLE_IMU)
    target_sources(app_core PRIVATE imu_mod.c)
endif()

if(CONFIG_ENABLE_ZLMEDIA)
    target_sources(app_core PRIVATE zlmedia_mod.c)
endif()

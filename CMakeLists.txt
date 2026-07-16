cmake_minimum_required(VERSION 3.16)
project(app_core VERSION 2.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(tools/ParseConfig.cmake)
set(APP_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.config" CACHE FILEPATH
    "Kconfig-style application configuration file; leave empty for direct -D overrides")
if(APP_CONFIG_FILE)
    load_app_config("${APP_CONFIG_FILE}")
endif()

option(CONFIG_ENABLE_WIFI "Enable WiFi module" ON)
option(CONFIG_ENABLE_CAMERA "Enable Camera module" ON)
option(CONFIG_ENABLE_GPS "Enable GPS module" OFF)
option(CONFIG_ENABLE_IMU "Enable IMU module" OFF)
option(CONFIG_ENABLE_ZLMEDIA "Enable ZLMedia module" ON)

if(CONFIG_ENABLE_ZLMEDIA AND NOT CONFIG_ENABLE_CAMERA)
    message(FATAL_ERROR "CONFIG_ENABLE_ZLMEDIA requires CONFIG_ENABLE_CAMERA")
endif()

find_package(Threads REQUIRED)

function(enable_project_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wshadow
        -Wformat=2
        -Wstrict-prototypes
        -Wmissing-prototypes
    )
endfunction()

add_library(app_core_framework STATIC
    core/sys_channel.c
    core/sys_component.c
    core/sys_core.c
    core/sys_event.c
    core/sys_service.c
)

target_include_directories(app_core_framework
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_compile_definitions(app_core_framework PUBLIC _POSIX_C_SOURCE=200809L)
target_link_libraries(app_core_framework PUBLIC Threads::Threads)
enable_project_warnings(app_core_framework)

add_executable(app_core app/main.c)
target_include_directories(app_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/app/features
)
target_link_libraries(app_core PRIVATE app_core_framework)
enable_project_warnings(app_core)

add_subdirectory(app/features)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET app_core_framework app_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO/LTO is not supported: ${ipo_error}")
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

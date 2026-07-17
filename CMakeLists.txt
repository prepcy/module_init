cmake_minimum_required(VERSION 3.16)
project(AppCore VERSION 3.0.0 LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(APP_CORE_BUILD_EXAMPLE "Build the App-Core example application" ON)
option(APP_CORE_TEST_INSTALL_PACKAGE "Test the installed CMake package" ON)

include(tools/ParseConfig.cmake)
set(APP_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/.config" CACHE FILEPATH
    "Manifest-generated application configuration file; leave empty for direct -D overrides")
if(APP_CONFIG_FILE)
    load_app_config("${APP_CONFIG_FILE}")
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

function(enable_linux_api target_name)
    target_compile_definitions(${target_name} PRIVATE _GNU_SOURCE _POSIX_C_SOURCE=200809L)
endfunction()

set(APP_CORE_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/include/app_core")
file(MAKE_DIRECTORY "${APP_CORE_GENERATED_INCLUDE_DIR}")
configure_file(core/sys_version.h.in "${APP_CORE_GENERATED_INCLUDE_DIR}/sys_version.h" @ONLY)

add_library(app_core_framework STATIC
    core/sys_channel.c
    core/sys_component.c
    core/sys_core.c
    core/sys_event.c
    core/sys_log.c
    core/sys_runtime.c
    core/sys_service.c
    core/sys_thread.c
    core/sys_types.c
)

target_include_directories(app_core_framework
    PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
		$<BUILD_INTERFACE:${APP_CORE_GENERATED_INCLUDE_DIR}>
		$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/app_core>
)
set_target_properties(app_core_framework PROPERTIES EXPORT_NAME framework)
target_compile_features(app_core_framework PUBLIC c_std_11)
enable_linux_api(app_core_framework)
target_link_libraries(app_core_framework PUBLIC Threads::Threads)
enable_project_warnings(app_core_framework)

if(APP_CORE_BUILD_EXAMPLE)
    add_executable(app_core app/main.c)
    target_include_directories(app_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app/features
    )
    target_link_libraries(app_core PRIVATE app_core_framework)
    enable_linux_api(app_core)
    enable_project_warnings(app_core)
endif()

add_subdirectory(app/features)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
		set_property(TARGET app_core_framework PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
		if(TARGET app_core)
			set_property(TARGET app_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
		endif()
    else()
        message(WARNING "IPO/LTO is not supported: ${ipo_error}")
    endif()
endif()

set(APP_CORE_PUBLIC_HEADERS
    core/sys_channel.h
    core/sys_component.h
    core/sys_core.h
    core/sys_event.h
    core/sys_log.h
    core/sys_runtime.h
    core/sys_service.h
    core/sys_thread.h
    core/sys_types.h
)

install(TARGETS app_core_framework
    EXPORT AppCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/app_core
)
install(FILES ${APP_CORE_PUBLIC_HEADERS} "${APP_CORE_GENERATED_INCLUDE_DIR}/sys_version.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/app_core
)
if(TARGET app_core)
    install(TARGETS app_core RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

configure_package_config_file(
    cmake/AppCoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/AppCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AppCore"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/AppCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(EXPORT AppCoreTargets
    FILE AppCoreTargets.cmake
    NAMESPACE AppCore::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AppCore"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/AppCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/AppCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/AppCore"
)
export(EXPORT AppCoreTargets FILE "${CMAKE_CURRENT_BINARY_DIR}/AppCoreTargets.cmake" NAMESPACE AppCore::)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

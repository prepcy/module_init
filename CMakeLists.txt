cmake_minimum_required(VERSION 3.10)
project(app_core C)

# 导出 compile_commands.json，供 clangd / VS Code 等 IDE 插件用于跳转和补全
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# 设置 C 语言标准为 C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 开启常用警告标志
add_compile_options(-Wall -Wextra)

# 引入 menuconfig 自动解析器
include(tools/ParseConfig.cmake)
load_dvr_build_config("${CMAKE_CURRENT_SOURCE_DIR}/.config")

# 定义应用特性编译配置选项（如果存在 .config，其值会被 menuconfig 设置强制覆盖）
option(CONFIG_ENABLE_WIFI "Enable WiFi Module" ON)
option(CONFIG_ENABLE_CAMERA "Enable Camera Module" ON)
option(CONFIG_ENABLE_GPS "Enable GPS Module" OFF)
option(CONFIG_ENABLE_IMU "Enable IMU Module" OFF)
option(CONFIG_ENABLE_ZLMEDIA "Enable ZLMedia Module" ON)

# 在 Release 模式下开启链接期优化（LTO），优化函数指针间接调用的开销
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_output)
    if(lto_supported)
        message(STATUS "LTO (Link-Time Optimization) is supported and enabled.")
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO is not supported: ${lto_output}")
    endif()
endif()

# 核心与应用层的全局头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/include/generated
)

# 声明主执行程序，关联主入口及解耦的核心机制代码
add_executable(${PROJECT_NAME}
    app/main.c
    core/sys_core.c
)

# 引入功能特性子目录进行编译源文件追加
add_subdirectory(app/features)

# 引入多线程库支持并进行链接保护
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)


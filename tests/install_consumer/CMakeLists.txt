cmake_minimum_required(VERSION 3.16)
project(AppCoreConsumer LANGUAGES C)

find_package(AppCore 3 REQUIRED)

add_executable(app_core_consumer main.c)
target_link_libraries(app_core_consumer PRIVATE AppCore::framework)

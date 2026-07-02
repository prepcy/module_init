# Parse .config style key/value lines and seed CMake cache defaults.
# Supported lines:
#   CONFIG_FOO=value
#   # CONFIG_FOO is not set

function(_cfg_to_cmake_bool out_var raw_value)
    string(STRIP "${raw_value}" _val)
    string(TOUPPER "${_val}" _val_upper)
    if(_val_upper STREQUAL "Y" OR
       _val_upper STREQUAL "YES" OR
       _val_upper STREQUAL "ON" OR
       _val_upper STREQUAL "TRUE" OR
       _val_upper STREQUAL "1")
        set(${out_var} "ON" PARENT_SCOPE)
    else()
        set(${out_var} "OFF" PARENT_SCOPE)
    endif()
endfunction()

function(_cfg_unquote out_var raw_value)
    string(STRIP "${raw_value}" _val)
    if(_val MATCHES "^\"(.*)\"$")
        set(_val "${CMAKE_MATCH_1}")
    endif()
    set(${out_var} "${_val}" PARENT_SCOPE)
endfunction()

function(_cfg_set_cache_force cache_key cache_type cache_value)
    set(${cache_key} "${cache_value}" CACHE ${cache_type} "Seeded from .config" FORCE)
endfunction()

function(_cfg_apply_mapping config_key config_value)
    _cfg_unquote(_clean_value "${config_value}")
    _cfg_to_cmake_bool(_bool_val "${config_value}")

    # 1. 自动、动态地将任何 CONFIG_XXX 变量注入到 CMake Cache 中 (使用 FORCE 覆盖粘性缓存)
    string(STRIP "${config_value}" _val)
    string(TOUPPER "${_val}" _val_upper)
    if(_val_upper MATCHES "^(Y|YES|ON|TRUE|1|N|NO|OFF|FALSE|0)$")
        _cfg_set_cache_force("${config_key}" BOOL "${_bool_val}")
    else()
        _cfg_set_cache_force("${config_key}" STRING "${_clean_value}")
    endif()

    # 2. 特殊别名与 CMake 内置变量兼容映射（向下兼容原有宏与传统名字）
    if(config_key STREQUAL "CONFIG_TARGET_HDVR" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(TARGET_PROJECT STRING "hdvr")
    elseif(config_key STREQUAL "CONFIG_TARGET_YDVR" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(TARGET_PROJECT STRING "ydvr")
    elseif(config_key STREQUAL "CONFIG_BUILD_RELEASE" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(CMAKE_BUILD_TYPE STRING "Release")
    elseif((config_key STREQUAL "CONFIG_BUILD_DEBUG" OR config_key STREQUAL "CONFIG_DEBUG_MODE") AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(CMAKE_BUILD_TYPE STRING "Debug")
    elseif(config_key STREQUAL "CONFIG_MCC_BOARD_1" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(MCC_BOARD STRING "BOARD_1")
    elseif(config_key STREQUAL "CONFIG_MCC_BOARD_2" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(MCC_BOARD STRING "BOARD_2")
    elseif(config_key STREQUAL "CONFIG_MCC_BOARD_3" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(MCC_BOARD STRING "BOARD_3")
    elseif(config_key STREQUAL "CONFIG_HDVR_SOC_MIX410" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(HDVR_SOC_PLATFORM STRING "mix410")
    elseif(config_key STREQUAL "CONFIG_HDVR_SOC_MIX210" AND _bool_val STREQUAL "ON")
        _cfg_set_cache_force(HDVR_SOC_PLATFORM STRING "mix210")

    # 去除 CONFIG_ 前缀的同名传统变量兼容（如 CONFIG_PLATFORM -> CONFIG_PLATFORM，以及部分旧宏）
    elseif(config_key MATCHES "^CONFIG_(BUILD_SHARED_LIBS|ENABLE_ASAN|GENERATE_COMPILE_COMMANDS|VERBOSE_BUILD|PLATFORM|WEBCAM|AI)$")
        string(REPLACE "CONFIG_" "" _target_name "${config_key}")
        if(_target_name STREQUAL "VERBOSE_BUILD")
            set(_target_name "CMAKE_VERBOSE_MAKEFILE")
        endif()
        _cfg_set_cache_force("${_target_name}" BOOL "${_bool_val}")

    # 打包选项变量兼容（去除 CONFIG_ 前缀）
    elseif(config_key MATCHES "^CONFIG_(HDVR_ENABLE_EXT4_PACKAGING|HDVR_ENABLE_UBI_PACKAGING)$")
        string(REPLACE "CONFIG_" "" _target_name "${config_key}")
        _cfg_set_cache_force("${_target_name}" BOOL "${_bool_val}")

    elseif(config_key MATCHES "^CONFIG_(HDVR_EXT4_IMAGE_SIZE_MB|HDVR_UBI_PEB_SIZE|HDVR_UBI_MIN_IO_SIZE|HDVR_UBI_SUB_PAGE_SIZE|HDVR_UBI_LEB_SIZE|HDVR_UBI_MAX_LEB_CNT|HDVR_UBI_VOLUME_NAME)$")
        string(REPLACE "CONFIG_" "" _target_name "${config_key}")
        # 兼容 ext4 大小的特殊 0 和 n 处理
        if(_target_name STREQUAL "HDVR_EXT4_IMAGE_SIZE_MB")
            string(TOUPPER "${_clean_value}" _ext4_size_upper)
            if(NOT _clean_value STREQUAL "" AND NOT _ext4_size_upper STREQUAL "N" AND NOT _ext4_size_upper STREQUAL "OFF")
                _cfg_set_cache_force(HDVR_EXT4_IMAGE_SIZE_MB STRING "${_clean_value}")
            endif()
        else()
            _cfg_set_cache_force("${_target_name}" STRING "${_clean_value}")
        endif()
    endif()
endfunction()

function(load_dvr_build_config config_file)
    if(NOT EXISTS "${config_file}")
        return()
    endif()

    message(STATUS "Loading optional build config: ${config_file}")

    file(STRINGS "${config_file}" _cfg_lines)
    foreach(_line IN LISTS _cfg_lines)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "")
            continue()
        endif()

        if(_line MATCHES "^# (CONFIG_[A-Za-z0-9_]+) is not set$")
            _cfg_apply_mapping("${CMAKE_MATCH_1}" "n")
            continue()
        endif()

        if(_line MATCHES "^(CONFIG_[A-Za-z0-9_]+)=(.*)$")
            _cfg_apply_mapping("${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
        endif()
    endforeach()

    # 动态生成 C/C++ 宏定义的预处理头文件
    generate_autoconf_header("${CMAKE_BINARY_DIR}/include/generated/autoconf.h")
endfunction()

function(generate_autoconf_header output_file)
    set(_header_content "/* Automatically generated by CMake. Do not edit. */\n#pragma once\n\n")
    
    get_cmake_property(_vars VARIABLES)
    list(SORT _vars)
    
    foreach(_var IN LISTS _vars)
        if(_var MATCHES "^CONFIG_[A-Za-z0-9_]+$")
            set(_val "${${_var}}")
            if(_val STREQUAL "ON" OR _val STREQUAL "1" OR _val STREQUAL "TRUE" OR _val STREQUAL "YES")
                string(APPEND _header_content "#define ${_var} 1\n")
            elseif(_val STREQUAL "OFF" OR _val STREQUAL "0" OR _val STREQUAL "FALSE" OR _val STREQUAL "NO")
                string(APPEND _header_content "#undef ${_var}\n")
            else()
                if(_val MATCHES "^\".*\"$")
                    string(APPEND _header_content "#define ${_var} ${_val}\n")
                else()
                    string(APPEND _header_content "#define ${_var} \"${_val}\"\n")
                endif()
            endif()
        endif()
    endforeach()

    # Mix210 runs a 64-bit user-space SDK. The shared vendor headers still
    # include the Mix410 autoconf.h, so derive this ABI macro from the selected
    # platform to keep td_phys_addr_t and ioctl structs aligned with libss_mpi.a.
    if(HDVR_SOC_PLATFORM STREQUAL "mix210" OR CONFIG_HDVR_SOC_MIX210)
        string(APPEND _header_content "#define CONFIG_PHYS_ADDR_BIT_WIDTH_64 1\n")
    else()
        string(APPEND _header_content "#undef CONFIG_PHYS_ADDR_BIT_WIDTH_64\n")
    endif()

    set(_write_file TRUE)
    if(EXISTS "${output_file}")
        file(READ "${output_file}" _existing_content)
        if(_existing_content STREQUAL _header_content)
            set(_write_file FALSE)
        endif()
    endif()
    
    if(_write_file)
        get_filename_component(_dir "${output_file}" DIRECTORY)
        file(MAKE_DIRECTORY "${_dir}")
        file(WRITE "${output_file}" "${_header_content}")
        message(STATUS "Generated autoconf header: ${output_file}")
    endif()
endfunction()

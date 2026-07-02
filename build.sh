#!/bin/bash

# 一键编译脚本
# 若任意步骤出错，则立即停止运行
set -e

# 获取当前脚本所在目录作为工作根路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"

# 默认编译配置
ENABLE_WIFI="ON"
ENABLE_CAMERA="ON"
ENABLE_GPS="OFF"
ENABLE_IMU="OFF"
CLEAN_BUILD=false

# 帮助信息输出函数
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "参数选项:"
    echo "  -w, --wifi <on|off>      是否编译 WiFi 模块 (默认: on)"
    echo "  -c, --camera <on|off>    是否编译 Camera 模块 (默认: on)"
    echo "  -g, --gps <on|off>       是否编译 GPS 模块 (默认: off)"
    echo "  -i, --imu <on|off>       是否编译 IMU 模块 (默认: off)"
    echo "  --clean                  编译前清空 build 构建目录"
    echo "  -h, --help               显示本帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 --wifi off --camera on    # 禁用 WiFi 编译，启用 Camera 编译"
    echo "  $0 --clean --gps on          # 清空构建历史并启用 GPS 编译"
    exit 0
}

# 转换参数值为 CMake 的布尔开关 (ON/OFF)
parse_bool() {
    case "$(echo "$1" | tr '[:upper:]' '[:lower:]')" in
        on|yes|true|1) echo "ON" ;;
        off|no|false|0) echo "OFF" ;;
        *)
            echo "错误: 无效参数值 '$1'，必须为 on 或 off" >&2
            exit 1
            ;;
    esac
}

# 检查选项后面是否有缺失的参数值
check_arg() {
    if [ -z "$2" ] || [[ "$2" == -* ]]; then
        echo "错误: 选项 $1 后面必须跟上具体参数值 (on 或 off)" >&2
        exit 1
    fi
}

# 循环解析命令行参数
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        -w|--wifi)
            check_arg "$1" "$2"
            ENABLE_WIFI=$(parse_bool "$2")
            shift 2 ;;
        -c|--camera)
            check_arg "$1" "$2"
            ENABLE_CAMERA=$(parse_bool "$2")
            shift 2 ;;
        -g|--gps)
            check_arg "$1" "$2"
            ENABLE_GPS=$(parse_bool "$2")
            shift 2 ;;
        -i|--imu)
            check_arg "$1" "$2"
            ENABLE_IMU=$(parse_bool "$2")
            shift 2 ;;
        --clean)
            CLEAN_BUILD=true
            shift ;;
        -h|--help)
            show_help ;;
        *)
            echo "错误: 未知的命令行参数选项 '$1'" >&2
            show_help
            exit 1 ;;
    esac
done

# 如果指定了 --clean，删除原构建目录及根目录的链接文件
if [ "$CLEAN_BUILD" = true ]; then
    echo "🧹 正在清理历史构建产物..."
    rm -rf "${BUILD_DIR}"
    rm -f "${SCRIPT_DIR}/compile_commands.json"
fi

# 创建并进入构建目录
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "============================================="
echo "⚙️  配置 CMake 编译参数:"
echo "   - CONFIG_ENABLE_WIFI   = ${ENABLE_WIFI}"
echo "   - CONFIG_ENABLE_CAMERA = ${ENABLE_CAMERA}"
echo "   - CONFIG_ENABLE_GPS    = ${ENABLE_GPS}"
echo "   - CONFIG_ENABLE_IMU    = ${ENABLE_IMU}"
echo "============================================="

# 运行 cmake 并配置相应的功能模块
cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCONFIG_ENABLE_WIFI=${ENABLE_WIFI} \
    -DCONFIG_ENABLE_CAMERA=${ENABLE_CAMERA} \
    -DCONFIG_ENABLE_GPS=${ENABLE_GPS} \
    -DCONFIG_ENABLE_IMU=${ENABLE_IMU} \
    "${SCRIPT_DIR}"

# 执行多线程编译
echo "🔨 正在编译源文件..."
make -j$(nproc 2>/dev/null || echo 4)

# 创建 compile_commands.json 软链接到项目根目录，供 clangd / VS Code 用于 IDE 补全与跳转
if [ -f "compile_commands.json" ]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${SCRIPT_DIR}/compile_commands.json"
    echo "🔗 已建立 compile_commands.json 到项目根目录的软链接。"
fi

echo "============================================="
echo "🎉 编译完成！可直接运行以下命令执行程序："
echo "   ./build/app_core"
echo "============================================="

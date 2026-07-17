#!/usr/bin/env bash

# 一键编译脚本
# 若任意步骤出错，则立即停止运行
set -euo pipefail

# 获取当前脚本所在目录作为工作根路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/output"
CONFIG_FILE="${SCRIPT_DIR}/.config"
MENUCONFIG_SCRIPT="${SCRIPT_DIR}/tools/menuconfig.py"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# 帮助信息输出函数
show_help() {
    echo "用法: $0 [命令/选项]"
    echo ""
    echo "命令参数:"
    echo "  menuconfig                    开启图形化配置界面"
    echo "  load [FILE]                   从文件导入配置；缺省时根据模块清单生成默认配置"
    echo "  save [FILE]                   导出当前配置；缺省文件为 app.config.backup"
    echo "  clean                         清空 build 构建目录"
    echo "  debug                         使用 Debug 模式构建"
    echo "  release                       使用 Release + LTO 模式构建"
    echo "  -h, --help                    显示本帮助信息"
    echo ""
    echo "说明:"
    echo "  直接运行 $0 将基于当前根目录下的 .config 配置文件进行增量编译构建。"
    exit 0
}

# 运行 menuconfig
run_menuconfig() {
    if [ ! -f "${MENUCONFIG_SCRIPT}" ]; then
        echo "错误: 未找到 menuconfig 脚本: ${MENUCONFIG_SCRIPT}" >&2
        exit 1
    fi
    if ! command -v python3 &>/dev/null; then
        echo "错误: 未找到 python3，无法启动 menuconfig。" >&2
        exit 1
    fi
    python3 "${MENUCONFIG_SCRIPT}" --config "${CONFIG_FILE}"
    exit 0
}

# 执行清理
run_clean() {
    echo "[CLEAN] 正在清理历史构建产物..."
    rm -rf "${BUILD_DIR}"
    rm -f "${SCRIPT_DIR}/compile_commands.json"
    echo "[CLEAN] 清理完毕。"
}

# 载入配置
run_load() {
    local src_file="$1"
    if [ -z "${src_file}" ]; then
        python3 "${MENUCONFIG_SCRIPT}" --write-default "${CONFIG_FILE}"
        echo "[LOAD] 已根据模块清单生成默认配置"
        exit 0
    fi
    if [ ! -f "${src_file}" ]; then
        echo "错误: 找不到配置文件 '${src_file}'" >&2
        exit 1
    fi
    cp "${src_file}" "${CONFIG_FILE}"
    echo "[LOAD] 已导入配置: ${src_file} -> .config"
    exit 0
}

# 保存配置
run_save() {
    local dest_file="$1"
    if [ -z "${dest_file}" ]; then
        dest_file="${SCRIPT_DIR}/app.config.backup"
    fi
    if [ ! -f "${CONFIG_FILE}" ]; then
        echo "错误: 当前根目录下不存在 .config，无法保存" >&2
        exit 1
    fi
    cp "${CONFIG_FILE}" "${dest_file}"
    echo "[SAVE] 已保存配置: .config -> ${dest_file}"
    exit 0
}

# 默认行为：若没有命令行参数，直接执行编译；若有，进行命令解析
if [ "$#" -gt 0 ]; then
    case "$1" in
        menuconfig|--menuconfig)
            run_menuconfig ;;
        clean|--clean)
            run_clean
            exit 0 ;;
        load|--load)
            run_load "${2:-}" ;;
        save|--save)
            run_save "${2:-}" ;;
        debug|--debug)
            BUILD_TYPE="Debug" ;;
        release|--release)
            BUILD_TYPE="Release" ;;
        -h|--help)
            show_help ;;
        *)
            echo "错误: 未知的命令行参数或命令 '$1'" >&2
            show_help
            exit 1 ;;
    esac
fi

# 编译前检查：如果 .config 不存在，则根据模块清单生成默认配置
if [ ! -f "${CONFIG_FILE}" ]; then
    echo "[CONFIG] 根据模块清单生成默认配置..."
    python3 "${MENUCONFIG_SCRIPT}" --write-default "${CONFIG_FILE}"
fi

# 打印编译信息
echo "============================================="
echo "[CONFIG] 正在加载 .config 进行 CMake 配置..."
echo "============================================="

# 运行 CMake，配置参数由 .config 和构建模式共同决定
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

# 执行多线程编译
echo "[BUILD] 正在编译源文件..."
cmake --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || echo 4)"

# 创建 compile_commands.json 软链接到项目根目录，供 clangd / VS Code 用于 IDE 补全与跳转
if [ -f "${BUILD_DIR}/compile_commands.json" ]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${SCRIPT_DIR}/compile_commands.json"
    echo "[LINK] 已建立 compile_commands.json 到项目根目录的软链接。"
fi

echo "============================================="
echo "[SUCCESS] 编译完成！可直接运行以下命令执行程序："
echo "   ./output/app_core"
echo "============================================="

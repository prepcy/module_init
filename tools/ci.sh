#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CI_ROOT="${CI_ROOT:-/tmp/app-core-ci}"
PROFILE="${1:-quick}"

run_build() {
    local name="$1"
    local compiler="$2"
    local build_type="$3"
    shift 3

    local build_dir="${CI_ROOT}/${name}"
    rm -rf "${build_dir}"
    cmake -S "${ROOT_DIR}" -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DCMAKE_C_COMPILER="${compiler}" \
        -DBUILD_TESTING=ON \
        "$@"
    cmake --build "${build_dir}" --parallel
    ctest --test-dir "${build_dir}" --output-on-failure
}

run_sanitizer() {
    local name="$1"
    local sanitizer="$2"
    local build_dir="${CI_ROOT}/${name}"
    local flags="-fsanitize=${sanitizer} -fno-omit-frame-pointer -O1 -g"

    rm -rf "${build_dir}"
    cmake -S "${ROOT_DIR}" -B "${build_dir}" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_COMPILER=/usr/bin/gcc \
        -DCMAKE_C_FLAGS="${flags}" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=${sanitizer}" \
        -DAPP_CORE_TEST_INSTALL_PACKAGE=OFF \
        -DBUILD_TESTING=ON
    cmake --build "${build_dir}" --parallel
    ctest --test-dir "${build_dir}" --output-on-failure
}

expect_configure_failure() {
    local name="$1"
    shift

    local build_dir="${CI_ROOT}/${name}"
    rm -rf "${build_dir}"
    if cmake -S "${ROOT_DIR}" -B "${build_dir}" "$@"; then
        echo "configuration unexpectedly succeeded: ${name}" >&2
        exit 1
    fi
}

find_working_clang() {
    local candidate

    for candidate in /usr/bin/clang "$(command -v clang 2>/dev/null || true)"; do
        if [[ -n "${candidate}" && -x "${candidate}" ]] && "${candidate}" --version >/dev/null 2>&1; then
            echo "${candidate}"
            return 0
        fi
    done
    return 1
}

run_build gcc-debug /usr/bin/gcc Debug
if [[ "${PROFILE}" == "quick" ]]; then
    exit 0
fi
if [[ "${PROFILE}" != "full" ]]; then
    echo "usage: $0 [quick|full]" >&2
    exit 2
fi

run_build gcc-release /usr/bin/gcc Release
run_build all-modules /usr/bin/gcc Debug -DAPP_CONFIG_FILE= \
    -DCONFIG_ENABLE_WIFI=ON -DCONFIG_ENABLE_GPS=ON -DCONFIG_ENABLE_IMU=ON \
    -DCONFIG_ENABLE_CAMERA=ON -DCONFIG_ENABLE_ZLMEDIA=ON
run_build minimal /usr/bin/gcc Debug -DAPP_CONFIG_FILE= \
    -DCONFIG_ENABLE_WIFI=OFF -DCONFIG_ENABLE_GPS=OFF -DCONFIG_ENABLE_IMU=OFF \
    -DCONFIG_ENABLE_CAMERA=OFF -DCONFIG_ENABLE_ZLMEDIA=OFF
expect_configure_failure invalid-dependency -DAPP_CONFIG_FILE= \
    -DCONFIG_ENABLE_CAMERA=OFF -DCONFIG_ENABLE_ZLMEDIA=ON
run_build library-only /usr/bin/gcc Release -DAPP_CORE_BUILD_EXAMPLE=OFF

if clang_compiler="$(find_working_clang)"; then
    clang_tools=()
    if [[ -x /usr/bin/llvm-ar-10 && -x /usr/bin/llvm-ranlib-10 ]]; then
        clang_tools=(-DCMAKE_AR=/usr/bin/llvm-ar-10 -DCMAKE_RANLIB=/usr/bin/llvm-ranlib-10)
    fi
    run_build clang-debug "${clang_compiler}" Debug "${clang_tools[@]}"
fi

run_sanitizer asan-ubsan address,undefined
run_sanitizer tsan thread

if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    rm -rf "${CI_ROOT}/arm-release"
    cmake -S "${ROOT_DIR}" -B "${CI_ROOT}/arm-release" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$(command -v arm-linux-gnueabihf-gcc)" \
        -DAPP_CORE_BUILD_EXAMPLE=ON \
        -DBUILD_TESTING=OFF
    cmake --build "${CI_ROOT}/arm-release" --parallel
fi

if command -v cppcheck >/dev/null 2>&1; then
    cppcheck --enable=warning,performance,portability --error-exitcode=1 \
        --std=c11 --suppress=missingIncludeSystem "${ROOT_DIR}/core" "${ROOT_DIR}/app"
fi

if [[ -x /usr/bin/clang-tidy ]]; then
    /usr/bin/clang-tidy -p "${CI_ROOT}/gcc-debug" \
        "${ROOT_DIR}/core/sys_channel.c" \
        "${ROOT_DIR}/core/sys_component.c" \
        "${ROOT_DIR}/core/sys_core.c" \
        "${ROOT_DIR}/core/sys_event.c" \
        "${ROOT_DIR}/core/sys_log.c" \
        "${ROOT_DIR}/core/sys_runtime.c" \
        "${ROOT_DIR}/core/sys_service.c" \
        "${ROOT_DIR}/core/sys_thread.c" \
        "${ROOT_DIR}/core/sys_types.c" \
        "${ROOT_DIR}/app/main.c" \
        "${ROOT_DIR}/app/features/camera_mod.c" \
        "${ROOT_DIR}/app/features/gps_mod.c" \
        "${ROOT_DIR}/app/features/imu_mod.c" \
        "${ROOT_DIR}/app/features/wifi_mod.c" \
        "${ROOT_DIR}/app/features/zlmedia_mod.c"
fi

git -C "${ROOT_DIR}" diff --check

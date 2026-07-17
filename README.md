# App-Core

App-Core 3 是面向 Linux/POSIX C11 应用的模块化底层框架。它借鉴 Linux 内核在分层、静态发现、
依赖管理、引用计数、有界队列和故障回滚方面的设计思想，但只解决用户态应用的工程治理问题，
不实现内核、网络协议栈、文件系统、驱动或 RTOS 兼容层。

## 提供的基础能力

| 领域 | 框架机制 | 主要保证 |
| --- | --- | --- |
| 组件治理 | `sys_component` | `init → start → stop → deinit`、依赖排序、required/optional、逆序回滚 |
| 控制面 | `sys_service` | 模块/接口 ID、ABI 和操作表大小校验、安全注销 |
| 消息面 | `sys_event` | 同步反馈、有界异步队列、ABI 标记、退订屏障和统计 |
| 数据面 | `sys_channel` | 预分配缓冲池、引用计数、零 payload 拷贝、有界背压和丢弃策略 |
| 进程运行时 | `sys_runtime` | `signalfd`/`eventfd`/`poll` 驱动的 SIGINT、SIGTERM 和程序化退出 |
| 并发治理 | `sys_thread` | 线程命名、协作停止、超时 join、运行及未回收句柄统计 |
| 可观测性 | `sys_log`、状态快照 | 统一日志、稳定错误文本、组件/服务/事件/通道统计 |
| 构建治理 | `*.module` 清单 | 模块 ID、配置、源码和依赖的单一构建事实源 |

网络、存储、媒体和设备访问继续复用 Linux 的 socket、epoll、io_uring、文件描述符、标准协议帧及
成熟第三方库。App-Core 负责这些业务模块的生命周期、契约、通信和可诊断性，不复制已有系统能力。

## 快速开始

```bash
./build.sh
ctest --test-dir output --output-on-failure
./output/app_core
```

常用构建入口：

```bash
./build.sh menuconfig
./build.sh release
./tools/ci.sh quick
./tools/ci.sh full
```

## 模块接入

每个应用模块在 `app/features/` 中提供一个清单，例如：

```text
name=example
id=0x0600
config=ENABLE_EXAMPLE
prompt=启用 Example 模块
default=n
sources=example_mod.c
depends=ENABLE_WIFI
```

CMake 和 `menuconfig.py` 会拒绝重复 ID/名称/配置项、未知字段、越界 ID、不存在的源码、未知依赖和
依赖环。稳定模块 ID 发布后不得复用或改义。

## 安装和复用

```bash
cmake -S . -B output -DCMAKE_BUILD_TYPE=Release -DAPP_CORE_BUILD_EXAMPLE=OFF
cmake --build output --parallel
cmake --install output --prefix /opt/app-core
```

下游 CMake 项目：

```cmake
find_package(AppCore 3 REQUIRED)
target_link_libraries(my_app PRIVATE AppCore::framework)
```

详细架构边界见 [docs/architecture.md](docs/architecture.md)，开发与构建标准见
[docs/build.md](docs/build.md)。历史接续信息见 [docs/project-memory.md](docs/project-memory.md)，测试项目和
正式准入说明见 [docs/testing-guide.md](docs/testing-guide.md)。

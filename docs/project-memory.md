# App-Core 项目记忆与接续说明

> 最后更新：2026-07-16
> 工作目录：`/home/loong/demo/module_init`
> 用途：重新打开开发窗口后，优先阅读本文，以快速恢复项目背景、决策、当前状态和验证结论。

## 1. 项目目标

App-Core 是面向 Linux/POSIX C11 应用的通用底层框架，目标是约束多人开发中的模块边界、生命周期、
通信、资源释放、错误处理和测试准入，使网络、存储、媒体、设备和业务模块能够在统一规则下组合。

明确边界：

- 不考虑 RTOS、裸机或 Windows 兼容。
- 不实现 Linux 内核、网络协议栈、文件系统、驱动或调度器。
- 网络和存储业务复用 Linux socket、epoll、io_uring、文件描述符、标准协议帧和成熟第三方库。
- 框架只负责应用层治理：生命周期、通信契约、线程退出、状态、日志和质量准入。
- 代码优先级为：可读性 > 可维护性 > 性能；只有真实基准证明瓶颈后才增加复杂优化。
- 必须支持模块通信、小消息、大流量数据和明确背压。

## 2. 开发操作约束

接续开发时遵守：

- 所有 Shell 命令使用 `rtk` 前缀。
- 文件修改优先使用 `apply_patch`。
- 不重置或覆盖当前未提交改动。
- 使用 C11、pthread 和 Linux/POSIX API。
- 公共 API 使用统一 `sys_err_t`，资源申请和关闭结果必须处理。
- 当前任务未要求提交时，不自动创建 Git commit。

## 3. Git 历史基线

最近三个已提交版本：

```text
e48b1ad feat(core): 重构 Linux C 应用框架
1fcd864 feat(async): 添加锁优化，异步处理框架
f7b54b4 feat(communication): 添加通信框架和数据传输框架
```

`e48b1ad` 是本轮 App-Core 3 改造前的干净基线。当前 App-Core 3 改动仍未提交，禁止通过
`git reset --hard`、`git checkout --` 等方式丢弃。

建议在最终审查后使用类似提交说明：

```text
feat(core): 完善 App-Core 3 应用框架治理与测试体系
```

## 4. 当前架构版本

项目版本已升级为 App-Core 3.0.0，公共版本位于生成的 `sys_version.h`，ABI 主版本为 3。

核心分层：

```text
业务模块
  ├─ sys_service：类型化控制面
  ├─ sys_event：同步/异步消息面
  └─ sys_channel：大流量数据面

应用治理层
  ├─ sys_component：组件依赖和四阶段生命周期
  ├─ sys_runtime：Linux 信号和进程退出
  ├─ sys_thread：受管线程
  ├─ sys_log / sys_types：日志和错误模型
  └─ sys_core：统一启动、关闭和状态聚合

Linux/POSIX 与成熟领域库
```

## 5. 本轮已完成的主要改造

### 5.1 模块清单成为构建事实源

每个模块使用 `app/features/*.module` 声明：

```text
name
id
config
prompt
default
sources
depends
```

CMake 和 `tools/menuconfig.py` 会检查：

- 重复模块 ID、名称和配置名；
- 十六进制/十进制等价值的重复 ID；
- 未知或重复字段；
- 非法、越界 ID；
- 不存在或越出模块目录的源码；
- 未知依赖和依赖环；
- `.config` 中过期或未知的配置项。

旧的 `app/app_modules.h`、`tools/Kconfig` 和 `tools/defconfig` 已删除。模块 ID 头文件由 CMake 自动生成。

### 5.2 四阶段组件生命周期

生命周期为：

```text
init → start → stop → deinit
```

支持：

- CORE、SERVICE、APPLICATION 阶段；
- required/optional 策略；
- 依赖拓扑排序和确定性启动顺序；
- init/start 失败后的逆序回滚；
- stop 错误向 `sys_core_shutdown()` 返回；
- DISCOVERED、INITIALIZING、INITIALIZED、STARTING、RUNNING、STOPPING、STOPPED、FAILED、SKIPPED 状态；
- 组件健康检查和状态快照。

required 失败会终止启动；optional 失败会保留 FAILED/SKIPPED 状态并允许应用降级运行。

### 5.3 通信与大流量数据

- `sys_service`：校验 module ID、interface ID、ABI 版本和操作表大小；注销会等待引用归零。
- `sys_event`：同步错误反馈、异步深拷贝小消息、有界队列、ABI 版本、回调失败和队列统计。
- `sys_channel`：预分配 buffer pool、有界指针队列、payload 零拷贝、引用计数和统计。
- 通道满策略：`SYS_CHANNEL_FULL_FAIL`、`SYS_CHANNEL_FULL_DROP_OLDEST`。
- buffer 契约字段：type、version、generation、sequence、timestamp。

Camera/ZLMedia 示例使用通道传输视频帧。消费者会验证数据类型、格式版本和流 generation，避免使用
旧流或不兼容数据。

### 5.4 运行时、线程和停机

- `sys_runtime` 在创建业务线程前屏蔽 SIGINT/SIGTERM。
- 使用 `signalfd + eventfd + poll` 将信号和程序化退出统一到普通控制流。
- `sys_runtime_destroy()` 必须由创建线程调用并恢复原信号掩码。
- `sys_thread` 提供线程命名、协作式 stop token、超时 join、运行线程数和未 join 句柄数。
- 事件工作线程已经纳入统一线程管理。
- 事件总线关闭会等待同步和异步在途回调结束，避免订阅对象释放后仍被访问。

### 5.5 日志、错误和状态

- `sys_log` 支持 DEBUG、INFO、WARNING、ERROR、自定义 sink、时间、模块、TID、文件和行号。
- `sys_types` 提供稳定错误文本和 errno 映射。
- `sys_core_get_status()` 聚合组件、服务、线程和事件总线状态。
- `sys_channel_get_stats()`、`sys_event_get_stats()`、`sys_service_get_status()` 提供运行快照。

### 5.6 安装与复用

- 项目支持 `APP_CORE_BUILD_EXAMPLE=OFF` 的纯库构建。
- 支持 CMake install/export/package。
- 下游通过 `find_package(AppCore 3 REQUIRED)` 和 `AppCore::framework` 使用。
- 安装消费测试会实际安装头文件和静态库，再配置、编译并运行独立下游程序。

## 6. 当前测试与验证结论

完整准入命令：

```bash
rtk ./tools/ci.sh full
```

最后一次完整执行退出码为 0，验证包括：

- GCC Debug：15/15 测试通过；
- GCC Release：15/15 测试通过；
- Clang Debug：15/15 测试通过；
- 模块全开：15/15 测试通过；
- 模块全关：15/15 测试通过；
- 纯库模式：14/14 测试通过；
- 非法依赖配置：按预期配置失败；
- ASan/UBSan：14/14 测试通过；
- TSan：14/14 测试通过；
- ARM Linux Release 交叉构建：通过；
- cppcheck：通过；
- clang-tidy，`WarningsAsErrors: '*'`：通过；
- `git diff --check`：通过。

Sanitizer 构建关闭安装消费测试，是因为静态库带有 Sanitizer 插桩，而独立消费者默认未链接相同运行库；
这不影响正常安装包测试，普通 GCC/Clang 构建均会执行安装消费测试。

本机 PATH 中 `/opt/xtools/.../clang` 与当前 glibc 不兼容。`tools/ci.sh` 已优先选择可运行的
`/usr/bin/clang`，并在需要时显式指定 `/usr/bin/llvm-ar-10` 和 `/usr/bin/llvm-ranlib-10`。

## 7. 性能判断

当前没有发现结构性的大量性能损耗：

- 服务调用主要增加注册表锁和引用计数，定位为低频控制面。
- 异步事件会分配并复制 payload，因此限制为小消息。
- 大流量通道只传递 buffer 指针，不复制 payload。
- 每次通道发送/接收仍有互斥锁、条件变量和引用计数，不是 lock-free。

现有 `test_channel_stress` 会传输 10,000 个有序 buffer，用于并发正确性和数据完整性，不是正式吞吐
基准。正式项目必须在目标硬件上根据帧大小、队列深度、生产者/消费者数量和 I/O 模型建立性能基线。

## 8. 已知边界与后续事项

框架已经适合作为大多数单进程 Linux C 应用的通用底座，但生产项目仍需按领域补充：

- systemd、watchdog、崩溃转储和自动恢复；
- 配置热更新、持久化、密钥和权限管理；
- Prometheus/OpenTelemetry 等指标导出和生产日志后端；
- seccomp、namespace、多进程或容器故障隔离；
- 网络连接池、协议编解码、数据库迁移和存储一致性模块；
- 长时间 soak、模糊测试、性能基准和真实目标机测试；
- 如基准证明有需要，再增加 SPSC、批量收发或领域专用队列。

不要把这些领域能力继续堆入 `core/`。应以独立模块接入，保持 core 职责窄、接口稳定、行为可测试。

## 9. 下次打开窗口的建议顺序

1. 阅读本文和 `docs/architecture.md`。
2. 执行 `rtk git status --short`，确认 App-Core 3 改动仍在且未被覆盖。
3. 执行 `rtk ./tools/ci.sh quick` 做快速回归。
4. 修改前阅读 `docs/build.md` 和 `docs/testing-guide.md`。
5. 若准备提交，先执行 `rtk ./tools/ci.sh full` 和 `rtk git diff --check`。
6. 审查完整差异后，再按用户指示创建 commit。

## 10. 关键文档与入口

- `README.md`：项目概览和快速使用。
- `docs/architecture.md`：架构边界、Linux 对照和性能模型。
- `docs/build.md`：模块接入、生命周期和开发标准。
- `docs/testing-guide.md`：测试项目、作用和正式准入流程。
- `tools/ci.sh`：本地完整质量矩阵。
- `.github/workflows/ci.yml`：远程 CI 示例。

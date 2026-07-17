# App-Core 测试项目与正式准入说明

## 1. 测试目标

App-Core 测试不是只确认“程序能运行”，而是验证以下架构保证：

- 生命周期顺序确定，失败能够完整回滚；
- required/optional 降级策略符合定义；
- 服务、事件和数据通道的 ABI、所有权和关闭语义正确；
- 并发操作不存在已知数据竞争、死锁和释放后使用；
- 输出参数在失败时保持安全状态；
- 模块配置和安装包可以被独立项目使用；
- GCC、Clang、不同优化级别和 ARM 工具链行为一致。

测试失败时应先修复根因，禁止仅删除断言、延长随机等待时间或屏蔽 Sanitizer 报告。

## 2. 测试入口

日常快速测试：

```bash
rtk ./tools/ci.sh quick
```

手工 Debug 流程：

```bash
rtk cmake -S . -B output -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
rtk cmake --build output --parallel
rtk ctest --test-dir output --output-on-failure
```

提交或发布前完整准入：

```bash
rtk ./tools/ci.sh full
```

只运行单项测试：

```bash
rtk ctest --test-dir output -R test_channel --output-on-failure
```

查看测试清单：

```bash
rtk ctest --test-dir output -N
```

## 3. 单元与集成测试项目

### 3.1 `test_channel`

文件：`tests/test_channel.c`

测试内容：

- 非法缓冲池和通道容量返回 `SYS_ERR_INVALID_PARAM`；
- 创建失败时输出指针被清零；
- buffer acquire、大小设置、发送、接收和释放的完整所有权流程；
- `SYS_CHANNEL_FULL_FAIL` 在队列满时返回 `SYS_ERR_QUEUE_FULL`；
- type、version、generation 数据契约能够随 buffer 正确传递；
- sent、received、full、drop、高水位、当前深度和容量统计正确；
- channel close 后，空队列接收返回 `SYS_ERR_CLOSED`；
- `SYS_CHANNEL_FULL_DROP_OLDEST` 会淘汰最旧数据并保持剩余顺序；
- 所有 buffer 归还后缓冲池可以销毁。

作用：防止大流量数据面出现引用泄漏、静默覆盖、错误队列顺序、契约丢失和统计失真。

### 3.2 `test_channel_stress`

文件：`tests/test_channel_stress.c`

测试内容：

- 一个生产线程和一个消费线程并发传输 10,000 个 buffer；
- 缓冲池只有 64 个块、通道容量为 32，持续触发复用和背压；
- 生产者永久等待发送，消费者永久等待接收；
- 每个 sequence 必须严格连续，最终消费数量必须等于生产数量；
- 关闭后线程退出，缓冲池恢复空闲并成功销毁。

作用：验证并发条件下的顺序、条件变量唤醒、引用计数和通道关闭。它是正确性压力测试，不是性能基准。

### 3.3 `test_event`

文件：`tests/test_event.c`

测试内容：

- 非法订阅参数返回错误并清空输出句柄；
- 同步发布立即执行回调并传递正确数据；
- 异步发布复制消息并由事件线程执行；
- 事件 ID、ABI 版本和 payload size 正确；
- 退订会等待已经在途的异步回调完成；
- 回调错误会返回给同步发布者并进入失败统计；
- 无订阅者异步发布可以安全完成；
- 同步/异步发布数、回调失败数、队列深度和高水位统计正确。

作用：验证小消息总线的时序、深拷贝、ABI 契约、错误传播和可观测性。

当前未单独覆盖异步事件队列达到最大容量后的 `queue_full_count`，正式扩展事件总线时应增加一个阻塞
回调并填满队列的确定性故障测试。

### 3.4 `test_event_shutdown`

文件：`tests/test_event_shutdown.c`

测试内容：

- 同步事件回调主动阻塞；
- 另一个线程在回调尚未结束时调用 `sys_core_shutdown()`；
- 关闭流程不得提前释放 subscription；
- 回调释放后，发布线程和关闭线程都必须正常完成。

作用：针对释放后使用和停机竞态。此测试应始终在 TSan 和 ASan 下执行。

### 3.5 `test_lifecycle`

文件：`tests/test_lifecycle.c`

测试内容：

- 第二组件显式依赖第一组件；
- 初始化顺序必须为 first init、second init；
- 启动顺序必须为 first start、second start；
- 关闭顺序必须为 second stop、first stop；
- 释放顺序必须为 second deinit、first deinit。

作用：验证依赖拓扑和四阶段生命周期的正常路径，防止依赖模块尚未启动就被调用，或退出顺序反转。

### 3.6 `test_lifecycle_failure`

文件：`tests/test_lifecycle_failure.c`

测试内容：

- 前置 required 组件 init 成功；
- 后续 required 组件 init 返回失败；
- `sys_core_init()` 返回原始错误；
- 已初始化组件必须执行 deinit；
- core 不得进入 RUNNING 状态。

作用：验证初始化阶段的部分成功回滚，防止资源泄漏和半初始化应用继续运行。

### 3.7 `test_lifecycle_start_failure`

文件：`tests/test_lifecycle_start_failure.c`

测试内容：

- 两个组件 init 均成功；
- 第一组件 start 成功，第二组件 start 失败；
- 第一组件必须 stop；
- 第二组件和第一组件必须逆序 deinit；
- 失败组件状态保持 FAILED，`last_error` 保留原始错误。

作用：覆盖比 init 失败更复杂的启动阶段回滚，防止线程、服务或 I/O 已启动后残留。

### 3.8 `test_lifecycle_optional`

文件：`tests/test_lifecycle_optional.c`

测试内容：

- optional 组件 init 失败；
- 独立 required 组件仍能进入 RUNNING；
- `sys_core_init()` 整体成功；
- 状态快照显示一个 FAILED、一个 RUNNING；
- core 状态中的组件数量、失败数量和线程数量正确；
- shutdown 后受管线程和未 join 句柄均归零。

作用：验证降级运行不是静默吞错，同时不会因非关键模块失败而阻断整个应用。

### 3.9 `test_service`

文件：`tests/test_service.c`

测试内容：

- 非法 acquire 会清空输出引用；
- 注册服务成功，重复注册返回 `SYS_ERR_ALREADY_EXISTS`；
- ABI 不匹配返回 `SYS_ERR_ABI_MISMATCH`；
- acquire 后可以通过操作表调用服务；
- 引用计数状态从 0 变为 1，并在 release 后归还；
- unregister 后服务数量归零，后续 acquire 返回 `SYS_ERR_NOT_FOUND`。

作用：防止跨模块裸函数指针失效、接口版本误用、重复注册和卸载时引用泄漏。

### 3.10 `test_thread_runtime`

文件：`tests/test_thread_runtime.c`

测试内容：

- 超过 Linux 限制的线程名被拒绝并清空输出句柄；
- 受管线程启动后，active 和 unjoined 计数均为 1；
- 非阻塞 join 在线程仍运行时返回 `SYS_ERR_TIMEOUT`；
- stop token 能使线程协作退出；
- 成功 join 后 active 和 unjoined 均归零；
- runtime 非阻塞等待返回 timeout；
- `sys_runtime_request_stop()` 经 eventfd 唤醒 poll；
- runtime wait 返回 `SYS_ERR_CANCELLED`，随后恢复信号掩码并销毁。

作用：验证每个业务线程都有确定的停止和回收路径，避免进程退出卡死或线程句柄泄漏。

当前测试使用程序化 eventfd 停止。正式进程集成测试还应向子进程实际发送 SIGINT/SIGTERM，验证
`signalfd` 路径和退出码。

### 3.11 `test_log_types`

文件：`tests/test_log_types.c`

测试内容：

- 自定义日志 sink 可以接收结构化记录；
- 日志级别能够过滤低优先级消息；
- 格式化参数正确写入 message；
- 常用错误码返回稳定文本；
- 未知错误码返回统一文本；
- EINVAL、EACCES、EIO 等 errno 映射到正确框架错误。

作用：保证诊断接口稳定，避免各模块自行定义不一致的日志和错误语义。

### 3.12 `test_camera_rollback`

文件：`tests/test_camera_rollback.c`

测试内容：

- 测试订阅者故意拒绝 Camera stream-started 事件；
- Camera 启动必须返回订阅者错误；
- 已启动的 producer 线程必须停止并 join；
- 必须发布 stream-stopping 回滚通知；
- `camera_is_streaming()` 必须为 false；
- core 随后仍能正常关闭。

作用：验证真实业务模块在跨模块握手失败时，线程、通道、缓冲池和事件都能完整回滚。

### 3.13 `app_smoke`

测试对象：示例可执行程序 `app_core`

测试内容：

- runtime 和 core 能够启动；
- 默认启用模块可注册并被调用；
- 未启用模块以可诊断错误降级；
- Camera/ZLMedia 建立数据通道并运行约 500 ms；
- 应用正常停止数据流、组件、事件线程和 runtime；
- 进程退出码必须为 0。

作用：覆盖多个子系统组合后的端到端冒烟路径。纯库模式不构建示例，因此该模式为 14 项测试。

### 3.14 `test_manifest`

文件：`tests/test_manifest.py`

测试内容：

- 有效模块清单可以加载；
- 十进制 `1` 和十六进制 `0x1` 被识别为重复 ID；
- 依赖环被拒绝；
- 未知依赖被拒绝；
- `../` 越界源文件路径被拒绝；
- 未知字段被拒绝；
- `.config` 中过期配置项被拒绝。

作用：防止多人添加模块时产生 ID 冲突、构建依赖漂移和不安全源码路径。

### 3.15 `test_install_package`

文件：`tests/test_install.cmake.in`、`tests/install_consumer/`

测试内容：

1. 将当前构建安装到临时前缀；
2. 检查公共头文件、静态库和 CMake package 是否可安装；
3. 使用独立 CMake 项目执行 `find_package(AppCore 3 REQUIRED)`；
4. 链接 `AppCore::framework`；
5. 编译并运行消费者程序；
6. 消费者实际调用 `sys_core_init()` 和 `sys_core_shutdown()`。

作用：防止“源码树内能构建、安装后不能使用”的发布问题，例如遗漏头文件、依赖未导出或包版本错误。

## 4. 完整构建矩阵

`tools/ci.sh full` 除上述测试外，还执行：

| 项目 | 测试目的 | 通过标准 |
| --- | --- | --- |
| GCC Debug | 强警告、断言友好的日常构建 | 编译和全部 CTest 通过 |
| GCC Release | 优化和 LTO 兼容性 | 编译和全部 CTest 通过 |
| Clang Debug | 避免依赖 GCC 扩展或诊断差异 | 编译和全部 CTest 通过 |
| 模块全开 | 验证所有模块同时编译和注册 | 15 项测试通过 |
| 模块全关 | 验证最小应用和缺失服务降级 | 15 项测试通过 |
| 非法依赖 | ZLMedia 开启但 Camera 关闭 | CMake 必须配置失败 |
| 纯库模式 | 不构建示例，只交付框架 | 14 项测试和安装消费通过 |
| ARM Release | 验证 32 位 ARM Linux 编译兼容 | 交叉编译成功 |

交叉编译通过只表示源码和链接对 ARM 工具链兼容，不代表已经在真实板卡上运行。板端设备、驱动、性能和
长时间稳定性必须另外测试。

## 5. 动态分析

### ASan

检测越界访问、释放后使用、重复释放和部分内存泄漏。任何 ASan 报告均为阻断问题。

### UBSan

检测整数、移位、对齐、非法类型操作等未定义行为。未定义行为在 Debug 正常不代表 Release 安全。

### TSan

检测数据竞争和部分锁顺序问题。事件停机、通道压力和受管线程测试都必须在 TSan 下执行。

Sanitizer 配置关闭 `test_install_package`，原因是独立消费者没有继承被测静态库的 Sanitizer 链接参数。
安装包本身由普通 GCC/Clang 配置独立验证。

## 6. 静态分析

### 编译器警告

项目启用 `-Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wstrict-prototypes
-Wmissing-prototypes`。警告即错误。

### cppcheck

检查 warning、performance 和 portability 类问题。命令位于 `tools/ci.sh`。

### clang-tidy

`.clang-tidy` 启用 clang-analyzer、bugprone 和 performance，并配置 `WarningsAsErrors: '*'`。
当前只有一处针对 Clang 10 `va_list` 建模误报的精确 NOLINT，不允许使用大范围关闭规则掩盖新问题。

### 差异检查

`git diff --check` 用于阻止尾随空格、冲突标记和错误补丁格式进入提交。

## 7. 正式项目建议的测试分层

开发者本地：

- 每次修改运行相关单项测试；
- 提交前运行 `tools/ci.sh quick`；
- 并发、所有权或生命周期改动必须同时运行 TSan/ASan。

合并请求：

- 执行 `tools/ci.sh full`；
- 检查模块全开、全关和非法依赖；
- 新模块必须有正常、初始化失败、启动失败和关闭失败测试；
- 新跨模块结构必须测试 ABI/格式版本不匹配。

发布候选：

- 在正式目标硬件运行功能、压力和长时间 soak；
- 实际发送 SIGINT/SIGTERM，验证 systemd 停止和退出码；
- 建立 CPU、内存、延迟、吞吐、队列深度和丢弃率基线；
- 进行断网、磁盘满、权限不足、依赖服务退出和时钟变化等故障注入；
- 对网络协议、文件格式和外部输入增加 fuzz 测试；
- 保存编译器、工具链、内核、硬件和配置版本，确保测试可复现。

## 8. 新增测试的规范

1. 测试文件使用 `tests/test_<subject>.c` 命名。
2. 一个测试函数只验证一个主要概念，准备和清理步骤必须明确。
3. 测试不得依赖执行顺序、外网、真实时间长等待或随机 sleep。
4. 并发测试使用条件变量或原子状态建立确定性同步，不依靠“等待足够久”。
5. 每个成功申请的 buffer、channel、service ref、subscription 和 thread 必须在测试内释放。
6. 失败路径同时检查返回码、状态快照和资源回滚结果。
7. 新测试通过 `add_core_test()` 注册；涉及应用模块时显式链接所需模块源和 `app_module_config`。
8. 测试必须同时通过 GCC、Clang、ASan/UBSan 和 TSan；不允许仅在普通 Debug 下通过。
9. 性能测试与正确性测试分开，性能结果必须记录目标硬件、数据规模和统计方法。
10. 修复缺陷时先增加能够稳定复现问题的回归测试，再修改正式代码。

## 9. 当前仍应补充的测试

- 异步事件队列满和 `queue_full_count` 的确定性测试；
- 实际 SIGINT/SIGTERM 的子进程集成测试；
- 多生产者、多消费者通道压力和关闭竞态；
- 服务 unregister 与长时间在途调用并发测试；
- 组件 stop 返回失败时 core 状态和错误优先级测试；
- buffer/channel 引用计数极限与误用测试；
- 配置清单 CMake 侧的重复字段、重复 ID 和依赖环故障用例；
- 长时间运行、内存水位和性能回归基线；
- 网络、存储和媒体领域模块的真实 I/O 故障注入；
- 目标 ARM 板卡上的功能、并发、资源和性能验证。

这些项目是从“通用框架验证”进入“生产领域验证”的下一阶段，不应被当前 15 项测试的通过状态替代。

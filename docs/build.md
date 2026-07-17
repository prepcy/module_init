# App-Core 3 开发与构建标准

## 1. 支持范围

- 目标平台：Linux 用户态；语言标准：C11；并发基础：pthread。
- 可使用 Linux 专有接口，但必须在公共 API 中隐藏具体文件描述符和系统结构体。
- 不承诺 RTOS、Windows、裸机或内核态兼容。
- 网络、存储、加密、媒体编解码等能力优先复用 Linux 和成熟库，框架只提供治理机制。

## 2. 模块清单

新增模块只需增加 `app/features/<name>.module`、源码、公共头和测试。清单字段如下：

```text
name=example
id=0x0600
config=ENABLE_EXAMPLE
prompt=启用 Example 模块
default=n
sources=example_mod.c,example_worker.c
depends=ENABLE_WIFI
```

规则：

- `name` 使用小写蛇形命名；`config` 使用大写蛇形命名。
- `id` 为非零 `uint32_t`，发布后永久保留，不得复用。
- `default` 只能为 `y` 或 `n`。
- 源文件必须存在且不得通过绝对路径或 `..` 越出模块目录。
- 配置依赖必须存在且不得形成环。
- `.config` 由 `tools/menuconfig.py` 原子生成；未知或过期配置项会校验失败。

可复制 [module.module.example](module.module.example) 作为接入模板。

## 3. 四阶段生命周期

```c
SYS_COMPONENT_REGISTER(g_example_component,
	.id = SYS_MOD_EXAMPLE,
	.name = "example",
	.phase = SYS_COMPONENT_PHASE_SERVICE,
	.policy = SYS_COMPONENT_REQUIRED,
	.dependencies = dependencies,
	.dependency_count = SYS_ARRAY_SIZE(dependencies),
	.init = example_init,
	.start = example_start,
	.stop = example_stop,
	.deinit = example_deinit);
```

- `init()`：申请本地资源和注册接口，不启动业务流量。
- `start()`：依赖均处于 RUNNING 后启动线程、I/O 和业务处理。
- `stop()`：停止新工作、唤醒阻塞点、join 线程，并返回关闭错误。
- `deinit()`：释放注册项和空闲资源；必须可在已停止状态安全调用。
- required 组件失败会终止启动并逆序回滚；optional 组件失败会保留 FAILED/SKIPPED 状态并继续。
- 禁止持有模块锁调用事件回调、第三方库回调或其他模块服务。

## 4. 通信选择

- 请求/响应、控制命令：类型化 `sys_service`。
- 状态通知、关闭握手：`sys_event_publish_sync()`。
- 普通小消息：`sys_event_publish_async()`，payload 最大 64 KiB。
- 视频帧、音频块、文件块、网络批量数据：`sys_channel`，避免事件深拷贝。

所有跨模块结构必须携带 ABI 或格式版本。兼容扩展只能在结构尾部追加字段并升级版本策略，禁止改变
已有字段和函数指针的语义。

## 5. 数据通道所有权

```text
buffer_acquire:   pool -> producer
channel_send:     producer + queue
producer release: queue
channel_receive:  queue -> consumer
consumer release: pool
```

- `SYS_CHANNEL_FULL_FAIL` 用于不得静默丢失的数据，可选择等待或超时。
- `SYS_CHANNEL_FULL_DROP_OLDEST` 用于实时性高于完整性的流数据，并记录 drop/full 统计。
- 持久保存 `sys_channel_t *` 必须 retain；最后一个所有者负责 close/release。
- 销毁缓冲池前必须确认所有 buffer 已归还。

## 6. 线程与进程退出

- `sys_runtime_create()` 必须在创建其他线程之前调用，并由同一线程销毁。
- 业务线程统一通过 `sys_thread_create()` 创建，循环中检查 stop token。
- 每个成功创建的线程必须有唯一 join 路径；框架同时统计仍运行线程和未 join 句柄。
- 退出顺序固定为：停止入口 → 关闭/唤醒通道 → join → 注销服务/订阅 → 释放资源。
- 不在异步信号处理器中执行业务逻辑；SIGINT/SIGTERM 经 `signalfd` 进入普通控制流。

## 7. 构建、测试和发布

```bash
cmake -S . -B output -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build output --parallel
ctest --test-dir output --output-on-failure
```

模块矩阵通过 `-DAPP_CONFIG_FILE=` 禁用工作区配置覆盖，再传入 `-DCONFIG_ENABLE_*=ON/OFF`。

合入前执行 `./tools/ci.sh full`。准入项包括 GCC/Clang、Debug/Release、全开/全关、库模式、
安装包消费测试、ASan/UBSan、TSan、静态检查和 ARM 交叉构建（工具存在时）。任何数据竞争、资源泄漏、
未检查的关闭错误、静默截断或无统计的队列丢弃都属于阻断问题。

版本规则：

- 公共 API/ABI 不兼容变化升级主版本。
- 向后兼容能力增加升级次版本。
- 兼容修复升级补丁版本。
- 发布包通过 `sys_version.h` 和 `AppCoreConfigVersion.cmake` 同时暴露版本。

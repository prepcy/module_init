# App-Core V2 架构与开发标准

## 1. 支持范围

框架只面向 Linux/POSIX C 应用，语言标准为 C11，线程和同步原语使用 pthread。
公共 API 不承诺兼容 RTOS、Windows 或裸机环境。

框架核心只提供通用机制。Camera、WiFi 等事件 ID、命令和数据结构必须定义在所属模块中，
禁止把业务常量加入 `core/`。

## 2. 组件生命周期

每个模块定义一个静态组件描述符：

```c
static const uint32_t dependencies[] = { SYS_MOD_CAMERA };

SYS_COMPONENT_REGISTER(g_example_component,
			       SYS_MOD_EXAMPLE,
			       "example",
			       SYS_COMPONENT_PHASE_SERVICE,
			       dependencies,
			       SYS_ARRAY_SIZE(dependencies),
			       example_init,
			       example_exit);
```

规则：

- 模块 ID 必须显式赋值，发布后禁止改变。
- 初始化顺序由阶段和依赖决定，模块 ID 只作为同等候选的确定性排序键。
- `init()`必须检查每个资源申请和注册结果。
- `init()`失败前必须释放本函数已经获得、但尚未交给框架管理的资源。
- 框架失败时逆序回滚成功组件；退出时只调用成功启动组件的`exit()`。
- `exit()`必须可在模块处于空闲状态时安全调用。

## 3. 控制面：类型化服务

一个模块可以注册多个接口，键由`module_id + interface_id`组成。服务描述符必须提供：

- ABI 版本；
- 操作表大小；
- 静态生命周期操作表；
- 可诊断的服务名称。

调用方使用栈上的`sys_service_ref_t`获取服务，调用结束后立即释放。禁止缓存裸`ops`指针。
注销会先停止接受新引用，再等待在途调用完成。

新增字段时只能追加到操作表末尾并升级 ABI 策略；禁止改变已有函数指针的含义或签名。

## 4. 消息面：事件总线

同步事件适用于需要失败反馈或关闭握手的控制通知。回调在发布线程中执行，因此必须快速返回，
且不得形成循环锁依赖。

异步事件适用于普通状态通知：

- payload 完整深拷贝，最大 64 KiB；
- 队列有固定上限；
- 队列满返回`SYS_ERR_QUEUE_FULL`；
- 退订会等待已经排队的回调结束；
- 禁止在事件回调中执行退订；退订必须由模块控制线程或退出流程发起。

视频帧、音频块和文件块不得通过事件 payload 传输，应通过数据通道传递。

## 5. 数据面：缓冲池和通道

`sys_buffer_pool`在启动阶段一次性申请描述符和连续 payload 内存。运行期间只复用数据块，
避免每帧`malloc/free`。

`sys_channel`是带背压的有界队列：

- 发送和接收均支持非阻塞、超时和永久等待；
- 非阻塞发送遇到满队列返回`SYS_ERR_QUEUE_FULL`；
- 关闭后禁止新发送，但允许消费者取完已排队数据；
- 关闭会唤醒所有等待者；
- 最后一个通道所有权引用负责释放队列及残留缓冲区引用。

所有权转换：

```text
buffer_acquire:  pool -> producer
channel_send:    producer + queue
producer release: queue
channel_receive: queue -> consumer
consumer release: pool
```

模块必须明确选择满队列策略：阻塞、超时、丢最新、丢最旧或降级。当前 Camera 示例选择丢最新，
并记录丢帧数量。

## 6. 并发规则

- `volatile`不能用于线程同步；状态使用 C11 atomic 或 mutex。
- 禁止销毁仍可能被其他线程访问的 mutex、condition、channel 或 buffer pool。
- 禁止持有模块内部锁调用未知外部回调。
- 线程创建成功后必须有唯一且确定的 join 路径。
- 锁只保护状态转换，不在临界区执行网络、磁盘或长时间回调。
- 模块退出顺序必须先停止新工作，再唤醒线程，随后 join，最后释放资源。

## 7. 配置和构建

Kconfig、`.config`和 CMake 使用同名`CONFIG_*`变量。新增模块必须同时更新：

1. `tools/Kconfig`；
2. `tools/defconfig`；
3. `app/features/CMakeLists.txt`；
4. 模块稳定 ID；
5. 模块测试。

CMake 强制使用 C11、关闭编译器语言扩展，并启用警告即错误。公共 POSIX 能力通过
`_POSIX_C_SOURCE=200809L`显式声明。

CI 如需直接通过`-DCONFIG_*=...`构建配置矩阵，应同时传入`-DAPP_CONFIG_FILE=`，避免工作区
`.config`覆盖命令行矩阵参数。

## 8. 测试准入

提交前至少执行：

```bash
cmake -S . -B output -DCMAKE_BUILD_TYPE=Debug
cmake --build output --parallel
ctest --test-dir output --output-on-failure
```

CI 应包含：

- GCC 和 Clang；
- Debug 和 Release；
- 模块全开、全关及关键依赖组合；
- AddressSanitizer、UndefinedBehaviorSanitizer、ThreadSanitizer；
- 核心模块 100% 单元测试覆盖率，项目整体不低于 80%。

任何 Sanitizer 数据竞争、未检查返回值、静默数据截断或静默队列丢弃都属于阻断问题。

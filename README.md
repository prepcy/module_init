# App-Core

App-Core 是面向 Linux/POSIX C 应用的模块化基础框架。它借鉴 Linux 内核的组件发现、分层初始化、
引用计数和有界队列思想，但保持用户态代码简单、明确和可测试。

## 核心设计

框架将模块通信分为三个平面：

- **控制面**：`sys_service` 提供带模块 ID、接口 ID、ABI 版本和结构大小校验的类型化服务调用。
- **消息面**：`sys_event` 提供同步通知和有界异步通知。异步消息会完整复制，队列满时明确返回错误。
- **数据面**：`sys_channel` 使用预分配缓冲池和有界指针队列传输视频帧等大流量数据，不复制 payload。

模块通过 `SYS_COMPONENT_REGISTER` 自动注册，并显式声明初始化阶段和依赖。框架会检查重复 ID、
缺失依赖和循环依赖；初始化失败时只回滚已经成功启动的组件。

## 目录结构

```text
core/
  sys_core.*         框架总生命周期
  sys_component.*    组件依赖、初始化和逆序回滚
  sys_service.*      类型化服务注册和安全卸载
  sys_event.*        同步/异步事件总线
  sys_channel.*      缓冲池和大流量数据通道
app/
  app_modules.h      稳定模块 ID
  features/          示例业务模块
tests/               核心单元测试
tools/               配置工具
```

## 构建和测试

```bash
./build.sh                 # Debug 构建
./build.sh release         # Release + LTO 构建
./build.sh menuconfig      # 配置模块
./output/app_core
ctest --test-dir output --output-on-failure
```

默认启用 WiFi、Camera 和 ZLMedia。GPS、IMU 可通过 menuconfig 启用。ZLMedia 明确依赖 Camera，
无效配置会在 CMake 阶段终止，而不是生成行为不确定的程序。

## 数据所有权规则

1. `sys_buffer_acquire()` 返回一个调用者引用。
2. `sys_channel_send()` 为队列增加一个引用，发送者随后释放自己的引用。
3. `sys_channel_receive()` 将队列引用转移给消费者，消费者处理后必须释放。
4. 跨模块保存 `sys_channel_t *` 时必须调用 `sys_channel_retain()`。
5. 生产者停止时先关闭通道并唤醒消费者，所有消费者释放引用后才能销毁缓冲池。

详细的模块接入约束见 [docs/build.md](docs/build.md)。

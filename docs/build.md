# 嵌入式应用软件模块化解耦与自动注册框架（App-Core）设计文档

本框架深度借鉴 **Linux 内核驱动模型（__initcall 与 VFS 虚拟文件系统抽象）** 的底层逻辑，专为资源受限的嵌入式 Linux 应用环境设计。

通过 **“构建期文件裁剪 + 编译期段弹射 + 运行期对象代理”** 的组合拳，彻底攻克了传统嵌入式开发中 “宏污染严重”、“业务高度耦合”、“Git冲突频繁” 以及 “全量重新编译引发的时间雪崩” 等核心痛点。

---

## 一、 架构核心设计原则

### 1. 机制与策略分离（Mechanism vs Policy）

核心总线（`sys_core`）只负责提供**初始化时序控制**和**运行时万能指针托管**的“机制”，对具体业务（如WiFi、Camera等）的业务接口、参数、逻辑等“策略”一概不认识。

### 2. 编译依赖倒置（Compilation Dependency Inversion）

核心总线（`core/`）在编译期间**100%不依赖、不包含**应用层任何一个具体模块的头文件或枚举账本。应用层账本的任意修改，绝对不会波及核心总线的增量编译，构建效率大幅提升。

### 3. 动态自愈与平滑降维

上层业务通过通用 `int ID` 向中枢索要模块操纵杆（Ops指针）。若 `menuconfig` 裁剪了该模块，中枢天然返回 `NULL`。应用层通过基础的防空检查即可实现业务的动态安全跳过，整机绝对不会变砖或发生链接期未定义错误（Undefined Reference）。

---

## 二、 系统目录结构规划

完整的工程目录推荐采用以下解耦布局，确保核心总线可独立打包为闭源静态库（`.a`）：

```text
├── core/                  # 核心框架目录（100% 闭存，永不修改，无业务污染）
│   ├── sys_core.h         # 核心总线契约（采用通用整型引脚阻断依赖）
│   └── sys_core.c         # 核心总线实现（使用内部固定槽位安全数组）
│
└── app/                   # 业务应用层（日常频繁迭代修改区域）
    ├── app_modules.h      # 整个团队的【模块总账本】（仅在此追加模块枚举）
    ├── main.c             # 核心业务主程序入口（以不变应万变）
    └── features/          # 各个解耦的功能特性模块
        ├── wifi_mod.h / .c
        └── camera_mod.h / .c

```

---

## 三、 核心框架源码实现

### 1. 核心总线契约：`core/sys_core.h`

```c
/**
 * @file sys_core.h
 * @brief 机制总线头文件（永不修改，禁止引入任何 app/ 目录下的头文件）
 */

#ifndef SYS_CORE_H
#define SYS_CORE_H

#include <stddef.h>

/* =========================================================================
 * 1. 自动分级生命周期拉起机制
 * ========================================================================= */
typedef int (*initcall_t)(void);

/**
 * @brief 编译期段弹射宏
 * 利用 GCC 的 section 属性，强制将函数指针排列到连续的特殊内存段中
 */
#define __app_init_export(fn, prio) \
    static initcall_t __initcall_##fn __attribute__((used, section("app_init_prio" #prio "_sec"))) = fn

/* 应用层三大自解释优先级初始化宏 */
#define APP_INIT_PRIO_1(fn)   __app_init_export(fn, 1)  // 优先级 1：核心/基础设施层（最先执行）
#define APP_INIT_PRIO_2(fn)   __app_init_export(fn, 2)  // 优先级 2：组件/总线驱动层（次之执行）
#define APP_INIT_PRIO_3(fn)   __app_init_export(fn, 3)  // 优先级 3：业务/应用逻辑层（最后执行）

/**
 * @brief 开机全自动模块遍历初始化入口
 */
void do_initcalls(void);

/* =========================================================================
 * 2. 运行时模块对象代理托管机制
 * ========================================================================= */
// 核心内部预留的最大模块槽位数，通常 64 或 128 足够整个系统演进
#define SYS_CORE_MAX_SLOTS    64

/**
 * @brief 模块上报专属操纵杆（Ops虚函数表）
 * @param mod_id 通用的通用整型引脚 ID
 * @param ops 模块具体虚函数表的无类型指针
 */
int sys_subsystem_register(int mod_id, void *ops);

/**
 * @brief 外部索要指定模块的操纵杆
 */
void *sys_subsystem_get(int mod_id);

#endif // SYS_CORE_H

```

### 2. 核心总线实现：`core/sys_core.c`

```c
/**
 * @file sys_core.c
 * @brief 机制总线实现（完全基于 GCC 特性，不依赖任何外部自定义链接脚本）
 */

#include "sys_core.h"
#include <stdio.h>

/* 声明 GCC 在链接期根据段名自动生成的特殊边界符号（首尾物理指针） */
extern initcall_t __start_app_init_prio1_sec[]; extern initcall_t __stop_app_init_prio1_sec[];
extern initcall_t __start_app_init_prio2_sec[]; extern initcall_t __stop_app_init_prio2_sec[];
extern initcall_t __start_app_init_prio3_sec[]; extern initcall_t __stop_app_init_prio3_sec[];

/* 运行时操纵杆托管寄存器（对业务类型完全不感知的万能指针数组） */
static void *g_subsystem_registry[SYS_CORE_MAX_SLOTS] = { NULL };

/**
 * @brief 遍历并执行指定优先级内存段中的函数指针
 */
static void execute_prio_level(int prio_num, initcall_t *start, initcall_t *stop) {
    initcall_t *call = start;
    for (; call < stop; call++) {
        if (*call != NULL) {
            int ret = (*call)();
            if (ret != 0) {
                fprintf(stderr, "🚨 [Priority %d] Init failed with code: %d\n", prio_num, ret);
            }
        }
    }
}

void do_initcalls(void) {
    printf("==================================================\n");
    printf("🚀 [Framework] 启动时应用软件模块全自动分级初始化...\n");
    printf("==================================================\n");

    execute_prio_level(1, __start_app_init_prio1_sec, __stop_app_init_prio1_sec);
    execute_prio_level(2, __start_app_init_prio2_sec, __stop_app_init_prio2_sec);
    execute_prio_level(3, __start_app_init_prio3_sec, __stop_app_init_prio3_sec);

    printf("==================================================\n");
    printf("🎉 [Framework] 所有就绪功能模块并行拉起成功！\n");
    printf("==================================================\n\n");
}

int sys_subsystem_register(int mod_id, void *ops) {
    if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS || !ops) {
        return -1;
    }
    g_subsystem_registry[mod_id] = ops;
    return 0;
}

void *sys_subsystem_get(int mod_id) {
    if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
        return NULL;
    }
    return g_subsystem_registry[mod_id];
}

```

---

## 四、 应用层配置与业务落地示范

### 1. 模块身份账本：`app/app_modules.h`

这是**整个应用层唯一被允许频繁修改的公共总账**，仅用于分配数字引脚：

```c
#ifndef APP_MODULES_H
#define APP_MODULES_H

/**
 * @brief 模块身份唯一引脚枚举
 * 团队成员增加模块只需要在这里追加，绝对不会导致 core/ 重新编译
 */
typedef enum {
    SYS_MOD_WIFI = 0,   // WiFi 模块槽位
    SYS_MOD_CAMERA,     // 摄像头模块槽位
    SYS_MOD_IMU,        // 惯导传感器槽位

    SYS_MOD_APP_MAX     // 业务边界线
} sys_mod_id_t;

#endif // APP_MODULES_H

```

### 2. 特异性模块接入范例（以 WiFi 模块为例）

#### 📁 公共契约头文件：`app/features/wifi_mod.h`

模块定义其独特的、千奇百怪的专有 API。该文件**永不裁剪**，主程序可自由包含。

```c
#ifndef WIFI_MOD_H
#define WIFI_MOD_H

/* WiFi 独特的业务虚函数表，保留高度的 C 语言类型检查 */
typedef struct {
    int (*connect)(const char *ssid, const char *pwd);
    int (*get_rssi)(void);
    void (*disconnect)(void);
} wifi_ops_t;

#endif // WIFI_MOD_H

```

#### 📁 真实功能源文件：`app/features/wifi_mod.c`

此文件由 `menuconfig` 控制是否参与编译。

```c
#include "sys_core.h"     // 引入核心万能总线
#include "app_modules.h"  // 引入模块引脚账本
#include "wifi_mod.h"     // 引入模块契约
#include <stdio.h>

static int wifi_real_connect(const char *ssid, const char *pwd) {
    printf("  └─> [WiFi驱动] 正在向路由器 [%s] 发起物理握手... 成功！\n", ssid);
    return 0;
}

static int wifi_real_get_rssi(void) { return -45; }

static void wifi_real_disconnect(void) { printf("  └─> [WiFi驱动] 链路已安全释放。\n"); }

/* 映射模块真正的特异性超能力 */
static const wifi_ops_t my_real_wifi_ops = {
    .connect = wifi_real_connect,
    .get_rssi = wifi_real_get_rssi,
    .disconnect = wifi_real_disconnect
};

/**
 * @brief 模块静默自拉起函数
 */
static int wifi_subsys_init(void) {
    // 自动上报自己的特异性操纵杆，sys_mod_id_t 会隐式自动转为 int
    sys_subsystem_register(SYS_MOD_WIFI, (void *)&my_real_wifi_ops);
    return 0;
}
/* 声明该组件依赖日志，定位为 Priority 2 级别自动顺序加载 */
APP_INIT_PRIO_2(wifi_subsys_init);

```

### 3. 主程序无感调用：`app/main.c`

主程序以绝对清爽的流水线姿态作业，告别一切代码内的 `#ifdef`。

```c
#include <stdio.h>
#include "sys_core.h"     // 机制核心
#include "app_modules.h"  // 配置账本
#include "wifi_mod.h"     // WiFi 专属操纵集

int main(void) {
    // 1. 一句话通扫内存段，全自动有序拉起所有激活的模块
    do_initcalls();

    printf("--- [Main 业务] 主状态机正式启动 ---\n\n");

    // 2. 🌟 核心调用：向框架索要 WiFi 操纵杆
    wifi_ops_t *wifi = (wifi_ops_t *)sys_subsystem_get(SYS_MOD_WIFI);

    if (wifi != NULL) {
        // 如果 menuconfig 编译了 WiFi 模块，指针有效，直接调用专有特异性 API
        wifi->connect("MCU_WORKSPACE", "12345678");
        printf("[Main 业务] 实时获取 WiFi 信号强度: %d dBm\n", wifi->get_rssi());
    } else {
        // 如果 menuconfig 关闭了 WiFi 模块，拿到了 NULL，系统安全平滑退缩
        printf("ℹ️  [Main 业务] 提示：当前配置未启用 WiFi 模块，自动跳过网络同步逻辑。\n");
    }

    return 0;
}

```

---

## 五、 构建系统（CMakeLists.txt）无缝衔接指引

在 `menuconfig` 或构建系统的配置层，通过 **文件级别的包含控制** 来达成模块的彻底裁剪：

```cmake
# app/features/CMakeLists.txt

# 无论配置如何，各特性的公共头文件目录必须包含，以防止 main.c 报找不到头文件错误
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 🌟 核心防线：只有在宏开启时，才将对应的 .c 文件送入编译器
if(CONFIG_ENABLE_WIFI)
    target_sources(${PROJECT_NAME} PRIVATE wifi_mod.c)
endif()

if(CONFIG_ENABLE_CAMERA)
    target_sources(${PROJECT_NAME} PRIVATE camera_mod.c)
endif()

```

---

## 六、 工业级上线注意事项

1. **GCC 间接调用的性能锁（LTO优化）**：
由于代理模式采用了“虚函数表（通过函数指针间接调用）”，在 CPU 分支预测上会有微小的时钟开销。在量产编译时，请务必在 GCC/Clang 构建参数中开启 **`-flto`（Link-Time Optimization，链接期优化）**。开启后，编译器会穿透指针地址，自动将应用层的调用重新优化为直接跳转甚至直接内联，达到零性能开销。
2. **多线程并发安全防线**：
`do_initcalls()` 在设计上是**单线程同步**开机流水线。请确保所有模块的初始化函数内，只做基础的“句柄打开、内存申请、资源注册”，**严禁在 `PRIO_1/2/3` 函数内部执行死循环阻塞**。长耗时的阻塞任务（如连接网络、探测卫星），必须在初始化函数中通过 `pthread_create` 扔给独立的后台线程去异步处理。
3. **防空指针红线**：
业务层在使用任何通过 `sys_subsystem_get()` 拿到的指针前，**必须进行 `if (ptr != NULL)` 检查**。这是保障单分区嵌入式 Linux 环境下设备永远不崩溃变砖的最后一道软件防火墙。
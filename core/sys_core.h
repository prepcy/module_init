/**
 * @file sys_core.h
 * @brief 机制总线核心头文件
 *
 * 本文件定义了自动分级初始化生命周期宏以及轻量级运行时对象托管机制。
 * 本核心框架完全采用“机制与策略分离”的原则设计，不依赖具体业务模块的头文件与结构定义，
 * 实现编译依赖倒置，确保核心框架能够独立打包、无需随业务代码变动而重新编译。
 */

#ifndef SYS_CORE_H
#define SYS_CORE_H

#include <stddef.h>

/* =========================================================================
 * 框架级错误码枚举定义
 * ========================================================================= */
typedef enum {
	SYS_OK = 0,		    // 成功
	SYS_ERR_GENERIC = -1,	    // 通用/未知错误
	SYS_ERR_NOT_FOUND = -2,	    // 模块未注册或未启用
	SYS_ERR_NOT_SUPPORTED = -3, // 模块接口未实现
	SYS_ERR_INVALID_PARAM = -4  // 传入参数非法
} sys_err_t;

/* =========================================================================
 * 1. 自动分级初始化机制 (Auto-Initcall Section Export)
 * ========================================================================= */

/**
 * @brief 自动注册函数类型指针
 * 所有需要注册到开机拉起时序中的初始化函数，均须符合 `int func(void)` 的函数原型
 */
typedef int (*initcall_t)(void);

/**
 * @brief 编译期段弹射核心宏
 * @param fn 待弹射导出的初始化函数名
 * @param prio 弹射指定的优先级等级（1/2/3）
 *
 * 利用 GCC 的 `__attribute__((used, section(...)))` 属性，强制将函数指针排列到
 * 连续的特殊 ELF 内存段中。链接期由 GNU 链接器自动提取段的首尾物理指针进行遍历执行。
 */
#define __app_init_export(fn, prio)                                                                                    \
	static initcall_t __initcall_##fn __attribute__((used, section("app_init_prio" #prio "_sec"))) = fn

/**
 * @def APP_INIT_PRIO_1
 * @brief 优先级 1：核心/基础设施层初始化宏
 * 用于最底层的总线驱动、内存池、OS 任务管理等最先启动的组件注册
 */
#define APP_INIT_PRIO_1(fn) __app_init_export(fn, 1)

/**
 * @def APP_INIT_PRIO_2
 * @brief 优先级 2：系统组件/通用业务总线初始化宏
 * 用于 WiFi 驱动、Camera 驱动、数据总线等依赖基础组件的中层模块注册
 */
#define APP_INIT_PRIO_2(fn) __app_init_export(fn, 2)

/**
 * @def APP_INIT_PRIO_3
 * @brief 优先级 3：应用层业务/高层状态机逻辑初始化宏
 * 用于主业务状态机、整机联动控制等最后被激活的逻辑注册
 */
#define APP_INIT_PRIO_3(fn) __app_init_export(fn, 3)

/**
 * @brief 执行所有已导出段函数的遍历初始化
 *
 * 按照 Priority 1 -> 2 -> 3 的顺序，依次遍历相应内存段中的函数指针并同步调用。
 * 该函数在 `main` 启动初期最先被调用，为单线程同步执行。
 */
void do_initcalls(void);

/* =========================================================================
 * 2. 运行时模块托管机制（使用通用 int 阻断依赖）
 * ========================================================================= */

// 定义核心能够容纳的最大模块槽位数，避免动态内存分配，确保车规级/嵌入式稳定性
#define SYS_CORE_MAX_SLOTS 64

/**
 * @brief 注册模块具体特异性操纵杆 (虚函数表指针)
 * @param mod_id 统一的业务引脚槽位 ID (对应 `sys_mod_id_t` 枚举值)
 * @param ops 指向具体模块的自定义操作结构体虚表 (例如 `wifi_ops_t` 实例地址)
 * @return sys_err_t 注册结果状态码
 *
 * 供功能模块初始化阶段（即 `APP_INIT_PRIO_X` 中）向上汇报自身的控制指针。
 * 核心内部仅以 `void *` 万能指针类型进行托管存取，实现接口类型擦除，隔绝业务依赖。
 */
sys_err_t sys_subsystem_register(int mod_id, void *ops);

/**
 * @brief 注销指定模块的操纵杆
 * @param mod_id 统一的业务引脚槽位 ID
 * @return sys_err_t 注销结果状态码
 * 
 * 允许在运行阶段动态卸载/注销某个功能组件（配合设备动态热插拔与休眠断电等生命周期管理）。
 * 核心内部在加锁保护下将对应槽位清空为 NULL，随后外部业务再次获取时将自动降维避让。
 */
sys_err_t sys_subsystem_unregister(int mod_id);

/**
 * @brief 索要指定模块的操纵杆
 * @param mod_id 统一的业务引脚槽位 ID
 * @return void* 具体模块操作表指针的万能指针（若该模块被裁剪编译，则返回 `NULL`）
 *
 * 应用层/外部模块通过此函数动态拉取指定槽位的操纵杆指针，在取得指针后应强制转换为
 * 对应的业务虚表指针，并通过检查是否为 `NULL` 来决定是否平滑执行还是降维挂起。
 */
void *sys_subsystem_get(int mod_id);

#endif // SYS_CORE_H

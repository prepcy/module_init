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
 * 1. 自动段自排序初始化机制 (Auto-Initcall Alphabetical Section Export)
 * ========================================================================= */

/**
 * @brief 自动注册函数类型指针
 * 所有需要注册到开机拉起时序中的初始化函数，均须符合 `int func(void)` 的函数原型
 */
typedef int (*initcall_t)(void);

/**
 * @brief 自动注销函数类型指针
 * 所有需要注册到关机清理时序中的释放函数，均须符合 `void func(void)` 的函数原型
 */
typedef void (*exitcall_t)(void);

/**
 * @brief 初始化/注销调用段元数据结构
 * 强绑定初始化/注销函数与模块唯一 ID (枚举整数值)
 */
typedef struct {
	initcall_t func;
	exitcall_t exit_func;
	int mod_id;
} __attribute__((aligned(8))) sys_initcall_t;

/**
 * @brief 通用业务模块自注册宏
 * @param init_fn 模块的初始化启动函数名称
 * @param exit_fn 模块的注销释放函数名称 (若无则传入 NULL)
 * @param _mod_id_arg 模块对应的唯一整型 ID (如 SYS_MOD_WIFI 等枚举值)
 *
 * 利用 GCC 的 `__attribute__((used, section(...)))` 属性，将包含开机关机指针与模块 ID
 * 的元数据结构体直接放置到特殊的段 `"app_init_sec"` 中。
 * 运行时由框架层 do_initcalls 对其进行拷贝与排序，按枚举 ID 大小的物理顺序依次拉起；
 * 在关机时由 do_exitcalls 逆序进行卸载清理动作。
 *
 * 注意：宏参数名特意使用 `_mod_id_arg` 而非 `mod_id`，以避免与结构体指定初始化式
 * `.mod_id = xxx` 中的字段名 `mod_id` 发生 C 预处理器的命名冲突展开错误。
 */
#define APP_REGISTER(init_fn, exit_fn, _mod_id_arg)                                                                    \
	static const sys_initcall_t __initcall_##init_fn                                                               \
		__attribute__((used, section("app_init_sec"),                                                          \
			       aligned(8))) = { .func = init_fn, .exit_func = exit_fn, .mod_id = _mod_id_arg }

/**
 * @brief 执行所有已导出段函数的遍历初始化
 *
 * 按照模块 ID 升序依次调用段内函数。
 * 该函数在 `main` 启动初期最先被调用，为单线程同步执行。
 */
void do_initcalls(void);

/**
 * @brief 执行所有已导出段函数的逆序注销释放
 *
 * 按照模块 ID 降序（LIFO 后进先出）依次调用段内非 NULL 的注销函数。
 * 该函数在程序结束退出前被调用，完成资源优雅释放。
 */
void do_exitcalls(void);

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

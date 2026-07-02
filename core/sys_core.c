/**
 * @file sys_core.c
 * @brief 机制总线核心实现文件
 *
 * 本文件实现了解耦总线的数据托管和分级自动启动拉起。
 * 依靠 GCC Section 段和链接器符号机制，省去了手动的静态链表构建与注册，
 * 且不依赖任何外部自定义 Linker Script 链接脚本，移植性极强。
 */

#include "sys_core.h"
#include <stdio.h>
#include <pthread.h>

/**
 * 🌟 链接器特殊边界符号声明
 *
 * 当 GCC 在链接生成 ELF 时，GNU 链接器 (ld) 如果检测到有名为 "app_init_prioX_sec" 的段，
 * 会在符号表中自动定义以下两个边界标记变量：
 *   - `__start_段名`：该物理内存段在 ELF 中的起始首地址。
 *   - `__stop_段名` ：该物理内存段在 ELF 中的终止尾地址。
 *
 * 我们利用这组 `extern` 符号，实现在 C 语言层面以操作“连续数组”的方式读取段内的函数指针。
 */
extern initcall_t __start_app_init_prio1_sec[];
extern initcall_t __stop_app_init_prio1_sec[];
extern initcall_t __start_app_init_prio2_sec[];
extern initcall_t __stop_app_init_prio2_sec[];
extern initcall_t __start_app_init_prio3_sec[];
extern initcall_t __stop_app_init_prio3_sec[];

/**
 * @brief 运行时操纵杆托管寄存器（对业务类型完全不感知的万能指针数组）
 * 初始化为全 NULL。由于各子模块由引脚枚举唯一隔离槽位，读取时间复杂度为恒定的 O(1)。
 */
static void *g_subsystem_registry[SYS_CORE_MAX_SLOTS] = { NULL };

/**
 * @brief 临界区安全互斥锁，确保多线程并发访问与注册托管表的安全性
 */
static pthread_mutex_t g_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief 时序依赖检测状态变量与历史请求记录表
 *
 * - `g_initcalls_running_level` 记录当前执行到的生命周期阶段 (1: prio1, 2: prio2, 3: prio3, 4: init已结束)
 * - `g_subsystem_requested_levels` 对应模块 ID 被首次请求时的运行层级。若请求时未注册，则在此记录级别。
 */
static volatile int g_initcalls_running_level = 0;
static int g_subsystem_requested_levels[SYS_CORE_MAX_SLOTS] = { 0 };

/**
 * @brief 遍历执行指定段范围内的所有有效初始化函数
 * @param start 该优先级物理段的起始指针
 * @param stop 该优先级物理段的结束指针
 */
static void execute_prio_level(initcall_t *start, initcall_t *stop)
{
	initcall_t *call = start;
	// 顺序遍历指针数组，直到抵达段的物理尾部边界
	for (; call < stop; call++) {
		if (*call) {
			(*call)(); // 调用子系统自带的静默拉起程序
		}
	}
}

void do_initcalls(void)
{
	// 按严格的时序优先级 1 -> 2 -> 3 依次遍历并运行对应段函数，并实时标记执行层级
	g_initcalls_running_level = 1;
	execute_prio_level(__start_app_init_prio1_sec, __stop_app_init_prio1_sec);

	g_initcalls_running_level = 2;
	execute_prio_level(__start_app_init_prio2_sec, __stop_app_init_prio2_sec);

	g_initcalls_running_level = 3;
	execute_prio_level(__start_app_init_prio3_sec, __stop_app_init_prio3_sec);

	// 所有初始化完成，标记进入主程序运转阶段 (Level 4)
	g_initcalls_running_level = 4;
}

sys_err_t sys_subsystem_register(int mod_id, void *ops)
{
	// 边界安全防御：防止模块注册越界或提交空指针
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS || !ops) {
		return SYS_ERR_INVALID_PARAM;
	}

	// 1. 获取互斥锁，保障数据并发操作的线程安全
	pthread_mutex_lock(&g_registry_mutex);

	// 2. 时序依赖合理性动态校验
	// 如果检测到该模块在此前较早的初始化优先级阶段已被其他模块请求过，则抛出时序依赖警告
	int req_lvl = g_subsystem_requested_levels[mod_id];
	if (req_lvl != 0 && req_lvl < g_initcalls_running_level) {
		fprintf(stderr,
			"[Framework] WARNING: Timing dependency conflict detected! "
			"Module ID %d was requested at Priority %d phase, but not registered until Priority %d phase!\n",
			mod_id, req_lvl, g_initcalls_running_level);
	}

	// 3. 将业务虚函数表指针安全登记到指定的数字引脚槽位中
	g_subsystem_registry[mod_id] = ops;

	// 4. 释放互斥锁，退出临界区
	pthread_mutex_unlock(&g_registry_mutex);

	return SYS_OK;
}

sys_err_t sys_subsystem_unregister(int mod_id)
{
	// 1. 边界安全防御：防止模块注销越界
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
		return SYS_ERR_INVALID_PARAM;
	}

	// 2. 获取互斥锁，防止并发读写引发竞态冲突
	pthread_mutex_lock(&g_registry_mutex);

	// 3. 将物理槽位复位为 NULL，配合动态生命周期的模块热插拔与安全自愈降维
	g_subsystem_registry[mod_id] = NULL;

	// 4. 释放互斥锁，退出临界区
	pthread_mutex_unlock(&g_registry_mutex);

	return SYS_OK;
}

void *sys_subsystem_get(int mod_id)
{
	// 边界安全防御：非法槽位直接返回 NULL
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
		return NULL;
	}

	pthread_mutex_lock(&g_registry_mutex);
	void *ops = g_subsystem_registry[mod_id];

	// 🌟 记录请求时机，辅助时序冲突分析
	// 仅在开机顺序初始化期间 (g_initcalls_running_level < 4) 且获取目标为 NULL 时，记录首次被调用的优先级
	if (ops == NULL && g_initcalls_running_level > 0 && g_initcalls_running_level < 4) {
		if (g_subsystem_requested_levels[mod_id] == 0) {
			g_subsystem_requested_levels[mod_id] = g_initcalls_running_level;
		}
	}
	pthread_mutex_unlock(&g_registry_mutex);

	// 返回对应的业务操纵杆指针 (如模块未启用或被 CMake 裁剪，这里自然为 NULL)
	return ops;
}

/**
 * 🌟 占位初始化段弹射保护机制
 *
 * 原因：GNU 链接器的一个特性是，如果某个自定义段（如 app_init_prio1_sec）没有任何实体变量被强引用，
 * 该段便不会被保留，导致编译时报 `__start_app_init_prio1_sec` 符号未定义的链接错误（Undefined Reference）。
 *
 * 解决策略：我们在核心底层代码中，为 Priority 1、2、3 各导出一个极简的空实现 (dummy)。
 * 这样即使应用层裁剪了某一层级的所有业务模块，也能确保物理段始终存在，符号能安全解析。
 */
static int dummy_prio1_init(void)
{
	return 0;
}
static int dummy_prio2_init(void)
{
	return 0;
}
static int dummy_prio3_init(void)
{
	return 0;
}
APP_INIT_PRIO_1(dummy_prio1_init);
APP_INIT_PRIO_2(dummy_prio2_init);
APP_INIT_PRIO_3(dummy_prio3_init);

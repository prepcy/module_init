/**
 * @file sys_core.c
 * @brief 机制总线核心实现文件
 *
 * 本文件实现了解耦总线的数据托管和开机关机时序管理。
 * 依靠 GCC Section 段和链接器符号机制，省去了手动的静态链表构建与注册，
 * 且不依赖任何外部自定义 Linker Script 链接脚本，移植性极强。
 */

#include "sys_core.h"
#include <stdio.h>
#include <pthread.h>

/**
 * 链接器特殊边界符号声明
 *
 * 当 GCC 在链接生成 ELF 时，GNU 链接器 (ld) 如果检测到有名为 "app_init_sec" 的段，
 * 会在符号表中自动定义以下两个边界标记变量：
 *   - `__start_app_init_sec`：合并后的物理内存段起始首地址。
 *   - `__stop_app_init_sec` ：合并后的物理内存段终止尾地址。
 *
 * 我们利用这组 `extern` 符号，实现在 C 语言层面以操作“连续数组”的方式读取段内的元数据结构体。
 */
extern sys_initcall_t __start_app_init_sec[];
extern sys_initcall_t __stop_app_init_sec[];

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
 * - `g_initcalls_running_level` 记录当前启动加载阶段 (1: 正在进行开机时序顺序初始化, 4: 启动加载已结束)
 * - `g_subsystem_requested_levels` 记录每个模块是否在注册前就被越权提前请求过 (1: 是, 0: 否)
 */
static volatile int g_initcalls_running_level = 0;
static int g_subsystem_requested_levels[SYS_CORE_MAX_SLOTS] = { 0 };

void do_initcalls(void)
{
	int count = __stop_app_init_sec - __start_app_init_sec;
	if (count <= 0) {
		return;
	}

	// 边界安全防护：防止编译的导出结构体总数超限
	if (count > SYS_CORE_MAX_SLOTS) {
		count = SYS_CORE_MAX_SLOTS;
	}

	// 将所有段中的初始化配置复制至栈上临时缓冲区中，进行就地排序
	sys_initcall_t temp_calls[SYS_CORE_MAX_SLOTS];
	int valid_count = 0;
	for (int i = 0; i < count; i++) {
		if (__start_app_init_sec[i].func) {
			temp_calls[valid_count++] = __start_app_init_sec[i];
		}
	}

	// 经典冒泡排序：按照 mod_id 升序进行排序
	// 确保完全符合 app_modules.h 中枚举声明的从上到下的物理顺序
	for (int i = 0; i < valid_count - 1; i++) {
		for (int j = 0; j < valid_count - i - 1; j++) {
			if (temp_calls[j].mod_id > temp_calls[j + 1].mod_id) {
				sys_initcall_t temp = temp_calls[j];
				temp_calls[j] = temp_calls[j + 1];
				temp_calls[j + 1] = temp;
			}
		}
	}

	// 标记开机顺序初始化阶段启动
	g_initcalls_running_level = 1;

	// 依次顺序执行各个模块自加载程序
	for (int i = 0; i < valid_count; i++) {
		if (temp_calls[i].func) {
			temp_calls[i].func();
		}
	}

	// 初始化流程执行完毕，复位运行状态为 4
	g_initcalls_running_level = 4;
}

void do_exitcalls(void)
{
	int count = __stop_app_init_sec - __start_app_init_sec;
	if (count <= 0) {
		return;
	}

	if (count > SYS_CORE_MAX_SLOTS) {
		count = SYS_CORE_MAX_SLOTS;
	}

	// 将所有段中的自注册配置复制至栈上临时缓冲区中，进行降序排列
	sys_initcall_t temp_calls[SYS_CORE_MAX_SLOTS];
	int valid_count = 0;
	for (int i = 0; i < count; i++) {
		if (__start_app_init_sec[i].func) {
			temp_calls[valid_count++] = __start_app_init_sec[i];
		}
	}

	// 经典逆序冒泡排序：按照 mod_id 降序进行排序 (LIFO 原则)
	// 保证后初始化的模块最先注销，保障系统注销时的资源依赖链安全
	for (int i = 0; i < valid_count - 1; i++) {
		for (int j = 0; j < valid_count - i - 1; j++) {
			if (temp_calls[j].mod_id < temp_calls[j + 1].mod_id) {
				sys_initcall_t temp = temp_calls[j];
				temp_calls[j] = temp_calls[j + 1];
				temp_calls[j + 1] = temp;
			}
		}
	}

	// 依次顺序执行各个已注册模块的注销释放程序
	for (int i = 0; i < valid_count; i++) {
		if (temp_calls[i].exit_func) {
			temp_calls[i].exit_func();
		}
	}
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
	// 如果检测到该模块在此前已被其他模块越权提前请求过，则抛出时序依赖警告
	int req_lvl = g_subsystem_requested_levels[mod_id];
	if (req_lvl == 1 && g_initcalls_running_level == 1) {
		fprintf(stderr,
			"[Framework] WARNING: Timing dependency conflict detected! "
			"Module ID %d was requested before it was registered!\n",
			mod_id);
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

	// 记录请求时机，辅助时序冲突 analysis
	// 仅在开机顺序初始化期间 (g_initcalls_running_level == 1) 且获取目标为 NULL 时，记录该模块被提前请求
	if (ops == NULL && g_initcalls_running_level == 1) {
		g_subsystem_requested_levels[mod_id] = 1;
	}
	pthread_mutex_unlock(&g_registry_mutex);

	// 返回对应的业务操纵杆指针 (如模块未启用或被 CMake 裁剪，这里自然为 NULL)
	return ops;
}

#include <stdlib.h>
#include <sys/time.h>

/* =========================================================================
 * 3. 零拷贝数据通道机制 (Zero-Copy Ring Buffer & Buffer Pool)
 * ========================================================================= */

sys_buffer_t *sys_buffer_get_free(sys_buffer_t *pool, int pool_size)
{
	if (!pool) return NULL;
	for (int i = 0; i < pool_size; i++) {
		if (pool[i].ref_count == 0) {
			if (__sync_bool_compare_and_swap(&pool[i].ref_count, 0, 1)) {
				pool[i].length = 0;
				pool[i].timestamp = 0;
				return &pool[i];
			}
		}
	}
	return NULL;
}

void sys_buffer_ref(sys_buffer_t *buf)
{
	if (buf) {
		__sync_add_and_fetch(&buf->ref_count, 1);
	}
}

void sys_buffer_unref(sys_buffer_t *buf)
{
	if (buf) {
		__sync_sub_and_fetch(&buf->ref_count, 1);
	}
}

sys_ringbuf_t *sys_ringbuf_create(unsigned int size)
{
	unsigned int real_size = 1;
	while (real_size < size) {
		real_size <<= 1;
	}

	sys_ringbuf_t *rb = (sys_ringbuf_t *)malloc(sizeof(sys_ringbuf_t));
	if (!rb) return NULL;

	rb->buffers = (sys_buffer_t **)calloc(real_size, sizeof(sys_buffer_t *));
	if (!rb->buffers) {
		free(rb);
		return NULL;
	}

	rb->size = real_size;
	rb->in = 0;
	rb->out = 0;
	pthread_mutex_init(&rb->lock, NULL);
	pthread_cond_init(&rb->cond, NULL);
	return rb;
}

void sys_ringbuf_destroy(sys_ringbuf_t *rb)
{
	if (!rb) return;
	pthread_mutex_destroy(&rb->lock);
	pthread_cond_destroy(&rb->cond);
	unsigned int count = rb->in - rb->out;
	for (unsigned int i = 0; i < count; i++) {
		sys_buffer_t *buf = rb->buffers[(rb->out + i) & (rb->size - 1)];
		sys_buffer_unref(buf);
	}
	free(rb->buffers);
	free(rb);
}

sys_err_t sys_ringbuf_put(sys_ringbuf_t *rb, sys_buffer_t *buf)
{
	if (!rb || !buf) return SYS_ERR_INVALID_PARAM;

	pthread_mutex_lock(&rb->lock);
	if (rb->in - rb->out >= rb->size) {
		pthread_mutex_unlock(&rb->lock);
		return SYS_ERR_GENERIC;
	}

	sys_buffer_ref(buf);
	rb->buffers[rb->in & (rb->size - 1)] = buf;
	rb->in++;

	pthread_cond_signal(&rb->cond);
	pthread_mutex_unlock(&rb->lock);
	return SYS_OK;
}

sys_buffer_t *sys_ringbuf_get(sys_ringbuf_t *rb, int timeout_ms)
{
	if (!rb) return NULL;

	pthread_mutex_lock(&rb->lock);
	if (rb->in == rb->out) {
		if (timeout_ms < 0) {
			while (rb->in == rb->out) {
				pthread_cond_wait(&rb->cond, &rb->lock);
			}
		} else if (timeout_ms > 0) {
			struct timeval now;
			gettimeofday(&now, NULL);
			struct timespec ts;
			ts.tv_sec = now.tv_sec + (timeout_ms / 1000);
			ts.tv_nsec = (now.tv_usec * 1000) + ((timeout_ms % 1000) * 1000000);
			if (ts.tv_nsec >= 1000000000) {
				ts.tv_sec += 1;
				ts.tv_nsec -= 1000000000;
			}
			while (rb->in == rb->out) {
				int ret = pthread_cond_timedwait(&rb->cond, &rb->lock, &ts);
				if (ret != 0) {
					break;
				}
			}
		}
	}

	if (rb->in == rb->out) {
		pthread_mutex_unlock(&rb->lock);
		return NULL;
	}

	sys_buffer_t *buf = rb->buffers[rb->out & (rb->size - 1)];
	rb->out++;
	pthread_mutex_unlock(&rb->lock);
	return buf;
}

unsigned int sys_ringbuf_count(sys_ringbuf_t *rb)
{
	if (!rb) return 0;
	pthread_mutex_lock(&rb->lock);
	unsigned int count = rb->in - rb->out;
	pthread_mutex_unlock(&rb->lock);
	return count;
}

/* =========================================================================
 * 4. 异步发布-订阅事件总线 (Pub/Sub Event Bus)
 * ========================================================================= */

#define MAX_SUBSCRIBERS 16
#define MAX_EVENTS 32

typedef struct {
	event_cb_t callback;
	void *priv_data;
} sys_subscriber_t;

typedef struct {
	int event_id;
	sys_subscriber_t subscribers[MAX_SUBSCRIBERS];
	int subscriber_count;
} sys_event_entry_t;

static sys_event_entry_t g_event_bus[MAX_EVENTS];
static int g_event_entry_count = 0;
static pthread_mutex_t g_event_bus_mutex = PTHREAD_MUTEX_INITIALIZER;

sys_err_t sys_event_subscribe(int event_id, event_cb_t callback, void *priv_data)
{
	if (!callback) return SYS_ERR_INVALID_PARAM;

	pthread_mutex_lock(&g_event_bus_mutex);
	sys_event_entry_t *entry = NULL;
	for (int i = 0; i < g_event_entry_count; i++) {
		if (g_event_bus[i].event_id == event_id) {
			entry = &g_event_bus[i];
			break;
		}
	}

	if (!entry) {
		if (g_event_entry_count >= MAX_EVENTS) {
			pthread_mutex_unlock(&g_event_bus_mutex);
			return SYS_ERR_GENERIC;
		}
		entry = &g_event_bus[g_event_entry_count++];
		entry->event_id = event_id;
		entry->subscriber_count = 0;
	}

	for (int i = 0; i < entry->subscriber_count; i++) {
		if (entry->subscribers[i].callback == callback) {
			pthread_mutex_unlock(&g_event_bus_mutex);
			return SYS_OK;
		}
	}

	if (entry->subscriber_count >= MAX_SUBSCRIBERS) {
		pthread_mutex_unlock(&g_event_bus_mutex);
		return SYS_ERR_GENERIC;
	}

	entry->subscribers[entry->subscriber_count].callback = callback;
	entry->subscribers[entry->subscriber_count].priv_data = priv_data;
	entry->subscriber_count++;

	pthread_mutex_unlock(&g_event_bus_mutex);
	return SYS_OK;
}

sys_err_t sys_event_unsubscribe(int event_id, event_cb_t callback)
{
	if (!callback) return SYS_ERR_INVALID_PARAM;

	pthread_mutex_lock(&g_event_bus_mutex);
	for (int i = 0; i < g_event_entry_count; i++) {
		if (g_event_bus[i].event_id == event_id) {
			sys_event_entry_t *entry = &g_event_bus[i];
			for (int j = 0; j < entry->subscriber_count; j++) {
				if (entry->subscribers[j].callback == callback) {
					for (int k = j; k < entry->subscriber_count - 1; k++) {
						entry->subscribers[k] = entry->subscribers[k+1];
					}
					entry->subscriber_count--;
					pthread_mutex_unlock(&g_event_bus_mutex);
					return SYS_OK;
				}
			}
		}
	}

	pthread_mutex_unlock(&g_event_bus_mutex);
	return SYS_ERR_NOT_FOUND;
}

sys_err_t sys_event_publish(int event_id, const void *param, size_t param_len)
{
	pthread_mutex_lock(&g_event_bus_mutex);
	sys_event_entry_t *entry = NULL;
	for (int i = 0; i < g_event_entry_count; i++) {
		if (g_event_bus[i].event_id == event_id) {
			entry = &g_event_bus[i];
			break;
		}
	}

	if (!entry || entry->subscriber_count == 0) {
		pthread_mutex_unlock(&g_event_bus_mutex);
		return SYS_OK;
	}

	int sub_count = entry->subscriber_count;
	sys_subscriber_t subs_copy[MAX_SUBSCRIBERS];
	for (int i = 0; i < sub_count; i++) {
		subs_copy[i] = entry->subscribers[i];
	}
	pthread_mutex_unlock(&g_event_bus_mutex);

	sys_event_t event;
	event.event_id = event_id;
	event.param = (void *)param;
	event.param_len = param_len;

	for (int i = 0; i < sub_count; i++) {
		if (subs_copy[i].callback) {
			subs_copy[i].callback(&event, subs_copy[i].priv_data);
		}
	}
	return SYS_OK;
}

/* =========================================================================
 * 5. 统一字符设备接口 (Char Dev / ioctl Style)
 * ========================================================================= */

sys_err_t sys_subsystem_ioctl(int mod_id, unsigned int cmd, void *arg)
{
	sys_dev_ops_t *ops = (sys_dev_ops_t *)sys_subsystem_get(mod_id);
	if (!ops || !ops->ioctl) {
		return SYS_ERR_NOT_SUPPORTED;
	}
	return ops->ioctl(cmd, arg) == 0 ? SYS_OK : SYS_ERR_GENERIC;
}

/**
 * 占位初始化段弹射保护机制
 *
 * 原因：GNU 链接器的一个特性是，如果某个自定义段（如 app_init_sec）没有任何实体变量被强引用，
 * 该段便不会被保留，导致编译时报 `__start_app_init_sec` 符号未定义的链接错误（Undefined Reference）。
 *
 * 解决策略：我们在核心底层代码中，强制定义一个占位变量放在最低的 ID（-1），并强行加入该段中，
 * 确保该物理段绝对不为空。在执行初始化与注销时，我们对其跳过执行。
 */
static int dummy_prio_init(void)
{
	return 0;
}
APP_REGISTER(dummy_prio_init, NULL, -1);

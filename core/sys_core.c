/**
 * @file sys_core.c
 * @brief 机制总线核心实现文件
 *
 * 本文件实现了解耦总线的数据托管和开机关机时序管理。
 * 针对工业级高并发及动态卸载安全性进行了深度升级：
 * 1. 查表获取接口无锁化设计，高并发下零锁竞争开销。
 * 2. 引入读写锁 (pthread_rwlock_t) 保护执行期生命周期，防止卸载时产生野指针崩溃。
 * 3. 实现了带参数深拷贝的工作队列，由独立分发线程异步消费，达成事件回调“快进快出”防御。
 */

#include "sys_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

/**
 * 链接器特殊边界符号声明
 */
extern sys_initcall_t __start_app_init_sec[];
extern sys_initcall_t __stop_app_init_sec[];

/**
 * @brief 运行时操纵杆托管寄存器
 */
static void *g_subsystem_registry[SYS_CORE_MAX_SLOTS] = { NULL };

/**
 * @brief 读写锁，取代原全局粗暴互斥锁，保障并发读取及动态注册/注销时的生命周期安全
 */
static pthread_rwlock_t g_registry_rwlock = PTHREAD_RWLOCK_INITIALIZER;

/**
 * @brief 时序依赖检测状态变量与历史请求记录表
 */
static volatile int g_initcalls_running_level = 0;
static int g_subsystem_requested_levels[SYS_CORE_MAX_SLOTS] = { 0 };

/* =========================================================================
 * 异步事件工作队列数据结构与线程定义
 * ========================================================================= */
#define EVENT_QUEUE_SIZE 64

typedef struct {
	event_cb_t callback;
	sys_event_t event;
	char param_buf[128]; // 支持最大 128 字节事件参数的深拷贝，防发布端释放后野指针
	void *priv_data;
} sys_event_work_t;

static sys_event_work_t g_event_work_queue[EVENT_QUEUE_SIZE];
static int g_event_queue_in = 0;
static int g_event_queue_out = 0;
static pthread_mutex_t g_event_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_event_queue_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_event_worker_thread;
static volatile int g_event_worker_running = 0;

// 异步事件分发线程主循环
static void *sys_event_worker_loop(void *arg)
{
	(void)arg;
	while (g_event_worker_running) {
		pthread_mutex_lock(&g_event_queue_mutex);
		while (g_event_queue_in == g_event_queue_out && g_event_worker_running) {
			pthread_cond_wait(&g_event_queue_cond, &g_event_queue_mutex);
		}
		if (!g_event_worker_running) {
			pthread_mutex_unlock(&g_event_queue_mutex);
			break;
		}

		// 从环形队列中取出一项工作
		sys_event_work_t work = g_event_work_queue[g_event_queue_out & (EVENT_QUEUE_SIZE - 1)];
		g_event_queue_out++;
		pthread_mutex_unlock(&g_event_queue_mutex);

		// 在锁临界区外部安全调用回调函数，杜绝因回调操作总线引发的死锁
		if (work.callback) {
			if (work.event.param_len > 0) {
				work.event.param = work.param_buf;
			}
			work.callback(&work.event, work.priv_data);
		}
	}
	return NULL;
}

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

	// 复制至栈临时缓冲区中进行就地排序
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

	// 启动异步事件队列消费线程
	g_event_worker_running = 1;
	pthread_create(&g_event_worker_thread, NULL, sys_event_worker_loop, NULL);

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

	// 降序冒泡排序 (LIFO 关机逆序原则)
	for (int i = 0; i < valid_count - 1; i++) {
		for (int j = 0; j < valid_count - i - 1; j++) {
			if (temp_calls[j].mod_id < temp_calls[j + 1].mod_id) {
				sys_initcall_t temp = temp_calls[j];
				temp_calls[j] = temp_calls[j + 1];
				temp_calls[j + 1] = temp;
			}
		}
	}

	// 优雅注销异步事件工作线程
	if (g_event_worker_running) {
		g_event_worker_running = 0;
		pthread_cond_broadcast(&g_event_queue_cond);
		pthread_join(g_event_worker_thread, NULL);
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
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS || !ops) {
		return SYS_ERR_INVALID_PARAM;
	}

	// 1. 获取写锁，阻止并发的读取和注销操作
	pthread_rwlock_wrlock(&g_registry_rwlock);

	// 2. 时序依赖合理性动态校验
	int req_lvl = g_subsystem_requested_levels[mod_id];
	if (req_lvl == 1 && g_initcalls_running_level == 1) {
		fprintf(stderr,
			"[Framework] WARNING: Timing dependency conflict detected! "
			"Module ID %d was requested before it was registered!\n",
			mod_id);
	}

	// 3. 将操作集安全登记到槽位中
	g_subsystem_registry[mod_id] = ops;

	pthread_rwlock_unlock(&g_registry_rwlock);

	return SYS_OK;
}

sys_err_t sys_subsystem_unregister(int mod_id)
{
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
		return SYS_ERR_INVALID_PARAM;
	}

	// 1. 获取写锁，等待当前所有执行读代理的线程运行完毕后再执行注销
	pthread_rwlock_wrlock(&g_registry_rwlock);

	// 2. 将槽位清空为 NULL，回收注册对象
	g_subsystem_registry[mod_id] = NULL;

	pthread_rwlock_unlock(&g_registry_rwlock);

	return SYS_OK;
}

void *sys_subsystem_get(int mod_id)
{
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
		return NULL;
	}

	// 【无锁并发读取】
	// 内存屏障保障并发可见性。针对 aligned 对齐指针存取天生原子性，
	// 不需要排队全局锁，多线程高频并行查找性能达到硬件理论极速。
	__sync_synchronize();
	void *ops = g_subsystem_registry[mod_id];

	if (ops == NULL && g_initcalls_running_level == 1) {
		// 在初始化顺序校验时，才回退加写锁写入请求标志
		pthread_rwlock_wrlock(&g_registry_rwlock);
		g_subsystem_requested_levels[mod_id] = 1;
		pthread_rwlock_unlock(&g_registry_rwlock);
	}
	return ops;
}

void *sys_subsystem_get_lock(int mod_id)
{
	if (mod_id < 0 || mod_id >= SYS_CORE_MAX_SLOTS) {
		return NULL;
	}

	// 锁定读锁，在读锁锁定期内，任何 unregister 控制线程的写锁都将阻塞，
	// 确保在 get 到 ops 并执行其方法期间，模块接口不会被突发注销从内存中剥离
	pthread_rwlock_rdlock(&g_registry_rwlock);
	void *ops = g_subsystem_registry[mod_id];
	if (!ops) {
		pthread_rwlock_unlock(&g_registry_rwlock);
		return NULL;
	}
	return ops;
}

void sys_subsystem_put_lock(int mod_id)
{
	(void)mod_id;
	pthread_rwlock_unlock(&g_registry_rwlock);
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
	sys_event_flag_t flag;
} sys_subscriber_t;

typedef struct {
	int event_id;
	sys_subscriber_t subscribers[MAX_SUBSCRIBERS];
	int subscriber_count;
} sys_event_entry_t;

static sys_event_entry_t g_event_bus[MAX_EVENTS];
static int g_event_entry_count = 0;
static pthread_mutex_t g_event_bus_mutex = PTHREAD_MUTEX_INITIALIZER;

sys_err_t sys_event_subscribe_type(int event_id, event_cb_t callback, void *priv_data, sys_event_flag_t flag)
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
			entry->subscribers[i].flag = flag;
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
	entry->subscribers[entry->subscriber_count].flag = flag;
	entry->subscriber_count++;

	pthread_mutex_unlock(&g_event_bus_mutex);
	return SYS_OK;
}

sys_err_t sys_event_subscribe(int event_id, event_cb_t callback, void *priv_data)
{
	return sys_event_subscribe_type(event_id, callback, priv_data, EVENT_FLAG_SYNC);
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
						entry->subscribers[k] = entry->subscribers[k + 1];
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
		if (subs_copy[i].flag == EVENT_FLAG_SYNC) {
			if (subs_copy[i].callback) {
				subs_copy[i].callback(&event, subs_copy[i].priv_data);
			}
		} else {
			// 异步事件分发：将事件进行深拷贝并投递至全局工作队列中，防回调阻塞发布线程
			pthread_mutex_lock(&g_event_queue_mutex);
			if (g_event_queue_in - g_event_queue_out >= EVENT_QUEUE_SIZE) {
				// 异步工作队列满，进行丢包处理 (符合嵌入式容错策略)
				pthread_mutex_unlock(&g_event_queue_mutex);
				continue;
			}
			sys_event_work_t *work = &g_event_work_queue[g_event_queue_in & (EVENT_QUEUE_SIZE - 1)];
			work->callback = subs_copy[i].callback;
			work->event = event;
			work->priv_data = subs_copy[i].priv_data;
			if (param && param_len > 0) {
				size_t copy_size = param_len > sizeof(work->param_buf) ? sizeof(work->param_buf) : param_len;
				memcpy(work->param_buf, param, copy_size);
				work->event.param_len = copy_size;
			}
			g_event_queue_in++;
			pthread_cond_signal(&g_event_queue_cond);
			pthread_mutex_unlock(&g_event_queue_mutex);
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
 */
static int dummy_prio_init(void)
{
	return 0;
}
APP_REGISTER(dummy_prio_init, NULL, -1);

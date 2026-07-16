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
#include <pthread.h>

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

/**
 * @brief 安全获取指定模块的操纵杆并锁定读生命周期
 * @param mod_id 统一的业务引脚槽位 ID
 * @return void* 对应操作指针 (如返回非 NULL，必须在使用毕后调用 sys_subsystem_put_lock 释放)
 *
 * 通过获取读锁（Shared Lock）防止其他控制线程在调用期间执行注销/卸载操作，杜绝野指针崩溃。
 */
void *sys_subsystem_get_lock(int mod_id);

/**
 * @brief 释放由 sys_subsystem_get_lock 加锁的读生命周期锁
 * @param mod_id 统一的业务引脚槽位 ID
 */
void sys_subsystem_put_lock(int mod_id);

/* =========================================================================
 * 3. 零拷贝数据通道机制 (Zero-Copy Ring Buffer & Buffer Pool)
 * ========================================================================= */

/**
 * @brief 单帧/单块共享缓存描述符
 */
typedef struct {
	void *payload;          // 实际物理内存首地址
	size_t length;          // 有效数据长度
	size_t capacity;        // 缓冲区物理容量
	unsigned long timestamp;// 时间戳 (微秒)
	unsigned int frame_seq; // 帧序列号
	int ref_count;          // 引用计数
} sys_buffer_t;

/**
 * @brief 环形共享队列 (单读单写或多读多写 SPSC/MPMC RingBuffer)
 */
typedef struct {
	sys_buffer_t **buffers; // 缓冲区描述符指针数组
	unsigned int size;      // 队列深度 (必须为 2 的幂)
	unsigned int in;        // 写指针
	unsigned int out;       // 读指针
	pthread_mutex_t lock;   // 互斥锁
	pthread_cond_t cond;    // 消费者条件变量
} sys_ringbuf_t;

// Buffer pool & RingBuffer API
sys_buffer_t *sys_buffer_get_free(sys_buffer_t *pool, int pool_size);
void sys_buffer_ref(sys_buffer_t *buf);
void sys_buffer_unref(sys_buffer_t *buf);

sys_ringbuf_t *sys_ringbuf_create(unsigned int size);
void sys_ringbuf_destroy(sys_ringbuf_t *rb);
sys_err_t sys_ringbuf_put(sys_ringbuf_t *rb, sys_buffer_t *buf);
sys_buffer_t *sys_ringbuf_get(sys_ringbuf_t *rb, int timeout_ms);
unsigned int sys_ringbuf_count(sys_ringbuf_t *rb);

/* =========================================================================
 * 4. 异步发布-订阅事件总线 (Pub/Sub Event Bus)
 * ========================================================================= */

// 系统及业务事件 ID 定义
#define EVENT_SYS_READY          100
#define EVENT_SYS_SHUTDOWN       101
#define EVENT_CAM_STREAM_START   200
#define EVENT_CAM_STREAM_STOP    201
#define EVENT_WIFI_CONNECTED     300
#define EVENT_WIFI_DISCONNECTED  301

typedef struct {
	int event_id;       // 事件类型 (如 EVENT_CAM_STREAM_START 等)
	void *param;        // 携带的事件参数指针
	size_t param_len;   // 参数长度
} sys_event_t;

// 事件订阅回调函数类型
typedef void (*event_cb_t)(const sys_event_t *event, void *priv_data);

/**
 * @brief 事件分发模式 (同步/异步)
 */
typedef enum {
	EVENT_FLAG_SYNC = 0,   // 同步回调模式：在发布线程中立刻调用
	EVENT_FLAG_ASYNC = 1   // 异步回调模式：深拷贝数据推入工作队列，独立线程异步消费
} sys_event_flag_t;

sys_err_t sys_event_subscribe(int event_id, event_cb_t callback, void *priv_data);
sys_err_t sys_event_subscribe_type(int event_id, event_cb_t callback, void *priv_data, sys_event_flag_t flag);
sys_err_t sys_event_unsubscribe(int event_id, event_cb_t callback);
sys_err_t sys_event_publish(int event_id, const void *param, size_t param_len);

/* =========================================================================
 * 5. 统一字符设备接口 (Char Dev / ioctl Style)
 * ========================================================================= */

/**
 * @brief 虚拟字符设备操作表
 */
typedef struct {
	int (*open)(void);
	int (*close)(void);
	int (*read)(void *buf, size_t count);
	int (*write)(const void *buf, size_t count);
	int (*ioctl)(unsigned int cmd, void *arg);
} sys_dev_ops_t;

sys_err_t sys_subsystem_ioctl(int mod_id, unsigned int cmd, void *arg);

#endif // SYS_CORE_H

#include "camera_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#define CAM_POOL_SIZE 4
#define CAM_FRAME_CAPACITY 256

static sys_buffer_t g_cam_buffer_pool[CAM_POOL_SIZE];
static volatile int g_cam_streaming = 0;
static pthread_t g_cam_thread;
static sys_ringbuf_t *g_cam_rb = NULL;
static unsigned int g_frame_counter = 0;

// 模拟硬件中断/数据采集线程入口：向环形队列中放入视频帧
static void *camera_producer_worker(void *arg)
{
	(void)arg;
	printf("[Camera] 摄像头视频帧采集线程已启动...\n");
	while (g_cam_streaming) {
		// 1. 从共享缓冲池中获取空闲块 (Zero-Copy 资源复用)
		sys_buffer_t *buf = sys_buffer_get_free(g_cam_buffer_pool, CAM_POOL_SIZE);
		if (buf == NULL) {
			// 缓冲池已满，等待消费者消费，稍微避让
			usleep(10000); // 10ms
			continue;
		}

		// 2. 模拟向缓冲区写入原始摄像头帧数据
		snprintf((char *)buf->payload, buf->capacity, "CAMERA_RAW_FRAME_#%03u_MOCK_YUV", g_frame_counter);
		buf->length = strlen((char *)buf->payload) + 1;
		buf->frame_seq = g_frame_counter++;

		struct timeval tv;
		gettimeofday(&tv, NULL);
		buf->timestamp = tv.tv_sec * 1000000ULL + tv.tv_usec;

		// 3. 将 Buffer 指针推入环形队列 (QBUF 入队)
		sys_ringbuf_put(g_cam_rb, buf);

		printf("[Camera] 成功采集并发布视频帧 - Seq: %u, Size: %zu bytes, 缓冲区引用计数: %d\n",
		       buf->frame_seq, buf->length, buf->ref_count);

		// 4. 减一引用计数 (因为 get_free 会自动 +1 表示本分配，put 会再 +1，
		// 现在交给 RingBuffer 后本线程不再持有它，故做一次 unref 移交所有权)
		sys_buffer_unref(buf);

		// 模拟 5 FPS 帧率 (200ms 一帧)
		usleep(200000);
	}
	printf("[Camera] 摄像头视频帧采集线程已退出。\n");
	return NULL;
}

// 兼容原系统接口实现
static int cam_real_start(int fps, int w, int h)
{
	printf("[Camera驱动] 开启视频流成功！参数: %d FPS, 分辨率: %dx%d\n", fps, w, h);
	return 0;
}

static const camera_ops_t my_real_cam_ops = {
	.start_stream = cam_real_start,
	.get_frame_buffer = NULL,
	.stop_stream = NULL
};

// 虚拟字符设备操作表
static int camera_real_ioctl(unsigned int cmd, void *arg)
{
	(void)arg;
	switch (cmd) {
	case CMD_CAM_START_STREAM:
		if (g_cam_streaming) return 0;

		// 创建 RingBuffer，深度为 4
		g_cam_rb = sys_ringbuf_create(CAM_POOL_SIZE);
		if (!g_cam_rb) return -1;

		g_cam_streaming = 1;
		pthread_create(&g_cam_thread, NULL, camera_producer_worker, NULL);

		// 发布流媒体启动事件，将 RingBuffer 指针广播给订阅者 (如 ZLMedia)
		sys_event_publish(EVENT_CAM_STREAM_START, &g_cam_rb, sizeof(sys_ringbuf_t *));
		printf("[Camera] 成功启动摄像头采集并广播 EVENT_CAM_STREAM_START 事件。\n");
		break;

	case CMD_CAM_STOP_STREAM:
		if (!g_cam_streaming) return 0;

		printf("[Camera] 正在停止摄像头采集并广播 EVENT_CAM_STREAM_STOP 事件...\n");
		// 先广播通知消费者停止消费
		sys_event_publish(EVENT_CAM_STREAM_STOP, NULL, 0);

		g_cam_streaming = 0;
		pthread_join(g_cam_thread, NULL);

		sys_ringbuf_destroy(g_cam_rb);
		g_cam_rb = NULL;
		break;

	default:
		return -1;
	}
	return 0;
}

static const sys_dev_ops_t my_camera_dev_ops = {
	.open = NULL,
	.close = NULL,
	.read = NULL,
	.write = NULL,
	.ioctl = camera_real_ioctl
};

static int camera_subsys_init(void)
{
	printf("[Camera驱动] 正在自加载过程...\n");

	// 初始化共享内存缓冲池
	for (int i = 0; i < CAM_POOL_SIZE; i++) {
		g_cam_buffer_pool[i].payload = malloc(CAM_FRAME_CAPACITY);
		g_cam_buffer_pool[i].capacity = CAM_FRAME_CAPACITY;
		g_cam_buffer_pool[i].length = 0;
		g_cam_buffer_pool[i].ref_count = 0;
		g_cam_buffer_pool[i].frame_seq = 0;
	}

	// 注册传统接口槽位，用以维持安全代理向后兼容性
	sys_subsystem_register(SYS_MOD_CAMERA, (void *)&my_real_cam_ops);

	// 统一注册字符设备到对应模块引脚槽，支持 VFS/ioctl 控制
	sys_subsystem_register(SYS_MOD_CAMERA, (void *)&my_camera_dev_ops);

	return 0;
}

static void camera_subsys_exit(void)
{
	printf("[Camera驱动] 正在注销释放过程...\n");

	if (g_cam_streaming) {
		camera_real_ioctl(CMD_CAM_STOP_STREAM, NULL);
	}

	// 释放共享缓存块内存
	for (int i = 0; i < CAM_POOL_SIZE; i++) {
		free(g_cam_buffer_pool[i].payload);
		g_cam_buffer_pool[i].payload = NULL;
	}

	sys_subsystem_unregister(SYS_MOD_CAMERA);
}

APP_REGISTER(camera_subsys_init, camera_subsys_exit, SYS_MOD_CAMERA);

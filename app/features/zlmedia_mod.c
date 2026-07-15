#include "zlmedia_mod.h"
#include "sys_core.h"
#include "app_modules.h"
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

static int g_zlm_port = 8554;
static volatile int g_zlm_streaming = 0;
static pthread_t g_zlm_thread;
static sys_ringbuf_t *g_zlm_rb = NULL;

// 消费者线程入口：从环形缓冲区中提取视频帧并消耗
static void *zlmedia_consumer_worker(void *arg)
{
	(void)arg;
	printf("[ZLMedia] 流媒体服务器拉流线程已启动...\n");
	while (g_zlm_streaming) {
		// 带 500ms 超时获取 Buffer，防止关闭时卡死
		sys_buffer_t *buf = sys_ringbuf_get(g_zlm_rb, 500);
		if (buf) {
			// 读取 payload 模拟帧处理，全程零拷贝
			printf("[ZLMedia] 成功消费视频帧 - Seq: %u, Size: %zu bytes, Payload: %s, 缓冲区引用计数: %d\n",
			       buf->frame_seq, buf->length, (char *)buf->payload, buf->ref_count);
			
			// 模拟处理耗时
			usleep(100000); // 100ms
			
			// 释放引用计数，归还共享内存池
			sys_buffer_unref(buf);
		}
	}
	printf("[ZLMedia] 流媒体服务器拉流线程已退出。\n");
	return NULL;
}

// 订阅 Camera 事件的回调函数
static void on_camera_stream_event(const sys_event_t *event, void *priv_data)
{
	(void)priv_data;
	if (event->event_id == EVENT_CAM_STREAM_START) {
		if (event->param_len == sizeof(sys_ringbuf_t *)) {
			g_zlm_rb = *(sys_ringbuf_t **)event->param;
			g_zlm_streaming = 1;
			printf("[ZLMedia] 检测到 Camera 视频流开始事件，正在拉取流数据...\n");
			pthread_create(&g_zlm_thread, NULL, zlmedia_consumer_worker, NULL);
		}
	} else if (event->event_id == EVENT_CAM_STREAM_STOP) {
		printf("[ZLMedia] 检测到 Camera 视频流停止事件，正在释放消费句柄...\n");
		g_zlm_streaming = 0;
		pthread_join(g_zlm_thread, NULL);
		g_zlm_rb = NULL;
	}
}

// 设备 ioctl 接口控制
static int zlmedia_real_ioctl(unsigned int cmd, void *arg)
{
	switch (cmd) {
	case CMD_ZLM_GET_STATUS:
		if (arg) {
			*(int *)arg = g_zlm_streaming;
		}
		break;
	case CMD_ZLM_SET_PORT:
		if (arg) {
			g_zlm_port = *(int *)arg;
			printf("[ZLMedia] ioctl: 修改流媒体监听端口为 %d\n", g_zlm_port);
		}
		break;
	default:
		return -1;
	}
	return 0;
}

static const sys_dev_ops_t my_zlmedia_dev_ops = {
	.open = NULL,
	.close = NULL,
	.read = NULL,
	.write = NULL,
	.ioctl = zlmedia_real_ioctl
};

static int zlmedia_subsys_init(void)
{
	printf("[ZLMedia] 正在自加载过程...\n");

	// 注册设备操作表
	sys_subsystem_register(SYS_MOD_ZLMEDIA, (void *)&my_zlmedia_dev_ops);

	// 订阅控制流事件 (Camera 开始/结束事件)
	sys_event_subscribe(EVENT_CAM_STREAM_START, on_camera_stream_event, NULL);
	sys_event_subscribe(EVENT_CAM_STREAM_STOP, on_camera_stream_event, NULL);

	return 0;
}

static void zlmedia_subsys_exit(void)
{
	printf("[ZLMedia] 正在注销释放过程...\n");

	// 取消订阅
	sys_event_unsubscribe(EVENT_CAM_STREAM_START, on_camera_stream_event);
	sys_event_unsubscribe(EVENT_CAM_STREAM_STOP, on_camera_stream_event);

	// 如果仍在运行，强制关闭
	if (g_zlm_streaming) {
		g_zlm_streaming = 0;
		pthread_join(g_zlm_thread, NULL);
	}

	sys_subsystem_unregister(SYS_MOD_ZLMEDIA);
}

APP_REGISTER(zlmedia_subsys_init, zlmedia_subsys_exit, SYS_MOD_ZLMEDIA);

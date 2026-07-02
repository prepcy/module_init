#ifndef WIFI_MOD_H
#define WIFI_MOD_H

// WiFi 专有的特殊结构体
typedef struct {
	int (*connect)(const char *ssid, const char *pwd);
	int (*get_rssi)(void);
	void (*disconnect)(void);
} wifi_ops_t;

#endif // WIFI_MOD_H

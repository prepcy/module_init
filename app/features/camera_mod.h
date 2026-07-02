#ifndef CAMERA_MOD_H
#define CAMERA_MOD_H

typedef struct {
	int (*start_stream)(int fps, int resolution_width, int resolution_height);
	void *(*get_frame_buffer)(void);
	void (*stop_stream)(void);
} camera_ops_t;

#endif // CAMERA_MOD_H

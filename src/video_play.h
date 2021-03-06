#include <stdio.h>
#include "SDL/SDL.h"
#include "video_record.h"
#include "include.h"


#ifndef VIDEO_PLAY_H
#define VIDEO_PLAY_H
	SDL_Event event;

  pthread_mutex_t jpg_mutex;

	void print_overlay_info();
	void video_play_init();
	void video_frame_decompress();
	void video_frame_display(int bufferIndex);
	int sdl_init();
	void sdl_quit();
	void keyboard_capture();
	void xioctl(int ctrl, int value);
	void pan_relative(int pan);
	void tilt_relative(int tilt);
	void pan_reset();
	void tilt_reset();
	void panTilt_reset();
#endif

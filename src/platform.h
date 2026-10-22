#ifndef RHUN_DOOM_PLATFORM_H
#define RHUN_DOOM_PLATFORM_H
#include <stddef.h>
#include <stdint.h>

void doom_key_set(unsigned char key, int down);
void doom_keys_release(void);
void platform_audio_gate(int enabled);
void platform_audio_shutdown(void);
void platform_music_update(void);
void platform_audio_stats(unsigned long *samples, unsigned long *sounds);

size_t doom_png(const unsigned char *pixels, const unsigned char palette[768], unsigned char *out,
                size_t cap);
#endif

#include "doomtype.h"
#include "i_sound.h"
#include "platform.h"
#include "w_wad.h"
#include "z_zone.h"
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { RATE = 22050, BLOCK = 512, CHANNELS = 16 };
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
typedef struct {
    const unsigned char *data;
    uint32_t len, rate;
    double pos;
    float left, right;
} voice;
static voice voices[CHANNELS];
static AudioQueueRef queue;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int audible;
static _Atomic unsigned long mixed_samples, sound_starts;
static AUGraph graph;
static AudioUnit synth;
static int music_volume = 100;

static void mix(void *user, AudioQueueRef q, AudioQueueBufferRef buf) {
    (void)user;
    int16_t *out = buf->mAudioData;
    memset(out, 0, BLOCK * 4);
    pthread_mutex_lock(&lock);
    if (atomic_load(&audible)) {
        for (int f = 0; f < BLOCK; f++) {
            float left = 0, right = 0;
            for (int c = 0; c < CHANNELS; c++) {
                voice *v = &voices[c];
                if (!v->data)
                    continue;
                if (v->pos >= v->len) {
                    v->data = NULL;
                    continue;
                }
                float sample = ((int)v->data[(uint32_t)v->pos] - 128) * 128.0f;
                v->pos += (double)v->rate / RATE;
                left += sample * v->left;
                right += sample * v->right;
            }
            if (left > 32767)
                left = 32767;
            if (left < -32768)
                left = -32768;
            if (right > 32767)
                right = 32767;
            if (right < -32768)
                right = -32768;
            out[f * 2] = (int16_t)left;
            out[f * 2 + 1] = (int16_t)right;
            if (left || right)
                atomic_fetch_add(&mixed_samples, 1);
        }
    }
    pthread_mutex_unlock(&lock);
    buf->mAudioDataByteSize = BLOCK * 4;
    AudioQueueEnqueueBuffer(q, buf, 0, NULL);
}
static boolean sound_init(boolean prefix) {
    (void)prefix;
    AudioStreamBasicDescription f = {0};
    f.mSampleRate = RATE;
    f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    f.mBytesPerPacket = 4;
    f.mFramesPerPacket = 1;
    f.mBytesPerFrame = 4;
    f.mChannelsPerFrame = 2;
    f.mBitsPerChannel = 16;
    OSStatus s = AudioQueueNewOutput(&f, mix, NULL, NULL, NULL, 0, &queue);
    if (s) {
        fprintf(stderr, "DOOM audio unavailable: %d\n", (int)s);
        queue = NULL;
        return false;
    }
    for (int i = 0; i < 3; i++) {
        AudioQueueBufferRef b;
        if (AudioQueueAllocateBuffer(queue, BLOCK * 4, &b))
            return false;
        mix(NULL, queue, b);
    }
    s = AudioQueueStart(queue, NULL);
    fprintf(stderr, "DOOM AudioQueue: %d\n", (int)s);
    return s == 0;
}
static void sound_shutdown(void) {
    if (queue) {
        AudioQueueDispose(queue, 1);
        queue = NULL;
    }
}
static int lumpnum(sfxinfo_t *sfx) {
    char name[16];
    snprintf(name, sizeof name, "ds%s", sfx->name);
    return W_CheckNumForName(name);
}
static void sound_update(void) {}
static void params(int ch, int vol, int sep) {
    if (ch < 0 || ch >= CHANNELS)
        return;
    pthread_mutex_lock(&lock);
    voices[ch].left = vol / 127.0f * (254 - sep) / 254.0f;
    voices[ch].right = vol / 127.0f * sep / 254.0f;
    pthread_mutex_unlock(&lock);
}
static int start(sfxinfo_t *sfx, int ch, int vol, int sep) {
    if (ch < 0 || ch >= CHANNELS)
        return -1;
    int lump = sfx->lumpnum >= 0 ? sfx->lumpnum : lumpnum(sfx);
    if (lump < 0)
        return -1;
    unsigned len = W_LumpLength(lump);
    const unsigned char *p = W_CacheLumpNum(lump, PU_STATIC);
    if (len < 40 || p[0] != 3 || p[1] != 0)
        return -1;
    uint32_t rate = p[2] | p[3] << 8, n = p[4] | p[5] << 8 | p[6] << 16 | p[7] << 24;
    if (n > len - 8 || n <= 32 || !rate)
        return -1;
    pthread_mutex_lock(&lock);
    voices[ch] = (voice){
        p + 24, n - 32, rate, 0, vol / 127.0f * (254 - sep) / 254.0f, vol / 127.0f * sep / 254.0f};
    pthread_mutex_unlock(&lock);
    atomic_fetch_add(&sound_starts, 1);
    return ch;
}
static void stop(int ch) {
    if (ch < 0 || ch >= CHANNELS)
        return;
    pthread_mutex_lock(&lock);
    voices[ch].data = NULL;
    pthread_mutex_unlock(&lock);
}
static boolean playing(int ch) {
    if (ch < 0 || ch >= CHANNELS)
        return false;
    pthread_mutex_lock(&lock);
    int yes = voices[ch].data != NULL;
    pthread_mutex_unlock(&lock);
    return yes;
}
static void cache(sfxinfo_t *sounds, int n) {
    (void)sounds;
    (void)n;
}
static snddevice_t devices[] = {SNDDEVICE_SB};
sound_module_t DG_sound_module = {devices, 1,     sound_init, sound_shutdown, lumpnum, sound_update,
                                  params,  start, stop,       playing,        cache};

typedef struct {
    unsigned char *data;
    size_t len, start, pos;
    uint64_t tick;
    int loop, active;
    unsigned char velocity[16];
} song;
static song *current;
static uint64_t music_epoch, music_pause;
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void midi(unsigned status, unsigned a, unsigned b) {
    if (synth)
        MusicDeviceMIDIEvent(synth, status, a, b, 0);
}
static void silence(void) {
    for (int ch = 0; ch < 16; ch++)
        midi(0xb0 | ch, 120, 0);
}
static boolean music_init(void) {
    if (graph)
        return true;
    AUNode instrument, output;
    AudioComponentDescription d = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
                                   kAudioUnitManufacturer_Apple, 0, 0};
    OSStatus s = NewAUGraph(&graph);
    if (!s)
        s = AUGraphAddNode(graph, &d, &instrument);
    d.componentType = kAudioUnitType_Output;
    d.componentSubType = kAudioUnitSubType_DefaultOutput;
    if (!s)
        s = AUGraphAddNode(graph, &d, &output);
    if (!s)
        s = AUGraphOpen(graph);
    if (!s)
        s = AUGraphNodeInfo(graph, instrument, NULL, &synth);
    if (!s)
        s = AUGraphConnectNodeInput(graph, instrument, 0, output, 0);
    if (!s)
        s = AUGraphInitialize(graph);
    if (!s)
        s = AUGraphStart(graph);
    fprintf(stderr, "DOOM music synth: %d\n", (int)s);
    if (s) {
        if (graph)
            DisposeAUGraph(graph);
        graph = NULL;
        synth = NULL;
        return false;
    }
    return true;
}
static void music_shutdown(void) {
    silence();
    if (graph) {
        AUGraphStop(graph);
        DisposeAUGraph(graph);
        graph = NULL;
        synth = NULL;
    }
}
static void music_set_volume(int v) {
    music_volume = v;
    for (int ch = 0; ch < 16; ch++)
        midi(0xb0 | ch, 7, v);
}
static void music_pause_song(void) {
    if (!music_pause)
        music_pause = now_ms();
    silence();
}
static void music_resume_song(void) {
    if (music_pause) {
        music_epoch += now_ms() - music_pause;
        music_pause = 0;
    }
}
static void *music_register(void *data, int len) {
    unsigned char *p = data;
    if (len < 16 || memcmp(p, "MUS\x1a", 4))
        return NULL;
    size_t start = p[6] | p[7] << 8, n = p[4] | p[5] << 8;
    if (start < 16 || start > (size_t)len || n > (size_t)len - start)
        return NULL;
    song *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->data = malloc(start + n);
    if (!s->data) {
        free(s);
        return NULL;
    }
    memcpy(s->data, p, start + n);
    s->len = start + n;
    s->start = start;
    s->pos = start;
    memset(s->velocity, 100, sizeof s->velocity);
    return s;
}
static void music_unregister(void *handle) {
    song *s = handle;
    if (!s)
        return;
    if (current == s)
        current = NULL;
    free(s->data);
    free(s);
}
static void music_play(void *handle, boolean loop) {
    current = handle;
    if (!current)
        return;
    current->pos = current->start;
    current->tick = 0;
    current->active = 1;
    current->loop = loop;
    music_epoch = now_ms();
    music_pause = atomic_load(&audible) ? 0 : music_epoch;
    silence();
    music_set_volume(music_volume);
}
static void music_stop(void) {
    if (current)
        current->active = 0;
    silence();
}
static boolean music_playing(void) { return current && current->active; }
static int take(song *s, unsigned *v) {
    if (s->pos >= s->len)
        return 0;
    *v = s->data[s->pos++];
    return 1;
}
void platform_music_update(void) {
    song *s = current;
    if (!s || !s->active || music_pause || !atomic_load(&audible))
        return;
    uint64_t target = (now_ms() - music_epoch) * 140 / 1000;
    static const unsigned cc[] = {0, 0, 1, 7, 10, 11, 91, 93, 64, 67, 120, 123, 126, 127, 121};
    for (unsigned budget = 0; s->tick <= target && budget < 4096; budget++) {
        unsigned event, a, b = 0;
        if (!take(s, &event)) {
            music_stop();
            return;
        }
        unsigned musch = event & 15,
                 ch = musch == 15  ? 9
                      : musch >= 9 ? musch + 1
                                   : musch,
                 type = (event >> 4) & 7;
        if (type == 6) {
            if (!s->loop) {
                music_stop();
                return;
            }
            silence();
            s->pos = s->start;
            s->tick = target + 1;
        } else {
            if (!take(s, &a)) {
                music_stop();
                return;
            }
            switch (type) {
            case 0:
                midi(0x80 | ch, a & 127, 64);
                break;
            case 1:
                if (a & 128) {
                    if (!take(s, &b)) {
                        music_stop();
                        return;
                    }
                    s->velocity[musch] = b & 127;
                }
                midi(0x90 | ch, a & 127, s->velocity[musch]);
                break;
            case 2:
                midi(0xe0 | ch, (a & 1) << 6, (a >> 1) & 127);
                break;
            case 3:
                if (a >= 10 && a <= 14)
                    midi(0xb0 | ch, cc[a], 0);
                break;
            case 4:
                if (!take(s, &b)) {
                    music_stop();
                    return;
                }
                if (a == 0)
                    midi(0xc0 | ch, b & 127, 0);
                else if (a < 10)
                    midi(0xb0 | ch, cc[a], b & 127);
                break;
            default:
                music_stop();
                return;
            }
        }
        if (event & 128) {
            uint32_t delta = 0;
            unsigned n = 0;
            do {
                if (!take(s, &a) || ++n > 5) {
                    music_stop();
                    return;
                }
                delta = (delta << 7) | (a & 127);
            } while (a & 128);
            s->tick += delta;
        }
    }
}
music_module_t DG_music_module = {devices,
                                  1,
                                  music_init,
                                  music_shutdown,
                                  music_set_volume,
                                  music_pause_song,
                                  music_resume_song,
                                  music_register,
                                  music_unregister,
                                  music_play,
                                  music_stop,
                                  music_playing,
                                  platform_music_update};
void platform_audio_gate(int enabled) {
    int old = atomic_exchange(&audible, !!enabled);
    if (enabled && !old)
        music_resume_song();
    if (!enabled && old)
        music_pause_song();
}
void platform_audio_shutdown(void) {
    atomic_store(&audible, 0);
    sound_shutdown();
    music_shutdown();
}
void platform_audio_stats(unsigned long *samples, unsigned long *sounds) {
    *samples = atomic_load(&mixed_samples);
    *sounds = atomic_load(&sound_starts);
}

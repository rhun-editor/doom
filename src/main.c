#define _POSIX_C_SOURCE 200809L
#include "doomgeneric.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "g_game.h"
#include "i_video.h"
#include "m_config.h"
#include "m_controls.h"
#include "m_menu.h"
#include "p_mobj.h"
#include "platform.h"
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern void M_ClearMenus(void);

/* The engine owns stdout for its old diagnostic messages. The SDK writes to a
 * duplicate of the original pipe, so those messages can never corrupt framing. */
static FILE *rhun_output;
struct rx_ext;
static int doom_idle(struct rx_ext *ext);
#undef stdout
#define stdout rhun_output
#define RX_IDLE(ext) doom_idle(ext)
#define RHUNEXT_IMPLEMENTATION
#include "rhunext.h"
#undef stdout

static rx_ext *host;
static int64_t view, image;
static int started, focused, suspended, muted, manual_pause;
static int close_requested;
static int redraw_needed;
static int closure_pending(void);
static void set_pause(int on);
static void forget_view(void);
static int view_w = 960, view_h = 650;
static uint64_t epoch, pause_at, paused_ms, last_draw, next_tick;
static unsigned long frames, bytes;
static unsigned char png[67000];
static char wad_path[PATH_MAX], state_path[PATH_MAX];
static unsigned char key_state[256];
static uint16_t key_queue[1024];
static unsigned key_read, key_write;
static uint64_t pulse_until[256];
static unsigned char hotkey_down[256];
static uint64_t last_diagnostics;
static uint64_t wall_ms(void);
static void diagnostics(void) {
    if (!getenv("DOOM_DIAGNOSTICS"))
        return;
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/doom-stats.json", state_path) >= (int)sizeof path)
        return;
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    unsigned long samples, sounds;
    platform_audio_stats(&samples, &sounds);
    mobj_t *mo = started ? players[consoleplayer].mo : NULL;
    fprintf(f,
            "{\"wall_ms\":%llu,\"paired\":%d,\"frames\":%lu,\"bytes\":%lu,\"sounds\":%lu,"
            "\"samples\":%lu,\"paused\":%d,\"focused\":%d,\"ticks_ms\":%u,\"x\":%d,\"y\":%d,"
            "\"angle\":%u,\"ammo\":%d}\n",
            (unsigned long long)wall_ms(), host ? rx_has(host, "view-key-state") : 0, frames, bytes,
            sounds, samples, suspended, focused, started ? DG_GetTicksMs() : 0, mo ? mo->x : 0,
            mo ? mo->y : 0, mo ? mo->angle : 0, started ? players[consoleplayer].ammo[am_clip] : 0);
    fclose(f);
    if (snprintf(path, sizeof path, "%s/doom-stats.jsonl", state_path) >= (int)sizeof path)
        return;
    f = fopen(path, "a");
    if (!f)
        return;
    fprintf(
        f,
        "{\"wall_ms\":%llu,\"paired\":%d,\"frames\":%lu,\"sounds\":%lu,\"samples\":%lu,\"paused\":%"
        "d,\"focused\":%d,\"ticks_ms\":%u,\"x\":%d,\"y\":%d,\"angle\":%u,\"ammo\":%d}\n",
        (unsigned long long)wall_ms(), host ? rx_has(host, "view-key-state") : 0, frames, sounds,
        samples, suspended, focused, started ? DG_GetTicksMs() : 0, mo ? mo->x : 0, mo ? mo->y : 0,
        mo ? mo->angle : 0, started ? players[consoleplayer].ammo[am_clip] : 0);
    fclose(f);
}

static uint64_t wall_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
void doom_key_set(unsigned char key, int down) {
    down = !!down;
    if (key_state[key] == down)
        return;
    key_state[key] = down;
    unsigned next = (key_write + 1) % 1024;
    if (next == key_read) {
        key_read = key_write = 0;
        memset(key_state, 0, sizeof key_state);
        return;
    }
    key_queue[key_write] = (uint16_t)((down << 8) | key);
    key_write = next;
}
void doom_keys_release(void) {
    for (int i = 0; i < 256; i++) {
        pulse_until[i] = 0;
        hotkey_down[i] = 0;
        doom_key_set((unsigned char)i, 0);
    }
}
int DG_GetKey(int *pressed, unsigned char *key) {
    if (key_read == key_write)
        return 0;
    uint16_t v = key_queue[key_read];
    key_read = (key_read + 1) % 1024;
    *pressed = v >> 8;
    *key = v & 255;
    return 1;
}
uint32_t DG_GetTicksMs(void) {
    return (uint32_t)((pause_at ? pause_at : wall_ms()) - epoch - paused_ms);
}
void DG_SleepMs(uint32_t ms) {
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
void DG_Init(void) {}
void DG_SetWindowTitle(const char *title) { (void)title; }

static void draw_view(void) { redraw_needed = 1; }

static void draw_view_now(void) {
    if (!host || !view || closure_pending())
        return;
    rx_buf p = {0};
    rx_rect(&p, 0, 0, view_w, view_h, "#101014");
    double available_h = view_h - 92, w = view_w - 32;
    if (w < 16)
        w = 16;
    if (available_h < 16)
        available_h = 16;
    if (w > available_h * 4 / 3)
        w = available_h * 4 / 3;
    double h = w * 3 / 4, x = (view_w - w) / 2, y = 42 + (available_h - h) / 2;
    rx_label(&p, 16, 12, "normal", "left", "#f0d3a0", "DOOM");
    rx_label(&p, view_w - 16, 14, "small", "right", "muted",
             suspended ? "PAUSED. Click to resume."
             : focused ? "ESC menu  /  P pause"
                       : "Click the game to play");
    if (image > 0)
        rx_pic_image(&p, x, y, w, h, image);
    if (view_w >= 650) {
        rx_label(&p, view_w / 2, view_h - 37, "small", "center", "muted",
                 "WASD move / arrows turn / Space fire / E use / Shift run");
        rx_label(&p, view_w / 2, view_h - 19, "small", "center", "muted",
                 "1-7 weapons / Enter confirm / M sound / P pause");
    } else
        rx_label(&p, view_w / 2, view_h - 27, "small", "center", "muted",
                 "WASD / Space fire / E use / ESC menu");
    if (rx_draw(host, view, &p) < 0) {
        /* Closing a tab can overtake the one synchronous draw in flight. */
        if (rx_error_is(host, "noview"))
            forget_view();
        else
            fprintf(stderr, "DOOM draw: %s\n", rx_error(host));
    }
    rx_buf_free(&p);
}
static int closure_pending(void) {
    char closed[80];
    snprintf(closed, sizeof closed, "0 view %lld closed", (long long)view);
    for (rx_msg *m = host->queue; m; m = m->next)
        if (!strcmp(m->line, closed))
            return 1;
    return 0;
}
void DG_DrawFrame(void) {
    uint64_t now = wall_ms();
    if (!view || now - last_draw < 20 || closure_pending())
        return;
    unsigned char pal[768];
    for (int i = 0; i < 256; i++) {
        pal[i * 3] = colors[i].r;
        pal[i * 3 + 1] = colors[i].g;
        pal[i * 3 + 2] = colors[i].b;
    }
    size_t len = doom_png(DG_ScreenBuffer, pal, png, sizeof png);
    if (!len)
        return;
    int64_t next = rx_image(host, png, len);
    if (next < 1) {
        fprintf(stderr, "DOOM frame: %s\n", rx_error(host));
        return;
    }
    if (closure_pending()) {
        rx_unimage_async(host, next);
        return;
    }
    int64_t old = image;
    image = next;
    draw_view_now();
    redraw_needed = 0;
    if (old > 0)
        rx_unimage_async(host, old);
    last_draw = now;
    frames++;
    bytes += len;
}
static void set_pause(int on) {
    on = !!on;
    if (on == suspended)
        return;
    suspended = on;
    if (on) {
        pause_at = wall_ms();
        doom_keys_release();
    } else if (pause_at) {
        paused_ms += wall_ms() - pause_at;
        pause_at = 0;
    }
    platform_audio_gate(!on && !muted && focused);
    draw_view();
}
static void forget_view(void) {
    view = 0;
    focused = 0;
    manual_pause = 0;
    set_pause(1);
    if (image > 0)
        rx_unimage_async(host, image);
    image = 0;
}
static unsigned char map_key(const char *name) {
    if (!strcmp(name, "Up"))
        return KEY_UPARROW;
    if (!strcmp(name, "Down"))
        return KEY_DOWNARROW;
    if (!strcmp(name, "Left"))
        return KEY_LEFTARROW;
    if (!strcmp(name, "Right"))
        return KEY_RIGHTARROW;
    if (!strcmp(name, "Escape"))
        return KEY_ESCAPE;
    if (!strcmp(name, "Return"))
        return KEY_ENTER;
    if (!strcmp(name, "Tab"))
        return KEY_TAB;
    if (!strcmp(name, "Backspace"))
        return KEY_BACKSPACE;
    if (!strcmp(name, "Space"))
        return KEY_FIRE;
    if (strlen(name) == 1) {
        switch (*name) {
        case 'w':
            return KEY_UPARROW;
        case 's':
            return KEY_DOWNARROW;
        case 'a':
            return KEY_STRAFE_L;
        case 'd':
            return KEY_STRAFE_R;
        case 'e':
            return KEY_USE;
        default:
            return (unsigned char)*name;
        }
    }
    return 0;
}
static void new_game(void) {
    if (started) {
        manual_pause = 0;
        G_DeferedInitNew(sk_medium, 1, 1);
        set_pause(!focused);
    }
}
static void on_command(rx_ext *ext, const char *name, rx_doc doc, void *user) {
    (void)doc;
    (void)user;
    host = ext;
    if (!strcmp(name, "mute")) {
        muted = !muted;
        platform_audio_gate(!muted && !suspended && focused);
        return;
    }
    if (!strcmp(name, "pause")) {
        if (started) {
            manual_pause = !manual_pause;
            set_pause(!focused || manual_pause);
        }
        return;
    }
    if (strcmp(name, "play") && strcmp(name, "new_game"))
        return;
    if (!rx_has(ext, "views") || !rx_has(ext, "view-images") || !rx_has(ext, "view-keys")) {
        rx_toast(ext, "DOOM needs rhun 0.18 or newer (picture views, images and keys).");
        return;
    }
    if (!view)
        view = rx_view(ext, "DOOM");
    if (view < 1) {
        view = 0;
        return;
    }
    if (!started) {
        FILE *f = fopen(wad_path, "rb");
        if (!f) {
            rx_toast(
                ext,
                "DOOM game data is missing. Run tools/fetch-shareware.sh, then try Play again.");
            draw_view();
            return;
        }
        char magic[4];
        size_t n = fread(magic, 1, 4, f);
        fclose(f);
        if (n != 4 || memcmp(magic, "IWAD", 4)) {
            rx_toast(ext, "Invalid DOOM IWAD in data/doom1.wad.");
            return;
        }
        if (chdir(state_path)) {
            rx_toast(ext, "DOOM could not open its save folder.");
            return;
        }
        epoch = wall_ms() - 1;
        char *args[] = {"rhun-doom", "-iwad",   wad_path, "-config", "default.cfg", "-extraconfig",
                        "extra.cfg", "-window", "-warp",  "1",       "1",           NULL};
        started = 1;
        wipegamestate = GS_LEVEL;
        doomgeneric_Create(11, args);
        /* The engine's config is read during Create. Keep this extension's documented controls. */
        key_up = KEY_UPARROW;
        key_down = KEY_DOWNARROW;
        key_left = KEY_LEFTARROW;
        key_right = KEY_RIGHTARROW;
        key_strafeleft = KEY_STRAFE_L;
        key_straferight = KEY_STRAFE_R;
        key_fire = KEY_FIRE;
        key_use = KEY_USE;
        set_pause(1);
        fprintf(stderr, "DOOM ready: shareware E1M1, native 320x200, image protocol\n");
    } else {
        set_pause(!focused || manual_pause);
        if (!image) {
            last_draw = 0;
            DG_DrawFrame();
        }
        draw_view();
    }
    if (!strcmp(name, "new_game"))
        new_game();
}
static void on_view(rx_ext *ext, int64_t id, const rx_view_event *ev, void *user) {
    (void)ext;
    (void)user;
    if (id != view)
        return;
    if (!strcmp(ev->kind, "size")) {
        view_w = (int)ev->width;
        view_h = (int)ev->height;
        draw_view();
    } else if (!strcmp(ev->kind, "focus")) {
        focused = ev->on;
        if (started)
            set_pause(!focused || manual_pause);
        platform_audio_gate(focused && !suspended && !muted);
        draw_view();
    } else if (!strcmp(ev->kind, "down")) {
        focused = 1;
        manual_pause = 0;
        if (started)
            set_pause(0);
    } else if (!strcmp(ev->kind, "closed")) {
        forget_view();
    } else if (!strcmp(ev->kind, "key") && focused) {
        if (!strcmp(ev->key, "p") || !strcmp(ev->key, "m")) {
            unsigned char k = (unsigned char)*ev->key;
            if (rx_has(host, "view-key-state") && hotkey_down[k])
                return;
            if (k == 'p') {
                manual_pause = !manual_pause;
                set_pause(manual_pause);
            } else {
                muted = !muted;
                platform_audio_gate(!muted && !suspended);
            }
            hotkey_down[k] = 1;
            return;
        }
        if (suspended)
            return;
        if (rx_has(host, "view-key-state"))
            doom_key_set(KEY_RSHIFT, strstr(ev->mods, "shift") != NULL);
        unsigned char key = map_key(ev->key);
        if (key) {
            doom_key_set(key, 1);
            pulse_until[key] = rx_has(host, "view-key-state") ? 0 : wall_ms() + 100;
        }
    }
}
static void on_event(rx_ext *ext, const rx_event *ev, void *user) {
    (void)user;
    if (getenv("DOOM_DIAGNOSTICS") && !strcmp(ev->name, "view") && ev->argc >= 2 &&
        (!strcmp(ev->argv[1], "key") || !strcmp(ev->argv[1], "key-up") ||
         !strcmp(ev->argv[1], "mods"))) {
        fprintf(stderr, "DOOM input:");
        for (int i = 0; i < ev->argc; i++)
            fprintf(stderr, " %s", ev->argv[i]);
        fputc('\n', stderr);
    }
    if (strcmp(ev->name, "view") || ev->argc < 2 || atoll(ev->argv[0]) != view)
        return;
    if (!rx_has(ext, "view-key-state"))
        return;
    if (!strcmp(ev->argv[1], "key-up") && ev->argc >= 4) {
        if (strlen(ev->argv[3]) == 1)
            hotkey_down[(unsigned char)*ev->argv[3]] = 0;
        unsigned char key = map_key(ev->argv[3]);
        if (key)
            doom_key_set(key, 0);
    } else if (!strcmp(ev->argv[1], "mods") && ev->argc >= 3 && focused && !suspended) {
        doom_key_set(KEY_RSHIFT, strstr(ev->argv[2], "shift") != NULL);
    }
}
static void on_bye(rx_ext *ext, void *user) {
    (void)ext;
    (void)user;
    if (started)
        M_SaveDefaults();
    platform_audio_shutdown();
    unsigned long samples, sounds;
    platform_audio_stats(&samples, &sounds);
    fprintf(stderr, "DOOM stats: frames=%lu bytes=%lu sounds=%lu samples=%lu\n", frames, bytes,
            sounds, samples);
}
void doom_request_close(void) { close_requested = 1; }
static int doom_idle(rx_ext *ext) {
    host = ext;
    uint64_t now = wall_ms();
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    if (poll(&p, 1, 0) > 0)
        return 1;
    if (redraw_needed) {
        redraw_needed = 0;
        draw_view_now();
        if (ext->queue)
            return 1;
    }
    if (started && view && !suspended) {
        for (int k = 0; k < 256; k++)
            if (pulse_until[k] && now >= pulse_until[k]) {
                pulse_until[k] = 0;
                doom_key_set((unsigned char)k, 0);
            }
        if (!suspended && now >= next_tick) {
            next_tick = now + 28;
            doomgeneric_Tick();
            platform_music_update();
            if (close_requested) {
                close_requested = 0;
                int64_t old = view;
                view = 0;
                focused = 0;
                manual_pause = 0;
                M_ClearMenus();
                set_pause(1);
                if (old > 0)
                    rx_unview(host, old);
                if (image > 0)
                    rx_unimage_async(host, image);
                image = 0;
            }
        }
    }
    if (now - last_diagnostics >= 100) {
        diagnostics();
        last_diagnostics = now;
    }
    if (ext->queue)
        return 1;
    int timeout = (started && view && !suspended) ? 2 : 50;
    return poll(&p, 1, timeout) > 0;
}
static int on_copy(rx_ext *ext, int64_t id, int cut, rx_buf *out, void *user) {
    (void)ext;
    (void)id;
    (void)cut;
    (void)out;
    (void)user;
    return 1;
}
static void on_ready(rx_ext *ext, const char *project, void *user) {
    (void)project;
    if (getenv("DOOM_AUTOSTART"))
        on_command(ext, "play", 0, user);
}
int main(void) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof cwd))
        return 1;
    if (snprintf(wad_path, sizeof wad_path, "%s/data/doom1.wad", cwd) >= (int)sizeof wad_path)
        return 1;
    const char *state = getenv("RHUN_STATE");
    if (!state)
        state = getenv("HOME");
    if (!state || snprintf(state_path, sizeof state_path, "%s", state) >= (int)sizeof state_path)
        return 1;
    rhun_output = fdopen(dup(STDOUT_FILENO), "wb");
    if (!rhun_output || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
        return 1;
    setvbuf(stdin, NULL, _IONBF, 0);
    rx_handlers h = {0};
    h.ready = on_ready;
    h.command = on_command;
    h.view = on_view;
    h.bye = on_bye;
    h.view_copy = on_copy;
    h.event = on_event;
    int status = rx_run("views view-keys view-images view-key-state", &h, NULL);
    platform_audio_shutdown();
    fclose(rhun_output);
    return status;
}

#ifndef DOOM_OS_H
#define DOOM_OS_H
#include <stdint.h>
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#define getcwd _getcwd
#define chdir _chdir
#define dup _dup
#define dup2 _dup2
#define fdopen _fdopen
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static uint64_t doom_now_ms(void) { return GetTickCount64(); }
static void doom_sleep_ms(uint32_t ms) { Sleep(ms); }
static int doom_input_ready(int timeout) {
    HANDLE pipe = GetStdHandle(STD_INPUT_HANDLE);
    uint64_t until = doom_now_ms() + timeout;
    do {
        DWORD available;
        if (GetFileType(pipe) != FILE_TYPE_PIPE ||
            !PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL) || available)
            return 1;
        if (!timeout || doom_now_ms() >= until)
            return 0;
        Sleep(1);
    } while (1);
}
#else
#include <poll.h>
#include <time.h>
#include <unistd.h>
static uint64_t doom_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void doom_sleep_ms(uint32_t ms) {
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
    nanosleep(&t, NULL);
}
static int doom_input_ready(int timeout) {
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, timeout) > 0;
}
#endif
#endif

/* CPU time of the calling thread, for the tests that limit the work of our own code (AGENTS.md: not wall-clock time, which a busy or slow
 * runner stretches: the CPU time of a thread does not count the time it waited to be scheduled). */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L /* clock_gettime and CLOCK_THREAD_CPUTIME_ID under -std=c11 */
#endif
#include <SDL3/SDL.h>

#include "exhaust_platform.h"

#if defined(_WIN32)
#include <windows.h>
double ex_platform_thread_cpu_ms(void) {
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) return -1.0;
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) / 10000.0;                 /* 100 ns units */
}
#elif defined(__APPLE__)
#include <mach/mach.h>
double ex_platform_thread_cpu_ms(void) {
    thread_basic_info_data_t info;
    mach_msg_type_number_t n = THREAD_BASIC_INFO_COUNT;
    mach_port_t t = mach_thread_self();
    kern_return_t rc = thread_info(t, THREAD_BASIC_INFO, (thread_info_t)&info, &n);
    mach_port_deallocate(mach_task_self(), t);
    if (rc != KERN_SUCCESS) return -1.0;
    return (double)(info.user_time.seconds + info.system_time.seconds) * 1000.0 + (double)(info.user_time.microseconds + info.system_time.microseconds) / 1000.0;
}
#else
#include <time.h>
double ex_platform_thread_cpu_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return -1.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}
#endif

/* The CPU time if the system tells it, else the wall-clock time (a test then measures what it always did). */
double ex_platform_work_ms(void) {
    double c = ex_platform_thread_cpu_ms();
    return c >= 0.0 ? c : (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

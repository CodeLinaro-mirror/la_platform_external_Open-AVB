/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utc_helper.h>
#include <stdarg.h>

#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)   ((~(clockid_t) (fd) << 3) | CLOCKFD)

#ifndef LOG_ERROR
#define LOG_ERROR    1
#endif
#ifndef LOG_WARNING
#define LOG_WARNING     2
#endif
#ifndef LOG_INFO
#define LOG_INFO     3
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG    4
#endif
#define UTC_TEST_LOG_LEVEL LOG_INFO
#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"libutc_test", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"libutc_test", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"libutc_test", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"libutc_test", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};

#endif
#ifndef ANDROID

#define UTC_TEST_LOG_ERROR(fmt, ...) system_log(LOG_ERROR, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_WARNING(fmt, ...) system_log(LOG_WARNING, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_INFO(fmt, ...) system_log(LOG_INFO, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_DEBUG(fmt, ...) system_log(LOG_DEBUG, "[%d:%s:%d] " fmt ,gettid(),  __FUNCTION__, __LINE__,##__VA_ARGS__); printf(fmt,##__VA_ARGS__)

void system_log(int loglevel, const char *s, ...)
{
    va_list arg = {};

    if (loglevel == LOG_ERROR || loglevel <= UTC_TEST_LOG_LEVEL) {
        va_start(arg, s);
        vsyslog(loglevel, s, arg);
        va_end(arg);
    }
}


#else

#define UTC_TEST_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_TEST_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)

#endif

static volatile bool g_running = true;

void signal_handler(int signum) {
    printf("Received signal %d\n", signum);
    utc_helper_deinit();
    g_running = false;
}

void test_update_callback(int sync_status) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    UTC_TEST_LOG_INFO("\n>>> CALLBACK RECEIVED <<<\n");
    UTC_TEST_LOG_INFO("    New Sync Status: %d\n", sync_status);
    UTC_TEST_LOG_INFO("    Local Time:      %ld.%ld\n", ts.tv_sec, ts.tv_nsec);
}

int main(int argc, char *argv[])
{
    int sync_status = 0;
    uint64_t utc_time = 0;
    bool utc_available = false;
    utc_available = utc_helper_init();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (utc_available == 0) {
        UTC_TEST_LOG_INFO("UTC Init Success\n");
    } else {
        UTC_TEST_LOG_ERROR("UTC Init Failure\n");
        return 0;
    }

    bool monitor_mode = (argc > 1 && strcmp(argv[1], "m") == 0);

    if (monitor_mode) {
        UTC_TEST_LOG_INFO("Starting Monitor Mode. Waiting for updates...\n");
        
        if (utcRegisterUpdateCallback(test_update_callback) != 0) {
             UTC_TEST_LOG_ERROR("Failed to register callback!\n");
             utc_helper_deinit();
             return -1;
        }

        sync_status = utcGetSyncStatus();
        UTC_TEST_LOG_INFO("Initial Status: %d\n", sync_status);

        while (g_running) {
            sleep(1); 
        }
        
        UTC_TEST_LOG_INFO("Exiting Monitor Mode...\n");

    } else {
        sync_status = utcGetSyncStatus();
        utc_time = utcGetUtcTime();
        struct timespec real = {0};
        clock_gettime(CLOCK_REALTIME, &real);

        UTC_TEST_LOG_INFO("utc status %d last updated utc time %lu, current utc time %lu.%lu\n", sync_status, utc_time, real.tv_sec, real.tv_nsec);

        utc_helper_deinit();
    }

    return 0;
}

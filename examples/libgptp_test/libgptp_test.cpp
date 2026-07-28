/*===========================================================================
Copyright (c) 2019, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

============================================================================ */

/* ============================================================================
Changes from Qualcomm Technologies, Inc. are provided under the following license:
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <math.h>
#include <ctype.h>
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
#include <gptp_helper.h>
#include <stdarg.h>
#include <dirent.h>

#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#ifdef DLT_AVAILABLE
#include <dlt/dlt.h>
#include <unistd.h>
#endif

#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)   ((~(clockid_t) (fd) << 3) | CLOCKFD)
#define MAX_RETRY 10000
#define LOOP_CNT 1

#ifdef DLT_AVAILABLE

typedef enum {
    GPTP_LOG_LVL_CRITICAL = DLT_LOG_FATAL,
    GPTP_LOG_LVL_ERROR = DLT_LOG_ERROR,
    GPTP_LOG_LVL_EXCEPTION = DLT_LOG_ERROR,
    GPTP_LOG_LVL_WARNING = DLT_LOG_WARN,
    GPTP_LOG_LVL_INFO = DLT_LOG_INFO,
    GPTP_LOG_LVL_STATUS = DLT_LOG_INFO,
    GPTP_LOG_LVL_DEBUG = DLT_LOG_DEBUG,
    GPTP_LOG_LVL_VERBOSE = DLT_LOG_VERBOSE,
} GPTP_LOG_LEVEL;

#else

typedef enum {
    GPTP_LOG_LVL_CRITICAL,
    GPTP_LOG_LVL_ERROR,
    GPTP_LOG_LVL_EXCEPTION,
    GPTP_LOG_LVL_WARNING,
    GPTP_LOG_LVL_INFO,
    GPTP_LOG_LVL_STATUS,
    GPTP_LOG_LVL_DEBUG,
    GPTP_LOG_LVL_VERBOSE,
} GPTP_LOG_LEVEL;

#endif //DLT_AVAILABLE


#define LIB_GPTP_LOG_LEVEL GPTP_LOG_LVL_INFO

#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"libgptp", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};

#endif


#ifdef DLT_AVAILABLE
DLT_DECLARE_CONTEXT(dlt_con_gptp);
#endif

void libgptplogRegister(void)
{
#ifdef DLT_AVAILABLE
    DLT_REGISTER_APP("LPTP", "OpenAVB libgPTP");
    DLT_REGISTER_CONTEXT(dlt_con_gptp, "GNRL", "General Context");
#endif
}

void libgptplogUnregister(void)
{
#ifdef DLT_AVAILABLE
    DLT_UNREGISTER_CONTEXT(dlt_con_gptp);
    DLT_UNREGISTER_APP();
#endif
}


#ifndef ANDROID

void libgptpLog(GPTP_LOG_LEVEL level, const char *tag, const char *path,
                int line,
                const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
#ifdef DLT_AVAILABLE
    DLT_LOG(dlt_con_gptp, (DltLogLevelType)level, DLT_INT(gettid()),
            DLT_STRING(path), DLT_INT(line), DLT_STRING(msg));
#else

    if (level == GPTP_LOG_LVL_ERROR || level <= LIB_GPTP_LOG_LEVEL) {
        syslog(level, "[%d:%s:%d] %s\n", gettid(), path, line, msg);
    }

#endif
}

#define GPTP_LOG_ERROR(fmt, ...) libgptpLog(GPTP_LOG_LVL_ERROR, "ERROR    ", __func__, __LINE__, fmt, ## __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_WARNING(fmt, ...) libgptpLog(GPTP_LOG_LVL_WARNING, "WARNING  ", __func__, __LINE__, fmt, ## __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_INFO(fmt, ...) libgptpLog(GPTP_LOG_LVL_INFO, "INFO     ", __func__, __LINE__, fmt, ## __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_DEBUG(fmt, ...) libgptpLog(GPTP_LOG_LVL_DEBUG, "DEBUG    ", __FILE__, __LINE__, fmt, ## __VA_ARGS__); printf(fmt,##__VA_ARGS__)


#else

#define GPTP_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define GPTP_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)

#endif

static bool gptp_scaling_available = false;

/**
 * systemTime: Returns the current time in nanoseconds using the specified clock source.
 * Supported clock sources: CLOCK_REALTIME, CLOCK_MONOTONIC. Refs: kernel/kernel_platform/kernel/include/uapi/linux/time.h#50
 * Converts seconds and nanoseconds from clock_gettime() into a single uint64_t value.
 */
uint64_t systemTime(int clock)
{
    uint64_t ret;  // Variable to store the final time in nanoseconds
    // Array of supported clock sources: real-time, and monotonic
    static const clockid_t clocks[] = {
        CLOCK_REALTIME,
        CLOCK_MONOTONIC,
    };
    struct timespec t;  // Structure to hold seconds and nanoseconds
    t.tv_sec = t.tv_nsec = 0;  // Initialize both fields to zero
    // Get current time from the selected clock source
    clock_gettime(clocks[clock], &t);
    // Convert seconds to nanoseconds and add nanoseconds part
    ret = (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
    return ret;  // Return the computed time in nanoseconds
}

uint64_t getQtimerTime()
{
    uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerSec = 0, qTimerNanosNSec = 0;
#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
    asm volatile("mrs %0, cntfrq_el0" : "=r"(qTimerFreq));
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
    qTimerFreq =  19200000; //19.2 MHz TBD: find right asm instruction
#endif
    qTimerSec = (qTimerCount / qTimerFreq);
    qTimerNanosNSec = (qTimerCount % qTimerFreq);
    qTimerNanosNSec *= 1000000000;
    qTimerNanosNSec /= qTimerFreq;
    return (qTimerSec * 1000000000 + qTimerNanosNSec) ;
}

uint64_t getQtimerTicks()
{
    uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerSec = 0, qTimerNanosNSec = 0;
#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
#endif
    return (qTimerCount) ;
}

void do_some_tests_qtimer(int p_loop_cnt)
{
    int i = 0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    bool isSync = false;
    prev_vec_time = test_vec_time = getQtimerTime();
    gptpGetPtpTimeFromQTimeNs(&prev_gptp_time, prev_vec_time);

    for (i = 0; i < p_loop_cnt; i++)
        if (gptpGetPtpTimeFromQTimeNs_s(&test_gptp_time, test_vec_time, &isSync)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;
            GPTP_LOG_INFO("ns qtimer_time %" PRIi64 "  gptp_time %" PRIi64 " isSync %d\n",
                          delta_vec_time, delta_gptp_time, isSync);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;
            nanosleep(&ts, NULL);
            test_vec_time += 1000000UL;
        } else {
            GPTP_LOG_ERROR("Qtimer time test failed\n");
        }
}

void do_some_tests_sys(int p_loop_cnt)
{
    int i = 0;
    struct timespec ts = { 0, 1000000 };
    uint64_t prev_vec_time;
    uint64_t prev_gptp_time;
    uint64_t test_vec_time;
    uint64_t test_gptp_time;
    int64_t delta_vec_time;
    int64_t delta_gptp_time;
    bool isSync = false;
    prev_vec_time = test_vec_time = systemTime(CLOCK_REALTIME);
    gptpGetPtpTimefromSystime_s(&prev_gptp_time, prev_vec_time, &isSync);

    for (i = 0; i < p_loop_cnt; i++) {
        if (gptpGetPtpTimefromSystime_s(&test_gptp_time, test_vec_time, &isSync)) {
            delta_vec_time = test_vec_time;
            delta_vec_time -= prev_vec_time;
            delta_gptp_time = test_gptp_time;
            delta_gptp_time -= prev_gptp_time;
            GPTP_LOG_INFO("ns sys_time %" PRIi64 "  gptp_time %" PRIi64 " isSync %d\n",
                          delta_vec_time, delta_gptp_time, isSync);
            prev_vec_time = test_vec_time;
            prev_gptp_time = test_gptp_time;
            nanosleep(&ts, NULL);
            test_vec_time += 1000000UL;
        } else {
            GPTP_LOG_ERROR("Sys time test failed\n");
        }
    }
}

void loop_test(int p_loop_cnt)
{
    uint64_t ptp_time;
    syncMesaurementData_t syncData;
    pDelayMeasurementData_t delayData;
    gptpStatsType_t status;
    uint64_t qtimer_time;
    uint64_t prev_qtimer_time;
    uint64_t prev_ptp_time;
    int64_t delta_qtimer_time;
    int64_t delta_ptp_time;
    int64_t delta_qtimer_ptp;
    int64_t time_error;
    bool isSync = false;
    int rcvid = 0;
    prev_qtimer_time = getQtimerTime();
    gptpGetCurPtpTime(&prev_ptp_time);

    for (int i = 0; i < p_loop_cnt; i++) {
        qtimer_time = getQtimerTime();

        if (gptpGetCurPtpTime_s(&ptp_time, &isSync)) {
            delta_qtimer_time = qtimer_time - prev_qtimer_time;
            delta_ptp_time = ptp_time - prev_ptp_time;
            delta_qtimer_ptp = qtimer_time - ptp_time;
            GPTP_LOG_INFO("loop_test: qtimer_time %" PRIu64 " ptp_time %" PRIu64
                          " qtimer_delta %"
                          PRIi64 "  ptp_delta %" PRIi64  " qtimer_ptp_delta %" PRIi64 " isSync %d\n",
                          qtimer_time, ptp_time, delta_qtimer_time, delta_ptp_time, delta_qtimer_ptp,
                          isSync);
            prev_qtimer_time = qtimer_time;
            prev_ptp_time = ptp_time;
        } else {
            GPTP_LOG_ERROR("Failed to get PTP time\n");
        }

        memset(&syncData, 0, sizeof(syncData));

        if (gptpGetSyncMeasurementData(&syncData)) {
            GPTP_LOG_INFO("loop_test: *************** Sync Measurement Data *****************\n");
            GPTP_LOG_INFO("loop_test: precise_origin_timestamp %" PRIu64 "\n",
                          syncData.precise_origin_timestamp);
            GPTP_LOG_INFO("loop_test: reference_local_timestamp %" PRIu64 "\n",
                          syncData.reference_local_timestamp);
            GPTP_LOG_INFO("loop_test: reference_global_timestamp %" PRIu64 "\n",
                          syncData.reference_global_timestamp);
            GPTP_LOG_INFO("loop_test: sync_ingress_timestamp %" PRIu64 "\n",
                          syncData.sync_ingress_timestamp);
            GPTP_LOG_INFO("loop_test: correction_field %" PRIu64 "\n",
                          syncData.correction_field);
            GPTP_LOG_INFO("loop_test: sequence_id %u\n", syncData.sequence_id);
            GPTP_LOG_INFO("loop_test: pDelay %" PRIu64 "\n", syncData.pDelay);
            GPTP_LOG_INFO("loop_test: portNumber %d\n", syncData.portNumber);
            GPTP_LOG_INFO("loop_test: clockIdentity " CLK_STR "\n",
                          CLK_TO_STR(syncData.clockIdentity));
        } else {
            GPTP_LOG_ERROR("Failed to get Sync Measurement Data\n");
        }

        memset(&status, 0, sizeof(status));

        if (getgPTPStatus(&status)) {
            GPTP_LOG_INFO("loop_test: *********************** Status Data ************************\n");
            GPTP_LOG_INFO("loop_test: gptp_status %" PRIu64 "\n", status.gptp_status);
            GPTP_LOG_INFO("loop_test: rate_deviation %f\n", status.rate_deviation);
            GPTP_LOG_INFO("loop_test: IsMaster %d\n", status.IsMaster);
            GPTP_LOG_INFO("loop_test: offset %" PRId64 "\n", status.offset);
            GPTP_LOG_INFO("loop_test: d_status %x\n", status.d_status);
        } else {
            GPTP_LOG_ERROR("Failed to get GPTP Stat Data\n");
        }

        memset(&delayData, 0, sizeof(delayData));

        if (gptpGetPDelayMeasurementData(&delayData)) {
            GPTP_LOG_INFO("loop_test: ********************** PDelay Measurement Data ********************\n");
            GPTP_LOG_INFO("loop_test: request_origin_timestamp %" PRIu64 "\n",
                          delayData.request_origin_timestamp);
            GPTP_LOG_INFO("loop_test: request_receipt_timestamp %" PRIu64 "\n",
                          delayData.request_receipt_timestamp);
            GPTP_LOG_INFO("loop_test: response_origin_timestamp %" PRIu64 "\n",
                          delayData.response_origin_timestamp);
            GPTP_LOG_INFO("loop_test: response_receipt_timestamp %" PRIu64 "\n",
                          delayData.response_receipt_timestamp);
            GPTP_LOG_INFO("loop_test: reference_local_timestamp %" PRIu64 "\n",
                          delayData.reference_local_timestamp);
            GPTP_LOG_INFO("loop_test: reference_global_timestamp %" PRIu64 "\n",
                          delayData.reference_global_timestamp);
            GPTP_LOG_INFO("loop_test: sequence_id %u\n", delayData.sequence_id);
            GPTP_LOG_INFO("loop_test: pDelay %" PRIu64 "\n", delayData.pDelay);
            GPTP_LOG_INFO("loop_test: req_portNumber %d\n", delayData.req_portNumber);
            GPTP_LOG_INFO("loop_test: req_clockIdentity " CLK_STR "\n",
                          CLK_TO_STR(delayData.req_clockIdentity));
            GPTP_LOG_INFO("loop_test: resp_portNumber %d\n", delayData.resp_portNumber);
            GPTP_LOG_INFO("loop_test: resp_clockIdentity " CLK_STR "\n",
                          CLK_TO_STR(delayData.resp_clockIdentity));
        } else {
            GPTP_LOG_ERROR("Failed to get Path Delay Measurement Data\n");
        }

        rcvid = getTimeError(&time_error);

        if ( rcvid == 0) {
            GPTP_LOG_INFO("loop_test: ********************** Reverse sync - Slave clock offset ********************\n");
            GPTP_LOG_INFO("loop_test: time error %" PRIi64 "\n", time_error);
        } else if (rcvid < 0) {
            GPTP_LOG_ERROR("Failed to get time error\n");
        }

        // Suspends the execution of the calling thread for a specified number of microseconds.
        // 1 second = 1,000,000 microseconds, so usleep(1000000) pauses the program for 1 second.
        usleep(1000000);
    }
}


void do_some_tests_ptp(int p_loop_cnt)
{
    int i = 0;
    uint64_t ptp_time = 0;
    bool isSync = false;

    for (i = 0; i < p_loop_cnt; i++) {
        if (gptpGetCurPtpTime_s(&ptp_time, &isSync)) {
            GPTP_LOG_INFO("ns ptp_time %" PRIu64 " isSync %d\n", ptp_time, isSync);
        }
    }
}

void callback_handler(struct gptp_update update)
{
    GPTP_LOG_INFO("callback_handler:: got callback %" PRIu64 " %" PRId64 " \n",
                  update.curr_gptp_time, update.clock_adjust);
}

void do_some_tests_gptp_mono(int p_loop_cnt)
{
    int i = 0;
    uint64_t ptp_time = 0;
    uint64_t mono_time = 0;
    bool isSync = false;

    // Loop for the specified number of iterations
    for (i = 0; i < p_loop_cnt; i++) {
        // Retrieve the current gPTP and monotonic time pair along with sync status
        if (gptpGetCurgPtpMonotonicPair_s(&ptp_time, &mono_time, &isSync)) {
            // Log the retrieved times and synchronization status
            GPTP_LOG_INFO("ns ptp_time %" PRIu64 "  ns mono_time %" PRIu64 " isSync %d\n",
                          ptp_time,
                          mono_time, isSync);
        }
    }
}

#ifdef AVB_FEATURE_GVM_MODE
#define PTP_DEVICE_PATH_LEN 256
#define PTP_DEFAULT_DEVICE "/dev/ptp0"
static void getVirtDevice(char* device_path)
{
    const char *path = "/sys/devices/virtual/ptp/";
    struct dirent *entry;

    if (device_path == NULL) {
        GPTP_LOG_ERROR("device path is NULL\n");
        return;
    }
    DIR *dp = opendir(path);
    if (dp == NULL) {
        GPTP_LOG_ERROR("Failed to open /sys/devices/virtual/ptp/ so use default device\n");
        snprintf(device_path, PTP_DEVICE_PATH_LEN, "%s", PTP_DEFAULT_DEVICE);
        return;
    }

    while ((entry = readdir(dp))) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            snprintf(device_path, PTP_DEVICE_PATH_LEN, "%s%s", "/dev/", entry->d_name);
            GPTP_LOG_INFO("opening clock device: %s", device_path);
            closedir(dp);
            return;
        }
    }

    GPTP_LOG_ERROR("No device found in %s, using default device\n", path);
    snprintf(device_path, PTP_DEVICE_PATH_LEN, "%s", PTP_DEFAULT_DEVICE);
    closedir(dp);
}

#endif

#define TSC_DEVICE_PATH_LEN 256
#define TSC_DEFAULT_DEVICE "/dev/ptp0"

/* Trim in-place trailing and leading whitespace/newlines */
static void trim(char *s) {
    char *end, *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

/* Safe file read: read first line (or whole small file) into buf */
static int read_file_line(const char *path, char *buf, size_t buflen) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, (int)buflen, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static int getTscDevice(char* device_path)
{
    const char *path = "/sys/class/ptp/";
    const char *prefix = "QCOM TSC";
    int prefix_len = strlen(prefix);
    struct dirent *entry;
    char namebuf[128];

    if (device_path == NULL) {
        GPTP_LOG_ERROR("device path is NULL\n");
        return -1;
    }
    DIR *dp = opendir(path);
    if (dp == NULL) {
        GPTP_LOG_ERROR("Failed to open /sys/devices/virtual/ptp/ so use default device\n");
        snprintf(device_path, TSC_DEVICE_PATH_LEN, "%s", TSC_DEFAULT_DEVICE);
        return -1;
    }

    while ((entry = readdir(dp))) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (strncmp(entry->d_name, "ptp", 3) != 0) continue; // if not ptp device, skip it
            char clk_path[TSC_DEVICE_PATH_LEN]  = {0};
            int n = snprintf(clk_path, strlen(path)+strlen(entry->d_name)+strlen("///clock_name")+1, "%s%s/clock_name", path, entry->d_name);
            if (n <= 0 || (size_t)n >= sizeof(clk_path)) {
                // Path too long or snprintf error; skip safely
                continue;
            }
            if (read_file_line(clk_path, namebuf, sizeof(namebuf)) == 0) {
                trim(namebuf);
                GPTP_LOG_INFO("clock name in %s and clk path %s, with prefix %s \n", namebuf, clk_path,prefix);
                if (strncmp(namebuf, prefix,prefix_len) == 0) {
                    // If reading clock_name fails, just skip that entry
                    snprintf(device_path, TSC_DEVICE_PATH_LEN, "%s%s", "/dev/", entry->d_name);
                    GPTP_LOG_INFO("opening clock device: %s\n", device_path);
                    closedir(dp);
                    return 0;
                }
            }
        }
    }

    GPTP_LOG_ERROR("No device found in %s, using default device\n", path);
    snprintf(device_path, TSC_DEVICE_PATH_LEN, "%s", TSC_DEFAULT_DEVICE);
    closedir(dp);
    return -1;
}

void get_gptp_time(int p_loop_cnt)
{
    struct timespec ts;
    static clockid_t gPtpClockid = -1;
    uint64_t curr_gptp_time = 0;
#ifdef AVB_FEATURE_GVM_MODE
    char ptp_device[PTP_DEVICE_PATH_LEN] = {0};
    getVirtDevice(ptp_device);
    int gptp_phc_fd = open(ptp_device, O_RDWR );

    if ( gptp_phc_fd == -1 ||
            (gPtpClockid = FD_TO_CLOCKID(gptp_phc_fd)) == -1 ) {
        GPTP_LOG_ERROR("Failed to open PTP clock device error 0x%x(%s)\n", errno,
                       strerror(errno));
        return;
    }

    if (clock_gettime(gPtpClockid, &ts)) {
        GPTP_LOG_ERROR("clock_gettime failed 0x%x (%s)\n", errno, strerror(errno));
        close(gptp_phc_fd);
        return;
    }

    if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
        GPTP_LOG_WARNING("gptp time read taking longer time\n");
        close(gptp_phc_fd);
        return;
    }

    curr_gptp_time = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    GPTP_LOG_INFO("current gptp time = %" PRIu64 "\n", curr_gptp_time);
    close(gptp_phc_fd);
#else
    // Get the GPTP Time in Non-GVM mode
    for (int i = 0; i < p_loop_cnt; i++) {
        gptpTimeInfo_t ptp_data;

        // Retrieve GPTP status and current PTP time
        if (gptpGetStatusAndCurPtpTime(&ptp_data)) {
            if (ptp_data.status) {
                // Log GPTP status, port state, and time (seconds.nanoseconds)
                GPTP_LOG_INFO("gptp status %d port state %d gptp time %u.%u\n",
                              ptp_data.status,
                              ptp_data.port_state,
                              ptp_data.tv_sec,
                              ptp_data.tv_nsec);
            } else {
                // Log GPTP status and port status only
                GPTP_LOG_INFO("gptp status %d port state %d\n",
                              ptp_data.status,
                              ptp_data.port_state);
            }
        } else {
            // Log error if GPTP time retrieval fails
            GPTP_LOG_ERROR("GPTP time test failed\n");
        }
    }
#endif
    return;
}

void get_gptp_tsc_time(int p_loop_cnt)
{
    struct timespec ts;
    static clockid_t gTscClockid = -1;
    uint64_t curr_gptp_time;

    char ptp_device[TSC_DEVICE_PATH_LEN] = {0};
    int ret = getTscDevice(ptp_device);
    if (ret == -1) {
        GPTP_LOG_ERROR("Failed to get TSC device path\n");
        return;
    }
    int gptp_phc_fd = open(ptp_device, O_RDWR );

    if ( gptp_phc_fd == -1 ||
            (gTscClockid = FD_TO_CLOCKID(gptp_phc_fd)) == -1 ) {
        GPTP_LOG_ERROR("Failed to open TSC clock device error 0x%x(%s)\n", errno,
                       strerror(errno));
        return;
    }
    for (int i = 0; i < p_loop_cnt; i++) {
        if (clock_gettime(gTscClockid, &ts)) {
            GPTP_LOG_ERROR("clock_gettime failed 0x%x (%s)\n", errno, strerror(errno));
            close(gptp_phc_fd);
            return;
        }

        if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
            GPTP_LOG_WARNING("TSC time read taking longer time\n");
            close(gptp_phc_fd);
            return;
        }

        curr_gptp_time = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
        //GPTP_LOG_INFO("current TSC time = %" PRIu64 "\n", curr_gptp_time);
        GPTP_LOG_INFO("current TSC time = %ld.%ld\n", ts.tv_sec, ts.tv_nsec);
    }
    close(gptp_phc_fd);
    return;
}

void do_some_tests_gptp_boot(int p_loop_cnt)
{
    int i = 0;
    uint64_t ptp_time = 0;
    uint64_t boot_time_ns = 0;
    bool isSync = false;
    struct timespec boot;
    GPTP_LOG_INFO("do_some_tests_gptp_boot:\n");

    for (i = 0; i < p_loop_cnt; i++) {
        gptpGetCurPtpTime_s(&ptp_time, &isSync);
        clock_gettime(CLOCK_BOOTTIME, &boot);
        boot_time_ns = boot.tv_sec * 1000000000LL + boot.tv_nsec;
        GPTP_LOG_INFO("current ptp_time %" PRIu64 " ns boot_time_ns %" PRIu64
                      " isSync %d\n", ptp_time,
                      boot_time_ns, isSync);

        if (gptpGetBootTimeFromPtpTime_s(&boot_time_ns, ptp_time, &isSync)) {
            GPTP_LOG_INFO("gptpGetBootTimeFromPtpTime ptp_time %" PRIu64
                          " ns boot_time_ns %"
                          PRIu64 " isSync %d\n", ptp_time,
                          boot_time_ns, isSync);
        }

        if (gptpGetPtpTimeFromBootTime_s(&ptp_time, boot_time_ns, &isSync)) {
            GPTP_LOG_INFO("gptpGetPtpTimeFromBootTime ptp_time %" PRIu64
                          " ns boot_time_ns %"
                          PRIu64 " isSync %d\n", ptp_time,
                          boot_time_ns, isSync);
        }

        gptpGetCurPtpTime_s(&ptp_time, &isSync);
        clock_gettime(CLOCK_BOOTTIME, &boot);
        boot_time_ns = boot.tv_sec * 1000000000LL + boot.tv_nsec;
        ptp_time -= 1000000000LL; //just asking for boot time a second before
        boot_time_ns -= 1000000000LL;
        GPTP_LOG_INFO("current -1s ptp_time %" PRIu64 " ns boot_time_ns %" PRIu64
                      " isSync %d \n",
                      ptp_time,
                      boot_time_ns, isSync);

        if (gptpGetBootTimeFromPtpTime_s(&boot_time_ns, ptp_time, &isSync)) {
            GPTP_LOG_INFO("gptpGetBootTimeFromPtpTime ptp_time %" PRIu64
                          " ns boot_time_ns %"
                          PRIu64 " isSync %d\n", ptp_time,
                          boot_time_ns, isSync);
        }

        if (gptpGetPtpTimeFromBootTime_s(&ptp_time, boot_time_ns, &isSync)) {
            GPTP_LOG_INFO("gptpGetPtpTimeFromBootTime ptp_time %" PRIu64
                          " ns boot_time_ns %"
                          PRIu64 " isSync %d\n", ptp_time,
                          boot_time_ns, isSync);
        }

        sleep(1);
    }
}

#ifdef RGPTP_CLNT_ENABLED
static void rgptp_test(void)
{
    bool rgptp_avail = false;
    uint64_t test_rgptp_time;
    rgptp_avail = rgptpInit();

    if (rgptp_avail) {
        GPTP_LOG_INFO("RGPTP Available\n");

        if (rgptpGetCurPtpTime(&test_rgptp_time)) {
            GPTP_LOG_INFO("rgptp time %" PRIu64 ".%" PRIu64 "\n",
                          test_rgptp_time / 1000000000UL, test_rgptp_time % 1000000000UL);
        } else {
            GPTP_LOG_INFO("RGPTP time test failed\n");
        }

        if (!rgptpDeinit()) {
            GPTP_LOG_ERROR("RGPTP deinit failed\n");
        }
    } else {
        GPTP_LOG_WARNING("RGPTP Not Available\n");
    }

    return;
}

static void do_some_tests_rgptp_s(int time_s)
{
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for (i = 0; i < 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            GPTP_LOG_INFO("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n",
                          ptp_time, rptp_time, ns);
            sleep(time_s);
        }

        if (!rgptpDeinit()) {
            GPTP_LOG_ERROR("RGPTP deinit failed\n");
        }
    } else {
        GPTP_LOG_WARNING("RGPTP Not Available\n");
    }

    return;
}

static void do_some_tests_rgptp_u(int time_us)
{
    uint64_t rptp_time = 0;
    uint64_t ptp_time = 0;
    int i = 0;
    int64_t ns = 0;

    if (rgptpInit()) {
        for (i = 0; i < 200; i++) {
            gptpGetCurPtpTime(&ptp_time);
            rgptpGetCurPtpTime(&rptp_time);
            ns = (ptp_time - rptp_time);
            GPTP_LOG_INFO("gptp time: %" PRIu64 "rgptp time: %"PRIu64 " diff:%" PRId64 "\n",
                          ptp_time, rptp_time, ns);
            usleep(time_us);
        }

        if (!rgptpDeinit()) {
            GPTP_LOG_ERROR("RGPTP deinit failed\n");
        }
    } else {
        GPTP_LOG_WARNING("RGPTP Not Available\n");
    }

    return;
}
#endif

void signal_handler(int signum) {
    printf("Received signal %d\n", signum);
    gptpRegisterCallback(NULL);
    if (gptp_scaling_available && !gptpDeinit()) {
        GPTP_LOG_ERROR("GPTP deinit failed\n");
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    uint64_t test_vec_time = 0;
    uint64_t test_gptp_time = 0;
    gptpTimeInfo_t ptp_data;
    int retry = 0;
    RsyncStatus_t Rsync;

#ifdef DLT_AVAILABLE
    libgptplogRegister();
#endif

    gptp_scaling_available = gptpInit();

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (retry < MAX_RETRY && !(gptp_scaling_available = gptpInit())) {
        if (retry == 0) {
            GPTP_LOG_ERROR("GPTP Init Failed, retrying..\n");
        }

        usleep(5000);
        retry++;
    }

    if (gptp_scaling_available) {
        GPTP_LOG_INFO("Gptp Init Success\n");
    } else {
        GPTP_LOG_ERROR("GPTP Init Failure\n");
        return 0;
    }

    if (gptpGetStatusAndCurPtpTime(&ptp_data)) {
        if (ptp_data.status) {
            GPTP_LOG_INFO("gptp status %d port state %d gptp time %u.%u\n",
                          ptp_data.status, ptp_data.port_state, ptp_data.tv_sec, ptp_data.tv_nsec);
        } else {
            GPTP_LOG_INFO("gptp status %d port state %d\n", ptp_data.status,
                          ptp_data.port_state);
        }
    } else {
        GPTP_LOG_ERROR("GPTP time test failed\n");
    }
	GPTP_LOG_INFO("---------------------------------------------------------\n");

#ifndef LE_GVM
#ifndef AVB_FEATURE_GVM_MODE
    test_vec_time = systemTime(CLOCK_REALTIME);

    if (gptpGetPtpTimefromSystime(&test_gptp_time, test_vec_time)) {
         GPTP_LOG_INFO("real_time %5llu.%09llu   gptp_time %5llu.%09llu\n",
                      test_vec_time / 1000000000ULL, test_vec_time % 1000000000ULL,
                      test_gptp_time / 1000000000ULL, test_gptp_time % 1000000000ULL);
    } else {
        GPTP_LOG_ERROR("Real time test failed\n");
    }

    test_vec_time = getQtimerTime();

    if (gptpGetPtpTimeFromQTimeNs(&test_gptp_time, test_vec_time)) {
         GPTP_LOG_INFO("qtimer_time %5llu.%09llu   gptp_time %5llu.%09llu\n",
                      test_vec_time / 1000000000ULL, test_vec_time % 1000000000ULL,
                      test_gptp_time / 1000000000ULL, test_gptp_time % 1000000000ULL);
    } else {
        GPTP_LOG_ERROR("Qtimer time test failed\n");
    }

    test_vec_time = getQtimerTicks();

    if (gptpGetPtpTimeFromQTimeTickCount(&test_gptp_time, test_vec_time)) {
        GPTP_LOG_INFO("qtimer_ticks   %15" PRIu64 "   gptp_time %5llu.%09llu\n",
                      test_vec_time,
                      test_gptp_time / 1000000000ULL, test_gptp_time % 1000000000ULL);
    } else {
        GPTP_LOG_ERROR("Qtimer time tick test failed\n");
    }

    test_vec_time = systemTime(CLOCK_MONOTONIC);

    if (gptpGetPtpTimeFromMonoTime(&test_gptp_time, test_vec_time)) {
        GPTP_LOG_INFO("mono_time    %5llu.%09llu   gptp_time %5llu.%09llu\n",
                      test_vec_time / 1000000000ULL, test_vec_time % 1000000000ULL,
                      test_gptp_time / 1000000000ULL, test_gptp_time % 1000000000ULL);
    } else {
        GPTP_LOG_ERROR("Monotonic time test failed\n");
    }

    if (gptpGetCurPtpTime(&test_gptp_time)) {
        GPTP_LOG_INFO("current                          gptp_time %5" PRIu64 ".%09"
                      PRIu64 "\n",
                      test_gptp_time / 1000000000UL, test_gptp_time % 1000000000UL);
    } else {
        GPTP_LOG_ERROR("GPTP time test failed\n");
    }

#endif // END AVB_FEATURE_GVM_MODE
#endif // END LE_GVM
#ifndef LE_GVM
    int l_cnt = LOOP_CNT;

    if (argc == 3) {
        l_cnt = atoi(argv[2]);
    }

    if (argc == 2 || argc == 3 || argc == 5) {
        if (argv[1][0] == 'q') {
            GPTP_LOG_INFO("====================QTIMER based test====================\n");
            do_some_tests_qtimer(l_cnt);
        } else if (argv[1][0] == 's') {
            GPTP_LOG_INFO("====================SYSTEM based test====================\n");
            do_some_tests_sys(l_cnt);
        } else if (argv[1][0] == 'p') {
            GPTP_LOG_INFO("======================PTP based test=====================\n");
            do_some_tests_ptp(l_cnt);
        } else if (argv[1][0] == 'm') {
            GPTP_LOG_INFO("================gPTP Monotonic pair based test================\n");
            do_some_tests_gptp_mono(l_cnt);
        } else if (argv[1][0] == 'l') {
            GPTP_LOG_INFO("======================gPTP loop test=====================\n");
            loop_test(l_cnt);
        } else if (argv[1][0] == 'g') {
            GPTP_LOG_INFO("\n\n=======================clock_gettime based test=========================\n\n");
            get_gptp_time(l_cnt);
        } else if (argv[1][0] == 't') {
            GPTP_LOG_INFO("\n\n=======================clock_gettime TSC based test=========================\n\n");
            get_gptp_tsc_time(l_cnt);
        } else if (argv[1][0] == 'b') {
            GPTP_LOG_INFO("\n\n\n====================gPTP time boot time test=====================\n\n\n");
            do_some_tests_gptp_boot(l_cnt);
        } else if ((argc >= 3) && (argv[1][0] == 'R')) {
            GPTP_LOG_INFO("===============gPTP Reverse sync test===============\n");
            Rsync.reverseSyncEnabled = atoi(argv[2]);

            if (Rsync.reverseSyncEnabled && argc == 5) {
                Rsync.reverseSyncDomain = atoi(argv[3]);
                Rsync.reverseSyncRate = atof(argv[4]);
            }

            GPTP_LOG_INFO("RSYNC: %d, RSYNCDOMAIN %d, RSYNCRATE %f",
                          Rsync.reverseSyncEnabled, Rsync.reverseSyncDomain, Rsync.reverseSyncRate);

            if (setRsyncStatus(&Rsync)) {
                GPTP_LOG_ERROR("Error while setting reverse sync status");
                return 0;
            }
        }

#ifdef RGPTP_CLNT_ENABLED
        else if (argv[1][0] == 'r') {
            rgptp_test();
        }

#endif
    } else if (argc == 4 || argc == 6) {
        if (argv[1][0] == 'm') {
            GPTP_LOG_INFO("\n\n\n====================gPTP Monotonic pair based test=====================\n\n\n");
            int sleepduration = atoi(argv[3]);
            gptpRegisterCallback(&callback_handler);
            do_some_tests_gptp_mono(l_cnt);
            sleep(sleepduration);
            do_some_tests_gptp_mono(l_cnt);
            gptpRegisterCallback(NULL);
        } else if (argv[1][0] == 'l') {
            GPTP_LOG_INFO("\n\n\n====================gPTP loop test=====================\n\n\n");
            int sleepduration = atoi(argv[3]);
            loop_test(sleepduration);
        }
    }

#ifdef RGPTP_CLNT_ENABLED

    if (argc == 3) {
        if (argv[1][0] == 's') {
            int time_s = 0;
            time_s = atoi(argv[2]);
            GPTP_LOG_INFO("\n\n====================RPTP based test========================");
            GPTP_LOG_INFO("\nsleep interval: %ds\n", time_s);
            do_some_tests_rgptp_s(time_s);
        } else if (argv[1][0] == 'u') {
            int time_us = 0;
            time_us = atoi(argv[2]);
            GPTP_LOG_INFO("\n\n====================RPTP based test=====================");
            GPTP_LOG_INFO("\nsleep interval: %dus\n", time_us);
            do_some_tests_rgptp_u(time_us);
        }
    }

#endif
#endif // END LE_GVM

    if (!gptpDeinit()) {
        GPTP_LOG_ERROR("GPTP deinit failed\n");
    }

#ifdef DLT_AVAILABLE
    libgptplogUnregister();
#endif
    return 0;
}

/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <utc_ipc.hpp>
#include "utc_helper.h"

#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"libutc", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"libutc", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"libutc", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"libutc", fmt, __VA_ARGS__); printf(fmt,##__VA_ARGS__)

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};
#endif


#define UTC_LIB_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LIB_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LIB_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)
#define UTC_LIB_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__); printf(fmt,##__VA_ARGS__)

static int gUTCShmFd = -1;
static char *gUTCMmap = NULL;

static utc_update_cb_t gUserCallback = NULL;
static pthread_t gMonitorThreadId = 0;
static volatile bool gMonitorRunning = false;

unsigned char calculateChecksum(const char *str, size_t length) {
    unsigned char checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += str[i];
    }
    return checksum;
}

static bool UTCMemInit()
{
    if (gUTCShmFd  == -1) {
#ifdef ANDROID
        gUTCShmFd = open( UTC_SHM_NAME, O_RDWR, 0);
#else
        gUTCShmFd = shm_open(UTC_SHM_NAME, O_RDWR, 0);
#endif
        UTC_LIB_LOG_INFO("UTCMemInit %s %d\n", UTC_SHM_NAME, gUTCShmFd);

        if (gUTCShmFd == -1) {
            perror("shm_open()");
            return false;
        }

        gUTCMmap =
            (char *)mmap(NULL, UTC_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                         gUTCShmFd, 0);
        UTC_LIB_LOG_INFO("UTCMemInit mmap pointer %p\n", gUTCMmap);

        if (gUTCMmap == (char *) -1) {
            perror("mmap()");
            gUTCMmap = NULL;

            if (gUTCShmFd  != -1) {
                close(gUTCShmFd);
                gUTCShmFd = -1;
            }

            UTC_LIB_LOG_ERROR("UTCMemInit failed %s\n", UTC_SHM_NAME);
            gUTCShmFd = -1;
            return false;
        }
    }

    return true;
}



static void UTCMemDeinit()
{
    if (gUTCShmFd != -1) {
        if (gUTCMmap != NULL) {
            munmap(gUTCMmap, UTC_SHM_SIZE);
            gUTCMmap = NULL;
        }
        UTC_LIB_LOG_INFO("UTCMem umap \n");

        if (gUTCShmFd != -1) {
            close(gUTCShmFd);
        }
        gUTCShmFd = -1;

        UTC_LIB_LOG_INFO("UTCMemDeinit %zu\n", UTC_SHM_SIZE);
    }
}

static void* MonitorThreadFunc(void* arg) {
    UTC_LIB_LOG_INFO("Monitor thread started\n");
    
    if (gUTCMmap == NULL) {
        if (!UTCMemInit()) {
            UTC_LIB_LOG_ERROR("Monitor thread cannot init memory, exiting\n");
            return NULL;
        }
    }

    UtcShm* pUtcShm = (UtcShm*)gUTCMmap;
    
    while (gMonitorRunning) {
        pthread_mutex_lock(&pUtcShm->pMutex);

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += 1;

        int rc = pthread_cond_timedwait(&pUtcShm->pCond, &pUtcShm->pMutex, &ts);

        if (rc == 0) {
            gUtcTimeData* data = (gUtcTimeData*)(&pUtcShm->gData);
            if (calculateChecksum((const char *)data, sizeof(gUtcTimeData)) == pUtcShm->checksum) {

                int current_status = data->sync_status;
                pthread_mutex_unlock(&pUtcShm->pMutex);
                if (gUserCallback) {
                    gUserCallback(current_status);
                }
            } else {
                UTC_LIB_LOG_ERROR("Monitor Checksum failed\n");
                pthread_mutex_unlock(&pUtcShm->pMutex);
            }
        } else {
            pthread_mutex_unlock(&pUtcShm->pMutex);
        }
    }
    
    UTC_LIB_LOG_INFO("Monitor thread exiting\n");
    return NULL;
}

int utcGetSyncStatus(){
    int status = 0;
    if (gUTCMmap == NULL) {
        UTC_LIB_LOG_ERROR("utcGetSyncStatus memory failure %p\n", gUTCMmap);
        UTCMemInit();
        return false;
    }

    UtcShm* pUtcShm = (UtcShm*)gUTCMmap;
    gUtcTimeData* data = (gUtcTimeData*)(&pUtcShm->gData);
    pthread_mutex_lock(&pUtcShm->pMutex);
    if (calculateChecksum((const char *)data, sizeof(gUtcTimeData)) != pUtcShm->checksum) {
        UTC_LIB_LOG_ERROR("memory checksum failure \n");
        pthread_mutex_unlock(&pUtcShm->pMutex);
        return false;
    }
    status = data->sync_status;
    pthread_mutex_unlock(&pUtcShm->pMutex);

    return status;
}

uint64_t utcGetUtcTime(){
    uint64_t utc_time = 0;
    if (gUTCMmap == NULL) {
        UTC_LIB_LOG_ERROR("utcGetUtcTime memory failure %p\n", gUTCMmap);
        UTCMemInit();
        return false;
    }

    UtcShm* pUtcShm = (UtcShm*)gUTCMmap;
    gUtcTimeData* data = (gUtcTimeData*)(&pUtcShm->gData);
    pthread_mutex_lock(&pUtcShm->pMutex);
    if (calculateChecksum((const char *)data, sizeof(gUtcTimeData)) != pUtcShm->checksum) {
        UTC_LIB_LOG_ERROR("memory checksum failure \n");
        pthread_mutex_unlock(&pUtcShm->pMutex);
        return false;
    }
    utc_time = data->utc_time;
    pthread_mutex_unlock(&pUtcShm->pMutex);

    return utc_time;
}

uint64_t utcGetLastSyncTime(){
    uint64_t utc_time = 0;
    if (gUTCMmap == NULL) {
        UTC_LIB_LOG_ERROR("utcGetLastSyncTime memory failure %p\n", gUTCMmap);
        UTCMemInit();
        return false;
    }

    UtcShm* pUtcShm = (UtcShm*)gUTCMmap;
    gUtcTimeData* data = (gUtcTimeData*)(&pUtcShm->gData);
    pthread_mutex_lock(&pUtcShm->pMutex);
    if (calculateChecksum((const char *)data, sizeof(gUtcTimeData)) != pUtcShm->checksum) {
        UTC_LIB_LOG_ERROR("memory checksum failure \n");
        pthread_mutex_unlock(&pUtcShm->pMutex);
        return false;
    }
    utc_time = data->utc_time;
    pthread_mutex_unlock(&pUtcShm->pMutex);

    return utc_time;
}

uint64_t utcRegisterUpdateCallback(utc_update_cb_t cb) {
    if (cb == NULL) return false;
    
    gUserCallback = cb;

    if (!gMonitorRunning) {
        if (gUTCMmap == NULL) {
            if (!UTCMemInit()) {
                UTC_LIB_LOG_ERROR("utcRegisterUpdateCallback memory failure %p\n", gUTCMmap);
                return false;
            }
        }

        gMonitorRunning = true;
        int ret = pthread_create(&gMonitorThreadId, NULL, MonitorThreadFunc, NULL);
        if (ret != 0) {
            UTC_LIB_LOG_ERROR("Failed to create monitor thread: %d\n", ret);
            gMonitorRunning = false;
            return false;
        }
    }
    return 0;
}

int utc_helper_init() {
    if (!UTCMemInit()){
        UTC_LIB_LOG_ERROR("memory init failure \n");
        return -1;
    }
    return 0;
}

void utc_helper_deinit() {

    if (gMonitorRunning) {
        UTC_LIB_LOG_INFO("Stopping monitor thread...\n");
        gMonitorRunning = false;

        if (gMonitorThreadId != 0) {
            pthread_join(gMonitorThreadId, NULL);
            gMonitorThreadId = 0;
        }
        UTC_LIB_LOG_INFO("Monitor thread stopped.\n");
    }

    gUserCallback = NULL;

    UTCMemDeinit();
}


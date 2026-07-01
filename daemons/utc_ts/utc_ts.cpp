/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

/*******************************************
*
*           utc_ts Application
*
*********************************************/
#include <cstdint>
#include <sys/timex.h>
#include <gptp_helper.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fenv.h>
#include <math.h>
#include <stdbool.h>
#include <utc_ipc.hpp>
#include <sys/stat.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/capability.h>


#ifdef ANDROID
#include <log/log.h>
#else
#include <syslog.h>
#endif

#ifdef ANDROID

#define LOGE(fmt, ...) __android_log_print (ANDROID_LOG_ERROR,"utc_ts", fmt, __VA_ARGS__);
#define LOGW(fmt, ...) __android_log_print (ANDROID_LOG_WARN,"utc_ts", fmt, __VA_ARGS__);
#define LOGI(fmt, ...) __android_log_print (ANDROID_LOG_INFO,"utc_ts", fmt, __VA_ARGS__);
#define LOGD(fmt, ...) __android_log_print (ANDROID_LOG_DEBUG,"utc_ts", fmt, __VA_ARGS__);

enum _LOGGER_SEVERITY {
    QCLOG_ERROR         = ANDROID_LOG_ERROR,
    QCLOG_WARNING       = ANDROID_LOG_WARN,
    QCLOG_INFO          = ANDROID_LOG_INFO,
    QCLOG_DEBUG2        = ANDROID_LOG_DEBUG
};
#endif


/* These 4 macros are used only when Syntonize mode is enabled */
#define INTEGRAL 0.0003             /*!< PI controller integral factor*/
#define PROPORTIONAL 1.0            /*!< PI controller proportional factor*/
#define UPPER_FREQ_LIMIT  250.0     /*!< Upper frequency limit */
#define LOWER_FREQ_LIMIT -250.0     /*!< Lower frequency limit */

#define UPPER_LIMIT_PPM 250
#define LOWER_LIMIT_PPM -250
#define PPM_OFFSET_TO_RATIO(ppm) ((ppm) / ((FrequencyRatio)US_PER_SEC) + 1)

#define MIN_LS_RATIO 0.5
#define MAX_LS_RATIO 2.0

/* This is the threshold in ns for which frequency adjustments will be made */
#define PHASE_ERROR_THRESHOLD (1000000000)
#define DELTA_TIME_THRESHOLD (5000000)

/* This is the maximum count of phase error, outside of the threshold before
   adjustment is performed */
#define PHASE_ERROR_MAX_COUNT (6)


typedef long double FrequencyRatio;

#define UTC_LOG_ERROR(fmt, ...) LOGE("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__);
#define UTC_LOG_WARNING(fmt, ...) LOGW("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__);
#define UTC_LOG_INFO(fmt, ...) LOGI("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__);
#define UTC_LOG_DEBUG(fmt, ...) LOGD("[%s:%d] " fmt, __func__, __LINE__, ##__VA_ARGS__);


bool hab_thread_running = false;
pthread_t hab_Thread;
int32_t hab_hdl;
uint64_t prev_gptp_time = 0;
uint64_t prev_utc_time = 0;

uint64_t prev_utc_ref = 0;
uint64_t prev_expected_utc_ref = 0;
static int shm_fd = 0;
char *master_offset_buffer;
static int utc_fd = 0;


#define HAB_MMID_CREATE(major, minor) ((major&0xFFFF) | ((minor&0xFF)<<16))
#define HABMM_SOCKET_RECV_FLAGS_NON_BLOCKING 0x00000001
#define HABMM_SOCKET_RECV_FLAGS_UNINTERRUPTIBLE 0x00000002
#define HABMM_VNW_1 1401
#define HAB_UTC_SUB_ID 1
#ifdef ANDROID
#define DEFAULT_GROUPNAME "vendor_ptp"     /*!< Default groupname for the shared memory interface*/
#else
#define DEFAULT_GROUPNAME "vnw"     /*!< Default groupname for the shared memory interface*/
#endif


extern "C" int32_t habmm_socket_open(int32_t *handle, uint32_t mm_ip_id,
                                     uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_recv(int32_t handle, void *dst_buff,
                                     uint32_t *size_bytes, uint32_t timeout, uint32_t flags);
extern "C" int32_t habmm_socket_close(int32_t handle);

typedef enum {
    VALUE_STATE_UNAVAILABLE = 0,
    VALUE_STATE_VALID = 1,
    VALUE_STATE_INVALID = 2,
} ValueState;

typedef enum {
    VEHICLE_UTC_TIME_VALIDITY_TYPE_T_INVALID = 0,
    VEHICLE_UTC_TIME_VALIDITY_TYPE_T_VALID = 1,
} VehicleUtcTimeValidityTypeT;

struct utc_timeinfo_t {
    ValueState state;
    VehicleUtcTimeValidityTypeT sync_state;
    uint64_t curUtcTimeNanoSec;
    uint64_t curPtpTimeNanoSec;
};


int realtime_adjust_offset(long long offset)
{
    struct timex tx = {};
    int ret;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    tx.time.tv_sec = offset / 1000000000;
    tx.time.tv_usec = offset % 1000000000;

    if (offset < 0 && tx.time.tv_usec) {
        tx.time.tv_sec -= 1;
        tx.time.tv_usec += 1000000000;
    }

    ret = clock_adjtime(CLOCK_REALTIME, &tx);

    if (ret < 0) {
        UTC_LOG_ERROR("failed to realtime_adjust_offset %s", strerror(errno));
        return ret;
    }

    return 0;
}
int realtime_adjust_freq(float freq_offset)
{
    struct timex tx = {};
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_FREQUENCY;
    tx.freq  = long(freq_offset) << 16;
    tx.freq += long(fmodf( freq_offset, 1.0 ) * 65536.0);

    if (clock_adjtime(CLOCK_REALTIME, &tx) < 0) {
        UTC_LOG_ERROR("failed to realtime_adjust_freq %s", strerror(errno));
        return -1;
    }

    return 0;
}

unsigned char calculateChecksum(const char *str, size_t length) {
    unsigned char checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += str[i];
    }
    return checksum;
}


void updateShm(gUtcTimeData *pdata)
{
    gUtcTimeData* ptimedata;
    UtcShm* pUtcShm;
    static int previous_sync_status = false;

    if (master_offset_buffer != NULL) {
        pUtcShm = (UtcShm*)master_offset_buffer;
        /* lock */
        pthread_mutex_lock(&pUtcShm->pMutex);
        ptimedata = (gUtcTimeData*)(&pUtcShm->gData);
        ptimedata->sync_status = pdata->sync_status;
        ptimedata->utc_time = pdata->utc_time;
        ptimedata->gptp_time = pdata->gptp_time;
        pUtcShm->checksum = calculateChecksum((const char *)ptimedata, sizeof(gUtcTimeData));
        if (previous_sync_status != ptimedata->sync_status)
        {
            previous_sync_status = ptimedata->sync_status;
            pthread_cond_broadcast(&pUtcShm->pCond);
        }
        pthread_mutex_unlock(&pUtcShm->pMutex);
    }
}


void updateTime(utc_timeinfo_t* update)
{
    uint64_t curr_gptp = 0;
    bool sync_status = false;
    static int ppm_miss_count = 0;
    uint64_t curr_utc = 0;
    uint64_t curr_expected_utc = 0;
    int64_t delta_utc = 0;
    static float _ppm = 0;
    struct timespec real;
    long double phase_error;
    static double time_ratio = 1.0;
    static uint64_t cnt = 0;
    utc_timeinfo_t utc_update;

    memset(&utc_update, 0, sizeof(utc_timeinfo_t));
    memcpy(&utc_update, update, sizeof(utc_timeinfo_t));

    if (prev_utc_time != 0 && utc_update.curPtpTimeNanoSec != prev_gptp_time) {
        time_ratio = (double)(utc_update.curUtcTimeNanoSec -  prev_utc_time) /
                (double)(utc_update.curPtpTimeNanoSec - prev_gptp_time);
    } else {
        time_ratio = 1.0;
    }

    gptpGetCurPtpTime_s(&curr_gptp, NULL);
    sync_status = gptpGetSyncStatus();
    clock_gettime(CLOCK_REALTIME, &real);

    if (!sync_status || !utc_update.curPtpTimeNanoSec) {
        UTC_LOG_INFO("directly use someip utc as gptp is not in sync");
        curr_expected_utc = utc_update.curUtcTimeNanoSec;
    }
    else {
        if (time_ratio > MIN_LS_RATIO && time_ratio < MAX_LS_RATIO) {
            curr_expected_utc = utc_update.curUtcTimeNanoSec + (uint64_t)((curr_gptp -
                            utc_update.curPtpTimeNanoSec) * time_ratio);
        } else {
            curr_expected_utc = utc_update.curUtcTimeNanoSec + (curr_gptp - utc_update.curPtpTimeNanoSec);
        }
    }

    curr_utc = (real.tv_sec) * 1000000000LL + real.tv_nsec;
    delta_utc = curr_utc - curr_expected_utc;
    phase_error = (long double) - delta_utc;

#ifdef ENABLE_ADJUST_TIME
    if ((fabsl(phase_error) > PHASE_ERROR_THRESHOLD) || prev_utc_time == 0
            || ppm_miss_count > 10) {
        realtime_adjust_offset(phase_error);
    } else {
        FrequencyRatio freq_offset = 0;
        freq_offset = ((FrequencyRatio)(curr_expected_utc - prev_utc_ref)) /
                    (curr_utc - prev_utc_ref);

        // Check for jumps in REAL time or gptp time
        if ((fabs(freq_offset) < MIN_LS_RATIO) || (fabs(freq_offset) > MAX_LS_RATIO)) {
            UTC_LOG_WARNING("Real to UTC clock ratio (%Lf) exceeding threshold %lu %lu",
                            freq_offset, (curr_utc - prev_utc_ref),
                            (curr_expected_utc - prev_utc_ref));
            freq_offset = 1.0;
        } else {
            UTC_LOG_DEBUG("Real to UTC clock ratio (%Lf) delta %lu %lu",
                        freq_offset, (curr_utc - prev_utc_ref),
                        (curr_expected_utc - prev_utc_ref));
        }

        float syncPerSec = (float)(1.0 / pow((float)2,
                                            (utc_update.curUtcTimeNanoSec - prev_utc_time)));
        _ppm += (float) ((INTEGRAL * syncPerSec * phase_error) + PROPORTIONAL * ((
                            freq_offset - 1.0) * 1000000));
        UTC_LOG_DEBUG("phase_error = %Lf, ppm = %f", phase_error, _ppm );

        if ( _ppm < LOWER_FREQ_LIMIT ) {
            _ppm = LOWER_FREQ_LIMIT;
            ppm_miss_count++;
        } else if ( _ppm > UPPER_FREQ_LIMIT ) {
            _ppm = UPPER_FREQ_LIMIT;
            ppm_miss_count++;
        } else {
            ppm_miss_count = 0;
        }

        realtime_adjust_freq(_ppm);
    }
#else
    if (llabs(delta_utc) > DELTA_TIME_THRESHOLD) {
        realtime_adjust_offset(phase_error);
    }
#endif


    gUtcTimeData utcData = {0};
    utcData.sync_status = utc_update.sync_state;
    utcData.utc_time = curr_expected_utc;
    utcData.gptp_time = curr_gptp;
    updateShm(&utcData);

    prev_utc_time = utc_update.curUtcTimeNanoSec;
    prev_gptp_time = utc_update.curPtpTimeNanoSec;
    prev_utc_ref = curr_utc;
    prev_expected_utc_ref = curr_expected_utc;
    UTC_LOG_DEBUG("[%lu]curr_utc %lu curr_expected_utc %lu delta_utc %ld state %d",
                  cnt, curr_utc, curr_expected_utc, delta_utc, utc_update.sync_state);
    cnt++;
}


void* habLoop(void* param)
{
    int32_t ret;
    struct utc_timeinfo_t update;
    uint32_t len = sizeof(update);
    int32_t last_sync_state = 0;

    while (hab_thread_running) {
        memset(&update, 0, sizeof(update));

        do {
            len = sizeof(update);
            ret = habmm_socket_recv(hab_hdl, &update, &len, 0, 0);
        } while (-EINTR == ret || -EAGAIN == ret);

        if (ret) {
            UTC_LOG_ERROR("habmm_socket_recv failed, ret= 0x%x\n", ret);
            return NULL;
        }

	    //Customer mentioned gvm's validity must be true when we recived utc time from qnx
	    update.sync_state = VEHICLE_UTC_TIME_VALIDITY_TYPE_T_VALID;
        updateTime(&update);
        if (update.sync_state != last_sync_state) {
            UTC_LOG_INFO("sync_state change, prev(%d), curr(%d)", last_sync_state, update.sync_state);
            last_sync_state = update.sync_state;
        }
        if (utc_fd) {
            char status[32];
            snprintf(status, sizeof(status), "%d-%llu\n", update.sync_state, update.curUtcTimeNanoSec/1000000000ULL);
            lseek(utc_fd, 0, SEEK_SET);
            write(utc_fd, status, strlen(status));
        }

    }
    return NULL;
}

void utc_shm_deinit(void)
{
    int err = 0;
    if (master_offset_buffer != NULL && master_offset_buffer != (char*)-1) {
        if (munmap(master_offset_buffer, UTC_SHM_SIZE) != 0) {
            UTC_LOG_ERROR("munmap() failed - %s", strerror(errno));
        }
        master_offset_buffer = NULL;
    }

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }
}

int utc_shm_init(void) 
{
    pthread_mutexattr_t shared;
    const char* group_name;
    struct group* grp;
    UtcShm* pShm;
    mode_t oldumask = umask(0);
    int err;

    group_name = DEFAULT_GROUPNAME;
    grp = getgrnam(group_name);

    if (grp == NULL) {
        UTC_LOG_INFO("Group %s not found, will try root (0) instead", group_name);
    }

#ifdef ANDROID
    shm_fd = open(UTC_SHM_NAME, O_RDWR | O_CREAT, 0666);
#else
    shm_fd = shm_open(UTC_SHM_NAME, O_RDWR | O_CREAT, 0660);
#endif

    if (shm_fd == -1) {
        UTC_LOG_ERROR("shm_open(): %s", strerror(errno));
        return -1;
    }

    (void)umask(oldumask);

    if (fchown(shm_fd, -1, grp != NULL ? grp->gr_gid : 0) < 0) {
        UTC_LOG_ERROR("shm_open(): Failed to set ownership");
    }

    if (ftruncate(shm_fd, UTC_SHM_SIZE) == -1) {
        UTC_LOG_ERROR("ftruncate()");
        goto exit;
    }

    master_offset_buffer = (char*)mmap
                           (NULL, UTC_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_LOCKED | MAP_SHARED,
                            shm_fd, 0);

    if (master_offset_buffer == (char*) -1) {
        UTC_LOG_ERROR("mmap()");
        goto exit;
    }

    memset(master_offset_buffer, 0x0, UTC_SHM_SIZE);

    pShm = (UtcShm*)master_offset_buffer;

    /*create mutex attr */
    pthread_mutexattr_init(&shared);
    pthread_mutexattr_setpshared(&shared, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setprotocol(&shared, PTHREAD_PRIO_INHERIT);

    /*create a mutex */
    err = pthread_mutex_init(&pShm->pMutex, &shared);
    if (err != 0) {
        UTC_LOG_ERROR("sharedmem - Mutex initialization failed - %s", strerror(errno));
        goto exit;
    }

    /*create cond attr */
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC); 

    err = pthread_cond_init(&pShm->pCond, &cond_attr);
    if (err != 0) {
        UTC_LOG_ERROR("CondVar init failed");
        goto exit;
    }

    return 0;

exit:
    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    return -1;
}

int set_cap_sys_time(void) {
    int ret = 0;
    cap_t caps = cap_get_proc();
    if (!caps) {
        UTC_LOG_ERROR("Failed to get capabilities\n");
        return -1;
    }
    cap_flag_value_t cap_value;
    cap_value_t cap_list[1] = {CAP_SYS_TIME};
    cap_set_flag(caps, CAP_EFFECTIVE, 1, cap_list, CAP_SET);
    if (cap_set_proc(caps) != 0) {
        UTC_LOG_ERROR("cap set proc failed");
    }
    if (cap_get_flag(caps, CAP_SYS_TIME, CAP_PERMITTED, &cap_value) == 0) {
        if (cap_value == CAP_SET) {
            UTC_LOG_INFO("Process has CAP_SYS_TIME\n");
        } else {
            UTC_LOG_ERROR("Process does NOT have CAP_SYS_TIME\n");
            ret = -1;
        }
    } else {
        UTC_LOG_ERROR("Failed to get CAP_SYS_TIME flag\n");
        ret = -1;
    }
    cap_free(caps);
    return ret;
}

int utc_time_info_init(void) {
    const char* group_name;
    struct group* grp;

    group_name = DEFAULT_GROUPNAME;
    grp = getgrnam(group_name);

    if (grp == NULL) {
        UTC_LOG_INFO("Group %s not found, will try root (0) instead", group_name);
    }

    utc_fd = open(UTC_TIME_INFO, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (utc_fd < 0) {
        UTC_LOG_ERROR("open /dev/timeinfo failed - %s", strerror(errno));
        return -1;
    }

    if (fchown(utc_fd, -1, grp != NULL ? grp->gr_gid : 0) < 0) {
        UTC_LOG_ERROR("fchown(): Failed to set ownership - %s", strerror(errno));
    }
    const char *status = "0-0\n";
    write(utc_fd, status, strlen(status));
    return 0;
}

int main(int argc, char **argv)
{
    int32_t ret;
    int sig;
    sigset_t set;
    int err = 0;
    struct timespec timeout;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset( &set, SIGTERM );
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR2);

    pthread_sigmask(SIG_BLOCK, &set, NULL);

    timeout.tv_sec = 0;
	timeout.tv_nsec = 50000000;

    UTC_LOG_INFO("UTC Time Service starting");

    ret = set_cap_sys_time();
    if (ret < 0) {
        UTC_LOG_ERROR("set CAP_SYS_TIME capability failed\n");
    }

    ret = utc_time_info_init();
    if (ret < 0) {
        UTC_LOG_ERROR("utc time info init failed");
        goto exit;
    }

    while (!gptpInit()) {
        sig = sigtimedwait(&set, NULL, &timeout);
        if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP ) {
			perror("sigtimedwait()");
			goto exit;
		}
        UTC_LOG_WARNING("waiting for Gptp Init to  succeed\n");
    }

    ret = utc_shm_init();
    if (ret < 0) {
        UTC_LOG_ERROR("utc shared memory init failed");
        goto exit;
    }

    ret = habmm_socket_open(&hab_hdl, HAB_MMID_CREATE(HABMM_VNW_1, HAB_UTC_SUB_ID),
                            0, 0);
    if (ret < 0) {
        UTC_LOG_ERROR("habmm_socket_open: socket create failed\n");
        goto exit;
    }

    hab_thread_running = true;

    if ((err = pthread_create(&hab_Thread, NULL, habLoop, (void *) NULL))
            < 0) {
        hab_thread_running = false;
        UTC_LOG_ERROR("Error during creation of the thread %d\n", err);
        goto exit;
    } else {
        hab_thread_running = true;
    }

    UTC_LOG_INFO("UTC Time Service started successfully");

    do {
        sig = 0;

        if (sigwait(&set, &sig) != 0) {
            perror("sigwait()");
            break;
        }
    } while (sig == SIGHUP || sig == SIGUSR2);

exit:
    hab_thread_running = false;
    if (hab_hdl) {
        habmm_socket_close(hab_hdl);
    }
    if (hab_Thread) {
        pthread_join(hab_Thread, NULL);
    }
    gptpDeinit();
    utc_shm_deinit();
    if (utc_fd) {
        close(utc_fd);
        utc_fd = -1;
    }
    UTC_LOG_INFO("UTC Time Service exit");
    return 0;
}



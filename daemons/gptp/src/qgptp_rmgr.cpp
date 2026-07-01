/******************************************************************************

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

/*==================================================================


  @file qgptp_rmgr.cpp
  @brief Resource manager to read synchrnoized time from gptp


====================================================================*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <grp.h>

#include <pthread.h>

#include <qgptp_rmgr.h>
#include <gptp_log.hpp>

//#include <hw/dcmd_qc_ethernet.h>
#define MAX_STR_LEN 2048
//#include <qgptp_rmgr_ioctl.h>

#define PTP_TMR_PHY_ADDR    (0x1D000000u)
#define PTP_TMR_ADDR_MAP_SZ    (2048)
#define MAX_RMGR_NAME_LEN 513

/* gPTP LPM modes */
#define GPTP_LPM_MODE_OFF         1
#define GPTP_LPM_MODE_ON          2

//struct qgptp_rmgr_t *qgptp_rmgr;
static CommonPort *qgptp_port = NULL;
static void * qgptp_time_virt_addr = NULL;
static bool ptp_for_guest = false;
static bool gptp_rmgr_keepruning = false;
pthread_t thread_id;
uint32_t g_verbose_mode = 0;

uint64_t get_ntn_time(char *ifname)
{
    uint64_t time = 0;

    if (qgptp_port) {
        qgptp_port->getCurrentPtpTime(&time);
    }

    return time;
}

static void get_timesync_diagstats (char *ifname,
                                    PortAutoTimeSyncDiagData_t *timesync_diagstats)
{
    if (qgptp_port == NULL) {
        GPTP_LOG_ERROR("qgptp_port is NULL in get_timesync_diagstats");
        return;
    }
    if (timesync_diagstats == NULL) {
        GPTP_LOG_ERROR("timesync_diagstats is NULL in get_timesync_diagstats");
        return;
    }
    timesync_diagstats->timeSyncDriftCount =
        qgptp_port->timesync_diagstats.timeSyncDriftCount;
    timesync_diagstats->timeSyncStatusDID =
        qgptp_port->timesync_diagstats.timeSyncStatusDID;
}


int get_gptp_stats(char *reply_msg, uint64_t  replytime)
{
    static PortCounters_t Old_PortCounters = {};
    static uint64_t last_abstime = 0;
    PortCounters_t PortCounters = {};
    int nBytes = 0;

    if (qgptp_port) {
        qgptp_port->getPortStats(&PortCounters);
        Timestamp  mine, theirs;
        qgptp_port->getPeerOffset(mine, theirs);
        uint64_t delay = 0;
        qgptp_port->getLinkDelay(&delay);
        nBytes += snprintf(reply_msg, MAX_STR_LEN - nBytes,
                           "gPTP_slave:absTime %lu ns (%lu ns) - offsetMaster %s - rxSync %d (%d) - rxFollowUp %d (%d) - txPdelReq %d (%d) - rxPdelResp %d (%d) - rxPdelRespFollowUp %d (%d)\n",
                           replytime, last_abstime, theirs.toString().c_str(),
                           PortCounters.ieee8021AsPortStatRxSyncCount,
                           Old_PortCounters.ieee8021AsPortStatRxSyncCount,
                           PortCounters.ieee8021AsPortStatRxFollowUpCount,
                           Old_PortCounters.ieee8021AsPortStatRxFollowUpCount,
                           PortCounters.ieee8021AsPortStatTxPdelayRequest,
                           Old_PortCounters.ieee8021AsPortStatTxPdelayRequest,
                           PortCounters.ieee8021AsPortStatRxPdelayResponse,
                           Old_PortCounters.ieee8021AsPortStatRxPdelayResponse,
                           PortCounters.ieee8021AsPortStatRxPdelayResponseFollowUp,
                           Old_PortCounters.ieee8021AsPortStatRxPdelayResponseFollowUp);
        nBytes += snprintf(reply_msg + nBytes, MAX_STR_LEN - nBytes,
                           "gPTP_slave: rxPdelReq %d (%d) - txPdelResp %d (%d) -txPdelRespFollowUp %d (%d) \n",
                           PortCounters.ieee8021AsPortStatRxPdelayRequest,
                           Old_PortCounters.ieee8021AsPortStatRxPdelayRequest,
                           PortCounters.ieee8021AsPortStatTxPdelayResponse,
                           Old_PortCounters.ieee8021AsPortStatTxPdelayResponse,
                           PortCounters.ieee8021AsPortStatTxFollowUpCount,
                           Old_PortCounters.ieee8021AsPortStatTxFollowUpCount);
        nBytes += snprintf(reply_msg + nBytes, MAX_STR_LEN - nBytes,
                           "gPTP_slave: syncLoss %lu ns - syncTimeout %d  - nonContSyn %lu ns - PdelRespTimeout %lu ns rxPTPDiscard %d - clkRatio %9.3Lf - cumClkRatio %d - pdelayNeighbor %lu\n",
                           qgptp_port->get_avnu_loss_of_sync_message(),
                           PortCounters.ieee8021AsPortStatRxSyncReceiptTimeouts,
                           qgptp_port->get_avnu_sync_discontinutity(),
                           qgptp_port->get_avnu_pdelay_resp_timeout(),
                           PortCounters.ieee8021AsPortStatRxPTPPacketDiscard,
                           qgptp_port->getClockRateRatio(),
                           qgptp_port->getcumulativeRateRatio(), delay);
        nBytes += snprintf(reply_msg + nBytes, MAX_STR_LEN - nBytes,
                           "gPTP_slave: offsetScaledLogVariance %d TxSyncCount %d (%d) -TxPdelayResponseFollowUp %d (%d) - PdelayAllowedLostResponsesExceeded %d (%d)\n",
                           qgptp_port->getClock()->getGrandmasterClockQuality().offsetScaledLogVariance,
                           PortCounters.ieee8021AsPortStatTxSyncCount,
                           Old_PortCounters.ieee8021AsPortStatTxSyncCount,
                           PortCounters.ieee8021AsPortStatTxPdelayResponseFollowUp,
                           Old_PortCounters.ieee8021AsPortStatTxPdelayResponseFollowUp,
                           PortCounters.ieee8021AsPortStatPdelayAllowedLostResponsesExceeded,
                           Old_PortCounters.ieee8021AsPortStatPdelayAllowedLostResponsesExceeded);
        nBytes += snprintf(reply_msg + nBytes, MAX_STR_LEN - nBytes,
                           "gPTP_slave: RxAnnounce %d (%d)- AnnounceReceiptTimeout %d (%d) - TxAnnounce %d (%d) - deviation of local clock ratio: %s\n",
                           PortCounters.ieee8021AsPortStatRxAnnounce,
                           Old_PortCounters.ieee8021AsPortStatRxAnnounce,
                           PortCounters.ieee8021AsPortStatAnnounceReceiptTimeouts,
                           Old_PortCounters.ieee8021AsPortStatAnnounceReceiptTimeouts,
                           PortCounters.ieee8021AsPortStatTxAnnounce,
                           Old_PortCounters.ieee8021AsPortStatTxAnnounce,
                           mine.toString().c_str());
        nBytes += snprintf(reply_msg + nBytes, MAX_STR_LEN - nBytes,
                           "AVB_SYNC_TEST: Sequence_id %d (%d)- linkup_count %d (%d) - linkdown %d (%d) - test station state: %d (%d) - ether port link state: %d(%d)\n",
                           PortCounters.avb_sync_test_sequenceId,
                           Old_PortCounters.avb_sync_test_sequenceId,
                           PortCounters.avb_sync_test_linkup_count,
                           Old_PortCounters.avb_sync_test_linkup_count,
                           PortCounters.avb_sync_test_linkdown_count,
                           Old_PortCounters.avb_sync_test_linkdown_count,
                           PortCounters.avb_sync_test_station_state,
                           Old_PortCounters.avb_sync_test_station_state,
                           PortCounters.ethPortLinkState,
                           Old_PortCounters.ethPortLinkState);
        memcpy(&Old_PortCounters, &PortCounters, sizeof(PortCounters_t));
        last_abstime = replytime;
    }

    return nBytes;
}



static float get_ppm(char *ifname)
{
    float ret = 0;

    if (qgptp_port == NULL) {
        GPTP_LOG_ERROR("qgptp_port is NULL in get_ppm");
        return ret;
    }

    if (qgptp_port->getClock()) {
        ret = qgptp_port->getClock()->getPPMValue();
        GPTP_LOG_DEBUG("GPTP clock rate ratio (ppm) value is %f\n", ret);
    } else {
        GPTP_LOG_ERROR("Failed to get ppm value");
    }

    return ret;
}



int qgptp_rmgr_init(int* sct_shm_fd, sct_gptp_data **sct_buffer)
{
    int err;
    struct group *grp;
    const char *group_name;
    pthread_mutexattr_t shared;
    mode_t oldumask = umask(0);
    group_name = DEFAULT_GROUPNAME;
    grp = getgrnam( group_name );

    if ( grp == NULL ) {
        GPTP_LOG_INFO( "Group %s not found, will try root (0) instead", group_name );
    }

#ifdef ANDROID
    *sct_shm_fd = open( SCT_SHM_NAME, O_RDWR | O_CREAT, 0666 );
#else
    *sct_shm_fd = shm_open( SCT_SHM_NAME, O_RDWR | O_CREAT, 0660 );
#endif

    if ( *sct_shm_fd == -1 ) {
        GPTP_LOG_ERROR( "shm_open(): %s", strerror(errno) );
        goto exit_unlink;
    }

    (void) umask(oldumask);

    if (fchown(*sct_shm_fd, -1, grp != NULL ? grp->gr_gid : 0) < 0) {
        GPTP_LOG_ERROR("shm_open(): Failed to set ownership");
    }

    if ( ftruncate( *sct_shm_fd, SCT_SHM_SIZE ) == -1 ) {
        GPTP_LOG_ERROR( "ftruncate()" );
        goto exit_unlink;
    }

    *sct_buffer = (sct_gptp_data *) mmap
                  ( NULL, SCT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_LOCKED | MAP_SHARED,
                    *sct_shm_fd, 0 );

    if (  *sct_buffer == (sct_gptp_data *) - 1 ) {
        GPTP_LOG_ERROR( "mmap()" );
        *sct_buffer = NULL;
        goto exit_unlink;
    }

    memset(*sct_buffer, 0x0, SCT_SHM_SIZE);
    /*create mutex attr */
    err = pthread_mutexattr_init(&shared);

    if (err != 0) {
        GPTP_LOG_ERROR
        ("mutex attr initialization failed - %s",
         strerror(errno));
        goto exit_unlink;
    }

    pthread_mutexattr_setpshared(&shared, 1);
    pthread_mutexattr_setprotocol(&shared, PTHREAD_PRIO_INHERIT);
    /*create a mutex */
    err = pthread_mutex_init((pthread_mutex_t *)  & (*sct_buffer)->lock,
                             &shared);

    if (err != 0) {
        GPTP_LOG_ERROR
        ("sharedmem - Mutex initialization failed - %s",
         strerror(errno));
        goto exit_unlink;
    }

    return 0;
exit_unlink:
#ifdef ANDROID

    if (*sct_shm_fd != -1) {
        close(*sct_shm_fd);
        *sct_shm_fd = -1;
    }

    //unlink( SCT_SHM_NAME );
#else

    if (*sct_shm_fd != -1) {
        close(*sct_shm_fd);
        *sct_shm_fd = -1;
    }

#endif
    GPTP_LOG_INFO("qgptp_rmgr_init error exit %s", SCT_SHM_NAME);
    return -1;
}

int qgptp_rmgr_setport(CommonPort *port)
{
    if (!port) {
        GPTP_LOG_ERROR("qgptp_rmgr_init Failed as port is null\n");
        return -1;
    }

    qgptp_port = port;

    if (!qgptp_port->sct_buffer) {
        GPTP_LOG_ERROR("qgptp_rmgr_init Failed as qgptp_port->sct_buffer is null\n");
        return -1;
    }

    GPTP_LOG_INFO("qgptp_rgptp_setport success");
    return 0;
}

int qgptp_rmgr_deinit()
{
    if (!qgptp_port) {
        GPTP_LOG_ERROR("qgptp_rmgr_deinit Failed as port is null\n");
        return -1;
    }

    if ( qgptp_port->sct_buffer != NULL ) {
        sct_gptp_data *sct_buffer = qgptp_port->sct_buffer;

        if (sct_buffer) {
            memset(&sct_buffer->syncData, 0x0, sizeof(syncMesaurementData_t));
            memset(&sct_buffer->delayData, 0x0, sizeof(pDelayMeasurementData_t));
            memset(&sct_buffer->status, 0x0, sizeof(gptpStatsType_t));
            memset(&sct_buffer->syncInterval, 0x0, sizeof(syncInterval_t));
        }

        memset(qgptp_port->sct_buffer, 0x0, SCT_SHM_SIZE);
        munmap(qgptp_port->sct_buffer, SCT_SHM_SIZE);
        qgptp_port->sct_buffer = NULL;
#ifdef ANDROID

        if (qgptp_port->sct_shm_fd != -1) {
            close(qgptp_port->sct_shm_fd);
            qgptp_port->sct_shm_fd = -1;
        }

        //unlink( SCT_SHM_NAME );
#else

        if (qgptp_port->sct_shm_fd != -1) {
            close(qgptp_port->sct_shm_fd);
            qgptp_port->sct_shm_fd = -1;
        }

        //shm_unlink(SCT_SHM_NAME);
#endif
    }

    GPTP_LOG_INFO("qgptp_rmgr_deinit success %s", SCT_SHM_NAME);
    qgptp_port = NULL;
    return 0;
}

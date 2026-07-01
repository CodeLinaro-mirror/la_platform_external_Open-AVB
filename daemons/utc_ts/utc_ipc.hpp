/* ============================================================================
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#ifndef UTC_IPC_HPP
#define UTC_IPC_HPP

#define UTC_TIME_INFO "/dev/timeinfo"
#define UTC_SHM_NAME "/dev/utcshm"


typedef struct
{
    int sync_status;               //!< UTC Sync status
    uint64_t utc_time;              //!< UTC time
    uint64_t gptp_time;             //!< PTP time
}gUtcTimeData;

typedef struct
{
    pthread_mutex_t pMutex;
    pthread_cond_t  pCond;
    gUtcTimeData gData;
    unsigned char checksum;
}UtcShm;

#define UTC_SHM_SIZE (sizeof(UtcShm))   /*!< Shared memory size*/

#endif
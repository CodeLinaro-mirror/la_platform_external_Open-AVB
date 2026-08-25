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

Copyright (c) 2012-2015, Symphony Teleca Corporation, a Harman International Industries, Incorporated company
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS LISTED "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS LISTED BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Attributions: The inih library portion of the source code is licensed from
Brush Technology and Ben Hoyt - Copyright (c) 2009, Brush Technology and Copyright (c) 2009, Ben Hoyt.
Complete license and copyright information can be found at
https://github.com/benhoyt/inih/commit/74d2ca064fb293bc60a77b0bd068075b293cf175.
============================================================================ */

/* ============================================================================
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
============================================================================ */

#ifndef __GPTP_HELPER_H__
#define __GPTP_HELPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DLT_AVAILABLE
#include <dlt/dlt.h>
#include <unistd.h>
#endif

#define CLK_STR "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"
#define CLK_TO_STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define PTP_CLOCK_IDENTITY_LENGTH 8 /*!< Size of a clock identifier stored in the ClockIndentity class, described at IEEE 802.1AS-2011 Clause 8.5.2.4*/
#define GET_PTP_DATA                100

/* Note: below structures have to match the one in gptp daemon */
typedef struct {
    uint64_t precise_origin_timestamp;  //from sync_followup message
    uint64_t reference_local_timestamp;  //PTP timestamp when sync process is complete
    uint64_t reference_global_timestamp;  //PTP timestamp when sync process is complete
    uint64_t sync_ingress_timestamp;  //Sync message ingress PTP timestamp from EMAC HW
    uint64_t correction_field;  //from sync_followup message
    uint64_t pDelay; // last calculated path delay
    uint8_t clockIdentity[PTP_CLOCK_IDENTITY_LENGTH];  //from sync/sync_followup message
    uint16_t portNumber;  //from sync/sync_followp message
    uint16_t sequence_id;  // Sync messafe seq ID
} syncMesaurementData_t;

/* Note: below structures have to match the one in gptp daemon */

typedef struct {

    uint64_t request_origin_timestamp; //from pdelay_req messages ( T1, TX EMAC TimeStamp)
    uint64_t request_receipt_timestamp; //pdelay_req ingress timestamp (T2, in PDELY RESP)
    uint64_t response_origin_timestamp; //from pdelay_resonse timestamp (T3 in PDELAY RESP FOLL)
    uint64_t response_receipt_timestamp; //pdelay_response ingress timestamp (T4,PDELAY RESP RX EMAC TS)
    uint64_t reference_local_timestamp; //PTP timestamp when path delay calculation is done
    uint64_t reference_global_timestamp; //PTP timestamp when path delay calculation is done
    uint64_t pDelay; //last calculated path delay
    uint8_t req_clockIdentity[PTP_CLOCK_IDENTITY_LENGTH]; //from pdelay_req message
    uint8_t resp_clockIdentity[PTP_CLOCK_IDENTITY_LENGTH]; //from pdelay_response message
    uint16_t req_portNumber; //from pdelay_req message
    uint16_t resp_portNumber; //from pdelay_response message
    uint16_t sequence_id; //seq ID of pdelay_req and pdelay_response messages pair
} pDelayMeasurementData_t;

/* Note: below structures have to match the one in gptp daemon */

typedef enum {
    GPTP_STATUS_TIMEOUT = 0,
    GPTP_STATUS_SYNCHRONIZED = 0x1,
    GPTP_STATUS_SYNC_TOGATEWAY = 0x2,
    GPTP_STATUS_LEAP_FUTURE = 0x4,
    GPTP_STATUS_LEAP_PAST = 0x8,
} TimeBaseStatus_t;

/* Note: below structures have to match the one in gptp daemon */

typedef struct {
    uint64_t gptp_status;
    double rate_deviation;
    bool IsMaster;
    int64_t offset;
    uint16_t gmTimeBaseIndicator;
    uint32_t d_status;
} gptpStatsType_t;

typedef struct {
    int8_t sync_interval;
    int8_t pdelay_interval;
} syncInterval_t;

typedef struct {
    int8_t reverseSyncEnabled = 0;
    int8_t reverseSyncDomain = 0;
    double reverseSyncRate = 0;
} RsyncStatus_t;

struct gptp_update {
    uint64_t curr_gptp_time;
    int64_t clock_adjust;
};

typedef struct __attribute__ ((packed))
{
    bool status;
    int32_t port_state; // Current port state (e.g., 7 = MASTER, 9 = SLAVE)
    uint32_t tv_sec;
    uint32_t tv_nsec;
}
gptpTimeInfo_t;

/* Get PTP time in nanoseconds for system time in nanoseconds */
bool gptpGetPtpTimefromSystime(uint64_t *gptp_time_ns, uint64_t time_sys_ns);
bool gptpGetPtpTimefromSystime_s(uint64_t *gptp_time_ns, uint64_t time_sys_ns,
                                 bool* inSync);

/* Get PTP time in nanoseconds for system time in nanoseconds */
bool gptpGetTime(uint64_t *gptp_time_ns,
                 uint64_t time_sys_ns); //just an alias for backward compatibility

/* Get PTP time in nanoseconds for Qtimer time in nanoseconds */
bool gptpGetPtpTimeFromQTimeNs(uint64_t *gptp_time_ns, uint64_t time_qtimer_ns);
bool gptpGetPtpTimeFromQTimeNs_s(uint64_t *gptp_time_ns,
                                 uint64_t time_qtimer_ns, bool* inSync);


/* Get PTP time in nanoseconds for Qtimer time in ticks */
bool gptpGetPtpTimeFromQTimeTickCount(uint64_t *gptp_time_ns,
                                      uint64_t qtime_ticks);
bool gptpGetPtpTimeFromQTimeTickCount_s(uint64_t *gptp_time_ns,
                                        uint64_t qtime_ticks, bool* inSync);


/* Get PTP time in nanoseconds for Monotonic in nanoseconds */
bool gptpGetPtpTimeFromMonoTime(uint64_t *gptp_time_ns, uint64_t time_mono_ns);
bool gptpGetPtpTimeFromMonoTime_s(uint64_t *gptp_time_ns, uint64_t time_mono_ns,
                                  bool* inSync);


/* Get current PTP time and monolithic time in nanoseconds */
bool gptpGetCurgPtpMonotonicPair(uint64_t *gptp_time_cur,
                                 uint64_t *mono_time_cur);
bool gptpGetCurgPtpMonotonicPair_s(uint64_t *gptp_time_cur,
                                   uint64_t *mono_time_cur, bool* inSync);


/* Get PTP time in nanoseconds from Boot time */
bool gptpGetPtpTimeFromBootTime(uint64_t *gptp_time_bt, uint64_t time_boot_ns);
bool gptpGetPtpTimeFromBootTime_s(uint64_t *gptp_time_bt, uint64_t time_boot_ns,
                                  bool* inSync);


/* Get Boot time in nanoseconds from PTP time */
bool gptpGetBootTimeFromPtpTime(uint64_t *boot_time_ns, uint64_t ptp_time_ns);
bool gptpGetBootTimeFromPtpTime_s(uint64_t *boot_time_ns, uint64_t ptp_time_ns,
                                  bool* inSync);



/* Get current PTP time in nanoseconds */
bool gptpGetCurPtpTime(uint64_t *gptp_time_ns);
bool gptpGetCurPtpTime_s(uint64_t *gptp_time_ns, bool* inSync);



/* Get gptp port state */
int gptpGetPortState(void);

/* Get gptp port sync status */
bool gptpGetSyncStatus(void);

/* Get gptp sync measuremnt Data */
bool gptpGetSyncMeasurementData(syncMesaurementData_t *syncData);

/* Get gptp pdelay measuremnt Data */
bool gptpGetPDelayMeasurementData(pDelayMeasurementData_t *delayData);

/* Get gptp status */
bool getgPTPStatus(gptpStatsType_t *status);

/* Get current gptp status, port status and gptp time */
bool gptpGetStatusAndCurPtpTime(gptpTimeInfo_t *ptp_data);

typedef void(*GPTP_UPDATE_NOTIFY_CALLBACK)(struct gptp_update update);

bool gptpRegisterCallback(GPTP_UPDATE_NOTIFY_CALLBACK fn_ptr);

/* Set Rsync enable or disable*/
int setRsyncStatus(RsyncStatus_t *status);

/* get Rsync slave clock offset*/
int getTimeError(int64_t *timeError);

bool gptpInit(void);

bool gptpDeinit(void);

#ifdef  RGPTP_CLNT_ENABLED
/* Get current rptp time in nanoseconds */
bool rgptpGetCurPtpTime(uint64_t *rgptp_time_ns);

bool rgptpInit(void);

bool rgptpDeinit(void);
#endif

typedef struct {

    GPTP_UPDATE_NOTIFY_CALLBACK gPTP_Update_Event;

} gPTPLibInterfaceEvent;

typedef struct {

    bool (*gptpGetTimeIf)(uint64_t *gptp_time_ns, uint64_t time_sys_ns);
    bool (*gptpGetPtpTimeFromQTimeNsIf)(uint64_t *gptp_time_ns,
                                        uint64_t time_qtimer_ns);
    bool (*gptpGetPtpTimeFromQTimeTickCountIf) (uint64_t *gptp_time_ns,
            uint64_t qtime_ticks);
    bool (*gptpGetPtpTimeFromMonoTimeIf)(uint64_t *gptp_time_ns,
                                         uint64_t time_mono_ns);
    bool (*gptpGetCurPtpTimeIf)(uint64_t *gptp_time_ns);
    bool (*gptpGetCurgPtpMonotonicPairIf)(uint64_t *gptp_time_cur,
                                          uint64_t *mono_time_cur);
    bool (*gptpGetBootTimeFromPtpTimeIf)(uint64_t *boot_time_ns,
                                         uint64_t ptp_time_ns);
    bool (*gptpGetPTPTimeFromBootTimeIf)(uint64_t *ptp_time_ns,
                                         uint64_t boot_time_ns);
    bool (*getgPTPStatusIf)(gptpStatsType_t *status);
    bool (*gptpInitIf)(void);
    bool (*gptpDeinitIf)(void);
    bool (*gptpRegisterEvent)(void);
    bool (*gptpUnregisterEvent)(void);
    bool (*gptpGetSyncStatusIf)(void);
    bool (*gptpGetPortStateIf)(void);

} gPTPLibInterfaceReq;


typedef const gPTPLibInterfaceReq* (*get_gPTPLib_if_t)(const
        gPTPLibInterfaceEvent* eventCallback);
const gPTPLibInterfaceReq* get_gPTPLib_if(const gPTPLibInterfaceEvent*
        eventCallback);

#ifdef __cplusplus
}
#endif

#endif      /* __GPTP_HELPER_H__ */

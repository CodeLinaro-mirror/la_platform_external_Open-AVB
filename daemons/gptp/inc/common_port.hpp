/******************************************************************************

  Copyright (c) 2009-2012, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

/******************************************************************************

Changes from Qualcomm Technologies, Inc. are provided under the following license:
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

#ifndef COMMON_PORT_HPP
#define COMMON_PORT_HPP

#include <avbts_message.hpp>
#include <avbts_osthread.hpp>
#include <avbts_oscondition.hpp>
#include <avbts_ostimer.hpp>
#include <avbts_oslock.hpp>
#include <avbts_osnet.hpp>
#include <unordered_map>
#include <pthread.h>

#include <math.h>
#define IFNAME_SIZE 16
extern uint64_t get_ntn_time(char *ifname);

class IEEE1588Clock;

/**
 * @brief To know which process path this update function is called to modify offset
 */
typedef enum {
    SYNC_INTERVAL_TIMEOUT_PATH = 0,
    PROCESS_MESSAGE_PATH
} ProcessPath_t;


/**
 * @brief PortIdentity interface
 * Defined at IEEE 802.1AS Clause 8.5.2
 */
class PortIdentity
{
    private:
        ClockIdentity clock_id;
        uint16_t portNumber;
    public:
        /**
         * @brief Default Constructor
         */
        PortIdentity() { };

        /**
         * @brief  Constructs PortIdentity interface.
         * @param  clock_id Clock ID value as defined at IEEE 802.1AS Clause
         * 8.5.2.2
         * @param  portNumber Port Number
         */
        PortIdentity(uint8_t * clock_id, uint16_t * portNumber)
        {
            this->portNumber = *portNumber;
            this->portNumber = PLAT_ntohs(this->portNumber);
            this->clock_id.set(clock_id);
        }

        /**
         * @brief  Implements the operator '!=' overloading method. Compares
         * clock_id and portNumber.
         * @param  cmp Constant PortIdentity value to be compared against.
         * @return TRUE if the comparison value differs from the object's
         * PortIdentity value. FALSE otherwise.
         */
        bool operator!=(const PortIdentity & cmp) const
        {
            return
                !(this->clock_id == cmp.clock_id) ||
                this->portNumber != cmp.portNumber ? true : false;
        }

        /**
         * @brief  Implements the operator '==' overloading method. Compares
         * clock_id and portNumber.
         * @param  cmp Constant PortIdentity value to be compared against.
         * @return TRUE if the comparison value equals to the object's
         * PortIdentity value. FALSE otherwise.
         */
        bool operator==(const PortIdentity & cmp)const
        {
            return
                this->clock_id == cmp.clock_id &&
                this->portNumber == cmp.portNumber ? true : false;
        }

        /**
         * @brief  Implements the operator '<' overloading method. Compares
         * clock_id and portNumber.
         * @param  cmp Constant PortIdentity value to be compared against.
         * @return TRUE if the comparison value is lower than the object's
         * PortIdentity value. FALSE otherwise.
         */
        bool operator<(const PortIdentity & cmp)const
        {
            return
                this->clock_id < cmp.clock_id ?
                true : this->clock_id == cmp.clock_id &&
                this->portNumber < cmp.portNumber ? true : false;
        }

        /**
         * @brief  Implements the operator '>' overloading method. Compares
         * clock_id and portNumber.
         * @param  cmp Constant PortIdentity value to be compared against.
         * @return TRUE if the comparison value is greater than the object's
         * PortIdentity value. FALSE otherwise.
         */
        bool operator>(const PortIdentity & cmp)const
        {
            return
                this->clock_id > cmp.clock_id ?
                true : this->clock_id == cmp.clock_id &&
                this->portNumber > cmp.portNumber ? true : false;
        }

        /**
         * @brief  Gets the ClockIdentity string
         * @param  id [out] Pointer to an array of octets.
         * @return void
         */
        void getClockIdentityString(uint8_t *id)
        {
            clock_id.getIdentityString(id);
        }

        /**
         * @brief  Sets the ClockIdentity.
         * @param  clock_id Clock Identity to be set.
         * @return void
         */
        void setClockIdentity(ClockIdentity clock_id)
        {
            this->clock_id = clock_id;
        }

        /**
         * @brief  Gets the clockIdentity value
         * @return A copy of Clock identity value.
         */
        ClockIdentity getClockIdentity( void )
        {
            return this->clock_id;
        }

        /**
         * @brief  Gets the port number following the network byte order, i.e.
         * Big-Endian.
         * @param  id [out] Port number
         * @return void
         */
        void getPortNumberNO(uint16_t * id) // Network byte order
        {
            uint16_t portNumberNO = PLAT_htons(portNumber);
            *id = portNumberNO;
        }

        /**
         * @brief  Gets the port number in the host byte order, which can be
         * either Big-Endian
         * or Little-Endian, depending on the processor where it is running.
         * @param  id Port number
         * @return void
         */
        void getPortNumber(uint16_t * id)   // Host byte order
        {
            *id = portNumber;
        }

        /**
         * @brief  Sets the Port number
         * @param  id [in] Port number
         * @return void
         */
        void setPortNumber(uint16_t * id)
        {
            portNumber = *id;
        }
};

class phy_delay_spec_t;
typedef std::unordered_map<uint32_t, phy_delay_spec_t> phy_delay_map_t;

/**
 * @brief PortType enumeration. Autosar Specification of
 * Synchronized Time-Base Manager AUTOSAR Release 4.2.2
 * Time synchronization status DID
 */
enum PortAutoTimeSyncDID {
    /* Bit 0-3 */
    AUTO_SYNC_TIMEOUT = (1 << 0),
    AUTO_TIMELEAP = (1 << 1),
    AUTO_SYNC_TO_GATEWAY = (1 << 2),
    AUTO_GLOBAL_TIME_BASE = (1 << 3),
    /* Bit 4-7 are reserved */
    AUTO_RESERVED_B4 = (1 << 4),
    AUTO_RESERVED_B5 = (1 << 5),
    AUTO_RESERVED_B6 = (1 << 6),
    AUTO_RESERVED_B7 = (1 << 7)
};

typedef struct pm_gptp_ctx {
    /*PM Thread*/
    int pm_gptp_coid;
    int pm_gptp_chid;
    /*sys pm rm handle*/
    int  pm_gptp_client_fd;
    /*Sys Pow Manager Thread Id*/
    pthread_t pm_gptp_sys_tid;
} pm_gptp_ctx_t;

/**
 * @brief Structure for Autosar Specification of
    Synchronized Time-Base Manager AUTOSAR Release 4.2.2
 */
typedef struct {
    /* port sync drift event count */
    uint32_t timeSyncDriftCount;

    /* port sync status DID */
    uint8_t timeSyncStatusDID;

    /* DriftCount Valid */
    bool driftCountValid;
} PortAutoTimeSyncDiagData_t;


/**
 * @brief Structure for Port Counters
 */
typedef struct {
    int32_t ieee8021AsPortStatRxSyncCount = 0;
    int32_t ieee8021AsPortStatRxFollowUpCount = 0;
    int32_t ieee8021AsPortStatRxPdelayRequest = 0;
    int32_t ieee8021AsPortStatRxPdelayResponse = 0;
    int32_t ieee8021AsPortStatRxPdelayResponseFollowUp = 0;
    int32_t ieee8021AsPortStatRxAnnounce = 0;
    int32_t ieee8021AsPortStatRxPTPPacketDiscard = 0;
    int32_t ieee8021AsPortStatRxSyncReceiptTimeouts = 0;
    int32_t ieee8021AsPortStatAnnounceReceiptTimeouts = 0;
    int32_t ieee8021AsPortStatPdelayAllowedLostResponsesExceeded = 0;
    int32_t ieee8021AsPortStatTxSyncCount = 0;
    int32_t ieee8021AsPortStatTxFollowUpCount = 0;
    int32_t ieee8021AsPortStatTxPdelayRequest = 0;
    int32_t ieee8021AsPortStatTxPdelayResponse = 0;
    int32_t ieee8021AsPortStatTxPdelayResponseFollowUp = 0;
    int32_t ieee8021AsPortStatTxAnnounce = 0;
    uint64_t avnu_loss_of_sync_message = 0;
    uint64_t avnu_sync_discontinutity = 0;
    uint64_t avnu_pdelay_resp_timeout = 0;
    uint16_t avb_sync_test_sequenceId = 0;
    uint32_t avb_sync_test_linkup_count = 0;
    uint32_t avb_sync_test_linkdown_count = 0;
    uint32_t avb_sync_test_station_state = 0;
    char ifname[IFNAME_SIZE] = {0};
    int16_t offsetScaledLogVariance = 0;
    FrequencyRatio clockRatio = 0;
    EtherPortLinkState_t ethPortLinkState = (EtherPortLinkState_t) 0;
} PortCounters_t;


/* Note: if any modification below struct then update syncMesaurementData_t in gptp helper lib */
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

/* Note: if any modification below struct then update pDelayMeasurementData_t in gptp helper lib */
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

/* Note: if any modification below struct then update TimeBaseStatus_t in gptp helper lib */
typedef enum {
    GPTP_STATUS_TIMEOUT = 0,
    GPTP_STATUS_SYNCHRONIZED = 0x1,
    GPTP_STATUS_SYNC_TOGATEWAY = 0x2,
    GPTP_STATUS_LEAP_FUTURE = 0x4,
    GPTP_STATUS_LEAP_PAST = 0x8,
} TimeBaseStatus_t;

/* Note: if any modification below struct then update gptpStatsType_t in gptp helper lib */
typedef struct {
    uint64_t gptp_status;
    double rate_deviation;
    bool IsMaster;
    int64_t offset;
    uint16_t gmTimeBaseIndicator;
    uint32_t d_status;
    bool tsc_active;
} gptpStatsType_t;

typedef struct {
    int8_t sync_interval;
    int8_t pdelay_interval;
} syncInterval_t;

typedef struct {
    uint64_t captured_ptp_time;
    int64_t system_offset;
    int64_t q_offset;
    int64_t boot_offset;
    FrequencyRatio system_freq_offset;
    FrequencyRatio q_freq_offset;
    FrequencyRatio boot_freq_offset;
} ptp_ratios_t;

typedef struct {
    pthread_mutex_t lock;
    syncMesaurementData_t syncData;
    pDelayMeasurementData_t delayData;
    gptpStatsType_t status;
    syncInterval_t syncInterval;
    ptp_ratios_t ptpRatios;
} sct_gptp_data;

typedef struct {
    int8_t reverseSyncEnabled = 0;
    int8_t reverseSyncDomain = 0;
    double reverseSyncRate = 0;
} RsyncStatus_t;

/**
 * @brief Structure for initializing the port class
 */
typedef struct {
    /* clock IEEE1588Clock instance */
    IEEE1588Clock * clock;

    /* index Interface index */
    uint16_t index;

    /* timestamper Hardware timestamper instance */
    CommonTimestamper * timestamper;

    /* net_label Network label */
    InterfaceLabel *net_label;

    /* automotive_profile set the AVnu automotive profile */
    bool automotive_profile;

    /* Set to true if the port is the grandmaster. Used for fixed GM in
     * the the AVnu automotive profile */
    bool isGM;

    /* Set to true if the port is the grandmaster. Used for fixed GM in
     * the the AVnu automotive profile */
    bool testMode;

    /* Set to true if the port's network interface is up. Used to filter
     * false LINKUP/LINKDOWN events */
    bool linkUp;
    /*Set to true if the port  AsCapable is true*/
    bool asCapable;

    /* interfacename */
    char ifname[IFNAME_SIZE];
    /* gPTP 10.2.4.4 */
    int8_t initialLogSyncInterval;

    /* gPTP 11.5.2.2 */
    int8_t initialLogPdelayReqInterval;

    /* CDS 6.2.1.5 */
    int8_t operLogPdelayReqInterval;

    /* CDS 6.2.1.6 */
    int8_t operLogSyncInterval;

    /* Incoming Sync Arrival Time Difference Tolerance */
    double syncArrivalTimeDiffTolerance;

    /*802.1AS Recovered Clock Quality Testing*/
    int8_t reverseSyncEnabled;

    int8_t reverseSyncDomain;

    double reverseSyncRate;

    /* CDS 6.2.2.3 */
    FrequencyRatio _peer_rate_offset;

    uint8_t syncReceiptTimeout;

    uint8_t announceReceiptTimeout;

    uint8_t allowedLostResponses;

    uint8_t lostResponses;

    uint8_t syncClocks;

    bool isSigNoSend;

	bool disableSigMsg;

    bool tsc_enable;

    /* condition_factory OSConditionFactory instance */
    OSConditionFactory * condition_factory;

    /* thread_factory OSThreadFactory instance */
    OSThreadFactory * thread_factory;

    /* timer_factory OSTimerFactory instance */
    OSTimerFactory * timer_factory;

    /* lock_factory OSLockFactory instance */
    OSLockFactory * lock_factory;

    /* phy delay */
    phy_delay_map_t const *phy_delay;

    /* sync receipt threshold */
    unsigned int syncReceiptThreshold;

    /* neighbor delay threshold */
    int64_t neighborPropDelayThreshold;

    /* StbMSyncLossThreshold */
    int64_t stbMSyncLossThreshold;

    /* rgptp periodic sync time in ms */
    unsigned int rgptpSyncTime;

    /* safe car shared memory fd */
    int sct_shm_fd;

    /* safe car shared memory */
    sct_gptp_data *sct_buffer;

    /* To wait for gptp sync to complete */
    bool wait_for_sync;

    /* enable to not wait for interface link up */
    bool bypass_if_wait;
} PortInit_t;



/**
 * @brief Port functionality common to all network media
 */
class CommonPort
{
    private:
        LinkLayerAddress local_addr;
        PortIdentity port_identity;

        /* Network socket description
           physical interface number that object represents */
        uint16_t ifindex;

        /* Link speed in kb/sec */
        uint32_t link_speed;

        /* Signed value allows this to be negative result because of inaccurate
           timestamp */
        int64_t one_way_delay;
        int64_t neighbor_prop_delay_thresh;
        int64_t stbMSyncLossThreshold;

        PortState port_state;
        bool testMode;

        int8_t log_mean_sync_interval;
        int8_t deferred_log_mean_sync_interval;
        double syncArrivalTimeDiffTolerance;
        int8_t log_mean_announce_interval;
        int8_t initialLogSyncInterval;
        uint8_t announceReceiptTimeout;
        uint8_t syncReceiptTimeout;
        uint8_t syncClocks;
        /* CDS 6.2.1.5 */
        int8_t *operLogPdelayReqInterval;

        /*Sync threshold*/
        unsigned int sync_receipt_thresh;
        unsigned int wrongSeqIDCounter;

        PortCounters_t counters;

        OSThread *listening_thread;
        OSThread *link_thread;

        FrequencyRatio _peer_rate_offset;


        Timestamp _peer_offset_ts_theirs;
        Timestamp _peer_offset_ts_mine;
        bool _peer_offset_init;
        bool asCapable;
        bool tsc_enable;
        unsigned sync_count;  /* 0 for master, increment for each sync
                   * received as slave */
        unsigned pdelay_count;

        PTPMessageAnnounce *qualified_announce;

        uint16_t announce_sequence_id;
        uint16_t signal_sequence_id;
        uint16_t sync_sequence_id;

        uint16_t lastGmTimeBaseIndicator;

        OSLock *syncReceiptTimerLock;
        OSLock *syncIntervalTimerLock;
        OSLock *deferredSyncIntervalTimerLock;
        OSLock *announceIntervalTimerLock;

        bool bypass_if_wait;

    protected:
        static const int64_t INVALID_LINKDELAY = 3600000000000;
        static const int64_t ONE_WAY_DELAY_DEFAULT = INVALID_LINKDELAY;

        OSThreadFactory const * const thread_factory;
        OSTimerFactory const * const timer_factory;
        OSLockFactory const * const lock_factory;
        OSConditionFactory const * const condition_factory;
        CommonTimestamper * const _hw_timestamper;
        IEEE1588Clock * const clock;
        const bool isGM;

        phy_delay_map_t const * const phy_delay;
        OSNetworkInterface *net_iface;
        InterfaceLabel *net_label;

    public:

        /* AUTOSAR related stats */
        PortAutoTimeSyncDiagData_t timesync_diagstats;

        static const int64_t STBM_SYNCLOSS_THRESH =
            6000000; /* StbMSyncLossThreshold in nanosec */
        static const int64_t NEIGHBOR_PROP_DELAY_THRESH = 800;
        static const unsigned int DEFAULT_SYNC_RECEIPT_THRESH = 5;
        bool gPTP_lpm = false;
        int sct_shm_fd;
        sct_gptp_data *sct_buffer;

        /*SafeGptp Parameters*/
        syncMesaurementData_t syncInfo;

        pDelayMeasurementData_t pdelayInfo;
        CommonPort( PortInit_t *portInit );
        virtual ~CommonPort();

        /**
         * @brief Global media dependent port initialization
         * @return true on success
         */
        bool init_port( void );

        /**
         * @brief Media specific port initialization
         * @return true on success
         */
        virtual bool _init_port( void ) = 0;

        /**
         * @brief Initializes the hwtimestamper
         */
        void timestamper_init( void );
        void timestamper_deinit( void );

        /**
         * @brief Resets the hwtimestamper
         */
        void timestamper_reset( void );

        /**
         * @brief  Gets the link delay information.
         * @return one way delay if delay > 0, or zero otherwise.
         */
        uint64_t getLinkDelay( void )
        {
            return one_way_delay > 0LL ? one_way_delay : 0LL;
        }



        /**
         * @brief  Gets the link delay information.
         * @param  [in] delay Pointer to the delay information
         * @return True if valid, false if invalid
         */
        bool getLinkDelay( uint64_t *delay )
        {
            if (delay == NULL) {
                return false;
            }

            *delay = getLinkDelay();
            return *delay < INVALID_LINKDELAY;
        }

        /**
         * @brief  Sets link delay information.
         * Signed value allows this to be negative result because
         * of inaccurate timestamps.
         * @param  delay Link delay
         * @return True if one_way_delay is lower or equal than neighbor
         * propagation delay threshold False otherwise
         */
        bool setLinkDelay( int64_t delay )
        {
            one_way_delay = delay;
            int64_t abs_delay = (one_way_delay < 0 ?
                                 -one_way_delay : one_way_delay);

            if (testMode) {
                GPTP_LOG_STATUS("Link delay: %ld", delay);
            }

            return (abs_delay <= neighbor_prop_delay_thresh);
        }

        /**
         * @brief  Gets a pointer to IEEE1588Clock
         * @return Pointer to clock
         */
        IEEE1588Clock *getClock( void )
        {
            return clock;
        }

        /**
         * @brief  Gets the local_addr
         * @return LinkLayerAddress
         */
        LinkLayerAddress *getLocalAddr( void )
        {
            return &local_addr;
        }

        /**
         * @brief  Gets the testMode
         * @return bool of the test mode value
         */
        bool getTestMode( void )
        {
            return testMode;
        }
                /**
         * @brief  Gets the testMode
         * @return bool of the test mode value
         */
        bool getTSCState( void )
        {
            return tsc_enable;
        }

        /**
         * @brief  Sets the testMode
         * @param testMode changes testMode to this value
         */
        void setTestMode( bool testMode )
        {
            this->testMode = testMode;
        }

        /**
         * @brief  Serializes (i.e. copy over buf pointer) the information from
         * the variables (in that order):
         *  - asCapable;
         *  - Port Sate;
         *  - Link Delay;
         *  - Neighbor Rate Ratio
         * @param  buf [out] Buffer where to put the results.
         * @param  count [inout] Length of buffer. It contains maximum length
         * to be written
         * when the function is called, and the value is decremented by the
         * same amount the
         * buf size increases.
         * @return TRUE if it has successfully written to buf all the values
         * or if buf is NULL.
         * FALSE if count should be updated with the right size.
         */
        bool serializeState( void *buf, off_t *count );

        /**
         * @brief  Restores the serialized state from the buffer. Copies the
         * information from buffer
         * to the variables (in that order):
         *  - asCapable;
         *  - Port State;
         *  - Link Delay;
         *  - Neighbor Rate Ratio
         * @param  buf Buffer containing the serialized state.
         * @param  count Buffer lenght. It is decremented by the same size of
         * the variables that are
         * being copied.
         * @return TRUE if everything was copied successfully, FALSE otherwise.
         */
        bool restoreSerializedState( void *buf, off_t *count );

        /**
         * @brief  Sets the internal variabl sync_receipt_thresh, which is the
         * flag that monitors the amount of wrong syncs enabled before
         * switching
         * the ptp to master.
         * @param  th Threshold to be set
         * @return void
         */
        void setSyncReceiptThresh(unsigned int th)
        {
            sync_receipt_thresh = th;
        }

        /**
         * @brief  Gets the internal variabl sync_receipt_thresh, which is the
         * flag that monitors the amount of wrong syncs enabled before
         * switching
         * the ptp to master.
         * @return sync_receipt_thresh value
         */
        unsigned int getSyncReceiptThresh( void )
        {
            return sync_receipt_thresh;
        }

        /**
         * @brief  Sets the wrongSeqIDCounter variable
         * @param  cnt Value to be set
         * @return void
         */
        void setWrongSeqIDCounter(unsigned int cnt)
        {
            wrongSeqIDCounter = cnt;
        }

        /**
         * @brief  Gets the wrongSeqIDCounter value
         * @param  [out] cnt Pointer to the counter value. It must be valid
         * @return TRUE if ok and lower than the syncReceiptThreshold value.
         * FALSE otherwise
         */
        bool getWrongSeqIDCounter(unsigned int *cnt)
        {
            if ( cnt == NULL ) {
                return false;
            }

            *cnt = wrongSeqIDCounter;
            return ( *cnt < getSyncReceiptThresh() );
        }

        /**
         * @brief  Increments the wrongSeqIDCounter value
         * @param  [out] cnt Pointer to the counter value. Must be valid
         * @return TRUE if incremented value is lower than the
         * syncReceiptThreshold. FALSE otherwise.
         */
        bool incWrongSeqIDCounter(unsigned int *cnt)
        {
            if ( getAsCapable() ) {
                wrongSeqIDCounter++;
            }

            bool ret = wrongSeqIDCounter < getSyncReceiptThresh();

            if ( cnt != NULL) {
                *cnt = wrongSeqIDCounter;
            }

            return ret;
        }

        /**
         * @brief  Gets the hardware timestamper version
         * @return HW timestamper version
         */
        int getTimestampVersion();

        /**
         * @brief  Adjusts the clock frequency.
         * @param  freq_offset Frequency offset
         * @return TRUE if adjusted. FALSE otherwise.
         */
        bool _adjustClockRate( FrequencyRatio freq_offset );

        bool _adjustClockRate( const char *if_name, FrequencyRatio freq_offset );

        /**
         * @brief  Adjusts the clock frequency.
         * @param  freq_offset Frequency offset
         * @return TRUE if adjusted. FALSE otherwise.
         */
        bool adjustClockRate( FrequencyRatio freq_offset )
        {
            return _adjustClockRate( freq_offset );
        }

        bool adjustClockRate( const char *if_name, FrequencyRatio freq_offset )
        {
            return _adjustClockRate( if_name, freq_offset );
        }


        /**
         * @brief  Adjusts the clock phase.
         * @param  phase_adjust phase offset in ns
         * @return TRUE if adjusted. FALSE otherwise.
         */
        bool adjustClockPhase( int64_t phase_adjust );

        bool adjustClockPhase(const char *if_name, int64_t phase_adjust );

        /**
         * @brief  Gets extended error message from hardware timestamper
         * @param  msg [out] Extended error message
         * @return void
         */
        void getExtendedError(char *msg);

        /**
         * @brief  get gptp stats:
         * @param  PortStats[out] copied stats information
         * @return void
         */
        void getPortStats( PortCounters_t *PortStats )
        {
            if (PortStats != NULL) {
                memcpy(PortStats, &counters, sizeof(PortCounters_t));
            }
        }

        /**
         * @brief Sets the value of last valid SeqID
         * @param seqid Value to set
         * @return void
         */
        void setLastvalidSeqID(uint16_t seqid)
        {
            counters.avb_sync_test_sequenceId = seqid;
        }

        /**
         * @brief increments the linkup count
         *
         * @return void
         */
        void increment_LinkupCount()
        {
            counters.avb_sync_test_linkup_count++;
        }

        /**
         * @brief increments the linkdown count
         *
         * @return void
         */
        void increment_LinkdownCount()
        {
            counters.avb_sync_test_linkdown_count++;
        }


        /**
         * @brief Sets the value of last valid SeqID
         * @param seqid Value to set
         * @return void
         */
        void setPortStationState(uint32_t state)
        {
            counters.avb_sync_test_station_state = state;
        }

        /**
         * @brief Sets the Ether Port Link State
         * @param [in] state - Port state to set
         * @return void
         */
        void setEtherPortLinkState(EtherPortLinkState_t state)
        {
            counters.ethPortLinkState = state;
        }

        /**
         * @brief  getcumulativeRateRatio: returns cumulative rate ratio
         *
         * @return int cumulativeRateRatio
         */

        int getcumulativeRateRatio()
        {
            return (int)((_peer_rate_offset - 1.0) * pow(2, 41));
        }


        /**
         * @brief  getClockRateRatio: returns clock rate ratio
         *
         * @return FrequencyRatio clockRatio
         */

        FrequencyRatio getClockRateRatio()
        {
            return counters.clockRatio;
        }


        /**
         * @brief  setClockRateRatio: sets clock rate ratio into stats
         * @param [in]      master_local_freq_offset
         * @return void
         */
        void setClockRateRatio(FrequencyRatio master_local_freq_offset)
        {
            counters.clockRatio = master_local_freq_offset;
        }
        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxSyncCount
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxSyncCount( void )
        {
            counters.ieee8021AsPortStatRxSyncCount++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxFollowUpCount
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxFollowUpCount( void )
        {
            counters.ieee8021AsPortStatRxFollowUpCount++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxPdelayRequest
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxPdelayRequest( void )
        {
            counters.ieee8021AsPortStatRxPdelayRequest++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxPdelayResponse
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxPdelayResponse( void )
        {
            counters.ieee8021AsPortStatRxPdelayResponse++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxPdelayResponseFollowUp
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxPdelayResponseFollowUp( void )
        {
            counters.ieee8021AsPortStatRxPdelayResponseFollowUp++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxAnnounce
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxAnnounce( void )
        {
            counters.ieee8021AsPortStatRxAnnounce++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxPTPPacketDiscard
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxPTPPacketDiscard( void )
        {
            counters.ieee8021AsPortStatRxPTPPacketDiscard++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatRxSyncReceiptTimeouts
         * @return void
         */
        void incCounter_ieee8021AsPortStatRxSyncReceiptTimeouts( void )
        {
            counters.ieee8021AsPortStatRxSyncReceiptTimeouts++;
        }

        /**
         * @brief  timelog_avnu_loss_of_sync_message: record gptp time of sync loss
         *
         * @return void
         */
        void timelog_avnu_loss_of_sync_message()
        {
            counters.avnu_loss_of_sync_message = get_ntn_time(counters.ifname);
            getCurrentPtpTime(&counters.avnu_loss_of_sync_message);
        }

        /**
         * @brief  get_avnu_loss_of_sync_message: return gptp time of sync loss
         *
         * @return uint64_t gptp timestamp of the event
         */
        uint64_t get_avnu_loss_of_sync_message()
        {
            return counters.avnu_loss_of_sync_message;
        }


        /**
         * @brief  timelog_avnu_sync_discontinutity:record gptp time of sync discontinutiy
         *
         * @return void
         */
        void timelog_avnu_sync_discontinutity()
        {
            counters.avnu_sync_discontinutity = get_ntn_time(counters.ifname);
            getCurrentPtpTime(&counters.avnu_sync_discontinutity);
        }

        /**
         * @brief  get_avnu_sync_discontinutity:return gptp time of sync discontinutiy
         *
         * @return uint64_t gptp timestamp of the event
         */

        uint64_t get_avnu_sync_discontinutity()
        {
            return counters.avnu_sync_discontinutity;
        }

        /**
         * @brief  timelog_avnu_pdelay_resp_timeout:record gptp time of pdelay_resp_timeout
         *
         * @return void
         */
        void timelog_avnu_pdelay_resp_timeout()
        {
            counters.avnu_pdelay_resp_timeout = get_ntn_time(counters.ifname);
            getCurrentPtpTime(&counters.avnu_pdelay_resp_timeout);
        }

        /**
         * @brief  get_avnu_pdelay_resp_timeout:return gptp time of pdelay_resp_timeout
         *
         * @return uint64_t gptp timestamp of the event
         */

        uint64_t get_avnu_pdelay_resp_timeout()
        {
            return counters.avnu_pdelay_resp_timeout;
        }



        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatAnnounceReceiptTimeouts
         * @return void
         */
        void incCounter_ieee8021AsPortStatAnnounceReceiptTimeouts( void )
        {
            counters.ieee8021AsPortStatAnnounceReceiptTimeouts++;
        }


        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatPdelayAllowedLostResponsesExceeded
         * @return void
         */
        // TODO: Not called
        void incCounter_ieee8021AsPortStatPdelayAllowedLostResponsesExceeded
        ( void )
        {
            counters.
            ieee8021AsPortStatPdelayAllowedLostResponsesExceeded++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxSyncCount
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxSyncCount( void )
        {
            counters.ieee8021AsPortStatTxSyncCount++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxFollowUpCount
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxFollowUpCount( void )
        {
            counters.ieee8021AsPortStatTxFollowUpCount++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxPdelayRequest
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxPdelayRequest( void )
        {
            counters.ieee8021AsPortStatTxPdelayRequest++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxPdelayResponse
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxPdelayResponse( void )
        {
            counters.ieee8021AsPortStatTxPdelayResponse++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxPdelayResponseFollowUp
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxPdelayResponseFollowUp( void )
        {
            counters.ieee8021AsPortStatTxPdelayResponseFollowUp++;
        }

        /**
         * @brief  Increment IEEE Port counter:
         *         ieee8021AsPortStatTxAnnounce
         * @return void
         */
        void incCounter_ieee8021AsPortStatTxAnnounce( void )
        {
            counters.ieee8021AsPortStatTxAnnounce++;
        }

        /**
         * @brief  Logs port counters
         * @return void
         */
        void logIEEEPortCounters( void )
        {
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxSyncCount : %u",
              counters.ieee8021AsPortStatRxSyncCount );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxFollowUpCount : %u",
              counters.ieee8021AsPortStatRxFollowUpCount );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxPdelayRequest : %u",
              counters.ieee8021AsPortStatRxPdelayRequest );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxPdelayResponse : %u",
              counters.ieee8021AsPortStatRxPdelayResponse );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxPdelayResponseFollowUp "
              ": %u", counters.
              ieee8021AsPortStatRxPdelayResponseFollowUp );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxAnnounce : %u",
              counters.ieee8021AsPortStatRxAnnounce );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxPTPPacketDiscard : %u",
              counters.
              ieee8021AsPortStatRxPTPPacketDiscard );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatRxSyncReceiptTimeouts "
              ": %u", counters.
              ieee8021AsPortStatRxSyncReceiptTimeouts );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatAnnounceReceiptTimeouts "
              ": %u", counters.
              ieee8021AsPortStatAnnounceReceiptTimeouts );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatPdelayAllowed"
              "LostResponsesExceeded : %u", counters.
              ieee8021AsPortStatPdelayAllowedLostResponsesExceeded
            );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxSyncCount : %u",
              counters.ieee8021AsPortStatTxSyncCount );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxFollowUpCount : %u", counters.
              ieee8021AsPortStatTxFollowUpCount);
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxPdelayRequest : %u",
              counters.ieee8021AsPortStatTxPdelayRequest);
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxPdelayResponse : %u", counters.
              ieee8021AsPortStatTxPdelayResponse);
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxPdelayResponseFollowUp : %u",
              counters.ieee8021AsPortStatTxPdelayResponseFollowUp
            );
            GPTP_LOG_STATUS
            ( "IEEE Port Counter "
              "ieee8021AsPortStatTxAnnounce : %u",
              counters.ieee8021AsPortStatTxAnnounce);
        }

        /**
         * @brief  Get the cross timestamping information.
         * The gPTP subsystem uses these samples to calculate
         * ratios which can be used to translate or extrapolate
         * one clock into another clock reference. The gPTP service
         * uses these supplied cross timestamps to perform internal
         * rate estimation and conversion functions.
         * @param  system_time [out] System time
         * @param  device_time [out] Device time
         * @param  local_clock [out] Local clock
         * @param  nominal_clock_rate [out] Nominal clock rate
         * @return True in case of success. FALSE in case of error
         */
        void getDeviceTime
        ( Timestamp &system_time, Timestamp &mono_time, Timestamp &device_time,
          Timestamp &boot_time,
          uint32_t &local_clock, uint32_t & nominal_clock_rate );

        /**
         * @brief  Provide current gptp time
         * @param  ptp_time [out] Provide current gptp time
         * @return True in case of success. FALSE in case of error
         */
        void getCurrentPtpTime
        ( uint64_t* ptp_time );


        /**
         * @brief  Sets asCapable flag
         * @param  ascap flag to be set. If FALSE, marks peer_offset_init as
         * false.
         * @return void
         */
        void setAsCapable(bool ascap)
        {
            if ( ascap != asCapable ) {
                GPTP_LOG_STATUS
                ("AsCapable: %s", ascap == true
                 ? "Enabled" : "Disabled");
            }

            if ( !ascap ) {
                _peer_offset_init = false;
            }

            asCapable = ascap;
        }

        /**
         * @brief  Gets the asCapable flag
         * @return asCapable flag.
         */
        bool getAsCapable()
        {
            return ( asCapable );
        }

        /**
         * @brief  Gets the Peer rate offset. Used to calculate neighbor
         * rate ratio.
         * @return FrequencyRatio peer rate offset
         */
        FrequencyRatio getPeerRateOffset( void )
        {
            return _peer_rate_offset;
        }

        /**
         * @brief  Sets the peer rate offset. Used to calculate neighbor rate
         * ratio.
         * @param  offset Offset to be set
         * @return void
         */
        void setPeerRateOffset( FrequencyRatio offset )
        {
            _peer_rate_offset = offset;
        }

        /**
         * @brief  Sets peer offset timestamps
         * @param  mine Local timestamps
         * @param  theirs Remote timestamps
         * @return void
         */
        void setPeerOffset(Timestamp mine, Timestamp theirs)
        {
            _peer_offset_ts_mine = mine;
            _peer_offset_ts_theirs = theirs;
            _peer_offset_init = true;
        }

        /**
         * @brief  Gets peer offset timestamps
         * @param  mine [out] Reference to local timestamps
         * @param  theirs [out] Reference to remote timestamps
         * @return TRUE if peer offset has already been initialized. FALSE
         * otherwise.
         */
        bool getPeerOffset(Timestamp & mine, Timestamp & theirs)
        {
            mine = _peer_offset_ts_mine;
            theirs = _peer_offset_ts_theirs;
            return _peer_offset_init;
        }

        /**
         * @brief  Sets the neighbor propagation delay threshold
         * @param  delay Delay in nanoseconds
         * @return void
         */
        void setNeighPropDelayThresh(int64_t delay)
        {
            neighbor_prop_delay_thresh = delay;
        }

        /**
         * @brief  Restart PDelay
         * @return void
         */
        void restartPDelay()
        {
            _peer_offset_init = false;
        }

        /**
         * @brief  Gets a pointer to timer_factory object
         * @return timer_factory pointer
         */
        const OSTimerFactory *getTimerFactory()
        {
            return timer_factory;
        }

        /**
         * @brief Watch for link up and down events.
         * @return Its an infinite loop. Returns NULL in case of error.
         */
        void *watchNetLink( void )
        {
            // Should never return
            net_iface->watchNetLink(this);
            return NULL;
        }

        /**
         * @brief Receive frame
         */
        net_result recv
        ( LinkLayerAddress *addr, uint8_t *payload, size_t &length,
          uint32_t &link_speed )
        {
            net_result result = net_iface->nrecv( addr, payload, length );
            link_speed = this->link_speed;
            return result;
        }

        /**
         * @brief Send frame
         */
        net_result send
        ( LinkLayerAddress *addr, uint16_t etherType, uint8_t *payload,
          size_t length, bool timestamp )
        {
            return net_iface->send
                   ( addr, etherType, payload, length, timestamp );
        }

        /**
        * @brief Get the value of getstbMSyncLossThreshold.
        * @return getstbMSyncLossThreshold
        */
        int64_t getstbMSyncLossThreshold(void)
        {
            return stbMSyncLossThreshold;
        }

        /**
         * @brief Get the payload offset inside a packet
         * @return 0
         */
        unsigned getPayloadOffset()
        {
            return net_iface->getPayloadOffset();
        }

        bool linkWatch( OSThreadFunction func, OSThreadFunctionArg arg )
        {
            return link_thread->start( func, arg );
        }

        bool linkOpen( OSThreadFunction func, OSThreadFunctionArg arg )
        {
            return listening_thread->start( func, arg );
        }

        bool linkjoin( OSThreadExitCode arg )
        {
            return listening_thread->join( arg );
        }

        /**
         * @brief  Gets the portState information
         * @return PortState
         */
        PortState getPortState( void )
        {
            return port_state;
        }

        /**
         * @brief Sets the PortState
         * @param state value to be set
         * @return void
         */
        void setPortState( PortState state )
        {
            port_state = state;
        }

        /**
         * @brief  Gets port identity
         * @param  identity [out] Reference to PortIdentity
         * @return void
         */
        void getPortIdentity(PortIdentity & identity)
        {
            identity = this->port_identity;
        }

        /**
         * @brief  Gets the "best" announce
         * @return Pointer to PTPMessageAnnounce
         */
        PTPMessageAnnounce *calculateERBest( void );

        /**
         * @brief  Changes the port state
         * @param  state Current state
         * @param  changed_external_master TRUE if external master has
         * changed, FALSE otherwise
         * @return void
         */
        void recommendState(PortState state, bool changed_external_master);

        /**
         * @brief  Locks the TX port
         * @return TRUE if success. FALSE otherwise.
         */
        virtual bool getTxLock()
        {
            return true;
        }

        /**
         * @brief  Unlocks the port TX.
         * @return TRUE if success. FALSE otherwise.
         */
        virtual bool putTxLock()
        {
            return false;
        }

        /**
         * @brief  Adds a new qualified announce the port. IEEE 802.1AS
         * Clause 10.3.10.2
         * @param  annc PTP announce message
         * @return void
         */
        void setQualifiedAnnounce( PTPMessageAnnounce *annc )
        {
            delete qualified_announce;
            qualified_announce = annc;
        }

        /**
         * @brief  Switches port to a gPTP master
         * @param  annc If TRUE, starts announce event timer.
         * @return void
         */
        virtual void becomeMaster( bool annc ) = 0;

        /**
         * @brief  Switches port to a gPTP slave.
         * @param  restart_syntonization if TRUE, restarts the syntonization
         * @return void
         */
        virtual void becomeSlave( bool restart_syntonization ) = 0;

        /**
        * @brief  get reverse sync.
        * @param  void
        * @return int8_t
        */
        virtual int8_t getRsync(void) = 0;

        /**
        * @brief  enable reverse sync.
        * @param  void
        * @return void
        */
        virtual void enableRsync( int8_t RSyncDomain, double RSyncRate ) = 0;

        /**
         * @brief  disable reverse sync.
         * @param  void
         * @return void
         */
        virtual void disableRsync() = 0;

        /**
         * @brief �set reverse sync parameter.
         * @param �RsyncStatus_t reverse sync structure
         * @return TRUE if success. FALSE otherwise
         */
        bool setRsync(RsyncStatus_t *Rsync);

        /**
         * @brief  Sets current sync count value.
         * @param  cnt [in] sync count value
         * @return void
         */
        void setSyncCount(unsigned int cnt)
        {
            sync_count = cnt;
        }

        /**
         * @brief  Increments sync count
         * @return void
         */
        void incSyncCount()
        {
            ++sync_count;
        }

        /**
         * @brief  Gets current sync count value. It is set to zero
         * when master and incremented at each sync received for slave.
         * @return sync count
         */
        unsigned getSyncCount()
        {
            return sync_count;
        }

        /**
         * @brief  Gets which clocks has to be synced with ptp.
         * 0 is no clock
         * 1 is CLOCK_REALTIME
         * @return syncClocks
         */
        unsigned getSyncClocks()
        {
            return syncClocks;
        }


        /**
         * @brief  Sets current pdelay count value.
         * @param  cnt [in] pdelay count value
         * @return void
         */
        void setPdelayCount(unsigned int cnt)
        {
            pdelay_count = cnt;
        }

        /**
         * @brief  Increments Pdelay count
         * @return void
         */
        void incPdelayCount()
        {
            ++pdelay_count;
        }

        /**
         * @brief  Gets current pdelay count value. It is set to zero
         * when asCapable is false.
         * @return pdelay count
         */
        unsigned getPdelayCount()
        {
            return pdelay_count;
        }

        /**
         * @brief  Gets current operLogPdelayReqInterval.
         * @return operLogPdelayReqInterval;
         */
        int8_t getoperLogPdelayReqInterval()
        {
            return *operLogPdelayReqInterval;
        }

        /**
         * @brief  Increments announce sequence id and returns
         * @return Next announce sequence id.
         */
        uint16_t getNextAnnounceSequenceId( void )
        {
            return announce_sequence_id++;
        }

        /**
         * @brief  Increments signal sequence id and returns
         * @return Next signal sequence id.
         */
        uint16_t getNextSignalSequenceId( void )
        {
            return signal_sequence_id++;
        }

        /**
         * @brief  Increments sync sequence ID and returns
         * @return Next synce sequence id.
         */
        uint16_t getNextSyncSequenceId( void )
        {
            return sync_sequence_id++;
        }

        /**
         * @brief  Gets the lastGmTimeBaseIndicator
         * @return uint16 of the lastGmTimeBaseIndicator
         */
        uint16_t getLastGmTimeBaseIndicator( void )
        {
            return lastGmTimeBaseIndicator;
        }

        /**
         * @brief  Sets the lastGmTimeBaseIndicator
         * @param  gmTimeBaseIndicator from last Follow up message
         * @return void
         */
        void setLastGmTimeBaseIndicator(uint16_t gmTimeBaseIndicator)
        {
            lastGmTimeBaseIndicator = gmTimeBaseIndicator;
        }
        /**
         * @brief Get the status of syncReceiptTimeoutMultiplier.
         * @return syncReceiptTimeoutMultiplier
         */
        uint8_t getsyncReceiptTimeoutMultiplier(void)
        {
            return syncReceiptTimeout;
        }
        /**
         * @brief Get the status of anncReceiptTimeoutMultiplier.
         * @return anncReceiptTimeoutMultiplier
         */
        uint8_t getannounceReceiptTimeoutMultiplier(void)
        {
            return announceReceiptTimeout;
        }

        /**
         * @brief  Gets the incoming successive sync interval time difference tolerance
         * @return Tolerance value
         */
        double getSyncArrivalTimeDiffTolerance(void)
        {
            return syncArrivalTimeDiffTolerance;
        }
        /**
         * @brief  Sets the incoming successive sync interval time difference tolerance
         * @param  val Tolerance value
         * @return none
         */
        void setSyncArrivalTimeDiffTolerance(double val)
        {
            syncArrivalTimeDiffTolerance = val;
        }

        /**
         * @brief  Gets the sync interval value
         * @return Sync Interval
         */
        int8_t getSyncInterval( void )
        {
            return log_mean_sync_interval;
        }

        /**
         * @brief  Sets the sync interval value
         * @param  val time interval
         * @return none
         */
        void setSyncInterval( int8_t val )
        {
            log_mean_sync_interval = val;
        }

        /**
         * @brief  Gets the deferred sync interval value
         * @return Deferred Sync Interval
         */
        signed char getDeferredSyncInterval(void)
        {
            return deferred_log_mean_sync_interval;
        }

        /**
         * @brief  Sets the deferred sync interval value
         * @param  val log mean sync time interval
         * @return none
         */
        void setDeferredSyncInterval(signed char val)
        {
            deferred_log_mean_sync_interval = val;
        }

        /**
         * @brief  Sets the sync interval back to initial value
         * @return none
         */
        void resetInitSyncInterval( void )
        {
            log_mean_sync_interval = initialLogSyncInterval;;
        }

        /**
         * @brief  Sets the sync interval
         * @return none
         */
        void setInitSyncInterval( int8_t interval )
        {
            initialLogSyncInterval = interval;
        }

        /**
         * @brief  Gets the sync interval
         * @return sync interval
         */
        int8_t getInitSyncInterval( void )
        {
            return initialLogSyncInterval;
        }

        /**
         * @brief  Gets the announce interval
         * @return Announce interval
         */
        int8_t getAnnounceInterval( void )
        {
            return log_mean_announce_interval;
        }

        /**
         * @brief  Sets the announce interval
         * @param  val time interval
         * @return none
         */
        void setAnnounceInterval(int8_t val)
        {
            log_mean_announce_interval = val;
        }
        /**
         * @brief  Start sync receipt timer
         * @param  waitTime time interval
         * @return none
         */
        void startSyncReceiptTimer(long long unsigned int waitTime);

        /**
         * @brief  Stop sync receipt timer
         * @return none
         */
        void stopSyncReceiptTimer( void );

        /**
         * @brief  Start sync interval timer
         * @param  waitTime time interval in nanoseconds
         * @return none
         */
        void startSyncIntervalTimer(long long unsigned int waitTime, Event e);

        /**
         * @brief  stop sync interval timer
         * @param  event to stop sync interval
         * @return none
         */
        void stopSyncIntervalTimer(Event e);

        /**
         * @brief  Start sync interval timer
         * @param  waitTime time interval in nanoseconds
         * @return none
         */
        void startDeferredSyncIntervalTimer(long long unsigned int waitTime, Event e);

        /**
         * @brief  Stop sync interval timer
         * @param  e Event Type
         * @return none
         */
        void stopDeferredSyncIntervalTimer(Event e);

        /**
         * @brief  Start announce interval timer
         * @param  waitTime time interval
         * @return none
         */
        void startAnnounceIntervalTimer(long long unsigned int waitTime);

        /**
         * @brief  Starts announce event timer
         * @return void
         */
        void startAnnounce();

        /**
         * @brief Process default state change event
         * @return true if event is handled without errors
         */
        bool processStateChange( Event e );

        /**
         * @brief Default sync/announce timeout handler
         * @return true if event is handled without errors
         */
        bool processSyncAnnounceTimeout( Event e );


        /**
         * @brief Perform default event action, can be overridden by media
         * specific actions in _processEvent
         * @return true if event is handled without errors
         */
        bool processEvent( Event e );

        /**
         * @brief Perform media specific event handling action
         * @return true if event is handled without errors
         */
        virtual bool _processEvent( Event e ) = 0;

        virtual void syncDone() = 0;

        /**
         * @brief Sends a general message to a port. No timestamps
         * @param buf [in] Pointer to the data buffer
         * @param len Size of the message
         * @param mcast_type Enumeration
         * MulticastType (pdelay, none or other). Depracated.
         * @param destIdentity Destination port identity
         * @return void
         */
        virtual void sendGeneralPort
        (uint16_t etherType, uint8_t * buf, int len, MulticastType mcast_type,
         PortIdentity * destIdentity) = 0;

        /**
         * @brief Sets link speed
         */
        void setLinkSpeed( uint32_t link_speed )
        {
            this->link_speed = link_speed;
        }

        /**
         * @brief Returns link speed
         */
        uint32_t getLinkSpeed( void )
        {
            return link_speed;
        }

        /**
         * @brief Returns TX PHY delay
         */
        Timestamp getTxPhyDelay( uint32_t link_speed ) const;

        /**
         * @brief Returns RX PHY delay
         */
        Timestamp getRxPhyDelay( uint32_t link_speed ) const;

        /**
        * @brief Returns if automotive profile is set
        */
        virtual bool getAutomotiveProfile() = 0;
};

/**
 * @brief Specifies a RX/TX PHY compensation pair
 */
class phy_delay_spec_t
{
    private:
        uint16_t tx_delay;
        uint16_t rx_delay;
    public:
        /**
         * Constructor setting PHY compensation
         */
        phy_delay_spec_t(
            uint16_t tx_delay,
            uint16_t rx_delay )
        {
            this->tx_delay = tx_delay;
            this->rx_delay = rx_delay;
        }

        /**
         * Default constructor sets 0 PHY compensation
         */
        phy_delay_spec_t()
        {
            phy_delay_spec_t( 0, 0 );
        }

        /**
         * @brief sets PHY compensation
         */
        void set_delay(
            uint16_t tx_delay,
            uint16_t rx_delay )
        {
            this->tx_delay = tx_delay;
            this->rx_delay = rx_delay;
        }

        /**
         * @brief sets RX PHY compensation
         */
        void set_tx_delay(
            uint16_t tx_delay )
        {
            this->tx_delay = tx_delay;
        }

        /**
         * @brief sets TX PHY compensation
         */
        void set_rx_delay(
            uint16_t rx_delay )
        {
            this->rx_delay = rx_delay;
        }

        /**
         * @brief gets TX PHY compensation
         */
        uint16_t get_tx_delay() const
        {
            return tx_delay;
        }

        /**
         * @brief gets RX PHY compensation
         */
        uint16_t get_rx_delay() const
        {
            return rx_delay;
        }
};

#endif/*COMMON_PORT_HPP*/

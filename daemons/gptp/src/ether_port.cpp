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

#include <ieee1588.hpp>

#include <ether_port.hpp>
#include <avbts_message.hpp>
#include <avbts_clock.hpp>

#include <avbts_oslock.hpp>
#include <avbts_osnet.hpp>
#include <avbts_oscondition.hpp>
#include <ether_tstamper.hpp>
#include <linux_hal_common.hpp>
#include <linux/ptp_clock.h>

#include <gptp_log.hpp>

#include <stdio.h>

#include <math.h>

#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define MAX_NSEC 1000000000

extern bool waitForInterface();
extern LinuxSharedMemoryIPC *ipc;
int port_pipe_fds[2];
LinkLayerAddress EtherPort::other_multicast(OTHER_MULTICAST);
LinkLayerAddress EtherPort::pdelay_multicast(PDELAY_MULTICAST);
LinkLayerAddress EtherPort::test_status_multicast
( TEST_STATUS_MULTICAST );

/* Return *a - *b */
static inline ptp_clock_time pct_diff
( struct ptp_clock_time *a, struct ptp_clock_time *b )
{
    ptp_clock_time result = {0, 0, 0};

    if ( a->nsec >= b->nsec ) {
        result.nsec = a->nsec - b->nsec;
    } else {
        --a->sec;
        result.nsec = (MAX_NSEC - b->nsec) + a->nsec;
    }

    result.sec = a->sec - b->sec;
    return result;
}

static inline int64_t pctns(struct ptp_clock_time t)
{
    return t.sec * 1000000000LL + t.nsec;
}

#ifdef PTP_SW_QTIMER
static inline void updateQTimerToMonoOffset(void) {
    const int sample_count = 20;
    int64_t total_offset = 0;
    int64_t min_offset = INT64_MAX;
    int64_t max_offset = INT64_MIN;

    for (int i = 0; i < sample_count; ++i) {
        struct timespec mono;
        struct ptp_clock_time mono_pct, qtimer_pct;
        uint64_t qTimerCount = 0, qTimerFreq = 0;
        uint64_t qTimerNanosSec = 0, qTimerNanosNSec = 0;
        int64_t offset;

        clock_gettime(CLOCK_MONOTONIC, &mono);

#if __aarch64__
        asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
        asm volatile("mrs %0, cntfrq_el0" : "=r"(qTimerFreq));
#else
        asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
        qTimerFreq = 19200000; // 19.2 MHz fallback
#endif

        qTimerNanosSec = qTimerCount / qTimerFreq;
        qTimerNanosNSec = (qTimerCount % qTimerFreq) * 1000000000 / qTimerFreq;

        qtimer_pct.sec = qTimerNanosSec;
        qtimer_pct.nsec = qTimerNanosNSec;
        mono_pct.sec = mono.tv_sec;
        mono_pct.nsec = mono.tv_nsec;

        offset = pctns(pct_diff(&qtimer_pct, &mono_pct));
        total_offset += offset;

        if (offset < min_offset) min_offset = offset;
        if (offset > max_offset) max_offset = offset;
    }

    int64_t avg_offset = total_offset / sample_count;

    GPTP_LOG_WARNING("qtimer_to_mono_offset -- min:%ld max:%ld avg:%ld",
                     min_offset, max_offset, avg_offset);

    if (ipc) {
        ipc->updateQtimeToMonoOffset(avg_offset);
    }
}
#endif

OSThreadExitCode watchNetLinkWrapper(void *arg)
{
    EtherPort *port;
    port = (EtherPort *) arg;

    if (port->watchNetLink() == NULL) {
        return osthread_ok;
    } else {
        return osthread_error;
    }
}

OSThreadExitCode openPortWrapper(void *arg)
{
    EtherPort *port;
    port = (EtherPort *) arg;

    if (port->openPort(port) == NULL) {
        return osthread_ok;
    } else {
        return osthread_error;
    }
}

EtherPort::~EtherPort()
{
    delete port_ready_condition;
}

EtherPort::EtherPort( PortInit_t *portInit ) :
    CommonPort( portInit )
{
    automotive_profile = portInit->automotive_profile;
    linkUp = portInit->linkUp;
    linkUpCount = 0;
    linkDownCount = 0;
    setTestMode( portInit->testMode );
    pdelay_sequence_id = 0;
    pdelay_started = false;
    pdelay_halted = false;
    sync_rate_interval_timer_started = false;
    duplicate_resp_counter = 0;
    last_invalid_seqid = 0;
    initialLogPdelayReqInterval = portInit->initialLogPdelayReqInterval;
    operLogPdelayReqInterval = portInit->operLogPdelayReqInterval;
    operLogSyncInterval = portInit->operLogSyncInterval;
    syncArrivalTimeDiffTolerance = portInit->syncArrivalTimeDiffTolerance;
    isGM = portInit->isGM;
    disableSigMsg = portInit->disableSigMsg;
    lostResponses = 0;
    allowedLostResponses = portInit->allowedLostResponses;
    reverseSyncEnabled = portInit->reverseSyncEnabled;
    reverseSyncDomain = portInit->reverseSyncDomain;
    reverseSyncRate = portInit->reverseSyncRate;
    // Initialize to ZERO (0) last GM time base indicator on bootup.
    setLastGmTimeBaseIndicator(0);
    reset_log_limit(RESET_ALL_LOG);

    // Consider port is up even in bypass_if_wait is set
    setEtherLinkState(ETHER_PORT_STATE_LINK_UP);
    clock->updateEtherLinkState(ETHER_PORT_STATE_LINK_UP);

    if (std::isnan(syncArrivalTimeDiffTolerance)) {
        GPTP_LOG_INFO("Default syncArrivalTimeDiffTolerance: %lF", DEFAULT_SYNC_ARRIVAL_TIME_DIFF_TOLERANCE);
        setSyncArrivalTimeDiffTolerance(DEFAULT_SYNC_ARRIVAL_TIME_DIFF_TOLERANCE); // 20% of oper sync interval
    } else {
        GPTP_LOG_INFO("Using syncArrivalTimeDiffTolerance: %lF", syncArrivalTimeDiffTolerance);
        setSyncArrivalTimeDiffTolerance(syncArrivalTimeDiffTolerance);
    }

    if (automotive_profile) {
        setAsCapable( true );

        if (getInitSyncInterval() == LOG2_INTERVAL_INVALID) {
            setInitSyncInterval( -3 );    // 125 ms
        }

        if (initialLogPdelayReqInterval == LOG2_INTERVAL_INVALID) {
            initialLogPdelayReqInterval = 0;    // 1 second
        }

        if (operLogPdelayReqInterval == LOG2_INTERVAL_INVALID) {
            operLogPdelayReqInterval = 0;    // 1 second
        }

        if (operLogSyncInterval == LOG2_INTERVAL_INVALID) {
            operLogSyncInterval = 0;    // 1 second
        }

        if (reverseSyncEnabled == LOG2_INTERVAL_INVALID) {
            reverseSyncEnabled = 0;           // RSYNC
        }

        if (reverseSyncDomain == LOG2_INTERVAL_INVALID) {
            reverseSyncDomain = 0;           // RSYNC_DOMAIN
        }

        if (reverseSyncRate == LOG2_INTERVAL_INVALID) {
            reverseSyncRate = RSYNC_RATE_DEFAULT;           // RSYNC_RATE
        }
    } else {
        if (portInit->asCapable) {
            setAsCapable( true);
        } else {
            setAsCapable( false );
        }

        if ( getInitSyncInterval() == LOG2_INTERVAL_INVALID ) {
            setInitSyncInterval( -3 );    // 125 ms
        }

        if (initialLogPdelayReqInterval == LOG2_INTERVAL_INVALID) {
            initialLogPdelayReqInterval = 0;    // 1 second
        }

        if (operLogPdelayReqInterval == LOG2_INTERVAL_INVALID) {
            operLogPdelayReqInterval = 0;    // 1 second
        }

        if (operLogSyncInterval == LOG2_INTERVAL_INVALID) {
            operLogSyncInterval = 0;    // 1 second
        }

        if (reverseSyncEnabled == LOG2_INTERVAL_INVALID) {
            reverseSyncEnabled = 0;           // RSYNC
        }

        if (reverseSyncDomain == LOG2_INTERVAL_INVALID) {
            reverseSyncDomain = 0;           // RSYNC_DOMAIN
        }

        if (reverseSyncRate == LOG2_INTERVAL_INVALID) {
            reverseSyncRate = RSYNC_RATE_DEFAULT;           // RSYNC_RATE
        }
    }

    /*TODO: Add intervals below to a config interface*/
    log_min_mean_pdelay_req_interval = initialLogPdelayReqInterval;
    last_sync = NULL;
    last_pdelay_req = NULL;
    last_pdelay_resp = NULL;
    last_pdelay_resp_fwup = NULL;
    setPdelayCount(0);
    setSyncCount(0);

    if (automotive_profile) {
        if (isGM) {
            avbSyncState = 1;
        } else {
            avbSyncState = 2;
        }

        if (getTestMode()) {
            linkUpCount =
                1;  // TODO : really should check the current linkup status http://stackoverflow.com/questions/15723061/how-to-check-if-interface-is-up
            linkDownCount = 0;
        }
    } else {
        avbSyncState = 0;   /* Invalid value for avbSyncState */
    }
    increment_LinkupCount();
    setStationState(STATION_STATE_RESERVED);
}

bool EtherPort::_init_port( void )
{
    pdelay_rx_lock = lock_factory->createLock(oslock_recursive);
    if (pdelay_rx_lock == NULL) {
        GPTP_LOG_ERROR("Failed to create pdelay_rx_lock");
        return false;
    }
    port_tx_lock = lock_factory->createLock(oslock_recursive);
    if (port_tx_lock == NULL) {
        GPTP_LOG_ERROR("Failed to create port_tx_lock");
        return false;
    }
    pDelayIntervalTimerLock = lock_factory->createLock(oslock_recursive);
    if (pDelayIntervalTimerLock == NULL) {
        GPTP_LOG_ERROR("Failed to create pDelayIntervalTimerLock");
        return false;
    }
    port_ready_condition = condition_factory->createCondition();
    if (port_ready_condition == NULL) {
        GPTP_LOG_ERROR("Failed to create port_ready_condition");
        return false;
    }
    return true;
}

void EtherPort::startPDelay()
{
    if (!pdelayHalted()) {
        if (automotive_profile) {
            if (log_min_mean_pdelay_req_interval !=
                    PTPMessageSignalling::sigMsgInterval_NoSend) {
                long long unsigned int waitTime;
                waitTime = ((long long) (pow((double)2,
                                             log_min_mean_pdelay_req_interval) * 1000000000.0));
                waitTime = waitTime > EVENT_TIMER_GRANULARITY ? waitTime :
                           EVENT_TIMER_GRANULARITY;
                pdelay_started = true;
                startPDelayIntervalTimer(waitTime);
            }
        } else {
            pdelay_started = true;
            startPDelayIntervalTimer(32000000);
        }
    }
}

void EtherPort::stopPDelay()
{
    haltPdelay(true);
    pdelay_started = false;
    clock->deleteEventTimerLocked( this, PDELAY_INTERVAL_TIMEOUT_EXPIRES);
}

void EtherPort::startSyncRateIntervalTimer()
{
    if (automotive_profile) {
        sync_rate_interval_timer_started = true;

        if (isGM) {
            //nothing to do, will keep same rate until signaling message arrives to reduce sync rate
            //clock->addEventTimerLocked( this, SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED, 8000000000 );
        } else {
            // Slave will time out after 4 seconds
            clock->addEventTimerLocked( this, SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED,
                                        4000000000 );
        }
    }
}

void EtherPort::processMessage
( char *buf, int length, LinkLayerAddress *remote, uint32_t link_speed )
{
    GPTP_LOG_VERBOSE("Processing network buffer");
    PTPMessageCommon *msg =
        buildPTPMessage( buf, (int)length, remote, this );

    if (msg == NULL) {
        GPTP_LOG_ERROR("Discarding invalid message");
        return;
    }

    GPTP_LOG_VERBOSE("Processing message");

    if ( msg->isEvent() ) {
        Timestamp rx_timestamp = msg->getTimestamp();
        Timestamp phy_compensation = getRxPhyDelay( link_speed );
        GPTP_LOG_DEBUG( "RX PHY compensation: %s sec",
                        phy_compensation.toString().c_str() );
        phy_compensation._version = rx_timestamp._version;
        rx_timestamp = rx_timestamp - phy_compensation;
        msg->setTimestamp( rx_timestamp );
    }

    msg->processMessage(this);

    if (msg->garbage()) {
        delete msg;
    }
}

void *EtherPort::openPort( EtherPort *port )
{
    port_ready_condition->signal();
    int ret = pthread_setname_np(pthread_self(), "openPort");
    if (ret != 0) {
        GPTP_LOG_ERROR("pthread_setname_np failed");
    }

    while (linkstatus) {
        uint8_t buf[128];
        LinkLayerAddress remote;
        net_result rrecv;
        size_t length = sizeof(buf);
        uint32_t link_speed;

        if ( ( rrecv = recv( &remote, buf, length, link_speed ))
                == net_succeed ) {
            processMessage
            ((char *)buf, (int)length, &remote, link_speed );
        } else if (rrecv == net_fatal) {
            GPTP_LOG_ERROR("read from network interface failed");
            this->processEvent(FAULT_DETECTED);
            break;
        }
    }

    return NULL;
}

net_result EtherPort::port_send
( uint16_t etherType, uint8_t *buf, int size, MulticastType mcast_type,
  PortIdentity *destIdentity, bool timestamp )
{
    LinkLayerAddress dest;

    if (mcast_type != MCAST_NONE) {
        if (mcast_type == MCAST_PDELAY) {
            dest = pdelay_multicast;
        } else if (mcast_type == MCAST_TEST_STATUS) {
            dest = test_status_multicast;
        } else {
            dest = other_multicast;
        }
    } else {
        mapSocketAddr(destIdentity, &dest);
    }

    return send(&dest, etherType, (uint8_t *) buf, size, timestamp);
}

void EtherPort::sendEventPort
( uint16_t etherType, uint8_t *buf, int size, MulticastType mcast_type,
  PortIdentity *destIdentity, uint32_t *link_speed )
{
    net_result rtx = port_send
                     ( etherType, buf, size, mcast_type, destIdentity, true );

    if ( rtx != net_succeed ) {
        GPTP_LOG_LIMIT_ERROR(SEND_PORT_LOG, "sendEventPort(): failure");
        return;
    }

    *link_speed = this->getLinkSpeed();
    return;
}

void EtherPort::sendGeneralPort
( uint16_t etherType, uint8_t *buf, int size, MulticastType mcast_type,
  PortIdentity * destIdentity )
{
    net_result rtx = port_send(etherType, buf, size, mcast_type, destIdentity,
                               false);

    if (rtx != net_succeed) {
        GPTP_LOG_ERROR("sendGeneralPort(): failure");
    }

    return;
}

bool EtherPort::_processEvent( Event e )
{
    bool ret = false;
    OSThreadExitCode exit_code = osthread_ok;

    switch (e) {
        case POWERUP:
        case INITIALIZE:
            setEtherLinkState(ETHER_PORT_STATE_LINK_UP);
            clock->updateEtherLinkState(ETHER_PORT_STATE_LINK_UP);
            if (!automotive_profile) {
                //if ( getPortState() != PTP_SLAVE &&
                //  getPortState() != PTP_MASTER )
                if (getPortState() != PTP_MASTER) {
                    GPTP_LOG_STATUS("Starting PDelay");
                    startPDelay();
                }
            } else {
                startPDelay();
            }
            port_pipe_fds[0] = -1;
            port_pipe_fds[1] = -1;
            if (pipe(port_pipe_fds) == -1) {
                GPTP_LOG_ERROR("pipe create error\n");
                ret = false;
                break;
            }
            port_ready_condition->wait_prelock();

            if ( !linkWatch(watchNetLinkWrapper, (void *)this) ) {
                GPTP_LOG_ERROR("Error creating port link thread");
                ret = false;
                break;
            }

            if ( !linkOpen(openPortWrapper, (void *)this) ) {
                GPTP_LOG_ERROR("Error creating port thread");
                ret = false;
                break;
            }

            port_ready_condition->wait();

            if (automotive_profile) {
                setStationState(STATION_STATE_ETHERNET_READY);

                if (getTestMode()) {
                    APMessageTestStatus *testStatusMsg = new APMessageTestStatus(this);

                    if (testStatusMsg) {
                        testStatusMsg->sendPort(this);
                        delete testStatusMsg;
                    }
                }

                if (!isGM) {
                    startSyncReceiptTimer((unsigned long long)
                                          (getsyncReceiptTimeoutMultiplier()*
                                           ((double) pow((double)2, getSyncInterval()) *
                                            1000000000.0)));
                }
            }

            ret = true;
            break;

        case STATE_CHANGE_EVENT:

            // If the automotive profile is enabled, handle the event by
            // doing nothing and returning true, preventing the default
            // action from executing
            if ( automotive_profile ) {
                ret = true;
            } else {
                ret = false;
            }

            break;

        case LINKUP:
            if (linkstatus) {
                GPTP_LOG_WARNING("Already in LINKUP state, ignoring duplicate event.");
                ret = true;
                break;
            }
            if (!OSNetworkInterfaceFactory::buildInterface
                ( &net_iface, factory_name_t("default"), net_label,
                 _hw_timestamper, getTSC())) {
                return false;
            }
            timestamper_init();
#ifdef PTP_SW_QTIMER
            updateQTimerToMonoOffset();
#endif
            _init_port();
            linkstatus = true;
            port_pipe_fds[0] = -1;
            port_pipe_fds[1] = -1;
            if (pipe(port_pipe_fds) == -1) {
                GPTP_LOG_ERROR("pipe create error\n");
                ret = false;
                break;
            }
            port_ready_condition->wait_prelock();

            if ( !linkOpen(openPortWrapper, (void *)this) ) {
                GPTP_LOG_ERROR("Error creating port thread");
                ret = false;
                break;
            }

            port_ready_condition->wait();

            haltPdelay(false);
            startPDelay();

            if (automotive_profile) {
                GPTP_LOG_EXCEPTION("LINKUP");
            } else {
                GPTP_LOG_STATUS("LINKUP");
            }

            if ( clock->getPriority1() == 255 || getPortState() == PTP_SLAVE ) {
                becomeSlave( true );
            } else if ( getPortState() == PTP_MASTER ) {
                becomeMaster( true );
            } else {
                clock->addEventTimerLocked(this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES,
                                           ANNOUNCE_RECEIPT_TIMEOUT_MULTIPLIER * pow(2.0,
                                                   getAnnounceInterval()) * 1000000000.0);
            }

            if (automotive_profile) {
                setAsCapable( true );
                setStationState(STATION_STATE_ETHERNET_READY);

                if (getTestMode()) {
                    APMessageTestStatus *testStatusMsg = new APMessageTestStatus(this);

                    if (testStatusMsg) {
                        testStatusMsg->sendPort(this);
                        delete testStatusMsg;
                    }
                }

                resetInitSyncInterval();
                setAnnounceInterval( 0 );
                log_min_mean_pdelay_req_interval = initialLogPdelayReqInterval;

                if (!isGM) {
                    startSyncReceiptTimer((unsigned long long)
                                          (getsyncReceiptTimeoutMultiplier()*
                                           ((double) pow((double)2, getSyncInterval()) *
                                            1000000000.0)));
                }

                // Reset Sync count and pdelay count
                setPdelayCount(0);
                setSyncCount(0);

                // Start AVB SYNC at 2. It will decrement after each sync. When it reaches 0 the Test Status message
                // can be sent
                if (isGM) {
                    avbSyncState = 1;
                } else {
                    avbSyncState = 2;
                }

                if (getTestMode()) {
                    linkUpCount++;
                }
                increment_LinkupCount();
            }

            this->timestamper_reset();
            setEtherLinkState(ETHER_PORT_STATE_LINK_UP);
            clock->updateEtherLinkState(ETHER_PORT_STATE_LINK_UP);
            if (sct_buffer) {
                pthread_mutex_lock((pthread_mutex_t *) &sct_buffer->lock);
                sct_buffer->status.d_status = DEAMON_UP;
                pthread_mutex_unlock((pthread_mutex_t *) &sct_buffer->lock);
                GPTP_LOG_STATUS("sct_buffer d_status set on LINKUP");
            }
            ret = true;
            break;

        case LINKDOWN:
            if (!linkstatus) {
                GPTP_LOG_WARNING("Already in LINKDOWN state, ignoring duplicate event.");
                ret = true;
                break;
            }
            linkstatus = false;
            //delete all timers as in powerdown
            stopPDelay();
            clock->deleteEventTimerLocked( this, ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES );
            clock->deleteEventTimerLocked( this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES );
            clock->deleteEventTimerLocked( this, SYNC_INTERVAL_TIMEOUT_EXPIRES);
            clock->deleteEventTimerLocked( this, DEFERRED_SYNC_INTERVAL_RATE_CHANGE);
            clock->deleteEventTimerLocked( this, PDELAY_RESP_RECEIPT_TIMEOUT_EXPIRES);
            clock->deleteEventTimerLocked( this, SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED);
            clock->deleteEventTimerLocked( this, RSYNC_INTERVAL_TIMEOUT_EXPIRES );
            stopSyncReceiptTimer();
            setEtherLinkState(ETHER_PORT_STATE_LINK_DOWN);
            clock->updateEtherLinkState(ETHER_PORT_STATE_LINK_DOWN);

            if (port_pipe_fds[1] != -1) {
                char data = '1';
                ssize_t bytes_written = write(port_pipe_fds[1], &data, 1);
                if (bytes_written != 1) {
                    GPTP_LOG_ERROR("Failed to write to pipe: %s", strerror(errno));
                } else {
                    GPTP_LOG_INFO("Successfully wrote to pipe to interrupt select()");
                }
            }

            if (!linkjoin(exit_code)) {
                GPTP_LOG_ERROR("Failed to openport thread to join %d", exit_code);
                ret = false;
                break;
            }
            GPTP_LOG_INFO("openport thread to join %d", exit_code);
            // Release the Pipe
            if (port_pipe_fds[0] != -1) {
                close(port_pipe_fds[0]);
                port_pipe_fds[0] = -1;
            }
            if (port_pipe_fds[1] != -1) {
                close(port_pipe_fds[1]);
                port_pipe_fds[1] = -1;
            }
            setStationState(STATION_STATE_RESERVED);
            if (sct_buffer) {
                pthread_mutex_lock((pthread_mutex_t *) &sct_buffer->lock);
                sct_buffer->status.d_status = 0;
                pthread_mutex_unlock((pthread_mutex_t *) &sct_buffer->lock);
                GPTP_LOG_STATUS("sct_buffer d_status cleared on LINKDOWN");
            }
            if ( ipc ) {
                ipc->ipc_down();
                GPTP_LOG_ERROR("ipc DOWN");
            }
#ifdef LE_SHARED_MEM
            if ( ipc ) {
                ipc->updateSyncStatus(false, PTP_DISABLED);
            }

#endif
            if (automotive_profile) {
                GPTP_LOG_EXCEPTION("LINK DOWN");
            } else {
                setAsCapable(false);
                GPTP_LOG_STATUS("LINK DOWN");
            }

            if (getTestMode()) {
                linkDownCount++;
            }
            increment_LinkdownCount();
            timestamper_deinit();
            ret = true;
            break;

        case ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES:
        case SYNC_RECEIPT_TIMEOUT_EXPIRES:
            if ( !automotive_profile ) {
                ret = false;
                break;
            }

            // Automotive Profile specific action
            if (e == SYNC_RECEIPT_TIMEOUT_EXPIRES) {

            GPTP_LOG_LIMIT_EXCEPTION(SYNC_LOG, "SYNC receipt timeout");

                startSyncReceiptTimer((unsigned long long)
                                      (getsyncReceiptTimeoutMultiplier()*
                                       ((double) pow((double)2, getSyncInterval()) *
                                        1000000000.0)));
            }

            ret = true;
            break;
        case DEFERRED_SYNC_INTERVAL_RATE_CHANGE:
            GPTP_LOG_DEBUG("DEFERRED_SYNC_INTERVAL_RATE_CHANGE occurred");
            if (e == DEFERRED_SYNC_INTERVAL_RATE_CHANGE) {
                //Set deferred sync interval rate as current sync interval rate
                setSyncInterval(getDeferredSyncInterval());
                GPTP_LOG_INFO("Log mean sync interval changed to %d", getSyncInterval());
            }
            ret = true;
            break;
        case PDELAY_INTERVAL_TIMEOUT_EXPIRES:
            GPTP_LOG_DEBUG("PDELAY_INTERVAL_TIMEOUT_EXPIRES occured");
            {
                Timestamp req_timestamp;
                PTPMessagePathDelayReq *pdelay_req =
                    new PTPMessagePathDelayReq(this);
                if (pdelay_req == NULL) {
                    GPTP_LOG_ERROR("Failed to allocate PTPMessagePathDelayReq");
                    break;
                }
                PortIdentity dest_id;
                getPortIdentity(dest_id);
                pdelay_req->setPortIdentity(&dest_id);
                //PDely Stats
                dest_id.getPortNumber(&pdelayInfo.req_portNumber);
                dest_id.getClockIdentityString(pdelayInfo.req_clockIdentity);
                {
                    Timestamp pending =
                        PDELAY_PENDING_TIMESTAMP;
                    pdelay_req->setTimestamp(pending);
                }

                getPDelayRxLock();
                if (last_pdelay_req != NULL) {
                    delete last_pdelay_req;
                    last_pdelay_req = NULL;
                }

                setLastPDelayReq(pdelay_req);
                putPDelayRxLock();

                getTxLock();
                pdelay_req->sendPort(this, NULL);
                GPTP_LOG_DEBUG("*** Sent PDelay Request message");
                putTxLock();
                {
                    long long timeout;
                    long long interval;
                    timeout = PDELAY_RESP_RECEIPT_TIMEOUT_MULTIPLIER *
                              ((long long)
                               (pow((double)2, getPDelayInterval()) * 1000000000.0));
                    timeout = timeout > EVENT_TIMER_GRANULARITY ?
                              timeout : EVENT_TIMER_GRANULARITY;
                    clock->addEventTimerLocked
                    (this, PDELAY_RESP_RECEIPT_TIMEOUT_EXPIRES, timeout );
                    GPTP_LOG_DEBUG("Schedule PDELAY_RESP_RECEIPT_TIMEOUT_EXPIRES, "
                                   "PDelay interval %d, timeout %lld",
                                   getPDelayInterval(), timeout);
                    interval =
                        ((long long)
                         (pow((double)2, getPDelayInterval()) * 1000000000.0));
                    interval = interval > EVENT_TIMER_GRANULARITY ?
                               interval : EVENT_TIMER_GRANULARITY;
                    startPDelayIntervalTimer(interval);
                }
            }
            break;

        case SYNC_INTERVAL_TIMEOUT_EXPIRES: {
                /* Set offset from master to zero, update device vs
                   system time offset */
                // Send a sync message and then a followup to broadcast
                PTPMessageSync *sync = new PTPMessageSync(this);
                if (sync == NULL) {
                    GPTP_LOG_ERROR("Failed to allocate PTPMessageSync for SYNC_INTERVAL");
                    break;
                }
                PortIdentity dest_id;
                bool tx_succeed;
                getPortIdentity(dest_id);
                sync->setPortIdentity(&dest_id);
                getTxLock();
                tx_succeed = sync->sendPort(this, NULL);
                GPTP_LOG_DEBUG("Sent SYNC message");

                if ( automotive_profile &&
                        getPortState() == PTP_MASTER ) {
                    if (avbSyncState > 0) {
                        avbSyncState--;

                        if (avbSyncState == 0) {
                            // Send Avnu Automotive Profile status message
                            setStationState(STATION_STATE_AVB_SYNC);

                            if (getTestMode()) {
                                APMessageTestStatus *testStatusMsg = new APMessageTestStatus(this);

                                if (testStatusMsg) {
                                    testStatusMsg->sendPort(this);
                                    delete testStatusMsg;
                                }
                            }
                        }
                    }
                }

                putTxLock();

                if ( tx_succeed ) {
                    Timestamp sync_timestamp = sync->getTimestamp();
                    GPTP_LOG_VERBOSE("Successful Sync timestamp");
                    GPTP_LOG_VERBOSE("Seconds: %u",
                                     sync_timestamp.seconds_ls);
                    GPTP_LOG_VERBOSE("Nanoseconds: %u",
                                     sync_timestamp.nanoseconds);
                    PTPMessageFollowUp *follow_up = new PTPMessageFollowUp(this);
                    if (follow_up == NULL) {
                        GPTP_LOG_ERROR("Failed to allocate PTPMessageFollowUp for SYNC");
                    } else {
                        PortIdentity dest_id;
                        getPortIdentity(dest_id);
                        follow_up->setClockSourceTime(getClock()->getFUPInfo());
                        follow_up->setPortIdentity(&dest_id);
                        follow_up->setSequenceId(sync->getSequenceId());
                        follow_up->setPreciseOriginTimestamp
                        (sync_timestamp);
                        follow_up->sendPort(this, NULL);
                        GPTP_LOG_DEBUG("Sent SYNC follow_up message");
                        delete follow_up;
                    }
                } else {
                    GPTP_LOG_ERROR
                    ("*** Unsuccessful Sync timestamp");
                }

                delete sync;
            }
            break;

        case RSYNC_INTERVAL_TIMEOUT_EXPIRES: {
                GPTP_LOG_VERBOSE("RSYNC_INTERVAL_TIMEOUT_EXPIRES evt occured");
                /* Set offset from master to zero, update device vs
                   system time offset */
                // Send a sync message and then a followup to broadcast
                PTPMessageSync *sync = new PTPMessageSync(this);
                if (sync == NULL) {
                    GPTP_LOG_ERROR("Failed to allocate PTPMessageSync for RSYNC_INTERVAL");
                    break;
                }
                PortIdentity dest_id;
                bool tx_succeed;
                getPortIdentity(dest_id);
                sync->setPortIdentity(&dest_id);
                getTxLock();
                tx_succeed = sync->sendPort(this, NULL);
                GPTP_LOG_DEBUG("Sent RSYNC message");
                putTxLock();

                if ( tx_succeed ) {
                    Timestamp sync_timestamp = sync->getTimestamp();
                    GPTP_LOG_VERBOSE("Successful RSync timestamp");
                    GPTP_LOG_VERBOSE("Seconds: %u",
                                     sync_timestamp.seconds_ls);
                    GPTP_LOG_VERBOSE("Nanoseconds: %u",
                                     sync_timestamp.nanoseconds);
                    PTPMessageFollowUp *follow_up = new PTPMessageFollowUp(this);
                    if (follow_up == NULL) {
                        GPTP_LOG_ERROR("Failed to allocate PTPMessageFollowUp for RSYNC");
                    } else {
                        PortIdentity dest_id;
                        getPortIdentity(dest_id);
                        //setLastvalidSeqID(sync->getSequenceId());
                        follow_up->setClockSourceTime(getClock()->getFUPInfo());
                        follow_up->setPortIdentity(&dest_id);
                        follow_up->setSequenceId(sync->getSequenceId());
                        follow_up->setPreciseOriginTimestamp
                        (sync_timestamp);
                        follow_up->sendPort(this, NULL);
                        delete follow_up;
                    }
                } else {
                    GPTP_LOG_ERROR
                    ("*** Unsuccessful reverse sync timestamp");
                }

                delete sync;
            }
            break;

        case FAULT_DETECTED:
            GPTP_LOG_ERROR("Received FAULT_DETECTED event");

            if (!automotive_profile) {
                setAsCapable(false);
            }

            break;

        case PDELAY_DEFERRED_PROCESSING:
            GPTP_LOG_DEBUG("PDELAY_DEFERRED_PROCESSING occured");
            pdelay_rx_lock->lock();

            if (last_pdelay_resp_fwup == NULL) {
                GPTP_LOG_ERROR("PDelay Response Followup is NULL!");
                abort();
            }

            last_pdelay_resp_fwup->processMessage(this);

            if (last_pdelay_resp_fwup->garbage()) {
                delete last_pdelay_resp_fwup;
                this->setLastPDelayRespFollowUp(NULL);
            }

            pdelay_rx_lock->unlock();
            break;

        case PDELAY_RESP_RECEIPT_TIMEOUT_EXPIRES:
            GPTP_LOG_LIMIT_EXCEPTION(PDELAY_LOG, "PDelay Response Receipt Timeout");
            static bool counter_updated = false;
            timelog_avnu_pdelay_resp_timeout();
            GPTP_LOG_DEBUG("lostResponses: %d getAllowedLostResponses: %d", lostResponses, getAllowedLostResponses());
            if (lostResponses < getAllowedLostResponses()) {
                lostResponses++;
                counter_updated = false;
            } else {
                if (!counter_updated) {
                    incCounter_ieee8021AsPortStatPdelayAllowedLostResponsesExceeded();
                    if (!automotive_profile) {
                        setAsCapable(false); // set as As incapable only in non-automotive profile.
                    }
                    counter_updated = true;
                }
            }
            setPdelayCount(0);
            break;

        case PDELAY_RESP_PEER_MISBEHAVING_TIMEOUT_EXPIRES:
            GPTP_LOG_EXCEPTION("PDelay Resp Peer Misbehaving timeout expired! Restarting PDelay");
            haltPdelay(false);

            if ( getPortState() != PTP_SLAVE &&
                    getPortState() != PTP_MASTER ) {
                GPTP_LOG_STATUS("Starting PDelay" );
                startPDelay();
            }

            break;

        case SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED:
        {
            GPTP_LOG_INFO("SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED occured");
            sync_rate_interval_timer_started = false;
            if (!disableSigMsg) {
                bool sendSignalMessage = false;

                if (getSyncInterval() != operLogSyncInterval) {
                    setSyncInterval(operLogSyncInterval);
                    sendSignalMessage = true;
                }

                if (log_min_mean_pdelay_req_interval != operLogPdelayReqInterval) {
                    log_min_mean_pdelay_req_interval = operLogPdelayReqInterval;
                    sendSignalMessage = true;
                }

                if (sendSignalMessage && (getStationState() >= STATION_STATE_AVB_SYNC)) {
                    if (!isGM) {
                        // Send operational signalling message
                        PTPMessageSignalling *sigMsg = new PTPMessageSignalling(this);

                        if (sigMsg) {
                            if (automotive_profile) {
                                sigMsg->setintervals(PTPMessageSignalling::sigMsgInterval_NoChange,
                                                     getSyncInterval(), PTPMessageSignalling::sigMsgInterval_NoChange);
                            } else {
                                sigMsg->setintervals(log_min_mean_pdelay_req_interval, getSyncInterval(),
                                                     PTPMessageSignalling::sigMsgInterval_NoChange);
                            }

                            sigMsg->sendPort(this, NULL);
                            delete sigMsg;
                        }
                    }
                }
            }
            startSyncReceiptTimer((unsigned long long)(getsyncReceiptTimeoutMultiplier() *
                                                       ((double)pow((double)2, getSyncInterval()) *
                                                        1000000000.0)));
        }
        break;

        case POWERDOWN:
            //to ensure no processing happens for already expired events
            stopPDelay();
            setAsCapable(false);
            clock->deleteEventTimerLocked( this, ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES );
            clock->deleteEventTimerLocked( this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES );
            clock->deleteEventTimerLocked( this, SYNC_INTERVAL_TIMEOUT_EXPIRES);
            clock->deleteEventTimerLocked( this, DEFERRED_SYNC_INTERVAL_RATE_CHANGE);
            clock->deleteEventTimerLocked( this, PDELAY_RESP_RECEIPT_TIMEOUT_EXPIRES);
            clock->deleteEventTimerLocked( this, SYNC_RATE_INTERVAL_TIMEOUT_EXPIRED);
            clock->deleteEventTimerLocked( this, RSYNC_INTERVAL_TIMEOUT_EXPIRES );
            stopSyncReceiptTimer();
            GPTP_LOG_EXCEPTION("gptp powering down");
            ret = true;
            break;

        default:
            GPTP_LOG_ERROR
            ( "Unhandled event type in "
              "EtherPort::processEvent(), %d", e );
            ret = false;
            break;
    }

    return ret;
}

void EtherPort::recoverPort( void )
{
    return;
}

void EtherPort::becomeMaster( bool annc )
{
    setPortState( PTP_MASTER );
    // Stop announce receipt timeout timer
    clock->deleteEventTimerLocked( this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES );
    // Stop sync receipt timeout timer
    clock->setSyncStatus(true, PTP_MASTER);

    if (sct_buffer) {
        pthread_mutex_lock((pthread_mutex_t *) &sct_buffer->lock);
        sct_buffer->status.gptp_status = GPTP_STATUS_TIMEOUT;
        sct_buffer->status.d_status = DEAMON_UP;
        sct_buffer->status.IsMaster = 1;
        pthread_mutex_unlock((pthread_mutex_t *) &sct_buffer->lock);
    }

    stopSyncReceiptTimer();

    if ( annc ) {
        if (!automotive_profile) {
            startAnnounce();
        }
    }

    startSyncIntervalTimer(16000000, SYNC_INTERVAL_TIMEOUT_EXPIRES);
    GPTP_LOG_STATUS("Switching to Master" );
    clock->updateFUPInfo();
    return;
}

void EtherPort::becomeSlave( bool restart_syntonization )
{
    clock->deleteEventTimerLocked( this, ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES );
    clock->deleteEventTimerLocked( this, SYNC_INTERVAL_TIMEOUT_EXPIRES );
    clock->deleteEventTimerLocked( this, DEFERRED_SYNC_INTERVAL_RATE_CHANGE ); //Delete as this is used only in GM mode
    setPortState( PTP_SLAVE );
    clock->setSyncStatus(false, PTP_SLAVE);

    if (sct_buffer) {
        pthread_mutex_lock((pthread_mutex_t *) &sct_buffer->lock);
        sct_buffer->status.gptp_status = GPTP_STATUS_TIMEOUT;
        sct_buffer->status.d_status = DEAMON_UP;
        sct_buffer->status.IsMaster = 0;
        pthread_mutex_unlock((pthread_mutex_t *) &sct_buffer->lock);
    }

    if (!automotive_profile) {
        clock->addEventTimerLocked
        (this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES,
         (getannounceReceiptTimeoutMultiplier() *
          (unsigned long long)
          (pow((double)2, getAnnounceInterval()) * 1000000000.0)));
    }

    if (reverseSyncEnabled) {
        GPTP_LOG_INFO("RSYNC: %d, RSYNC_DOMAIN %d, RSYNC_RATE %f", reverseSyncEnabled,
                      reverseSyncDomain, reverseSyncRate);
        clock->setRsyncDomain(reverseSyncDomain);
        clock->setRsyncRate(reverseSyncRate);
        startSyncIntervalTimer(16000000 * reverseSyncRate,
                               RSYNC_INTERVAL_TIMEOUT_EXPIRES);
    }

    GPTP_LOG_STATUS("Switching to Slave" );

    if ( restart_syntonization ) {
        clock->newSyntonizationSetPoint();
    }

    getClock()->updateFUPInfo();
    return;
}

int8_t EtherPort::getRsync(void)
{
    return reverseSyncEnabled;
}

void EtherPort::enableRsync( int8_t RSyncDomain, double RSyncRate)
{
    reverseSyncEnabled = true;
    reverseSyncDomain = RSyncDomain;
    reverseSyncRate = RSyncRate;
    GPTP_LOG_INFO("%s:%d enable reverse sync, domain %d, rate %f",
                  __func__, __LINE__, reverseSyncEnabled, reverseSyncDomain, reverseSyncRate);

    if ( clock->getPriority1() == 255 || getPortState() == PTP_SLAVE ) {
        clock->setRsyncDomain(reverseSyncDomain);
        clock->setRsyncRate(reverseSyncRate);
        startSyncIntervalTimer(16000000 * reverseSyncRate,
                               RSYNC_INTERVAL_TIMEOUT_EXPIRES);
    }
}

void EtherPort::disableRsync( void )
{
    reverseSyncEnabled = false;
    GPTP_LOG_INFO("disable reverse sync", reverseSyncEnabled);

    if ( clock->getPriority1() == 255 || getPortState() == PTP_SLAVE ) {
        stopSyncIntervalTimer(RSYNC_INTERVAL_TIMEOUT_EXPIRES);
    }
}

void EtherPort::mapSocketAddr
( PortIdentity *destIdentity, LinkLayerAddress *remote )
{
    *remote = identity_map[*destIdentity];
    return;
}

void EtherPort::addSockAddrMap
( PortIdentity *destIdentity, LinkLayerAddress *remote )
{
    identity_map[*destIdentity] = *remote;
    return;
}

int EtherPort::getTxTimestamp
( PTPMessageCommon *msg, Timestamp &timestamp, unsigned &counter_value,
  bool last )
{
    PortIdentity identity;
    msg->getPortIdentity(&identity);
    return getTxTimestamp
           (&identity, msg->getMessageId(), timestamp, counter_value, last);
}

int EtherPort::getRxTimestamp
( PTPMessageCommon * msg, Timestamp & timestamp, unsigned &counter_value,
  bool last )
{
    PortIdentity identity;
    msg->getPortIdentity(&identity);
    return getRxTimestamp
           (&identity, msg->getMessageId(), timestamp, counter_value, last);
}

int EtherPort::getTxTimestamp
(PortIdentity *sourcePortIdentity, PTPMessageId messageId,
 Timestamp &timestamp, unsigned &counter_value, bool last )
{
    EtherTimestamper *timestamper =
        dynamic_cast<EtherTimestamper *>(_hw_timestamper);

    if (timestamper) {
        return timestamper->HWTimestamper_txtimestamp
               ( sourcePortIdentity, messageId, timestamp,
                 counter_value, last );
    }

    timestamp = clock->getSystemTime();
    return 0;
}

int EtherPort::getRxTimestamp
( PortIdentity * sourcePortIdentity, PTPMessageId messageId,
  Timestamp &timestamp, unsigned &counter_value, bool last )
{
    EtherTimestamper *timestamper =
        dynamic_cast<EtherTimestamper *>(_hw_timestamper);

    if (timestamper) {
        return timestamper->HWTimestamper_rxtimestamp
               (sourcePortIdentity, messageId, timestamp, counter_value,
                last);
    }

    timestamp = clock->getSystemTime();
    return 0;
}

void EtherPort::startPDelayIntervalTimer
( long long unsigned int waitTime )
{
    if (pDelayIntervalTimerLock->trylock() == oslock_fail) {
        return;
    }

    clock->deleteEventTimerLocked(this, PDELAY_INTERVAL_TIMEOUT_EXPIRES);
    clock->addEventTimerLocked(this, PDELAY_INTERVAL_TIMEOUT_EXPIRES, waitTime);
    pDelayIntervalTimerLock->unlock();
}

void EtherPort::syncDone()
{
    GPTP_LOG_VERBOSE("Sync complete");

    if (automotive_profile && getPortState() == PTP_SLAVE) {
#ifdef LOG_LIMIT
        reset_log_limit(RESET_ALL_LOG);
#endif

        if (avbSyncState > 0) {
            avbSyncState--;

            if (avbSyncState == 0) {
                setStationState(STATION_STATE_AVB_SYNC);

                if (getTestMode()) {
                    APMessageTestStatus *testStatusMsg =
                        new APMessageTestStatus(this);

                    if (testStatusMsg) {
                        testStatusMsg->sendPort(this);
                        delete testStatusMsg;
                    }
                }
            }
        }
    }

    if (automotive_profile) {
        if (!sync_rate_interval_timer_started) {
            if ( getSyncInterval() != operLogSyncInterval ) {
                startSyncRateIntervalTimer();
            }
        }
    }

    if ( !pdelay_started ) {
        startPDelay();
    }
}

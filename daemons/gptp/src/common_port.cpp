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

#include <common_port.hpp>
#include <avbts_clock.hpp>
#include <common_tstamper.hpp>
#include <gptp_cfg.hpp>
#include <qgptp_rmgr.h>
#include <unistd.h>

extern bool waitForInterface();
extern void gptpDaemonServDeInit(void);
extern void gptpDaemonServInit(void);

CommonPort::CommonPort( PortInit_t *portInit ) :
    thread_factory( portInit->thread_factory ),
    timer_factory( portInit->timer_factory ),
    lock_factory( portInit->lock_factory ),
    condition_factory( portInit->condition_factory ),
    _hw_timestamper( portInit->timestamper ),
    clock( portInit->clock ),
    isGM( portInit->isGM ),
    phy_delay( portInit->phy_delay )
{
    one_way_delay = ONE_WAY_DELAY_DEFAULT;
    neighbor_prop_delay_thresh = portInit->neighborPropDelayThreshold;
    announceReceiptTimeout = portInit->announceReceiptTimeout;
    syncReceiptTimeout = portInit->syncReceiptTimeout;
    syncClocks = portInit->syncClocks;
    net_label = portInit->net_label;
    link_thread = thread_factory->createThread();
    if (link_thread == NULL) {
        GPTP_LOG_ERROR("Failed to create link_thread");
    }
    listening_thread = thread_factory->createThread();
    if (listening_thread == NULL) {
        GPTP_LOG_ERROR("Failed to create listening_thread");
    }
    sync_receipt_thresh = portInit->syncReceiptThreshold;
    wrongSeqIDCounter = 0;
    _peer_rate_offset = portInit->_peer_rate_offset;
    _peer_offset_init = false;
    ifindex = portInit->index;
    testMode = false;
    port_state = PTP_INITIALIZING;
    clock->registerPort(this, ifindex);
    qualified_announce = NULL;
    announce_sequence_id = 0;
    signal_sequence_id = 0;
    sync_sequence_id = 0;
    initialLogSyncInterval = portInit->initialLogSyncInterval;
    operLogPdelayReqInterval = &(portInit->operLogPdelayReqInterval);
    syncArrivalTimeDiffTolerance = portInit->syncArrivalTimeDiffTolerance;
    log_mean_announce_interval = 0;
    pdelay_count = 0;
    sct_shm_fd = portInit->sct_shm_fd;
    sct_buffer = portInit->sct_buffer;
    bypass_if_wait = portInit->bypass_if_wait;
    net_iface = NULL;
    asCapable = false;
    tsc_enable = portInit->tsc_enable;
    link_speed = INVALID_LINKSPEED;
    qgptp_rmgr_setport(this);
    memset(&counters, 0x0, sizeof(counters));
    memset(&syncInfo, 0x0, sizeof(syncMesaurementData_t));
    memset(&pdelayInfo, 0x0, sizeof(pDelayMeasurementData_t));

    if (sct_buffer) {
        memset(&sct_buffer->syncData, 0x0, sizeof(syncMesaurementData_t));
        memset(&sct_buffer->delayData, 0x0, sizeof(pDelayMeasurementData_t));
        memset(&sct_buffer->status, 0x0, sizeof(gptpStatsType_t));
        memset(&sct_buffer->syncInterval, 0x0, sizeof(syncInterval_t));
    }

    PLAT_strlcpy(counters.ifname, portInit->ifname, IF_NAMESIZE);
    stbMSyncLossThreshold = portInit->stbMSyncLossThreshold;
    memset(&timesync_diagstats, 0x0, sizeof(timesync_diagstats));

    if (portInit->stbMSyncLossThreshold > 0) {
        timesync_diagstats.driftCountValid = 1;
    }
}

CommonPort::~CommonPort()
{
    delete qualified_announce;
}

bool CommonPort::init_port( void )
{
    GPTP_LOG_INFO("Entry: %s", __func__);
    log_mean_sync_interval = initialLogSyncInterval;
    syncReceiptTimerLock = lock_factory->createLock(oslock_recursive);

    if (!syncReceiptTimerLock) {
        GPTP_LOG_ERROR("create syncReceiptTimerLock failure");
        return false;
    }

    syncIntervalTimerLock = lock_factory->createLock(oslock_recursive);

    if (!syncIntervalTimerLock) {
        GPTP_LOG_ERROR("create syncIntervalTimerLock failure");
        return false;
    }

    deferredSyncIntervalTimerLock = lock_factory->createLock(oslock_recursive);

    if (!deferredSyncIntervalTimerLock) {
        GPTP_LOG_ERROR("create deferredSyncIntervalTimerLock failure");
        return false;
    }

    announceIntervalTimerLock = lock_factory->createLock(oslock_recursive);

    if (!announceIntervalTimerLock) {
        GPTP_LOG_ERROR("create announceIntervalTimerLock failure");
        return false;
    }

    if (!OSNetworkInterfaceFactory::buildInterface
            ( &net_iface, factory_name_t("default"), net_label,
              _hw_timestamper, tsc_enable)) {
        return false;
    }

    this->net_iface->getLinkLayerAddress(&local_addr);
    clock->setClockIdentity(&local_addr);
    this->timestamper_init();
    port_identity.setClockIdentity(clock->getClockIdentity());
    port_identity.setPortNumber(&ifindex);
    return _init_port();
}

void CommonPort::timestamper_init( void )
{
    if ( _hw_timestamper != NULL ) {
        if ( !_hw_timestamper->HWTimestamper_init
                ( net_label, net_iface, tsc_enable)) {
            GPTP_LOG_ERROR
            ( "Failed to initialize hardware timestamper, "
              "falling back to software timestamping" );
            return;
        }
    }
}

void CommonPort::timestamper_deinit(  )
{
    if ( _hw_timestamper != NULL ) {
        if ( !_hw_timestamper->HWTimestamper_deinit
                ( net_label, &net_iface )) {
            GPTP_LOG_ERROR
            ( "Failed to deinitialize hardware timestamper" );
            return;
        }
    }
}

void CommonPort::timestamper_reset( void )
{
    if ( _hw_timestamper != NULL ) {
        _hw_timestamper->HWTimestamper_reset();
    }
}

PTPMessageAnnounce *CommonPort::calculateERBest( void )
{
    return qualified_announce;
}

void CommonPort::recommendState
( PortState state, bool changed_external_master )
{
    bool reset_sync = false;

    switch (state) {
        case PTP_MASTER:
            if ( getPortState() != PTP_MASTER ) {
                setPortState( PTP_MASTER );
                // Start announce receipt timeout timer
                // Start sync receipt timeout timer
                becomeMaster( true );
                reset_sync = true;
            }

            break;

        case PTP_SLAVE:
            if ( getPortState() != PTP_SLAVE ) {
                becomeSlave( true );
                reset_sync = true;
            } else {
                if ( changed_external_master ) {
                    GPTP_LOG_STATUS("Changed master!" );
                    clock->newSyntonizationSetPoint();
                    clock->updateFUPInfo();
                    reset_sync = true;
                }
            }

            break;

        default:
            GPTP_LOG_ERROR
            ("Invalid state change requested by call to "
             "1588Port::recommendState()");
            break;
    }

    if ( reset_sync ) {
        sync_count = 0;
    }

    return;
}

bool CommonPort::setRsync( RsyncStatus_t *Rsync )
{
    if (!Rsync) {
        return false;
    }

    if (Rsync->reverseSyncEnabled == 1) {
        enableRsync(Rsync->reverseSyncDomain, Rsync->reverseSyncRate);
    } else {
        disableRsync();
    }

    return true;
}


bool CommonPort::serializeState( void *buf, off_t *count )
{
    bool ret = true;

    if ( buf == NULL ) {
        *count = sizeof(asCapable) + sizeof(port_state) +
                 sizeof(one_way_delay) + sizeof(*operLogPdelayReqInterval) +
                 sizeof(_peer_rate_offset);
        return true;
    }

    if ( port_state != PTP_MASTER && port_state != PTP_SLAVE ) {
        *count = 0;
        ret = false;
        goto bail;
    }

    /* asCapable */
    if ( *count >= (off_t) sizeof( asCapable )) {
        memcpy( buf, &asCapable, sizeof( asCapable ));
        *count -= sizeof( asCapable );
        buf = ((char *)buf) + sizeof( asCapable );
    } else {
        *count = sizeof( asCapable ) - *count;
        ret = false;
    }

    /* Port State */
    if ( ret && *count >= (off_t) sizeof( port_state )) {
        memcpy( buf, &port_state, sizeof( port_state ));
        *count -= sizeof( port_state );
        buf = ((char *)buf) + sizeof( port_state );
    } else if ( ret == false ) {
        *count += sizeof( port_state );
    } else {
        *count = sizeof( port_state ) - *count;
        ret = false;
    }

    /* Link Delay */
    if ( ret && *count >= (off_t) sizeof( one_way_delay )) {
        memcpy( buf, &one_way_delay, sizeof( one_way_delay ));
        *count -= sizeof( one_way_delay );
        buf = ((char *)buf) + sizeof( one_way_delay );
    } else if ( ret == false ) {
        *count += sizeof( one_way_delay );
    } else {
        *count = sizeof( one_way_delay ) - *count;
        ret = false;
    }

    /* operLogPdelayReqInterval */
    if ( ret && *count >= (off_t) sizeof( *operLogPdelayReqInterval )) {
        memcpy( buf, operLogPdelayReqInterval, sizeof( *operLogPdelayReqInterval ));
        *count -= sizeof( *operLogPdelayReqInterval );
        buf = ((char *)buf) + sizeof( *operLogPdelayReqInterval );
    } else if ( ret == false ) {
        *count += sizeof( *operLogPdelayReqInterval );
    } else {
        *count = sizeof( *operLogPdelayReqInterval ) - *count;
        ret = false;
    }

    /* Neighbor Rate Ratio */
    if ( ret && *count >= (off_t) sizeof( _peer_rate_offset )) {
        memcpy( buf, &_peer_rate_offset, sizeof( _peer_rate_offset ));
        *count -= sizeof( _peer_rate_offset );
        buf = ((char *)buf) + sizeof( _peer_rate_offset );
    } else if ( ret == false ) {
        *count += sizeof( _peer_rate_offset );
    } else {
        *count = sizeof( _peer_rate_offset ) - *count;
        ret = false;
    }

bail:
    return ret;
}

bool CommonPort::restoreSerializedState
( void *buf, off_t *count )
{
    bool ret = true;

    /* asCapable */
    if ( *count >= (off_t) sizeof( asCapable )) {
        memcpy( &asCapable, buf, sizeof( asCapable ));
        *count -= sizeof( asCapable );
        buf = ((char *)buf) + sizeof( asCapable );
    } else {
        *count = sizeof( asCapable ) - *count;
        ret = false;
    }

    /* Port State */
    if ( ret && *count >= (off_t) sizeof( port_state )) {
        memcpy( &port_state, buf, sizeof( port_state ));
        *count -= sizeof( port_state );
        buf = ((char *)buf) + sizeof( port_state );
    } else if ( ret == false ) {
        *count += sizeof( port_state );
    } else {
        *count = sizeof( port_state ) - *count;
        ret = false;
    }

    /* Link Delay */
    if ( ret && *count >= (off_t) sizeof( one_way_delay )) {
        memcpy( &one_way_delay, buf, sizeof( one_way_delay ));
        *count -= sizeof( one_way_delay );
        buf = ((char *)buf) + sizeof( one_way_delay );
    } else if ( ret == false ) {
        *count += sizeof( one_way_delay );
    } else {
        *count = sizeof( one_way_delay ) - *count;
        ret = false;
    }

    /* operLogPdelayReqInterval */
    if ( ret && *count >= (off_t) sizeof( *operLogPdelayReqInterval )) {
        memcpy( operLogPdelayReqInterval, buf, sizeof( *operLogPdelayReqInterval ));
        *count -= sizeof( *operLogPdelayReqInterval );
        buf = ((char *)buf) + sizeof( *operLogPdelayReqInterval );
    } else if ( ret == false ) {
        *count += sizeof( *operLogPdelayReqInterval );
    } else {
        *count = sizeof( *operLogPdelayReqInterval ) - *count;
        ret = false;
    }

    /* Neighbor Rate Ratio */
    if ( ret && *count >= (off_t) sizeof( _peer_rate_offset )) {
        memcpy( &_peer_rate_offset, buf, sizeof( _peer_rate_offset ));
        *count -= sizeof( _peer_rate_offset );
        buf = ((char *)buf) + sizeof( _peer_rate_offset );
    } else if ( ret == false ) {
        *count += sizeof( _peer_rate_offset );
    } else {
        *count = sizeof( _peer_rate_offset ) - *count;
        ret = false;
    }

    return ret;
}

void CommonPort::startSyncReceiptTimer
( long long unsigned int waitTime )
{
    clock->getTimerQLock();
    syncReceiptTimerLock->lock();
    clock->deleteEventTimer( this, SYNC_RECEIPT_TIMEOUT_EXPIRES );
    clock->addEventTimer
    ( this, SYNC_RECEIPT_TIMEOUT_EXPIRES, waitTime );
    syncReceiptTimerLock->unlock();
    clock->putTimerQLock();
}

void CommonPort::stopSyncReceiptTimer( void )
{
    clock->getTimerQLock();
    syncReceiptTimerLock->lock();
    clock->deleteEventTimer( this, SYNC_RECEIPT_TIMEOUT_EXPIRES );
    syncReceiptTimerLock->unlock();
    clock->putTimerQLock();
}

void CommonPort::startSyncIntervalTimer
( long long unsigned int waitTime, Event e)
{
    if ( syncIntervalTimerLock->trylock() == oslock_fail ) {
        return;
    }

    clock->deleteEventTimerLocked(this, e);
    clock->addEventTimerLocked
    (this, e, waitTime);
    syncIntervalTimerLock->unlock();
}

void CommonPort::stopSyncIntervalTimer( Event e )
{
    clock->getTimerQLock();

    if ( syncIntervalTimerLock->trylock() == oslock_fail ) {
        return;
    }

    clock->deleteEventTimerLocked(this, e);
    syncIntervalTimerLock->unlock();
    clock->putTimerQLock();
}

void CommonPort::startDeferredSyncIntervalTimer(long long unsigned int waitTime, Event e)
{
    if (deferredSyncIntervalTimerLock->trylock() == oslock_fail) {
        return;
    }
    clock->deleteEventTimerLocked(this, e);
    clock->addEventTimerLocked(this, e, waitTime);
    deferredSyncIntervalTimerLock->unlock();
}

void CommonPort::stopDeferredSyncIntervalTimer(Event e)
{
    clock->getTimerQLock();
    if (deferredSyncIntervalTimerLock->trylock() == oslock_fail) {
        return;
    }
    clock->deleteEventTimerLocked(this, e);
    deferredSyncIntervalTimerLock->unlock();
    clock->putTimerQLock();
}

void CommonPort::startAnnounceIntervalTimer
( long long unsigned int waitTime )
{
    announceIntervalTimerLock->lock();
    clock->deleteEventTimerLocked
    ( this, ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES );
    clock->addEventTimerLocked
    ( this, ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES, waitTime );
    announceIntervalTimerLock->unlock();
}

bool CommonPort::processStateChange( Event e )
{
    bool changed_external_master;
    uint8_t LastEBestClockIdentity[PTP_CLOCK_IDENTITY_LENGTH];
    int number_ports, j;
    PTPMessageAnnounce *EBest = NULL;
    char EBestClockIdentity[PTP_CLOCK_IDENTITY_LENGTH];
    CommonPort **ports;

    // Nothing to do if we are slave only
    if ( clock->getPriority1() == 255 ) {
        return true;
    }

    clock->getPortList(number_ports, ports);
    /* Find EBest for all ports */
    j = 0;

    for (int i = 0; i < number_ports; ++i) {
        while (ports[j] == NULL) {
            ++j;
        }

        if ( ports[j]->getPortState() == PTP_DISABLED ||
                ports[j]->getPortState() == PTP_FAULTY ) {
            continue;
        }

        if ( EBest == NULL ) {
            EBest = ports[j]->calculateERBest();
        } else if ( ports[j]->calculateERBest() ) {
            if ( ports[j]->
                    calculateERBest()->isBetterThan(EBest)) {
                EBest = ports[j]->calculateERBest();
            }
        }
    }

    if (EBest == NULL) {
        return true;
    }

    /* Check if we've changed */
    clock->getLastEBestIdentity().
    getIdentityString( LastEBestClockIdentity );
    EBest->getGrandmasterIdentity( EBestClockIdentity );

    if ( memcmp( EBestClockIdentity, LastEBestClockIdentity,
                 PTP_CLOCK_IDENTITY_LENGTH ) != 0 ) {
        ClockIdentity newGM;
        changed_external_master = true;
        newGM.set((uint8_t *) EBestClockIdentity );
        clock->setLastEBestIdentity( newGM );
    } else {
        changed_external_master = false;
    }

    if ( clock->isBetterThan( EBest )) {
        // We're Grandmaster, set grandmaster info to me
        ClockIdentity clock_identity;
        unsigned char priority1;
        unsigned char priority2;
        PortIdentity portId;
        uint16_t portNumber = 0;
        ClockQuality clock_quality;
        getPortIdentity(portId);
        portId.getPortNumber(&portNumber);
        clock_identity = getClock()->getClockIdentity();
        getClock()->setGrandmasterClockIdentity(clock_identity, portNumber);
        priority1 = getClock()->getPriority1();
        getClock()->setGrandmasterPriority1( priority1 );
        priority2 = getClock()->getPriority2();
        getClock()->setGrandmasterPriority2( priority2 );
        clock_quality = getClock()->getClockQuality();
        getClock()->setGrandmasterClockQuality( clock_quality );
    }

    j = 0;

    for ( int i = 0; i < number_ports; ++i ) {
        while (ports[j] == NULL) {
            ++j;
        }

        if ( ports[j]->getPortState() ==
                PTP_DISABLED ||
                ports[j]->getPortState() ==
                PTP_FAULTY) {
            continue;
        }

        if (clock->isBetterThan(EBest)) {
            // We are the GrandMaster, all ports are master
            EBest = NULL;   // EBest == NULL : we were grandmaster
            ports[j]->recommendState( PTP_MASTER,
                                      changed_external_master );
        } else {
            if ((EBest) && ( EBest == ports[j]->calculateERBest())) {
                // The "best" Announce was received on this
                // port
                ClockIdentity clock_identity;
                unsigned char priority1;
                unsigned char priority2;
                PortIdentity portId;
                uint16_t portNumber = 0;
                ClockQuality *clock_quality;
                ports[j]->recommendState
                ( PTP_SLAVE, changed_external_master );
                ports[j]->getPortIdentity(portId);
                portId.getPortNumber(&portNumber);
                clock_identity =
                    EBest->getGrandmasterClockIdentity();
                getClock()->setGrandmasterClockIdentity(clock_identity, portNumber);
                priority1 = EBest->getGrandmasterPriority1();
                getClock()->setGrandmasterPriority1
                ( priority1 );
                priority2 =
                    EBest->getGrandmasterPriority2();
                getClock()->setGrandmasterPriority2
                ( priority2 );
                clock_quality =
                    EBest->getGrandmasterClockQuality();
                getClock()->setGrandmasterClockQuality
                (*clock_quality);
            } else {
                /* Otherwise we are the master because we have
                   sync'd to a better clock */
                ports[j]->recommendState
                (PTP_MASTER, changed_external_master);
            }
        }
    }

    return true;
}


bool CommonPort::processSyncAnnounceTimeout( Event e )
{
    // We're Grandmaster, set grandmaster info to me
    ClockIdentity clock_identity;
    unsigned char priority1;
    unsigned char priority2;
    PortIdentity portId;
    uint16_t portNumber = 0;
    ClockQuality clock_quality;
    getPortIdentity(portId);
    portId.getPortNumber(&portNumber);
    Timestamp system_time;
    Timestamp q_time;
    Timestamp boot_time;
    Timestamp device_time;
    uint32_t local_clock, nominal_clock_rate;

    // Nothing to do
    if ( clock->getPriority1() == 255 ) {
        return true;
    }

    // Restart timer
    if ( e == ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES ) {
        clock->addEventTimerLocked
        (this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES,
         (getannounceReceiptTimeoutMultiplier() *
          (unsigned long long)
          (pow((double)2, getAnnounceInterval()) *
           1000000000.0)));
    } else {
        startSyncReceiptTimer
        ((unsigned long long)
         (getsyncReceiptTimeoutMultiplier() *
          ((double) pow((double)2, getSyncInterval()) *
           1000000000.0)));
    }

    if ( getPortState() == PTP_MASTER ) {
        return true;
    }

    GPTP_LOG_STATUS(
        "*** %s Timeout Expired - Becoming Master",
        e == ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES ? "Announce" :
        "Sync" );
    clock_identity = getClock()->getClockIdentity();
    getClock()->setGrandmasterClockIdentity(clock_identity, portNumber);
    priority1 = getClock()->getPriority1();
    getClock()->setGrandmasterPriority1( priority1 );
    priority2 = getClock()->getPriority2();
    getClock()->setGrandmasterPriority2( priority2 );
    clock_quality = getClock()->getClockQuality();
    getClock()->setGrandmasterClockIdentity(clock_identity, portNumber);
    setPortState( PTP_MASTER );
    getDeviceTime( system_time, q_time, device_time, boot_time,
                   local_clock, nominal_clock_rate );
    (void) clock->calcLocalSystemClockRateDifference
    ( device_time, system_time, q_time, boot_time, nullptr, nullptr );
    setQualifiedAnnounce( NULL );
    clock->addEventTimerLocked
    ( this, SYNC_INTERVAL_TIMEOUT_EXPIRES,
      16000000 );
    startAnnounce();
    return true;
}

bool CommonPort::processEvent( Event e )
{
    bool ret;

    switch ( e ) {
        default:
            // Unhandled event
            ret = _processEvent( e );
            break;

        case POWERUP:
        case INITIALIZE:
            GPTP_LOG_DEBUG("Received POWERUP/INITIALIZE event");

            // If port has been configured as master or slave, run media
            // specific configuration. If it hasn't been configured
            // start listening for announce messages
            if ( clock->getPriority1() == 255 ||
                    port_state == PTP_SLAVE ) {
                becomeSlave( true );
            } else if ( port_state == PTP_MASTER ) {
                becomeMaster( true );
                uint16_t portNumber = 0;
                PortIdentity portId;
                getPortIdentity(portId);
                portId.getPortNumber(&portNumber);
                uint8_t gmClockid[PTP_CLOCK_IDENTITY_LENGTH];
                ClockIdentity clock_identity;
                clock_identity = getClock()->getClockIdentity();
                getClock()->getClockIdentity().getIdentityString(gmClockid);
                GPTP_LOG_DEBUG("GM Clock Identity :"
                               "%02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n",
                               gmClockid[0], gmClockid[1], gmClockid[2],
                               gmClockid[3], gmClockid[4], gmClockid[5],
                               gmClockid[6], gmClockid[7]);
                getClock()->setGrandmasterClockIdentity(clock_identity, portNumber);
            } else {
                clock->addEventTimerLocked(this, ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES,
                                           ANNOUNCE_RECEIPT_TIMEOUT_MULTIPLIER * pow(2.0,
                                                   getAnnounceInterval()) * 1000000000.0);
            }

            // Do any media specific initialization
            ret = _processEvent( e );
            break;
        case LINKDOWN:
            gptpDaemonServDeInit();
            ret = _processEvent( e );
            break;
        case LINKUP:
            GPTP_LOG_DEBUG("Received LINKUP event");
            if (!bypass_if_wait) {
                while (waitForInterface()) {

                    GPTP_LOG_DEBUG( "waitForInterface \n");
                }
                GPTP_LOG_INFO( "eth interface is up.. \n");
            } else {
                GPTP_LOG_INFO( "Bypass Ethernet check.. \n");
            }
            ret = _processEvent( e );
            gptpDaemonServInit();
            break;

        case STATE_CHANGE_EVENT:
            ret = _processEvent( e );

            // If this event wasn't handled in a media specific way, call
            // the default action
            if ( !ret ) {
                ret = processStateChange( e );
            }

            break;

        case ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES:
        case SYNC_RECEIPT_TIMEOUT_EXPIRES:
            if (e == ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES) {
                incCounter_ieee8021AsPortStatAnnounceReceiptTimeouts();
            } else { // (e == SYNC_RECEIPT_TIMEOUT_EXPIRES)
                incCounter_ieee8021AsPortStatRxSyncReceiptTimeouts();
                timelog_avnu_loss_of_sync_message();
                clock->setSyncStatus(false, port_state);

                if (sct_buffer) {
                    pthread_mutex_lock((pthread_mutex_t *) &sct_buffer->lock);
                    sct_buffer->status.gptp_status = GPTP_STATUS_TIMEOUT;
                    memset(&sct_buffer->syncData, 0x0, sizeof(syncMesaurementData_t));
                    memset(&syncInfo, 0x0, sizeof(syncMesaurementData_t));
                    pthread_mutex_unlock((pthread_mutex_t *) &sct_buffer->lock);
                }

                /* AutoDID syncreceipt timeout*/
                timesync_diagstats.timeSyncStatusDID |= AUTO_SYNC_TIMEOUT;
            }

            ret = _processEvent( e );

            // If this event wasn't handled in a media specific way, call
            // the default action
            if ( !ret ) {
                ret = processSyncAnnounceTimeout( e );
            }

            break;

        case ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES:
            GPTP_LOG_DEBUG("ANNOUNCE_INTERVAL_TIMEOUT_EXPIRES occured");

            // Send an announce message
            if ( asCapable) {
                PTPMessageAnnounce *annc =
                    new PTPMessageAnnounce(this);
                if (annc == NULL) {
                    GPTP_LOG_ERROR("Failed to allocate PTPMessageAnnounce");
                } else {
                    PortIdentity dest_id;
                    PortIdentity gmId;
                    ClockIdentity clock_id = clock->getClockIdentity();
                    gmId.setClockIdentity(clock_id);
                    getPortIdentity( dest_id );
                    annc->setPortIdentity( &dest_id );
                    annc->sendPort( this, NULL );
                    delete annc;
                }
            }

            startAnnounceIntervalTimer
            ((uint64_t)( pow((double)2, getAnnounceInterval()) *
                         1000000000.0 ));
            ret = true;
            break;

        case SYNC_INTERVAL_TIMEOUT_EXPIRES:
            GPTP_LOG_DEBUG("SYNC_INTERVAL_TIMEOUT_EXPIRES occured");
            // If asCapable is true attempt some media specific action
            ret = true;

            if ( asCapable ) {
                ret = _processEvent( e );
            }

            /* Do getDeviceTime() after transmitting sync frame
               causing an update to local/system timestamp */
            {
                Timestamp system_time;
                Timestamp q_time;
                Timestamp boot_time;
                Timestamp device_time;
                uint32_t local_clock, nominal_clock_rate;
                FrequencyRatio local_system_freq_offset = 1;
                FrequencyRatio local_q_freq_offset = 1;
                FrequencyRatio local_boot_freq_offset = 1;
                int64_t local_system_offset;
                int64_t local_q_offset;
                int64_t local_boot_offset;
                getDeviceTime
                ( system_time, q_time, device_time, boot_time,
                  local_clock, nominal_clock_rate );
                GPTP_LOG_VERBOSE
                ( "port::processEvent(): System time: %u,%u "
                  "Device Time: %u,%u",
                  system_time.seconds_ls,
                  system_time.nanoseconds,
                  device_time.seconds_ls,
                  device_time.nanoseconds );
                local_system_offset =
                    TIMESTAMP_TO_NS(system_time) -
                    TIMESTAMP_TO_NS(device_time);
                local_q_offset =
                    TIMESTAMP_TO_NS(q_time) -
                    TIMESTAMP_TO_NS(device_time);
                local_boot_offset =
                    TIMESTAMP_TO_NS(boot_time) -
                    TIMESTAMP_TO_NS(device_time);
                local_system_freq_offset =
                    clock->calcLocalSystemClockRateDifference
                    ( device_time, system_time, q_time, boot_time, &local_q_freq_offset,
                      &local_boot_freq_offset );
                clock->setMasterOffset
                ( this, 0, device_time, 1.0,
                  local_system_offset, system_time,
                  local_system_freq_offset,
                  local_q_offset, q_time,
                  local_q_freq_offset,
                  local_boot_offset, boot_time,
                  local_boot_freq_offset, getSyncCount(),
                  pdelay_count, port_state, asCapable, SYNC_INTERVAL_TIMEOUT_PATH);
            }
            // Call media specific action for completed sync
            syncDone();
            // Restart the timer
            startSyncIntervalTimer
            ((uint64_t)( pow((double)2, getSyncInterval()) *
                         1000000000.0 ), SYNC_INTERVAL_TIMEOUT_EXPIRES);
            break;

        case RSYNC_INTERVAL_TIMEOUT_EXPIRES: {
                GPTP_LOG_VERBOSE("RSYNC_INTERVAL_TIMEOUT_EXPIRES occured, getSyncInterval = %d, getRSyncRate = %f",
                                 getSyncInterval(), clock->getRSyncRate());
                ret = true;
                ret = _processEvent( e );
                startSyncIntervalTimer
                ((uint64_t)( pow((double)2, getSyncInterval()) *
                             1000000000.0 * clock->getRSyncRate()), RSYNC_INTERVAL_TIMEOUT_EXPIRES);
                break;
            }
    }

    return ret;
}

void CommonPort::getDeviceTime
( Timestamp &system_time, Timestamp &mono_time, Timestamp &device_time,
  Timestamp &boot_time,
  uint32_t &local_clock, uint32_t &nominal_clock_rate )
{
    if (_hw_timestamper) {
        _hw_timestamper->HWTimestamper_gettime
        ( &system_time, &mono_time, &device_time, &boot_time,
          &local_clock, &nominal_clock_rate );
    } else {
        device_time = system_time = boot_time = clock->getSystemTime();
        local_clock = nominal_clock_rate = 0;
    }

    return;
}

void CommonPort::getCurrentPtpTime( uint64_t* ptp_time )
{
    if (_hw_timestamper) {
        _hw_timestamper->HWTimestamper_getptptime
        ( ptp_time );
    }
}

void CommonPort::startAnnounce()
{
    startAnnounceIntervalTimer(16000000);
}

int CommonPort::getTimestampVersion()
{
    if (_hw_timestamper == NULL) {
        GPTP_LOG_ERROR("_hw_timestamper is NULL in getTimestampVersion");
        return -1;
    }
    return _hw_timestamper->getVersion();
}

bool CommonPort::_adjustClockRate( FrequencyRatio freq_offset )
{
    if ( _hw_timestamper ) {
        return _hw_timestamper->HWTimestamper_adjclockrate
               ((float) freq_offset );
    } else {
        GPTP_LOG_ERROR("Rate:_hw_timestamper is null\n");
    }

    return false;
}

bool CommonPort::_adjustClockRate( const char *if_name,
                                   FrequencyRatio freq_offset )
{
    if ( _hw_timestamper ) {
        return _hw_timestamper->HWTimestamper_adjclockrate
               (  if_name, (float) freq_offset );
    } else {
        GPTP_LOG_ERROR("Rate:_hw_timestamper is null\n");
    }

    return false;
}

void CommonPort::getExtendedError( char *msg )
{
    if (_hw_timestamper) {
        _hw_timestamper->HWTimestamper_get_extderror(msg);
        return;
    }

    *msg = '\0';
}

bool CommonPort::adjustClockPhase( int64_t phase_adjust )
{
    if ( _hw_timestamper ) {
        return _hw_timestamper->
               HWTimestamper_adjclockphase( phase_adjust );
    } else {
        GPTP_LOG_ERROR("phase:_hw_timestamper is null\n");
    }

    return false;
}

bool CommonPort::adjustClockPhase(const char *if_name, int64_t phase_adjust )
{
    if ( _hw_timestamper ) {
        return _hw_timestamper->
               HWTimestamper_adjclockphase( if_name, phase_adjust );
    } else {
        GPTP_LOG_ERROR("phase:_hw_timestamper is null\n");
    }

    return false;
}

Timestamp CommonPort::getTxPhyDelay( uint32_t link_speed ) const
{
    if ( phy_delay->count( link_speed ) != 0 )
        return Timestamp
               ( phy_delay->at(link_speed).get_tx_delay(), 0, 0 );

    return Timestamp(0, 0, 0);
}

Timestamp CommonPort::getRxPhyDelay( uint32_t link_speed ) const
{
    if ( phy_delay->count( link_speed ) != 0 )
        return Timestamp
               ( phy_delay->at(link_speed).get_rx_delay(), 0, 0 );

    return Timestamp(0, 0, 0);
}

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

Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

#include <ieee1588.hpp>
#include <avbts_clock.hpp>
#include <avbts_oslock.hpp>
#include <avbts_ostimerq.hpp>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <errno.h>

#ifdef ANDROID
#include <sys/timex.h>
#endif

#define TAI_CLOCK 0

std::string ClockIdentity::getIdentityString()
{
    uint8_t cid[PTP_CLOCK_IDENTITY_LENGTH];
    getIdentityString(cid);
    char scid[PTP_CLOCK_IDENTITY_LENGTH * 3 + 1];
    char* pscid = scid;

    for (unsigned i = 0; i < PTP_CLOCK_IDENTITY_LENGTH; ++i) {
        unsigned byte = cid[i];
        PLAT_snprintf(pscid, 4, "%2.2X:", byte);
        pscid += 3;
    }

    scid[PTP_CLOCK_IDENTITY_LENGTH * 3 - 1] = '\0';
    return std::string(scid);
}

void ClockIdentity::set(LinkLayerAddress * addr)
{
    uint64_t tmp1 = 0;
    uint32_t tmp2;
    addr->toOctetArray((uint8_t *) & tmp1);
    tmp2 = tmp1 & 0xFFFFFF;
    tmp1 >>= 24;
    tmp1 <<= 16;
    tmp1 |= 0xFEFF;
    tmp1 <<= 24;
    tmp1 |= tmp2;
    memcpy(id, &tmp1, PTP_CLOCK_IDENTITY_LENGTH);
}

IEEE1588Clock::IEEE1588Clock
( bool forceOrdinarySlave, bool syntonize, uint8_t priority1,
  uint8_t priority2, uint8_t clockClass, OSTimerQueueFactory *timerq_factory,
  OS_IPC *ipc,
  OSLockFactory *lock_factory )
{
    this->priority1 = priority1;
    this->priority2 = priority2;
    number_ports = 0;
    this->forceOrdinarySlave = forceOrdinarySlave;
    /*TODO: Make the values below configurable*/
    clock_quality.clockAccuracy = 0x22;
    //clock_quality.cq_class = 248;
    clock_quality.cq_class = clockClass;
    clock_quality.offsetScaledLogVariance = 0x436A;
    time_source = 160;
    domain_number = 0;
    rsync_domain_number = 1;
    rsync_rate = RSYNC_RATE_DEFAULT;
    GPTP_LOG_INFO("rsync_domain_number: %d, rsync_rate %f \n", rsync_domain_number,
                  rsync_rate);
    _syntonize = syntonize;
    _new_syntonization_set_point = false;
    _ppm = 0;
    _phase_error_violation = 0;
    _freq_valid = 0;
    _master_local_freq_offset_init = false;
    _local_system_freq_offset_init = false;
    this->ipc = ipc;
    memset( &LastEBestIdentity, 0xFF, sizeof( LastEBestIdentity ));
    timerq_lock = lock_factory->createLock( oslock_recursive );
    if (timerq_lock == NULL) {
        GPTP_LOG_ERROR("Failed to create timerq_lock");
    }
    // This should be done LAST!! to pass fully initialized clock object
    timerq = timerq_factory->createOSTimerQueue( this );
    if (timerq == NULL) {
        GPTP_LOG_ERROR("Failed to create timerq");
    }
    fup_info = new FollowUpTLV();
    if (fup_info == NULL) {
        GPTP_LOG_ERROR("Failed to allocate fup_info");
    }
    fup_status = new FollowUpTLV();
    if (fup_status == NULL) {
        GPTP_LOG_ERROR("Failed to allocate fup_status");
    }
    return;
}

bool IEEE1588Clock::serializeState( void *buf, off_t *count )
{
    bool ret = true;

    if ( buf == NULL ) {
        *count = sizeof( _master_local_freq_offset ) + sizeof(
                     _local_system_freq_offset ) + sizeof( LastEBestIdentity );
        return true;
    }

    // Master-Local Frequency Offset
    if ( *count >= (off_t) sizeof( _master_local_freq_offset )) {
        memcpy
        ( buf, &_master_local_freq_offset,
          sizeof( _master_local_freq_offset ));
        *count -= sizeof( _master_local_freq_offset );
        buf = ((char *)buf) + sizeof( _master_local_freq_offset );
    } else {
        *count = sizeof( _master_local_freq_offset ) - *count;
        ret = false;
    }

    // Local-System Frequency Offset
    if ( ret && *count >= (off_t) sizeof( _local_system_freq_offset )) {
        memcpy
        ( buf, &_local_system_freq_offset, (off_t)
          sizeof( _local_system_freq_offset ));
        *count -= sizeof( _local_system_freq_offset );
        buf = ((char *)buf) + sizeof( _local_system_freq_offset );
    } else if ( ret == false ) {
        *count += sizeof( _local_system_freq_offset );
    } else {
        *count = sizeof( _local_system_freq_offset ) - *count;
        ret = false;
    }

    // LastEBestIdentity
    if ( ret && *count >= (off_t) sizeof( LastEBestIdentity )) {
        memcpy( buf, &LastEBestIdentity, (off_t) sizeof( LastEBestIdentity ));
        *count -= sizeof( LastEBestIdentity );
        buf = ((char *)buf) + sizeof( LastEBestIdentity );
    } else if ( ret == false ) {
        *count += sizeof( LastEBestIdentity );
    } else {
        *count = sizeof( LastEBestIdentity ) - *count;
        ret = false;
    }

    return ret;
}

bool IEEE1588Clock::restoreSerializedState( void *buf, off_t *count )
{
    bool ret = true;

    /* Master-Local Frequency Offset */
    if ( *count >= (off_t) sizeof( _master_local_freq_offset )) {
        memcpy
        ( &_master_local_freq_offset, buf,
          sizeof( _master_local_freq_offset ));
        *count -= sizeof( _master_local_freq_offset );
        buf = ((char *)buf) + sizeof( _master_local_freq_offset );
    } else {
        *count = sizeof( _master_local_freq_offset ) - *count;
        ret = false;
    }

    /* Local-System Frequency Offset */
    if ( ret && *count >= (off_t) sizeof( _local_system_freq_offset )) {
        memcpy
        ( &_local_system_freq_offset, buf,
          sizeof( _local_system_freq_offset ));
        *count -= sizeof( _local_system_freq_offset );
        buf = ((char *)buf) + sizeof( _local_system_freq_offset );
    } else if ( ret == false ) {
        *count += sizeof( _local_system_freq_offset );
    } else {
        *count = sizeof( _local_system_freq_offset ) - *count;
        ret = false;
    }

    /* LastEBestIdentity */
    if ( ret && *count >= (off_t) sizeof( LastEBestIdentity )) {
        memcpy( &LastEBestIdentity, buf, sizeof( LastEBestIdentity ));
        *count -= sizeof( LastEBestIdentity );
        buf = ((char *)buf) + sizeof( LastEBestIdentity );
    } else if ( ret == false ) {
        *count += sizeof( LastEBestIdentity );
    } else {
        *count = sizeof( LastEBestIdentity ) - *count;
        ret = false;
    }

    return ret;
}

Timestamp IEEE1588Clock::getSystemTime(void)
{
    return (Timestamp(0, 0, 0));
}

void timerq_handler(void *arg)
{
    event_descriptor_t *event_descriptor = (event_descriptor_t *) arg;
    event_descriptor->port->processEvent(event_descriptor->event);
}

void IEEE1588Clock::addEventTimer
( CommonPort *target, Event e, unsigned long long time_ns )
{
    event_descriptor_t *event_descriptor = new event_descriptor_t();
    if (event_descriptor == NULL) {
        GPTP_LOG_ERROR("Failed to allocate event_descriptor in addEventTimer");
        return;
    }
    event_descriptor->event = e;
    event_descriptor->port = target;
    timerq->addEvent
    ((unsigned)(time_ns / 1000), (int)e, timerq_handler, (void**)&event_descriptor,
     true, NULL, true, NULL);
}


void IEEE1588Clock::addTimer
(unsigned long long time_ns, timeirq_handler func, void *arg, bool oneshot,
 timer_t *timer_handle)
{
    timerq->addEvent
    ((unsigned)(time_ns / 1000), 0, func, (void**)&arg, false, NULL, oneshot,
     &timer_handle);
}


void IEEE1588Clock::addEventTimerLocked
( CommonPort *target, Event e, unsigned long long time_ns )
{
    if ( getTimerQLock() == oslock_fail ) {
        return;
    }

    addEventTimer( target, e, time_ns );

    if ( putTimerQLock() == oslock_fail ) {
        return;
    }
}

void IEEE1588Clock::setGrandmasterClockIdentity(ClockIdentity id,
        uint16_t portNumber)
{
    if (id != grandmaster_clock_identity) {
        GPTP_LOG_INFO("New Grandmaster \"%s\" (previous \"%s\")",
                      id.getIdentityString().c_str(),
                      grandmaster_clock_identity.getIdentityString().c_str());
        grandmaster_clock_identity = id;

        if (ipc != NULL) {
            ipc->updateGmId(grandmaster_clock_identity, portNumber);
        }
    }
}

void IEEE1588Clock::setSyncStatus(bool is_sync, PortState port_state)
{
    if (ipc != NULL) {
        ipc->updateSyncStatus(is_sync, port_state);
    }
}

void IEEE1588Clock::setProxyMode(int32_t proxy_value)
{
    if (ipc != NULL) {
        ipc->setProxyMode(proxy_value);
    }
}

void IEEE1588Clock::updateEtherLinkState(EtherPortLinkState_t LinkState)
{
    if (ipc != NULL)
    {
        ipc->updateEtherLinkState(LinkState);
    }
}

bool IEEE1588Clock::getSyncStatus(void)
{
    if (ipc != NULL) {
        return ipc->getSyncStatus();
    }

    return 0;
}

void IEEE1588Clock::deleteEventTimer
( CommonPort *target, Event event )
{
    timerq->cancelEvent((int)event, NULL);
}

void IEEE1588Clock::deleteTimer
( timer_t *rgptp_pulse_timerId )
{
    timerq->cancelTimer(&rgptp_pulse_timerId);
}


void IEEE1588Clock::deleteEventTimerLocked
( CommonPort *target, Event event )
{
    if ( getTimerQLock() == oslock_fail ) {
        return;
    }

    timerq->cancelEvent((int)event, NULL);

    if ( putTimerQLock() == oslock_fail ) {
        return;
    }
}

FrequencyRatio IEEE1588Clock::calcLocalSystemClockRateDifference(
    Timestamp local_time, Timestamp system_time, Timestamp q_time,
    Timestamp boot_time,
    FrequencyRatio *local_q_freq_offset, FrequencyRatio *local_boot_freq_offset )
{
    unsigned long long inter_system_time;
    unsigned long long inter_q_time;
    unsigned long long inter_boot_time;
    unsigned long long inter_local_time;
    FrequencyRatio ptp_offset;
    FrequencyRatio ptp_offset_q;
    FrequencyRatio ptp_offset_b;
    GPTP_LOG_DEBUG( "Calculated local to system clock rate difference" );

    if ( !_local_system_freq_offset_init ) {
        _prev_system_time = system_time;
        _prev_q_time = q_time;
        _prev_boot_time = boot_time;
        _prev_local_time = local_time;
        _local_system_freq_offset_init = true;
        return 1.0;
    }

    inter_system_time =
        TIMESTAMP_TO_NS(system_time) - TIMESTAMP_TO_NS(_prev_system_time);
    inter_q_time =
        TIMESTAMP_TO_NS(q_time) - TIMESTAMP_TO_NS(_prev_q_time);
    inter_boot_time =
        TIMESTAMP_TO_NS(boot_time) - TIMESTAMP_TO_NS(_prev_boot_time);
    inter_local_time  =
        TIMESTAMP_TO_NS(local_time) -  TIMESTAMP_TO_NS(_prev_local_time);

    if ( inter_system_time != 0 ) {
        ptp_offset = ((FrequencyRatio)inter_local_time) / inter_system_time;
    } else {
        ptp_offset = 1.0;
    }

    if ( inter_q_time != 0 ) {
        ptp_offset_q = ((FrequencyRatio)inter_local_time) / inter_q_time;
    } else {
        ptp_offset_q = 1.0;
    }

    if ( inter_boot_time != 0 ) {
        ptp_offset_b = ((FrequencyRatio)inter_local_time) / inter_boot_time;
    } else {
        ptp_offset_b = 1.0;
    }

    // Check for jumps in system time or local time
    if ((fabs(ptp_offset) < MIN_LS_RATIO) || (fabs(ptp_offset) > MAX_LS_RATIO)) {
        GPTP_LOG_WARNING("Local to system clock ratio (%Lf) exceeding threshold",
                         ptp_offset);
        ptp_offset = 1.0;
    }

    if ((fabs(ptp_offset_q) < MIN_LS_RATIO)
            || (fabs(ptp_offset_q) > MAX_LS_RATIO)) {
        GPTP_LOG_WARNING("Local to qtime clock ratio (%Lf) exceeding threshold",
                         ptp_offset_q);
        ptp_offset_q = 1.0;
    }

    if ((fabs(ptp_offset_b) < MIN_LS_RATIO)
            || (fabs(ptp_offset_b) > MAX_LS_RATIO)) {
        GPTP_LOG_WARNING("Local to boottime clock ratio (%Lf) exceeding threshold",
                         ptp_offset_b);
        ptp_offset_b = 1.0;
    }

    /*GPTP_LOG_WARNING("Local-system clock ratio = %Lf, local-mono clock ratio = %Lf",
            ppt_offset, ppt_offset_mono);*/

    _prev_system_time = system_time;
    _prev_q_time = q_time;
    _prev_boot_time = boot_time;
    _prev_local_time = local_time;

    if (local_q_freq_offset != nullptr) {
        *local_q_freq_offset = ptp_offset_q;
    }

    if (local_boot_freq_offset != nullptr) {
        *local_boot_freq_offset = ptp_offset_b;
    }

    return ptp_offset;
}



FrequencyRatio IEEE1588Clock::calcMasterLocalClockRateDifference(
    Timestamp master_time, Timestamp sync_time )
{
    unsigned long long inter_sync_time;
    unsigned long long inter_master_time;
    FrequencyRatio ppt_offset;
    GPTP_LOG_DEBUG( "Calculated master to local clock rate difference" );

    if ( !_master_local_freq_offset_init ) {
        _prev_sync_time = sync_time;
        _prev_master_time = master_time;
        _master_local_freq_offset_init = true;
        return 1.0;
    }

    inter_sync_time =
        TIMESTAMP_TO_NS(sync_time) - TIMESTAMP_TO_NS(_prev_sync_time);
    uint64_t master_time_ns = TIMESTAMP_TO_NS(master_time);
    uint64_t prev_master_time_ns = TIMESTAMP_TO_NS(_prev_master_time);
    inter_master_time = master_time_ns - prev_master_time_ns;

    if ( inter_sync_time != 0 ) {
        ppt_offset = ((FrequencyRatio)inter_master_time) / inter_sync_time;
    } else {
        ppt_offset = 1.0;
    }

    if ( master_time_ns < prev_master_time_ns ) {
        GPTP_LOG_ERROR("Negative time jump detected - inter_master_time: %lld, inter_sync_time: %lld, incorrect ppt_offset: %Lf",
                       inter_master_time, inter_sync_time, ppt_offset);
        _master_local_freq_offset_init = false;
        return NEGATIVE_TIME_JUMP;
    }

    _prev_sync_time = sync_time;
    _prev_master_time = master_time;
    return ppt_offset;
}

#define AVERAGE_WINDOW 4 //TODO: adjust as needed, probably too wide a window
ValueAverage_int64 local_system_offset_avg(AVERAGE_WINDOW);
ValueAverage_FR local_system_freq_offset_avg(AVERAGE_WINDOW);
ValueAverage_int64 local_q_offset_avg(AVERAGE_WINDOW);
ValueAverage_FR local_q_freq_offset_avg(AVERAGE_WINDOW);
ValueAverage_int64 local_boot_offset_avg(AVERAGE_WINDOW);
ValueAverage_FR local_boot_freq_offset_avg(AVERAGE_WINDOW);


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
        GPTP_LOG_ERROR("failed to realtime_adjust_offset %s", strerror(errno));
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
        GPTP_LOG_ERROR("failed to realtime_adjust_freq %s", strerror(errno));
        return -1;
    }

    return 0;
}

#if TAI_CLOCK
int tai_adjust(long long offset)
{
    struct timex tx;
    int ret;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_TAI;
    tx.constant = offset;
    ret = clock_adjtime(CLOCK_REALTIME, &tx);

    if (ret < 0) {
        GPTP_LOG_ERROR("failed to adjust TAI offset %s", strerror(errno));
        return ret;
    }

    return 0;
}
#endif

void synchronize_clocks(CommonPort *port)
{
    uint8_t syncClocks = port->getSyncClocks();
    uint64_t curr_gptp = 0;
    static int ppm_miss_count = 0;

    if (syncClocks & 0x1) {
        uint64_t curr_real = 0;
        int64_t delta_real = 0;
        static uint64_t prev_gptp_time = 0;
        static uint64_t prev_real_time = 0;
        static float _ppm = 0;
        struct timespec real;
        long double phase_error;
        port->getCurrentPtpTime( &curr_gptp );
        clock_gettime(CLOCK_REALTIME, &real);
        curr_real = (real.tv_sec) * 1000000000LL + real.tv_nsec;
        delta_real = curr_real - curr_gptp;
        phase_error = (long double) - delta_real;

        if ((fabsl(phase_error) > PHASE_ERROR_THRESHOLD) || prev_gptp_time == 0
                || ppm_miss_count > 10) {
            realtime_adjust_offset(phase_error);
        } else {
            FrequencyRatio freq_offset = 0;
            freq_offset = ((FrequencyRatio)(curr_gptp - prev_gptp_time)) /
                          (curr_real - prev_real_time);

            // Check for jumps in REAL time or gptp time
            if ((fabs(freq_offset) < MIN_LS_RATIO) || (fabs(freq_offset) > MAX_LS_RATIO)) {
                GPTP_LOG_WARNING("Real to Gptp clock ratio (%Lf) exceeding threshold %lld %lld",
                                 freq_offset, (curr_real - prev_real_time), (curr_gptp - prev_gptp_time));
                freq_offset = 1.0;
            } else {
                GPTP_LOG_DEBUG("Real to Gptp clock ratio (%Lf) delta %lld %lld",
                               freq_offset, (curr_real - prev_real_time), (curr_gptp - prev_gptp_time));
            }

            float syncPerSec = (float)(1.0 / pow((float)2, port->getSyncInterval()));
            _ppm += (float) ((INTEGRAL * syncPerSec * phase_error) + PROPORTIONAL * ((
                                 freq_offset - 1.0) * 1000000));
            GPTP_LOG_DEBUG("phase_error = %Lf, ppm = %f", phase_error, _ppm );

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

        prev_gptp_time = curr_gptp;
        prev_real_time = curr_real;
        GPTP_LOG_DEBUG("curr_gptp %lld curr_real %lld delta_real %lld",
                       curr_gptp, curr_real, delta_real);
#if TAI_CLOCK
        uint64_t curr_tai = 0;
        int64_t delta_tai = 0;
        struct timespec tai;
        port->getCurrentPtpTime( &curr_gptp );
        clock_gettime(CLOCK_TAI, &tai);
        curr_tai = (tai.tv_sec) * 1000000000LL + tai.tv_nsec;
        delta_tai = curr_gptp - curr_tai;
        GPTP_LOG_STATUS("curr_gptp %lld curr_tai %lld delta_tai %lld",
                        curr_gptp, curr_tai, delta_tai);
#endif
    }
}


void IEEE1588Clock::setMasterOffset
( CommonPort *port, int64_t master_local_offset,
  Timestamp local_time, FrequencyRatio master_local_freq_offset,
  int64_t local_system_offset, Timestamp system_time,
  FrequencyRatio local_system_freq_offset,
  int64_t local_q_offset, Timestamp q_time,
  FrequencyRatio local_q_freq_offset,
  int64_t local_boot_offset, Timestamp boot_time,
  FrequencyRatio local_boot_freq_offset, unsigned sync_count,
  unsigned pdelay_count, PortState port_state, bool asCapable,
  uint32_t process_path)
{
    uint64_t curr_gptp = 0;
    _master_local_freq_offset = master_local_freq_offset;
    _local_system_freq_offset = local_system_freq_offset;
    static bool initialdrift = false;
    static int prev_rsync_state = 0;
    RsyncStatus_t rSync;

    if (port->getTestMode()) {
        GPTP_LOG_STATUS("Clock offset:%lld   Clock rate ratio:%Lf   Sync Count:%u   PDelay Count:%u",
                        master_local_offset, master_local_freq_offset, sync_count, pdelay_count);
    }

    if (port->sct_buffer) {
        pthread_mutex_lock((pthread_mutex_t *) &port->sct_buffer->lock);
        port->sct_buffer->syncInterval.sync_interval = port->getSyncInterval();
        port->sct_buffer->syncInterval.pdelay_interval =
            port->getoperLogPdelayReqInterval();
        pthread_mutex_unlock((pthread_mutex_t *) &port->sct_buffer->lock);
    }

    if ( ipc != NULL ) {
        uint8_t grandmaster_id[PTP_CLOCK_IDENTITY_LENGTH];
        uint8_t clock_id[PTP_CLOCK_IDENTITY_LENGTH];
        PortIdentity port_identity;
        uint16_t port_number;
        grandmaster_clock_identity.getIdentityString(grandmaster_id);
        clock_identity.getIdentityString(clock_id);
        port->getPortIdentity(port_identity);
        port_identity.getPortNumber(&port_number);

        // Limit freq offset to reasonable values. First few values can be
        // unreasonably large/small during initial sync which would
        // affect the average value for a while.
        if ((local_system_freq_offset < (1.0 - FREQ_OFFSET_MAX)) ||
                (local_system_freq_offset > (1.0 + FREQ_OFFSET_MAX)) ) {
            local_system_freq_offset = 1.0;
        }

        if ((local_q_freq_offset < (1.0 - FREQ_OFFSET_MAX)) ||
                (local_q_freq_offset > (1.0 + FREQ_OFFSET_MAX)) ) {
            local_q_freq_offset = 1.0;
        }

        if ((local_boot_freq_offset < (1.0 - FREQ_OFFSET_MAX)) ||
                (local_boot_freq_offset > (1.0 + FREQ_OFFSET_MAX)) ) {
            local_boot_freq_offset = 1.0;
        }

        local_system_offset_avg.push(local_system_offset);
        local_system_freq_offset_avg.push(local_system_freq_offset);
        local_q_offset_avg.push(local_q_offset);
        local_q_freq_offset_avg.push(local_q_freq_offset);
        local_boot_offset_avg.push(local_boot_offset);
        local_boot_freq_offset_avg.push(local_boot_freq_offset);

        if (port->getTestMode()) {
            GPTP_LOG_STATUS("MASTER Clock offset:%lld   Clock rate ratio:%Lf   Sync Count:%u   PDelay Count:%u",
                            master_local_offset, master_local_freq_offset, sync_count, pdelay_count);
            GPTP_LOG_STATUS("SYSTEM Clock offset:%lld  (avg:%lld)  Clock rate ratio:%Lf  avg(%Lf)   Sync Count:%u   PDelay Count:%u",
                            local_system_offset, local_system_offset_avg.get(), local_system_freq_offset,
                            local_system_freq_offset_avg.get(), sync_count, pdelay_count);
            GPTP_LOG_STATUS("QTIMER Clock offset:%lld  (avg:%lld)  Clock rate ratio:%Lf  avg(%Lf)   Sync Count:%u   PDelay Count:%u",
                            local_q_offset, local_q_offset_avg.get(), local_q_freq_offset,
                            local_q_freq_offset_avg.get(), sync_count, pdelay_count);
            GPTP_LOG_STATUS("Boot Clock offset:%lld  (avg:%lld)  Clock rate ratio:%Lf  avg(%Lf)   Sync Count:%u   PDelay Count:%u",
                            local_boot_offset, local_boot_offset_avg.get(), local_boot_freq_offset,
                            local_boot_freq_offset_avg.get(), sync_count, pdelay_count);
        }

        port->setClockRateRatio(master_local_freq_offset);
        ipc->update(
            master_local_offset, local_system_offset_avg.get(), local_q_offset_avg.get(),
            local_boot_offset_avg.get(),
            master_local_freq_offset, local_system_freq_offset_avg.get(),
            local_q_freq_offset_avg.get(), local_boot_freq_offset_avg.get(),
            TIMESTAMP_TO_NS(local_time),
            sync_count, pdelay_count, port_state, asCapable, &rSync, process_path);

        if (prev_rsync_state != rSync.reverseSyncEnabled) {
            port->setRsync(&rSync);
        }

        prev_rsync_state = rSync.reverseSyncEnabled;

        if (port->getTestMode()) {
            GPTP_LOG_STATUS("%s:%d reverseSyncEnabled = %d reverseSyncRate = %lf reverseSyncDomain = %d\n",
                            __func__, __LINE__, rSync.reverseSyncEnabled, rSync.reverseSyncRate,
                            rSync.reverseSyncDomain );
        }

        ipc->update_grandmaster(
            grandmaster_id, domain_number);
        ipc->update_network_interface(
            clock_id, priority1,
            clock_quality.cq_class, clock_quality.offsetScaledLogVariance,
            clock_quality.clockAccuracy,
            priority2, domain_number,
            port->getSyncInterval(),
            port->getAnnounceInterval(),
            0, // TODO:  Was port->getPDelayInterval() before refactoring.  What do we do now?
            port_number);
    }

    if ( master_local_offset == 0 && master_local_freq_offset == 1.0 ) {
        synchronize_clocks(port);
        return;
    }

    if ( _syntonize ) {
       if ( _new_syntonization_set_point
                || _phase_error_violation > PHASE_ERROR_MAX_COUNT  || _freq_valid<0 ) {
            _new_syntonization_set_point = false;
            _phase_error_violation = 0;
            /* Make sure that there are no transmit operations
               in progress */
            getTxLockAll();

            if (port->getTestMode()) {
                GPTP_LOG_STATUS("Adjust clock phase offset:%lld", -master_local_offset);
            }

            port->adjustClockPhase( -master_local_offset );
            _master_local_freq_offset_init = false;
            _local_system_freq_offset_init = false;
            restartPDelayAll();
            putTxLockAll();
            master_local_offset = 0;
            _freq_valid = 0;
        }

        //Adjust for frequency offset
        long double phase_error = (long double) - master_local_offset;

        if ( fabsl(phase_error) > PHASE_ERROR_THRESHOLD ) {
            ++_phase_error_violation;
        } else {
           float syncPerSec = (float)(1.0 / pow((float)2, port->getSyncInterval()));
            _ppm += (float) ((INTEGRAL * syncPerSec * phase_error) + PROPORTIONAL * ((
                                 master_local_freq_offset - 1.0) * 1000000));
            GPTP_LOG_DEBUG("old ppm calculation clock rate ppm:%f, phase_error = %Lf, syncPerSec = %f, master_local_freq_offset = %Lf",
                                                            _ppm,        phase_error,     syncPerSec,    master_local_freq_offset);

            if ( _ppm < LOWER_FREQ_LIMIT ) {
                _ppm = LOWER_FREQ_LIMIT;
                _freq_valid--;
            }

            else if ( _ppm > UPPER_FREQ_LIMIT ) {
                _ppm = UPPER_FREQ_LIMIT;
                _freq_valid--;
            }
            else if (_ppm != 0 ) {
                _freq_valid = FREQ_VALID_COUNT;
            }
            GPTP_LOG_DEBUG(" Freq valid:%d", _freq_valid);
        }



        if ( port->getTestMode() ) {
            GPTP_LOG_STATUS("Adjust clock rate ppm:%f", _ppm);
        }

        if ( !port->adjustClockRate( _ppm ) ) {
            GPTP_LOG_ERROR( "Failed to adjust clock rate ppm:%f", _ppm);
        }
    }

    port->getCurrentPtpTime( &curr_gptp );
    //Sync Status
    port->syncInfo.reference_local_timestamp = curr_gptp;
    port->syncInfo.reference_global_timestamp = curr_gptp;

    if (sync_count > 1) {
        initialdrift = true;
    }

    synchronize_clocks(port);

    if (initialdrift) {
        if ((master_local_offset > port->getstbMSyncLossThreshold())
                && port->timesync_diagstats.driftCountValid) {
            GPTP_LOG_INFO("master_local_offset: %llu, exceeding STBM_SYNC_LOSS_THRESH: %llu\n",
                          master_local_offset, port->getstbMSyncLossThreshold());
            port->timesync_diagstats.timeSyncDriftCount++;
            port->timesync_diagstats.timeSyncStatusDID |= AUTO_TIMELEAP;
        } else {
            port->timesync_diagstats.timeSyncStatusDID &= ~AUTO_TIMELEAP;
        }
    }

    return;
}

/* Get current time from system clock */
Timestamp IEEE1588Clock::getTime(void)
{
    return getSystemTime();
}

/* Get timestamp from hardware */
Timestamp IEEE1588Clock::getPreciseTime(void)
{
    return getSystemTime();
}

bool IEEE1588Clock::isBetterThan(PTPMessageAnnounce * msg)
{
    unsigned char this1[14];
    unsigned char that1[14];
    uint16_t tmp;

    if (msg == NULL) {
        return true;
    }

    this1[0] = priority1;
    that1[0] = msg->getGrandmasterPriority1();
    this1[1] = clock_quality.cq_class;
    that1[1] = msg->getGrandmasterClockQuality()->cq_class;
    this1[2] = clock_quality.clockAccuracy;
    that1[2] = msg->getGrandmasterClockQuality()->clockAccuracy;
    tmp = clock_quality.offsetScaledLogVariance;
    tmp = PLAT_htons(tmp);
    memcpy(this1 + 3, &tmp, sizeof(tmp));
    tmp = msg->getGrandmasterClockQuality()->offsetScaledLogVariance;
    tmp = PLAT_htons(tmp);
    memcpy(that1 + 3, &tmp, sizeof(tmp));
    this1[5] = priority2;
    that1[5] = msg->getGrandmasterPriority2();
    clock_identity.getIdentityString(this1 + 6);
    msg->getGrandmasterIdentity((char *)that1 + 6);
#if 0
    GPTP_LOG_DEBUG("(Clk)Us: ");

    for (int i = 0; i < 14; ++i) {
        GPTP_LOG_DEBUG("%hhx ", this1[i]);
    }

    GPTP_LOG_DEBUG("(Clk)Them: ");

    for (int i = 0; i < 14; ++i) {
        GPTP_LOG_DEBUG("%hhx ", that1[i]);
    }

#endif
    return (memcmp(this1, that1, 14) < 0) ? true : false;
}

IEEE1588Clock::~IEEE1588Clock(void)
{
    if (fup_info) {
        delete fup_info;
    }

    if (fup_status) {
        delete fup_status;
    }
}

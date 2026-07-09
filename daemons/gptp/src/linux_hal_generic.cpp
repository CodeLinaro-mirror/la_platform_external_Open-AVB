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
/* ============================================================================

Changes from Qualcomm Technologies, Inc. are provided under the following license:
Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
SPDX-License-Identifier: BSD-3-Clause-Clear

============================================================================ */

#include <linux_hal_generic.hpp>
#include <linux_hal_generic_tsprivate.hpp>
#include <sys/select.h>
#include <sys/socket.h>
#include <netpacket/packet.h>
#include <errno.h>
#include <linux/ethtool.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock.h>
#ifdef ANDROID
#include <sys/syscall.h>
#include <sys/timex.h>
#else
#include <syscall.h>
#endif
#include <limits.h>
#include <dirent.h>

#define TX_PHY_TIME 184
#define RX_PHY_TIME 382

#define QTIMER_RESAMPLING 5

char ptp_dev_index[PTP_CLOCK_DEVICE_LENGTH] = {0};
char ptp_device[] = PTP_DEVICE;
extern int port_pipe_fds[2];

net_result LinuxNetworkInterface::nrecv
( LinkLayerAddress *addr, uint8_t *payload, size_t &length )
{
    fd_set readfds;
    int err = 0;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    union {
        struct cmsghdr cm;
        char control_data[CMSG_SPACE(256)];
    } control;
    struct sockaddr_ll remote;
    struct iovec sgentry;
    net_result ret = net_succeed;
    bool got_net_lock;
    LinuxTimestamperGeneric *gtimestamper;
    struct timeval timeout = { 0, 16000 }; // 16 ms

    if ( !net_lock.lock( &got_net_lock )) {
        GPTP_LOG_ERROR("A Failed to lock mutex");
        return net_fatal;
    }

    if ( !got_net_lock ) {
        return net_trfail;
    }

    FD_ZERO( &readfds );
    if (sd_event >= 0 && sd_event < FD_SETSIZE && fcntl(sd_event, F_GETFD) != -1) {
        FD_SET(sd_event, &readfds);
    } else {
        GPTP_LOG_ERROR("Invalid sd_event fd");
        ret = net_fatal;
        goto done;
    }
    FD_SET(port_pipe_fds[0], &readfds);

    err = select( (port_pipe_fds[0] > sd_event ? port_pipe_fds[0] : sd_event) + 1, &readfds, NULL, NULL, &timeout );

    if ( err == 0 ) {
        ret = net_trfail;
        goto done;
    } else if ( err == -1 ) {
        if ( err == EINTR ) {
            // Caught signal
            GPTP_LOG_ERROR("select() recv signal");
            ret = net_trfail;
            goto done;
        } else {
            GPTP_LOG_ERROR("select() failed");
            ret = net_fatal;
            goto done;
        }
    } else {
         if ( FD_ISSET(port_pipe_fds[0], &readfds) ) {
            char pipebuf;
            read(port_pipe_fds[0], &pipebuf, 1);
            if (pipebuf == '1') {
                GPTP_LOG_INFO("cleanup openport thread\n");
                ret = net_fatal;
                goto done;
            }
        } else if ( !FD_ISSET( sd_event, &readfds )) {
            ret = net_trfail;
            goto done;
        }
    }

    memset( &msg, 0, sizeof( msg ));
    msg.msg_iov = &sgentry;
    msg.msg_iovlen = 1;
    sgentry.iov_base = payload;
    sgentry.iov_len = length;
    memset( &remote, 0, sizeof(remote));
    msg.msg_name = (caddr_t) &remote;
    msg.msg_namelen = sizeof( remote );
    msg.msg_control = &control;
    msg.msg_controllen = sizeof(control);

    err = recvmsg( sd_event, &msg, MSG_DONTWAIT );

    if ( err == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
        GPTP_LOG_DEBUG("recvmsg() EAGAIN/EWOULDBLOCK after select(), returning net_trfail");
        ret = net_trfail;
        goto done;
    }

    if ( err < 0 ) {
        if ( errno == ENOMSG ) {
            GPTP_LOG_ERROR("Got ENOMSG: %s:%d", __FILE__, __LINE__);
            ret = net_trfail;
            goto done;
        }

        GPTP_LOG_ERROR("recvmsg failed: %s (errno=%d)", strerror(errno), errno);
        ret = net_fatal;
        goto done;
    }

    *addr = LinkLayerAddress( remote.sll_addr );
    gtimestamper = dynamic_cast<LinuxTimestamperGeneric *>(timestamper);

    if ( err > 0 && !(payload[0] & 0x8) && gtimestamper != NULL ) {
        /* Retrieve the timestamp */
        cmsg = CMSG_FIRSTHDR(&msg);

        while ( cmsg != NULL ) {
            if
            ( cmsg->cmsg_level == SOL_SOCKET &&
                    cmsg->cmsg_type == SO_TIMESTAMPING ) {
                struct timespec *ts_device, *ts_system;
                Timestamp device, system;
                ts_system = ((struct timespec *) CMSG_DATA(cmsg)) + 1;
                system = tsToTimestamp( ts_system );
                ts_device = ts_system + 1;
                device = tsToTimestamp( ts_device );
                gtimestamper->pushRXTimestamp( &device );
                break;
            }

            cmsg = CMSG_NXTHDR(&msg, cmsg);
        }
    }

    length = err;
done:

    if ( !net_lock.unlock()) {
        GPTP_LOG_ERROR("A Failed to unlock, %d", err);
        return net_fatal;
    }

    return ret;
}


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

/* Check if name matches "ptp" followed by at least one digit, return index via out_idx */
static int parse_ptp_index(const char *name, int *out_idx) {
    if (strncmp(name, "ptp", 3) != 0) return 0; // not a ptp entry
    const char *p = name + 3;
    if (!isdigit((unsigned char)*p)) return 0;
    long val = 0;
    char *endptr = NULL;
    val = strtol(p, &endptr, 10);
    if (endptr == p || val < 0 || val > INT_MAX) return 0;
    *out_idx = (int)val;
    return 1;
}

int findphcTscIndex(void) {
    const char *ptp_dir = "/sys/class/ptp";
    DIR *d = opendir(ptp_dir);
    struct dirent *de;
    char path[PATH_MAX];
    char namebuf[128];

    if (!d) {
        // Could log errno for diagnostics
        return -1;
    }

    while ((de = readdir(d)) != NULL) {
        // Skip "." and ".."
        if (de->d_name[0] == '.') continue;

        int idx = -1;
        if (!parse_ptp_index(de->d_name, &idx)) {
            continue;
        }

        // Build path to clock_name: /sys/class/ptp/ptp<idx>/clock_name
        // Use snprintf and guard against truncation
        int n = snprintf(path, sizeof(path), "%s/%s/clock_name", ptp_dir, de->d_name);
        if (n <= 0 || (size_t)n >= sizeof(path)) {
            // Path too long or snprintf error; skip safely
            continue;
        }

        if (read_file_line(path, namebuf, sizeof(namebuf)) == 0) {
            trim(namebuf);
            if (strcmp(namebuf, "QCOM TSC") == 0) {
                closedir(d);
                return idx;
            }
        }
        // If reading clock_name fails, just skip that entry
    }

    closedir(d);
    return -1; // Not found
}

int findPhcIndex( InterfaceLabel *iface_label )
{
    int sd;
    InterfaceName *ifname;
    struct ethtool_ts_info info;
    struct ifreq ifr;

    if (( ifname = dynamic_cast<InterfaceName *>(iface_label)) == NULL ) {
        GPTP_LOG_ERROR("findPTPIndex requires InterfaceName");
        return -1;
    }

    sd = socket( AF_UNIX, SOCK_DGRAM, 0 );

    if ( sd < 0 ) {
        GPTP_LOG_ERROR("findPTPIndex: failed to open socket");
        return -1;
    }

    memset( &ifr, 0, sizeof(ifr));
    memset( &info, 0, sizeof(info));
    info.cmd = ETHTOOL_GET_TS_INFO;
    ifname->toString( ifr.ifr_name, IFNAMSIZ - 1 );
    ifr.ifr_data = (char *) &info;

    if ( ioctl( sd, SIOCETHTOOL, &ifr ) < 0 ) {
        GPTP_LOG_ERROR("findPTPIndex: ioctl(SIOETHTOOL) failed");
        close(sd);
        return -1;
    }

    close(sd);
    return info.phc_index;
}

int resolve_ptp_index(bool tsc_enabled,  InterfaceLabel *iface_label) {
    int phc_idx = 0;
    if ( tsc_enabled ) {
        phc_idx = findphcTscIndex();
        return phc_idx;
    } else {
        // Fallback / default path: use existing PHC selection
        phc_idx = findPhcIndex(iface_label);
        return phc_idx; // propagate phc_idx (>=0 success, negative on error)
    }
}

LinuxTimestamperGeneric::~LinuxTimestamperGeneric()
{
    if ( _private != NULL ) {
        delete _private;
    }

#ifdef WITH_IGBLIB

    if ( igb_private != NULL ) {
        delete igb_private;
    }

#endif
}

LinuxTimestamperGeneric::LinuxTimestamperGeneric()
{
    _private = NULL;
#ifdef WITH_IGBLIB
    igb_private = NULL;
#endif
    sd = -1;
}

bool LinuxTimestamperGeneric::Adjust( void *tmx ) const
{
    int ptp_fd;

    if ( clock_adjtime(_private->clockid, (struct timex*)tmx ) != 0 ) {
        GPTP_LOG_ERROR("Failed to adjust PTP clock rate %d", _private->clockid);
        ptp_fd = open( ptp_device, O_RDWR );

        if ( ptp_fd != -1 && (_private->clockid = FD_TO_CLOCKID(ptp_fd)) != -1 ) {
            GPTP_LOG_INFO("open PTP clock device is success");
            GPTP_LOG_INFO("opened clock device: %s", ptp_device);
        }
        return false;
    }

    return true;
}

bool LinuxTimestamperGeneric::HWTimestamper_init
( InterfaceLabel *iface_label, OSNetworkInterface *iface, bool tsc_enable)
{
    cross_stamp_good = false;
    int phc_index;
    int count = 0;
    struct timespec ts;

#ifdef PTP_HW_CROSSTSTAMP
    struct ptp_clock_caps ptp_capability;
#endif
    _private = new LinuxTimestamperGenericPrivate;
    if (_private == NULL) {
        GPTP_LOG_ERROR("Failed to allocate LinuxTimestamperGenericPrivate");
        return false;
    }
    pthread_mutex_init( &_private->cross_stamp_lock, NULL );
    // Determine the correct PTP clock interface
    //phc_index = findPhcIndex( iface_label );
    phc_index = resolve_ptp_index(tsc_enable, iface_label);

    if ( phc_index < 0 ) {
        GPTP_LOG_ERROR("Failed to find PTP device index");
        return false;
    }

    snprintf
    ( ptp_device + PTP_DEVICE_IDX_OFFS,
      sizeof(ptp_device) - PTP_DEVICE_IDX_OFFS, "%d", phc_index );
    GPTP_LOG_INFO("Using clock device: %s", ptp_device);
    snprintf(ptp_dev_index, PTP_CLOCK_DEVICE_LENGTH, "%d", phc_index );

    do {
        phc_fd = open( ptp_device, O_RDWR );

        if ( phc_fd == -1 || (_private->clockid = FD_TO_CLOCKID(phc_fd)) == -1 ) {
            GPTP_LOG_ERROR("Failed to open PTP clock device: %d  count: %d", phc_fd, count);
            usleep(50000);
            count++;
        } else {
            GPTP_LOG_INFO("opened clock device: %s", ptp_device);
        }

    } while ( ( ( phc_fd == -1 )
                || ( (_private->clockid = FD_TO_CLOCKID(phc_fd)) == -1 ) )
              && ( count < 1000 ) );
    // only if tsc enable set the clock to current time to start TSC timer
    if(tsc_enable){
        clock_gettime( CLOCK_REALTIME, &ts );
        clock_settime( _private->clockid, &ts );
    }
#ifdef PTP_HW_CROSSTSTAMP

    // Query PTP stack for availability of HW cross-timestamp
    if ( ioctl( phc_fd, PTP_CLOCK_GETCAPS, &ptp_capability ) == -1 ) {
        GPTP_LOG_ERROR("Failed to query PTP clock capabilities");
        return false;
    }

    precise_timestamp_enabled = ptp_capability.cross_timestamping;
#endif

    if ( !resetFrequencyAdjustment() ) {
        GPTP_LOG_ERROR("Failed to reset (zero) frequency adjustment");
        return false;
    }

    if ( dynamic_cast<LinuxNetworkInterface *>(iface) != NULL ) {
        // Check if iface is present in the list, if not add to iface_list
        auto it = std::find(iface_list.begin(), iface_list.end(), iface);
        if (it == iface_list.end()) {
            iface_list.push_front
                        ( (dynamic_cast<LinuxNetworkInterface *>(iface)) );
        }
    }

    return true;
}

bool LinuxTimestamperGeneric::HWTimestamper_deinit
( InterfaceLabel *iface_label, OSNetworkInterface **iface )
{
    if(phc_fd != -1) {
        if (close(phc_fd) == -1) {
            GPTP_LOG_DEBUG("%s:%d Error in closing errno = %d(%s)", __func__,__LINE__, errno, strerror(errno));
        } else {
            GPTP_LOG_DEBUG("%s:%d close successful", __func__,__LINE__);
        }
        phc_fd = -1;
    }

    if (iface && *iface) {
        auto it = std::find(iface_list.begin(), iface_list.end(), *iface);
        if (it != iface_list.end()) {
            delete *it;
            *it = nullptr;
            iface_list.erase(it);
        }
        *iface = nullptr;
    }

    if (_private != NULL) {
        pthread_mutex_destroy(&_private->cross_stamp_lock);
        delete _private;
        _private = nullptr;
    }

    return true;
}

void LinuxTimestamperGeneric::HWTimestamper_reset()
{
    if ( !resetFrequencyAdjustment() ) {
        GPTP_LOG_ERROR("Failed to reset (zero) frequency adjustment");
    }
}

int LinuxTimestamperGeneric::HWTimestamper_txtimestamp
( PortIdentity *identity, PTPMessageId messageId, Timestamp &timestamp,
  unsigned &clock_value, bool last )
{
    int err;
    int ret = GPTP_EC_EAGAIN;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct sockaddr_ll remote;
    struct iovec sgentry;
    union {
        struct cmsghdr cm;
        char control_data[CMSG_SPACE(256)];
    } control;

    if ( sd == -1 ) {
        return -1;
    }

    memset( &msg, 0, sizeof( msg ));
    msg.msg_iov = &sgentry;
    msg.msg_iovlen = 1;
    sgentry.iov_base = NULL;
    sgentry.iov_len = 0;
    memset( &remote, 0, sizeof(remote));
    msg.msg_name = (caddr_t) &remote;
    msg.msg_namelen = sizeof( remote );
    msg.msg_control = &control;
    msg.msg_controllen = sizeof(control);
    err = recvmsg( sd, &msg, MSG_ERRQUEUE );

    if ( err == -1 ) {
        if ( errno == EAGAIN ) {
            ret = GPTP_EC_EAGAIN;
            goto done;
        } else {
            ret = GPTP_EC_FAILURE;
            goto done;
        }
    }

    // Retrieve the timestamp
    cmsg = CMSG_FIRSTHDR(&msg);

    while ( cmsg != NULL ) {
        if ( cmsg->cmsg_level == SOL_SOCKET &&
                cmsg->cmsg_type == SO_TIMESTAMPING ) {
            struct timespec *ts_device, *ts_system;
            Timestamp device, system;
            ts_system = ((struct timespec *) CMSG_DATA(cmsg)) + 1;
            system = tsToTimestamp( ts_system );
            ts_device = ts_system + 1;
            device = tsToTimestamp( ts_device );
            system._version = version;
            device._version = version;
            timestamp = device;
            ret = 0;
            break;
        }

        cmsg = CMSG_NXTHDR(&msg, cmsg);
    }

    if ( ret != 0 ) {
        GPTP_LOG_ERROR("Received a error message, but didn't find a valid timestamp");
    }

done:

    if ( ret == 0 || last ) {
        net_lock->unlock();
    }

    return ret;
}

bool LinuxTimestamperGeneric::post_init( int ifindex, int sd,
        TicketingLock *lock , bool tsc_enable)
{
    int timestamp_flags = 0;
    struct ifreq device;
    struct hwtstamp_config hwconfig;
    int err;
    this->sd = sd;
    this->net_lock = lock;
    memset( &device, 0, sizeof(device));
    device.ifr_ifindex = ifindex;
    err = ioctl( sd, SIOCGIFNAME, &device );

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ("Failed to get interface name: %s", strerror(errno));
        return false;
    }

    device.ifr_data = (char *) &hwconfig;
    memset( &hwconfig, 0, sizeof( hwconfig ));

    hwconfig.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
    if(tsc_enable){
#ifdef TSC_FEATURE
    GPTP_LOG_INFO("TSC used:: HWTSTAMP_TX_EXTERNAL_TIME_SRC hw type set");
    hwconfig.tx_type = HWTSTAMP_TX_EXTERNAL_TIME_SRC;
#else
    GPTP_LOG_INFO("HWTSTAMP_TX_ON hw type set (non-GEN5 target)");
    hwconfig.tx_type = HWTSTAMP_TX_ON;
#endif
   } else {
    hwconfig.tx_type = HWTSTAMP_TX_ON;
    GPTP_LOG_INFO("HWTSTAMP_TX_ON hw type set");
   }
    err = ioctl( sd, SIOCSHWTSTAMP, &device );
    GPTP_LOG_INFO("post_init:: SIOCSHWTSTAMP ioctl called");

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ("Failed to configure timestamping: %s", strerror(errno));
        return false;
    }

    timestamp_flags |= SOF_TIMESTAMPING_TX_HARDWARE;
    timestamp_flags |= SOF_TIMESTAMPING_RX_HARDWARE;
    timestamp_flags |= SOF_TIMESTAMPING_SYS_HARDWARE;
    timestamp_flags |= SOF_TIMESTAMPING_RAW_HARDWARE;
    err = setsockopt
          ( sd, SOL_SOCKET, SO_TIMESTAMPING, &timestamp_flags,
            sizeof(timestamp_flags) );

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ("Failed to configure timestamping on socket: %s",
         strerror(errno));
        return false;
    }

    return true;
}

#define MAX_NSEC 1000000000

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

static inline Timestamp pctTimestamp( struct ptp_clock_time *t )
{
    Timestamp result = {0, 0, 0};
    result.seconds_ls = t->sec & 0xFFFFFFFF;
    result.seconds_ms = t->sec >> sizeof(result.seconds_ls) * 8;
    result.nanoseconds = t->nsec;
    return result;
}

// Use HW cross-timestamp if available
bool LinuxTimestamperGeneric::HWTimestamper_gettime
( Timestamp *system_time, Timestamp *q_time, Timestamp *device_time,
  Timestamp *boot_time, uint32_t *local_clock,
  uint32_t *nominal_clock_rate ) const
{
    if ( phc_fd == -1 ) {
        return false;
    }

#ifdef PTP_HW_CROSSTSTAMP

    if ( precise_timestamp_enabled ) {
        struct ptp_sys_offset_precise offset;
        memset( &offset, 0, sizeof(offset));

        if ( ioctl( phc_fd, PTP_SYS_OFFSET_PRECISE, &offset ) == 0 ) {
            *device_time = pctTimestamp( &offset.device );
            *system_time = pctTimestamp( &offset.sys_realtime );
            return true;
        }
    }

#endif
    {
        unsigned i;
        struct ptp_clock_time *pct;
        struct ptp_clock_time *system_time_l = NULL, *device_time_l = NULL;
        int64_t interval = LLONG_MAX;
        struct ptp_sys_offset offset;
        memset( &offset, 0, sizeof(offset));
        offset.n_samples = PTP_MAX_SAMPLES;

        if ( ioctl( phc_fd, PTP_SYS_OFFSET, &offset ) == -1 ) {
            return false;
        }

        pct = &offset.ts[0];

        for ( i = 0; i < offset.n_samples; ++i ) {
            int64_t interval_t;
            interval_t = pctns(pct_diff( pct + 2 * i + 2, pct + 2 * i ));

            if ( interval_t < interval ) {
                system_time_l = pct + 2 * i;
                device_time_l = pct + 2 * i + 1;
                interval = interval_t;
            }
        }

        if (device_time_l != NULL && system_time_l != NULL) {
            *device_time = pctTimestamp( device_time_l );
            *system_time = pctTimestamp( system_time_l );
            //return true;
        } else {
            return false;
        }
    }
#ifdef PTP_SW_QTIMER
    {
        int64_t interval = 0;
        int64_t calculated_q_time = 0;

        // Find average delta between qtimer and system time
        for (int i = 0; i < QTIMER_RESAMPLING; ++i ) {
            struct timespec real;
            struct ptp_clock_time real_pct;
            struct ptp_clock_time qtimer_pct;
            uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerNanosSec = 0,
                     qTimerNanosNSec = 0;
            clock_gettime(_private->clockid, &real);
#if __aarch64__
            asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
            asm volatile("mrs %0, cntfrq_el0" : "=r"(qTimerFreq));
#else
            asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
            qTimerFreq =  19200000; //19.2 MHz TBD: find right asm instruction
#endif
            qTimerNanosSec = (qTimerCount / qTimerFreq);
            qTimerNanosNSec = (qTimerCount % qTimerFreq);
            qTimerNanosNSec *= 1000000000;
            qTimerNanosNSec /= qTimerFreq;
            /*GPTP_LOG_WARNING("qTimerCount = %llu, qTimeFreq = %llu, qTime=%llu.%09llu",
                qTimerCount, qTimerFreq, qTimerNanosSec, qTimerNanosNSec);*/
            qtimer_pct.sec = qTimerNanosSec;
            qtimer_pct.nsec = qTimerNanosNSec;
            real_pct.sec = real.tv_sec;
            real_pct.nsec = real.tv_nsec;
            interval += pctns(pct_diff(&real_pct, &qtimer_pct));
        }

        // Calculate monotonic qtimer time equivanlent to system time above, which
        // will allow us to easily calculate qtimer<->gptp time offset.
        calculated_q_time = TIMESTAMP_TO_NS(*device_time);
        calculated_q_time -= (interval / QTIMER_RESAMPLING);
        q_time->set64(calculated_q_time);
        /*GPTP_LOG_WARNING("system_time = %d.%d, mono_time = %d.%d, device_time_l = %d.%d",
                system_time->seconds_ls, system_time->nanoseconds,
                mono_time->seconds_ls, mono_time->nanoseconds,
                device_time->seconds_ls, device_time->nanoseconds);*/
    }
#endif
    {
        int64_t interval = 0;
        int64_t calculated_boot_time = 0;

        // Find average delta between qtimer and system time
        for (int i = 0; i < QTIMER_RESAMPLING; ++i ) {
            struct timespec real;
            struct timespec boot;
            struct ptp_clock_time real_pct;
            struct ptp_clock_time boot_pct;
            uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerNanosSec = 0,
                     qTimerNanosNSec = 0;
            clock_gettime(_private->clockid, &real);
            clock_gettime(CLOCK_BOOTTIME, &boot);
            boot_pct.sec = boot.tv_sec;
            boot_pct.nsec = boot.tv_nsec;
            real_pct.sec = real.tv_sec;
            real_pct.nsec = real.tv_nsec;
            interval += pctns(pct_diff(&real_pct, &boot_pct));
        }

        // Calculate monotonic qtimer time equivanlent to system time above, which
        // will allow us to easily calculate qtimer<->gptp time offset.
        calculated_boot_time = TIMESTAMP_TO_NS(*device_time);
        calculated_boot_time -= (interval / QTIMER_RESAMPLING);
        boot_time->set64(calculated_boot_time);
        /*GPTP_LOG_WARNING("system_time = %d.%d, mono_time = %d.%d, device_time_l = %d.%d",
                system_time->seconds_ls, system_time->nanoseconds,
                mono_time->seconds_ls, mono_time->nanoseconds,
                device_time->seconds_ls, device_time->nanoseconds);*/
    }
    return true;
}


bool LinuxTimestamperGeneric::HWTimestamper_getptptime( uint64_t
        *ptp_cur_time )
{
    struct timespec ts;
    ts.tv_sec = ts.tv_nsec = 0;
    *ptp_cur_time = 0;

    if (clock_gettime(_private->clockid, &ts)) {
        GPTP_LOG_ERROR("clock_gettime failed");
        return false;
    }

    *ptp_cur_time = (ts.tv_sec) * 1000000000LL + ts.tv_nsec;
    return true;
}

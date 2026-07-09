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

#include <linux_hal_common.hpp>
#include <sys/types.h>
#include <avbts_clock.hpp>
#include <ether_port.hpp>

#include <pthread.h>
#include <linux_ipc.hpp>

#include <sys/mman.h>
#include <fcntl.h>
#include <grp.h>
#include <net/if.h>

#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <net/ethernet.h> /* the L2 protocols */

#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <gptp_cfg.hpp>
#ifdef GPTP_VFIO
#include "ptp_vfio.h"
#include <atomic>
#endif

#ifndef PTP_SEC_OFFSET
#define PTP_SEC_OFFSET                      0x00007008
#endif

#ifndef PTP_NANO_SEC_OFFSET
#define PTP_NANO_SEC_OFFSET                 0x0000700C
#endif

#ifndef PTP_ADDEND_OFFSET
#define PTP_ADDEND_OFFSET                   0x00007018
#endif

uint64_t prev_gptp_sync_time = 0;
uint64_t prev_qtimer_sync_time = 0;
uint64_t prev_qtimer_inc_ratio = 1000000000;
uintptr_t qtimer_base_addr;
uintptr_t ptp_base_addr;

#define MAC_STNSR_TSSS_LPOS 0
#define MAC_STNSR_TSSS_HPOS 30
#define GET_VALUE(data, lbit, hbit) ((data >>lbit) & (~(~0<<(hbit-lbit+1))))
#define in32(port) (*((volatile uint32_t *) (port)))

extern char ptp_dev_index[PTP_CLOCK_DEVICE_LENGTH];

static __inline__ uint64_t __attribute__((__unused__)) in64(uintptr_t __addr)
{
    return *(volatile uint64_t *)__addr;
}

Timestamp tsToTimestamp(struct timespec *ts)
{
    Timestamp ret = {0, 0, 0};
    int seclen = sizeof(ts->tv_sec) - sizeof(ret.seconds_ls);

    if (seclen > 0) {
        ret.seconds_ms =
            ts->tv_sec >> (sizeof(ts->tv_sec) - seclen) * 8;
        ret.seconds_ls = ts->tv_sec & 0xFFFFFFFF;
    } else {
        ret.seconds_ms = 0;
        ret.seconds_ls = ts->tv_sec;
    }

    ret.nanoseconds = ts->tv_nsec;
    return ret;
}

LinuxNetworkInterface::~LinuxNetworkInterface()
{
    close( sd_event );
    close( sd_general );
    sd_event = -1;
    sd_general = -1;
}

net_result LinuxNetworkInterface::send
( LinkLayerAddress *addr, uint16_t etherType, uint8_t *payload, size_t length,
  bool timestamp )
{
    sockaddr_ll *remote = NULL;
    int err;
    remote = new struct sockaddr_ll;
    if (remote == NULL) {
        GPTP_LOG_ERROR("Failed to allocate sockaddr_ll in send()");
        return net_fatal;
    }
    memset( remote, 0, sizeof( *remote ));
    remote->sll_family = AF_PACKET;
    remote->sll_protocol = PLAT_htons( etherType );
    remote->sll_ifindex = ifindex;
    remote->sll_halen = ETH_ALEN;
    addr->toOctetArray( remote->sll_addr );

    if ( timestamp ) {
#ifndef ARCH_INTELCE
        net_lock.lock();
#endif
        err = sendto
              ( sd_event, payload, length, 0, (sockaddr *) remote,
                sizeof( *remote ));
    } else {
        err = sendto
              ( sd_general, payload, length, 0, (sockaddr *) remote,
                sizeof( *remote ));
    }

    delete remote;

    if ( err == -1 ) {
        GPTP_LOG_ERROR( "Failed to send: %s(%d)", strerror(errno), errno );
        return net_fatal;
    }

    return net_succeed;
}


void LinuxNetworkInterface::disable_rx_queue()
{
    struct packet_mreq mr_8021as;
    int err;

    if ( !net_lock.lock() ) {
        fprintf( stderr, "D rx lock failed\n" );
        _exit(0);
    }

    memset( &mr_8021as, 0, sizeof( mr_8021as ));
    mr_8021as.mr_ifindex = ifindex;
    mr_8021as.mr_type = PACKET_MR_MULTICAST;
    mr_8021as.mr_alen = 6;
    memcpy( mr_8021as.mr_address, P8021AS_MULTICAST, mr_8021as.mr_alen );
    err = setsockopt
          ( sd_event, SOL_PACKET, PACKET_DROP_MEMBERSHIP, &mr_8021as,
            sizeof( mr_8021as ));

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ( "Unable to add PTP multicast addresses to port id: %u",
          ifindex );
        return;
    }

    return;
}

void LinuxNetworkInterface::clear_reenable_rx_queue()
{
    struct packet_mreq mr_8021as;
    char buf[256];
    int err;

    while ( recvfrom( sd_event, buf, 256, MSG_DONTWAIT, NULL, 0 ) != -1 );

    memset( &mr_8021as, 0, sizeof( mr_8021as ));
    mr_8021as.mr_ifindex = ifindex;
    mr_8021as.mr_type = PACKET_MR_MULTICAST;
    mr_8021as.mr_alen = 6;
    memcpy( mr_8021as.mr_address, P8021AS_MULTICAST, mr_8021as.mr_alen );
    err = setsockopt
          ( sd_event, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr_8021as,
            sizeof( mr_8021as ));

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ( "Unable to add PTP multicast addresses to port id: %u",
          ifindex );
    }

    if ( !net_lock.unlock() ) {
        fprintf( stderr, "D failed unlock rx lock, %d\n", err );
    }
}

static void x_readEvent
( int sockint, EtherPort *pPort, int ifindex )
{
    int status;
    char buf[4096];
    struct iovec iov = { buf, sizeof buf };
    struct sockaddr_nl snl;
    struct msghdr msg = { (void *) &snl, sizeof snl, &iov, 1, NULL, 0, 0 };
    struct nlmsghdr *msgHdr;
    struct ifinfomsg *ifi;
    status = recvmsg(sockint, &msg, 0);

    if (status < 0) {
        GPTP_LOG_ERROR("read_netlink: Error recvmsg: %d", status);
        return;
    }

    if (status == 0) {
        GPTP_LOG_ERROR("read_netlink: EOF");
        return;
    }

    // Process the NETLINK messages
    for (msgHdr = (struct nlmsghdr *)buf; NLMSG_OK(msgHdr, (unsigned int)status);
            msgHdr = NLMSG_NEXT(msgHdr, status)) {
        if (msgHdr->nlmsg_type == NLMSG_DONE) {
            return;
        }

        if (msgHdr->nlmsg_type == NLMSG_ERROR) {
            GPTP_LOG_ERROR("netlink message error");
            return;
        }

        if (msgHdr->nlmsg_type == RTM_NEWLINK) {
            ifi = (struct ifinfomsg *)NLMSG_DATA(msgHdr);

            if (ifi->ifi_index == ifindex) {
                bool linkUp = ifi->ifi_flags & IFF_RUNNING;

                if (linkUp != pPort->getLinkUpState()) {
                    pPort->setLinkUpState(linkUp);

                    if (pPort->gPTP_lpm) {
                        GPTP_LOG_INFO("watchNetLink: LPM active, "
                                      "suppressing duplicate %s event",
                                      linkUp ? "LINKUP" : "LINKDOWN");
                    } else if (linkUp) {
                        pPort->processEvent(LINKUP);
                    } else {
                        pPort->processEvent(LINKDOWN);
                    }
                } else {
                    GPTP_LOG_DEBUG("False (repeated) %s event for the interface",
                                   linkUp ? "LINKUP" : "LINKDOWN");
                }
            }
        }
    }

    return;
}

static void x_initLinkUpStatus( EtherPort *pPort, int ifindex )
{
    struct ifreq device;
    memset(&device, 0, sizeof(device));
    device.ifr_ifindex = ifindex;
    int inetSocket = socket (AF_INET, SOCK_STREAM, 0);

    if (inetSocket < 0) {
        GPTP_LOG_ERROR("initLinkUpStatus error opening socket: %s", strerror(errno));
        return;
    }

    int r = ioctl(inetSocket, SIOCGIFNAME, &device);

    if (r < 0) {
        GPTP_LOG_ERROR("initLinkUpStatus error reading interface name: %s",
                       strerror(errno));
        close(inetSocket);
        return;
    }

    r = ioctl(inetSocket, SIOCGIFFLAGS, &device);

    if (r < 0) {
        GPTP_LOG_ERROR("initLinkUpStatus error reading flags: %s", strerror(errno));
        close(inetSocket);
        return;
    }

    if (device.ifr_flags & IFF_RUNNING) {
        GPTP_LOG_DEBUG("Interface %s is up", device.ifr_name);
        pPort->setLinkUpState(true);
    } //linkUp == false by default

    close(inetSocket);
}

#ifdef __ANDROID__
static inline __u32 ethtool_cmd_speed(const struct ethtool_cmd *ep)
{
    return (ep->speed_hi << 16) | ep->speed;
}
#endif



bool LinuxNetworkInterface::getLinkSpeed( int sd, uint32_t *speed )
{
    struct ifreq ifr;
    struct ethtool_cmd edata;
    ifr.ifr_ifindex = ifindex;

    if ( ioctl( sd, SIOCGIFNAME, &ifr ) == -1 ) {
        GPTP_LOG_ERROR
        ( "%s: SIOCGIFNAME failed: %s", __PRETTY_FUNCTION__,
          strerror( errno ));
        return false;
    }

    ifr.ifr_data = (char *) &edata;
    edata.cmd = ETHTOOL_GSET;

    if ( ioctl( sd, SIOCETHTOOL, &ifr ) == -1 ) {
        GPTP_LOG_WARNING
        ( "%s: SIOCETHTOOL failed: %s", __PRETTY_FUNCTION__,
          strerror( errno ));
        *speed = LINKSPEED_1G;
        GPTP_LOG_INFO( "Use default Link Speed: %d kb/sec", *speed );
        return true;
    }

    switch (ethtool_cmd_speed(&edata)) {
        default:
            GPTP_LOG_ERROR( "%s: Unknown/Unsupported Speed!",
                            __PRETTY_FUNCTION__ );
            return false;

        case SPEED_100:
            *speed = LINKSPEED_100MB;
            break;

        case SPEED_1000:
            *speed = LINKSPEED_1G;
            break;

        case SPEED_2500:
            *speed = LINKSPEED_2_5G;
            break;

        case SPEED_10000:
            *speed = LINKSPEED_10G;
            break;
    }

    GPTP_LOG_STATUS( "Link Speed: %d kb/sec", *speed );
    return true;
}

void LinuxNetworkInterface::watchNetLink( CommonPort *iPort )
{
    fd_set netLinkFD;
    int netLinkSocket;
    int inetSocket;
    struct sockaddr_nl addr;
    EtherPort *pPort =
        dynamic_cast<EtherPort *>(iPort);

    if ( pPort == NULL ) {
        GPTP_LOG_ERROR("NETLINK socket open error");
        return;
    }

    int ret = pthread_setname_np(pthread_self(), "watchNetLink");
    if (ret != 0) {
        GPTP_LOG_ERROR("pthread_setname_np failed");
    }

    netLinkSocket = socket (AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

    if (netLinkSocket < 0) {
        GPTP_LOG_ERROR("NETLINK socket open error");
        return;
    }

    memset((void *) &addr, 0, sizeof (addr));
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid ();
    addr.nl_groups = RTMGRP_LINK;

    if (bind (netLinkSocket, (struct sockaddr *) &addr, sizeof (addr)) < 0) {
        GPTP_LOG_ERROR("Socket bind failed");
        close (netLinkSocket);
        return;
    }

    /*
     * Open an INET family socket to be passed to getLinkSpeed() which calls
     * ioctl() because NETLINK sockets do not support ioctl(). Since we will
     * enter an infinite loop, there are no apparent close() calls for the
     * open sockets, but they will be closed on process termination.
     */
    inetSocket = socket (AF_INET, SOCK_STREAM, 0);

    if (inetSocket < 0) {
        GPTP_LOG_ERROR("watchNetLink error opening socket: %s", strerror(errno));
        close (netLinkSocket);
        return;
    }

    x_initLinkUpStatus(pPort, ifindex);

    if ( pPort->getLinkUpState() ) {
        uint32_t link_speed;
        getLinkSpeed( inetSocket, &link_speed );
        pPort->setLinkSpeed((int32_t) link_speed );
    } else {
        pPort->setLinkSpeed( INVALID_LINKSPEED );
    }

    while (1) {
        FD_ZERO(&netLinkFD);
        FD_CLR(netLinkSocket, &netLinkFD);
        FD_SET(netLinkSocket, &netLinkFD);
        // Wait forever for a net link event
        int retval = select(FD_SETSIZE, &netLinkFD, NULL, NULL, NULL);

        if (retval == -1)
            ; // Error on select. We will ignore and keep going
        else if (retval) {
            bool prev_link_up = pPort->getLinkUpState();
            x_readEvent(netLinkSocket, pPort, ifindex);

            // Don't do anything else if link state is the same
            if ( prev_link_up == pPort->getLinkUpState() ) {
                continue;
            }

            if ( pPort->getLinkUpState() ) {
                uint32_t link_speed;
                getLinkSpeed( inetSocket, &link_speed );
                pPort->setLinkSpeed((int32_t) link_speed );
            } else {
                pPort->setLinkSpeed( INVALID_LINKSPEED );
            }
        } else {
            ; // Would be timeout but Won't happen because we wait forever
        }
    }
}


struct LinuxTimerQueuePrivate {
    pthread_t signal_thread;
};

struct LinuxTimerQueueActionArg {
    timer_t timer_handle;
    struct sigevent sevp;
    void *inner_arg;
    ostimerq_handler func;
    int type;
    bool rm;
    bool oneshot;
};

LinuxTimerQueue::~LinuxTimerQueue()
{
    if ( _private != NULL ) {
        pthread_join(_private->signal_thread, NULL);
        delete _private;
    }
}

bool LinuxTimerQueue::init()
{
    _private = new LinuxTimerQueuePrivate;

    if ( _private == NULL ) {
        return false;
    }

    return true;
}

void *LinuxTimerQueueHandler( void *arg )
{
    LinuxTimerQueue *timerq = (LinuxTimerQueue *) arg;
    sigset_t waitfor;
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100000000; /* 100 ms */
    sigemptyset( &waitfor );

    int ret = pthread_setname_np(pthread_self(), "LinuxTimer");
    if (ret != 0) {
        GPTP_LOG_ERROR("pthread_setname_np failed");
    }

    while ( !timerq->stop ) {
        siginfo_t info;
        LinuxTimerQueueMap_t::iterator iter;
        sigaddset( &waitfor, SIGUSR1 );

        if ( sigtimedwait( &waitfor, &info, &timeout ) == -1 ) {
            if ( errno == EAGAIN ) {
                continue;
            } else {
                GPTP_LOG_ERROR("LinuxTimerQueueHandler sigtimedwait failed - %s",
                               strerror(errno));
                continue;
            }
        }

        if ( timerq->lock->lock() != oslock_ok ) {
            GPTP_LOG_ERROR("LinuxTimerQueueHandler timerq lock failed");
            continue;
        }

        iter = timerq->timerQueueMap.find(info.si_value.sival_int);

        if ( iter != timerq->timerQueueMap.end() ) {
            struct LinuxTimerQueueActionArg *action_arg = iter->second;

            if (!action_arg->oneshot) {
                timerq->LinuxTimerQueueAction( action_arg );
            } else {
                timerq->timerQueueMap.erase(iter);
                timerq->LinuxTimerQueueAction( action_arg );

                if ( action_arg->rm ) {
                    delete (event_descriptor_t *)action_arg->inner_arg;
                }

                timer_delete(action_arg->timer_handle);
                delete action_arg;
            }
        }

        if ( timerq->lock->unlock() != oslock_ok ) {
            GPTP_LOG_ERROR("LinuxTimerQueueHandler timerq unlock failed");
            continue;
        }
    }

    return NULL;
}

void LinuxTimerQueue::LinuxTimerQueueAction( LinuxTimerQueueActionArg *arg )
{
    arg->func( arg->inner_arg );
    return;
}

OSTimerQueue *LinuxTimerQueueFactory::createOSTimerQueue
( IEEE1588Clock *clock )
{
    LinuxTimerQueue *ret = new LinuxTimerQueue();

    if (ret == NULL) {
        GPTP_LOG_ERROR("Failed to allocate LinuxTimerQueue");
        return NULL;
    }

    if ( !ret->init() ) {
        delete ret;
        return NULL;
    }

    ret->key = 0;
    ret->stop = false;
    ret->lock = clock->timerQLock();

    if ( pthread_create
            ( &(ret->_private->signal_thread),
              NULL, LinuxTimerQueueHandler, ret ) != 0 ) {
        delete ret;
        return NULL;
    }

    return ret;
}



bool LinuxTimerQueue::addEvent
( unsigned long micros, int type, ostimerq_handler func,
  void **arg, bool rm, unsigned *event, bool oneshot, timer_t **timer_handle)
{
    LinuxTimerQueueActionArg *outer_arg;
    int err;
    LinuxTimerQueueMap_t::iterator iter;
    outer_arg = new LinuxTimerQueueActionArg;
    if (outer_arg == NULL) {
        GPTP_LOG_ERROR("Failed to allocate LinuxTimerQueueActionArg in addEvent");
        return false;
    }
    outer_arg->inner_arg = *arg;
    outer_arg->rm = rm;
    outer_arg->func = func;
    outer_arg->type = type;
    outer_arg->oneshot = oneshot;

    // Find key that we can use
    while ( timerQueueMap.find( key ) != timerQueueMap.end() ) {
        ++key;
    }

    {
        struct itimerspec its;
        memset(&(outer_arg->sevp), 0, sizeof(outer_arg->sevp));
        outer_arg->sevp.sigev_notify = SIGEV_SIGNAL;
        outer_arg->sevp.sigev_signo  = SIGUSR1;
        outer_arg->sevp.sigev_value.sival_int = key;

        if ( timer_create
                (CLOCK_MONOTONIC, &outer_arg->sevp, &outer_arg->timer_handle)
                == -1) {
            GPTP_LOG_ERROR("timer_create failed - %s", strerror(errno));
            delete outer_arg;
            return false;
        }

        timerQueueMap[key] = outer_arg;
        memset(&its, 0, sizeof(its));
        its.it_value.tv_sec = micros / 1000000;
        its.it_value.tv_nsec = (micros % 1000000) * 1000;

        if (!outer_arg->oneshot) {
            its.it_interval.tv_sec = its.it_value.tv_sec;
            its.it_interval.tv_nsec = its.it_value.tv_nsec;
        }

        err = timer_settime( outer_arg->timer_handle, 0, &its, NULL );

        if ( err < 0 ) {
            fprintf
            ( stderr, "Failed to arm timer: %s\n",
              strerror( errno ));
            timer_delete(outer_arg->timer_handle);
            timerQueueMap.erase(key);
            delete outer_arg;
            return false;
        }
    }

    if (timer_handle != NULL) {
        **timer_handle = outer_arg->timer_handle;
    }

    return true;
}


bool LinuxTimerQueue::cancelEvent( int type, unsigned *event )
{
    LinuxTimerQueueMap_t::iterator iter;

    for ( iter = timerQueueMap.begin(); iter != timerQueueMap.end();) {
        if ( ((iter->second)->type == type) && ((iter->second)->oneshot) ) {
            // Delete element
            if ( (iter->second)->rm ) {
                delete (event_descriptor_t *)(iter->second)->inner_arg;
            }

            timer_delete(iter->second->timer_handle);
            delete iter->second;
            timerQueueMap.erase(iter++);
        } else {
            ++iter;
        }
    }

    return true;
}

bool LinuxTimerQueue::cancelTimer( timer_t **timer_handle )
{
    LinuxTimerQueueMap_t::iterator iter;

    for ( iter = timerQueueMap.begin(); iter != timerQueueMap.end();) {
        if ( (iter->second)->timer_handle == **timer_handle ) {
            // Delete element
            if ( (iter->second)->rm ) {
                delete (event_descriptor_t *)(iter->second)->inner_arg;
            }

            timer_delete(iter->second->timer_handle);
            delete iter->second;
            timerQueueMap.erase(iter++);
            GPTP_LOG_INFO("cancelTimer");
        } else {
            ++iter;
        }
    }

    return true;
}



void* OSThreadCallback( void* input )
{
    OSThreadArg *arg = (OSThreadArg*) input;
    arg->ret = arg->func( arg->arg );
    return 0;
}

bool LinuxTimestamper::post_init( int ifindex, int sd, TicketingLock *lock, bool tsc_enable)
{
    return true;
}

LinuxTimestamper::~LinuxTimestamper() {}

unsigned long LinuxTimer::sleep(unsigned long micros)
{
    struct timespec req;
    struct timespec rem;
    req.tv_sec = micros / 1000000;
    req.tv_nsec = micros % 1000000 * 1000;
    int ret = nanosleep( &req, &rem );

    while ( ret == -1 && errno == EINTR ) {
        req = rem;
        ret = nanosleep( &req, &rem );
    }

    if ( ret == -1 ) {
        fprintf
        ( stderr, "Error calling nanosleep: %s\n", strerror( errno ));
        _exit(-1);
    }

    return micros;
}

struct TicketingLockPrivate {
    pthread_cond_t condition;
    pthread_mutex_t cond_lock;
};

bool TicketingLock::lock( bool *got )
{
    uint8_t ticket;
    bool yield = false;
    bool ret = true;

    if ( !init_flag ) {
        return false;
    }

    if ( pthread_mutex_lock( &_private->cond_lock ) != 0 ) {
        ret = false;
        goto done;
    }

    // Take a ticket
    ticket = cond_ticket_issue++;

    while ( ticket != cond_ticket_serving ) {
        if ( got != NULL ) {
            *got = false;
            --cond_ticket_issue;
            yield = true;
            goto unlock;
        }

        if ( pthread_cond_wait( &_private->condition, &_private->cond_lock ) != 0 ) {
            ret = false;
            goto unlock;
        }
    }

    if ( got != NULL ) {
        *got = true;
    }

unlock:

    if ( pthread_mutex_unlock( &_private->cond_lock ) != 0 ) {
        ret = false;
        goto done;
    }

#ifdef ANDROID

    if ( yield ) {
        sched_yield();
    }

#else

    if ( yield ) {
        pthread_yield();
    }

#endif
done:
    return ret;
}

bool TicketingLock::unlock()
{
    bool ret = true;

    if ( !init_flag ) {
        return false;
    }

    if ( pthread_mutex_lock( &_private->cond_lock ) != 0 ) {
        ret = false;
        goto done;
    }

    ++cond_ticket_serving;

    if ( pthread_cond_broadcast( &_private->condition ) != 0 ) {
        ret = false;
        goto unlock;
    }

unlock:

    if ( pthread_mutex_unlock( &_private->cond_lock ) != 0 ) {
        ret = false;
        goto done;
    }

done:
    return ret;
}

bool TicketingLock::init()
{
    int err;

    if ( init_flag ) {
        return false;    // Don't do this more than once
    }

    _private = new TicketingLockPrivate;

    if ( _private == NULL ) {
        return false;
    }

    err = pthread_mutex_init( &_private->cond_lock, NULL );

    if ( err != 0 ) {
        return false;
    }

    err = pthread_cond_init( &_private->condition, NULL );

    if ( err != 0 ) {
        return false;
    }

    in_use = false;
    cond_ticket_issue = 0;
    cond_ticket_serving = 0;
    init_flag = true;
    return true;
}

TicketingLock::TicketingLock()
{
    init_flag = false;
    _private = NULL;
}

TicketingLock::~TicketingLock()
{
    if ( _private != NULL ) {
        delete _private;
        _private = NULL;
    }
}

struct LinuxLockPrivate {
    pthread_t thread_id;
    pthread_mutexattr_t mta;
    pthread_mutex_t mutex;
    pthread_cond_t port_ready_signal;
};

bool LinuxLock::initialize( OSLockType type )
{
    int lock_c;
    _private = new LinuxLockPrivate;

    if ( _private == NULL ) {
        return false;
    }

    pthread_mutexattr_init(&_private->mta);

    if ( type == oslock_recursive ) {
        pthread_mutexattr_settype(&_private->mta, PTHREAD_MUTEX_RECURSIVE);
    }

    lock_c = pthread_mutex_init(&_private->mutex, &_private->mta);

    if (lock_c != 0) {
        GPTP_LOG_ERROR("Mutex initialization failed - %s", strerror(errno));
        return oslock_fail;
    }

    return oslock_ok;
}

LinuxLock::~LinuxLock()
{
    int lock_c = pthread_mutex_lock(&_private->mutex);

    if (lock_c == 0) {
        pthread_mutex_destroy( &_private->mutex );
    }
}

OSLockResult LinuxLock::lock()
{
    int lock_c;
    lock_c = pthread_mutex_lock(&_private->mutex);

    if (lock_c != 0) {
        fprintf( stderr, "LinuxLock: lock failed %d\n", lock_c );
        return oslock_fail;
    }

    return oslock_ok;
}

OSLockResult LinuxLock::trylock()
{
    int lock_c;
    lock_c = pthread_mutex_trylock(&_private->mutex);

    if (lock_c != 0) {
        return oslock_fail;
    }

    return oslock_ok;
}

OSLockResult LinuxLock::unlock()
{
    int lock_c;
    lock_c = pthread_mutex_unlock(&_private->mutex);

    if (lock_c != 0) {
        fprintf( stderr, "LinuxLock: unlock failed %d\n", lock_c );
        return oslock_fail;
    }

    return oslock_ok;
}

struct LinuxConditionPrivate {
    pthread_cond_t port_ready_signal;
    pthread_mutex_t port_lock;
};


LinuxCondition::~LinuxCondition()
{
    if ( _private != NULL ) {
        delete _private;
    }
}

bool LinuxCondition::initialize()
{
    int lock_c;
    _private = new LinuxConditionPrivate;

    if ( _private == NULL ) {
        return false;
    }

    pthread_cond_init(&_private->port_ready_signal, NULL);
    lock_c = pthread_mutex_init(&_private->port_lock, NULL);

    if (lock_c != 0) {
        return false;
    }

    return true;
}

bool LinuxCondition::wait_prelock()
{
    pthread_mutex_lock(&_private->port_lock);
    up();
    return true;
}

bool LinuxCondition::wait()
{
    pthread_cond_wait(&_private->port_ready_signal, &_private->port_lock);
    down();
    pthread_mutex_unlock(&_private->port_lock);
    return true;
}

bool LinuxCondition::signal()
{
    pthread_mutex_lock(&_private->port_lock);

    if (waiting()) {
        pthread_cond_broadcast(&_private->port_ready_signal);
    }

    pthread_mutex_unlock(&_private->port_lock);
    return true;
}

struct LinuxThreadPrivate {
    pthread_t thread_id;
};

bool LinuxThread::start(OSThreadFunction function, void *arg)
{
    sigset_t set;
    sigset_t oset;
    int err;
    _private = new LinuxThreadPrivate;

    if ( _private == NULL ) {
        return false;
    }

    arg_inner = new OSThreadArg();
    if (arg_inner == NULL) {
        GPTP_LOG_ERROR("Failed to allocate OSThreadArg in LinuxThread::start");
        delete _private;
        _private = NULL;
        return false;
    }
    arg_inner->func = function;
    arg_inner->arg = arg;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    err = pthread_sigmask(SIG_BLOCK, &set, &oset);

    if (err != 0) {
        GPTP_LOG_ERROR
        ("Add timer pthread_sigmask( SIG_BLOCK ... )");
        delete arg_inner;
        arg_inner = NULL;
        return false;
    }

    err = pthread_create(&_private->thread_id, NULL, OSThreadCallback,
                         arg_inner);

    if (err != 0) {
        GPTP_LOG_ERROR("pthread_create failed in LinuxThread::start");
        delete arg_inner;
        arg_inner = NULL;
        return false;
    }

    sigdelset(&oset, SIGALRM);
    err = pthread_sigmask(SIG_SETMASK, &oset, NULL);

    if (err != 0) {
        GPTP_LOG_ERROR
        ("Add timer pthread_sigmask( SIG_SETMASK ... )");
        return false;
    }

    return true;
}

bool LinuxThread::join(OSThreadExitCode & exit_code)
{
    int err = 0;

    if (_private && _private->thread_id != 0) {
        err = pthread_join(_private->thread_id, NULL);
        if (err != 0) {
            GPTP_LOG_ERROR("pthread_join failed: %d (%s)", err, strerror(err));
            return false;
        }
    }

    if (err != 0) {
        return false;
    }

    exit_code = arg_inner->ret;
    delete arg_inner;
    return true;
}

LinuxThread::LinuxThread()
{
    _private = NULL;
};

LinuxThread::~LinuxThread()
{
    if ( _private != NULL ) {
        delete _private;
        _private = NULL;
    }
}

LinuxSharedMemoryIPC::~LinuxSharedMemoryIPC()
{
    if ((master_offset_buffer != (char*) -1) && (master_offset_buffer != NULL)) {
        memset(master_offset_buffer, 0x0, SHM_SIZE);
        munmap(master_offset_buffer, SHM_SIZE);
    }

#ifdef ANDROID

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    //unlink( SHM_NAME );
#else

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    //shm_unlink(SHM_NAME);
#endif
#ifdef LE_SHARED_MEM

    if (gptp_fd != -1) {
        close(gptp_fd);
        gptp_fd = -1;
    }

#endif
#ifdef GPTP_VFIO
       vfio_ptp_device_deinit();
#endif
}


bool LinuxSharedMemoryIPC::init(
    OS_IPC_ARG* barg,
    int8_t reverseSyncEnabled,
    int8_t reverseSyncDomain,
    double reverseSyncRate,
    bool waitForSync )
{
    pthread_mutexattr_t shared;
    LinuxIPCArg* arg;
    struct group* grp;
    const char* group_name;
    mode_t oldumask = umask(0);
    int count = 0;
    int ret = -1;

    if (barg == NULL) {
        group_name = DEFAULT_GROUPNAME;
    } else {
        arg = dynamic_cast<LinuxIPCArg*> (barg);

        if (arg == NULL) {
            GPTP_LOG_ERROR("Wrong IPC init arg type");
            goto exit_error;
        } else {
            group_name = arg->group_name;
        }
    }

    grp = getgrnam(group_name);

    if (grp == NULL) {
        GPTP_LOG_INFO("Group %s not found, will try root (0) instead", group_name);
    }

#ifdef ANDROID
    shm_fd = open(SHM_NAME, O_RDWR | O_CREAT, 0666);
#else
    shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0660);
#endif

    if (shm_fd == -1) {
        GPTP_LOG_ERROR("shm_open(): %s", strerror(errno));
        goto exit_error;
    }

#ifdef LE_SHARED_MEM

    do {
        gptp_fd = open("/dev/gptp", O_RDWR);

        if (gptp_fd == -1 || (FD_TO_CLOCKID(gptp_fd)) == -1) {
            GPTP_LOG_ERROR("Failed to open gPTP kernel device %d %d", gptp_fd, count);
            usleep(50000);
            count++;
        } else {
            GPTP_LOG_INFO("opened gptp kernel device: /dev/gptp");
        }
    } while (((gptp_fd == -1)
              || ((FD_TO_CLOCKID(gptp_fd)) == -1))
             && (count < 1000));

#endif
    (void)umask(oldumask);

    if (fchown(shm_fd, -1, grp != NULL ? grp->gr_gid : 0) < 0) {
        GPTP_LOG_ERROR("shm_open(): Failed to set ownership");
    }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        GPTP_LOG_ERROR("ftruncate()");
        goto exit_unlink;
    }

    master_offset_buffer = (char*)mmap
                           (NULL, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_LOCKED | MAP_SHARED,
                            shm_fd, 0);

    if (master_offset_buffer == (char*) -1) {
        GPTP_LOG_ERROR("mmap()");
        goto exit_unlink;
    }

    memset(master_offset_buffer, 0x0, SHM_SIZE);
#ifdef GPTP_VFIO
    prev_gptp_sync_time = 0;
    prev_qtimer_sync_time = 0;
    prev_qtimer_inc_ratio = 1000000000;
    GPTP_LOG_INFO("LinuxSharedMemoryIPC::init vfio ptp entry.\n");
    ret = vfio_ptp_device_init();

    if (ret) {
        GPTP_LOG_INFO("LinuxSharedMemoryIPC:: vfio_ptp_device_init fail, ret %d\n",
                      ret);
        return false;
    }

    master_offset_buffer_vfio = (char*)vfio_carveout_mem_addr();

    if (master_offset_buffer_vfio == (char*) -1) {
        GPTP_LOG_ERROR("mmap()");
        goto exit_unlink;
    }

    qtimer_base_addr = (uintptr_t)vfio_qtimer_base_addr();
    ptp_base_addr = (uintptr_t)vfio_ptp_base_addr();
    memset(master_offset_buffer_vfio, 0x0, SHM_SIZE);
#endif
    /* set reverse sync parameters on sharedmem*/
    gPtpTimeData* ptimedata;
    ptimedata = (gPtpTimeData*)(master_offset_buffer + sizeof(pthread_mutex_t));
    ptimedata->reverseSyncEnabled = reverseSyncEnabled;
    ptimedata->reverseSyncDomain = reverseSyncDomain;
    ptimedata->reverseSyncRate = reverseSyncRate;

    if (waitForSync == 0) {
        ptimedata->d_status = DEAMON_UP;
        GPTP_LOG_ERROR("(%s:%d) waitforsync = %d", __func__, __LINE__, waitForSync);
    }

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
    err = pthread_mutex_init((pthread_mutex_t*)master_offset_buffer, &shared);

    if (err != 0) {
        GPTP_LOG_ERROR
        ("sharedmem - Mutex initialization failed - %s",
         strerror(errno));
        goto exit_unlink;
    }

    return true;
exit_unlink :
#ifdef ANDROID

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

    //unlink(SHM_NAME);
#else

    if (shm_fd != -1) {
        close(shm_fd);
        shm_fd = -1;
    }

#endif
#ifdef LE_SHARED_MEM

    if (gptp_fd != -1) {
        close(gptp_fd);
        gptp_fd = -1;
    }

#endif
exit_error:
    GPTP_LOG_ERROR("LinuxSharedMemoryIPC::init exit_error\n");
#ifdef GPTP_VFIO
    vfio_ptp_device_deinit();
#endif
    return false;
}

#ifdef GPTP_VFIO
void LinuxSharedMemoryIPC::vfio_ptp(int64_t ml_phoffset,
                                    int64_t ls_phoffset,
                                    int64_t lq_phoffset,
                                    int64_t lb_phoffset,
                                    FrequencyRatio ml_freqoffset,
                                    FrequencyRatio ls_freqoffset,
                                    FrequencyRatio lq_freqoffset,
                                    FrequencyRatio lb_freqoffset,
                                    uint64_t local_time,
                                    uint32_t sync_count,
                                    uint32_t pdelay_count,
                                    PortState port_state,
                                    bool asCapable,
                                    uint32_t process_path)
{
    int buf_offset = 0;
    pid_t process_id = getpid();
    gPtpTimeData* ptimedata;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;
    char* shm_buffer_vfio = master_offset_buffer_vfio;

    if (shm_buffer_vfio != NULL) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer_vfio;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer_vfio + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata = (gPtpTimeData*)(shm_buffer_vfio + buf_offset);
        seq0->fetch_add(1);
        ptimedata->ml_phoffset = ml_phoffset;
        ptimedata->ls_phoffset = ls_phoffset;
        ptimedata->lq_phoffset = lq_phoffset;
        ptimedata->ls_freqoffset = ls_freqoffset;
        ptimedata->lq_freqoffset = lq_freqoffset;
        ptimedata->local_time = local_time;
        ptimedata->sync_count = sync_count;
        ptimedata->pdelay_count = pdelay_count;
        ptimedata->asCapable = asCapable;
        ptimedata->port_state = port_state;
        ptimedata->process_id = process_id;
        ptimedata->lb_freqoffset = lb_freqoffset;
        ptimedata->lb_phoffset = lb_phoffset;
        ptimedata->d_status = DEAMON_UP;
#ifdef USE_CARVEOUT_GPTP
        /* Read 64 bits tick counter from QTMR0_F0V2_QTMR_V2
         * and write to the end of shared memory
         */
        uint64_t* current_tick = (uint64_t*)(shm_buffer_vfio + 0x1000 - sizeof(
                uint64_t));
        int64_t* ptp_qtimer_offset = (int64_t*)(shm_buffer_vfio + 0x1000 - 2 * sizeof(
                uint64_t));
        uint64_t* ptp_sync_time = (uint64_t*)(shm_buffer_vfio + 0x1000 - 5 * sizeof(
                uint64_t));
        uint64_t* qtimer_sync_time = (uint64_t*)(shm_buffer_vfio + 0x1000 - 6 * sizeof(
                                         uint64_t));
        uint64_t* qtimer_inc_ratio = (uint64_t*)(shm_buffer_vfio + 0x1000 - 7 * sizeof(
                                         uint64_t));
        uint32_t* a_lock1 = (uint32_t*)(shm_buffer_vfio + 0x1000 - 7 * sizeof(
                                            uint64_t) - sizeof(uint32_t));
        uint32_t* a_lock2 = (uint32_t*)(shm_buffer_vfio + 0x1000 - 7 * sizeof(
                                            uint64_t) - 2 * sizeof(uint32_t));
        uint32_t* d_status = (uint32_t*)(shm_buffer_vfio + 0x1000 - 10 * sizeof(
                                             uint64_t)); //location has to match the ptp-virtual
        uint64_t current_gptp_time = 0;
        uint64_t qtimer_tick = 0;
        uint64_t gptp_time_ns = 0;
        uint64_t gptp_time_s = 0;
        uint64_t gptp_time_s_pre = 0;
        a_lock1++;

#ifdef PTP_SW_QTIMER
#if __aarch64__
        asm volatile("mrs %0, cntvct_el0" : "=r" (qtimer_tick));
#else
        asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qtimer_tick));
#endif
#else
        qtimer_tick = in64((uintptr_t)qtimer_base_addr);
#endif
        ptimedata->local_time = local_time;
        /*Now Qtimer run with 19.2MHz clock*/
        uint64_t qtimer_ns = qtimer_tick * (1000000000.0 / 19200000.0);
        int64_t local_bypqtimer_offset = (int64_t)(qtimer_ns - ptimedata->local_time);
        *ptp_qtimer_offset = local_bypqtimer_offset;
        *current_tick = qtimer_tick;
        *d_status = DEAMON_UP; //Just an indication daemon is up

        if (prev_gptp_sync_time != 0) {
            *qtimer_inc_ratio = (2 * prev_qtimer_inc_ratio) / 3 + ((
                                    ptimedata->local_time - prev_gptp_sync_time) * 1000000000) / ((
                                                qtimer_ns - prev_qtimer_sync_time)) / 3;
            prev_qtimer_inc_ratio = *qtimer_inc_ratio;
        } else {
            *qtimer_inc_ratio = prev_qtimer_inc_ratio;
        }

        *ptp_sync_time = prev_gptp_sync_time = ptimedata->local_time;
        *qtimer_sync_time = prev_qtimer_sync_time = qtimer_ns;
        a_lock2++;
#endif
        seq1->fetch_add(1);
    }
}
#endif

bool LinuxSharedMemoryIPC::update(
    int64_t ml_phoffset,
    int64_t ls_phoffset,
    int64_t lq_phoffset,
    int64_t lb_phoffset,
    FrequencyRatio ml_freqoffset,
    FrequencyRatio ls_freqoffset,
    FrequencyRatio lq_freqoffset,
    FrequencyRatio lb_freqoffset,
    uint64_t local_time,
    uint32_t sync_count,
    uint32_t pdelay_count,
    PortState port_state,
    bool asCapable,
    RsyncStatus_t* rSync, uint32_t process_path)
{
    int buf_offset = 0;
    pid_t process_id = getpid();
    char* shm_buffer = master_offset_buffer;
    gPtpTimeData* ptimedata;

    if (shm_buffer != NULL) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t*)shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata = (gPtpTimeData*)(shm_buffer + buf_offset);
        ptimedata->ls_phoffset = ls_phoffset;
        ptimedata->lq_phoffset = lq_phoffset;
        ptimedata->ml_freqoffset = ml_freqoffset;
        ptimedata->ls_freqoffset = ls_freqoffset;
        ptimedata->lq_freqoffset = lq_freqoffset;
        ptimedata->local_time = local_time;
        ptimedata->sync_count = sync_count;
        ptimedata->pdelay_count = pdelay_count;
        ptimedata->asCapable = asCapable;
        ptimedata->port_state = port_state;
        ptimedata->process_id = process_id;
        ptimedata->lb_freqoffset = lb_freqoffset;
        ptimedata->lb_phoffset = lb_phoffset;
        ptimedata->d_status = DEAMON_UP;
        rSync->reverseSyncDomain = ptimedata->reverseSyncDomain;
        rSync->reverseSyncRate = ptimedata->reverseSyncRate;
        rSync->reverseSyncEnabled = ptimedata->reverseSyncEnabled;

        if ((ptimedata->port_state == PTP_SLAVE) ||
                ((ptimedata->port_state == PTP_MASTER) &&
                 (ptimedata->reverseSyncEnabled == 1) &&
                 (process_path == PROCESS_MESSAGE_PATH))) {
            ptimedata->ml_phoffset = ml_phoffset;
        } else if (ptimedata->reverseSyncEnabled == 0) {
            ptimedata->ml_phoffset = 0;
        }

        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t*)shm_buffer);
#ifdef GPTP_VFIO
        vfio_ptp(ml_phoffset,
                 ls_phoffset,
                 lq_phoffset,
                 lb_phoffset,
                 ml_freqoffset,
                 ls_freqoffset,
                 lq_freqoffset,
                 lb_freqoffset,
                 local_time,
                 sync_count,
                 pdelay_count,
                 port_state,
                 asCapable,
                 process_path);
#endif
    }

    return true;
}

bool LinuxSharedMemoryIPC::updateGmId(ClockIdentity& id, uint16_t portNumber)
{
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if ( shm_buffer != NULL ) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        id.getIdentityString(ptimedata->gmIdentifier);
        ptimedata->portNumber = portNumber;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if ( shm_buffer != NULL ) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        id.getIdentityString(ptimedata->gmIdentifier);
        ptimedata->portNumber = portNumber;
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
    return true;
}


bool LinuxSharedMemoryIPC::updateSyncStatus(bool is_sync, PortState port_state)
{
    int buf_offset = 0;
    int ret = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if (shm_buffer != NULL) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        ptimedata->sync_status = is_sync;
        ptimedata->port_state = port_state;
        memcpy(ptimedata->ptp_dev_index, ptp_dev_index, PTP_CLOCK_DEVICE_LENGTH);
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if (shm_buffer != NULL) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        ptimedata->sync_status = is_sync;
        ptimedata->port_state = port_state;
        memcpy(ptimedata->ptp_dev_index, ptp_dev_index, PTP_CLOCK_DEVICE_LENGTH);
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
#ifdef LE_SHARED_MEM

    if (gptp_fd != -1) {
        gptpTimeInfo_t ptp_status;
        ptp_status.status = is_sync;
        ptp_status.port_status = port_state;
        ret = ioctl(gptp_fd, SET_PTP_DATA, &ptp_status);

        if (ret) {
            GPTP_LOG_ERROR("set PTP status in kernel failed 0x%x (%s)\n", errno,
                           strerror(errno));

            if (gptp_fd != -1) {
                close(gptp_fd);
                gptp_fd = -1;
            }
        }

        GPTP_LOG_DEBUG("set PTP status updated in kernel: %d port_status %d\n",
                       ptp_status.status, ptp_status.port_status);
    }

#endif
    return true;
}

bool LinuxSharedMemoryIPC::setProxyMode(int32_t proxy_value)
{
    int buf_offset = 0;
    char* shm_buffer = master_offset_buffer;
    gPtpTimeData* ptimedata;

    if (shm_buffer != NULL) {
        /* lock */
        //pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata = (gPtpTimeData*)(shm_buffer + buf_offset);
        ptimedata->in_proxy_mode = proxy_value;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
        GPTP_LOG_DEBUG("in_proxy_mode = %" PRIu8 "\n", ptimedata->in_proxy_mode);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if (shm_buffer != NULL) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata = (gPtpTimeData*)(shm_buffer + buf_offset);
        seq0->fetch_add(1);
        ptimedata->in_proxy_mode = proxy_value;
        /* unlock */
        seq1->fetch_add(1);
        GPTP_LOG_DEBUG("in_proxy_mode = %" PRIu8 "\n", ptimedata->in_proxy_mode);
    }

#endif
    return true;
}

bool LinuxSharedMemoryIPC::updateEtherLinkState(EtherPortLinkState_t LinkState)
{
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;
    if (shm_buffer != NULL) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        ptimedata->etherPortLinkState = LinkState;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if (shm_buffer != NULL) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        ptimedata->etherPortLinkState = LinkState;
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
    return true;
}

bool LinuxSharedMemoryIPC::getSyncStatus(void)
{
    bool sync_stat = 0;
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if (shm_buffer != NULL) {
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        sync_stat = ptimedata->sync_status;
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if (shm_buffer != NULL) {
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        sync_stat = ptimedata->sync_status;
        seq1->fetch_add(1);
    }

#endif
    return sync_stat;
}


bool LinuxSharedMemoryIPC::updateQtimeToMonoOffset(int64_t offset)
{
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if (shm_buffer != NULL) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        ptimedata->qtime_to_mono_offset = offset;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if (shm_buffer != NULL) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        ptimedata->qtime_to_mono_offset = offset;
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
    return true;
}

bool LinuxSharedMemoryIPC::update_grandmaster(
    uint8_t gptp_grandmaster_id[],
    uint8_t gptp_domain_number )
{
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if ( shm_buffer != NULL ) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        memcpy(ptimedata->gptp_grandmaster_id, gptp_grandmaster_id,
               PTP_CLOCK_IDENTITY_LENGTH);
        ptimedata->gptp_domain_number = gptp_domain_number;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if ( shm_buffer != NULL ) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        memcpy(ptimedata->gptp_grandmaster_id, gptp_grandmaster_id,
               PTP_CLOCK_IDENTITY_LENGTH);
        ptimedata->gptp_domain_number = gptp_domain_number;
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
    return true;
}

bool LinuxSharedMemoryIPC::update_network_interface(
    uint8_t  clock_identity[],
    uint8_t  priority1,
    uint8_t  clock_class,
    int16_t  offset_scaled_log_variance,
    uint8_t  clock_accuracy,
    uint8_t  priority2,
    uint8_t  domain_number,
    int8_t   log_sync_interval,
    int8_t   log_announce_interval,
    int8_t   log_pdelay_interval,
    uint16_t port_number )
{
    int buf_offset = 0;
    char *shm_buffer = master_offset_buffer;
    gPtpTimeData *ptimedata;

    if ( shm_buffer != NULL ) {
        /* lock */
        pthread_mutex_lock((pthread_mutex_t *) shm_buffer);
        buf_offset += sizeof(pthread_mutex_t);
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        memcpy(ptimedata->clock_identity, clock_identity, PTP_CLOCK_IDENTITY_LENGTH);
        ptimedata->priority1 = priority1;
        ptimedata->clock_class = clock_class;
        ptimedata->offset_scaled_log_variance = offset_scaled_log_variance;
        ptimedata->clock_accuracy = clock_accuracy;
        ptimedata->priority2 = priority2;
        ptimedata->domain_number = domain_number;
        ptimedata->log_sync_interval = log_sync_interval;
        ptimedata->log_announce_interval = log_announce_interval;
        ptimedata->log_pdelay_interval = log_pdelay_interval;
        ptimedata->port_number   = port_number;
        /* unlock */
        pthread_mutex_unlock((pthread_mutex_t *) shm_buffer);
    }

#ifdef GPTP_VFIO
    buf_offset = 0;
    shm_buffer = master_offset_buffer_vfio;
    std::atomic<uint32_t> *seq0;
    std::atomic<uint32_t> *seq1;

    if ( shm_buffer != NULL ) {
        /* lock */
        seq0 = (std::atomic<uint32_t> *)shm_buffer;
        seq1 = (std::atomic<uint32_t> *)(shm_buffer + sizeof(
                                         std::atomic<uint32_t>));
        buf_offset += (2 * sizeof(std::atomic<uint32_t>));
        ptimedata   = (gPtpTimeData *) (shm_buffer + buf_offset);
        seq0->fetch_add(1);
        memcpy(ptimedata->clock_identity, clock_identity, PTP_CLOCK_IDENTITY_LENGTH);
        ptimedata->priority1 = priority1;
        ptimedata->clock_class = clock_class;
        ptimedata->offset_scaled_log_variance = offset_scaled_log_variance;
        ptimedata->clock_accuracy = clock_accuracy;
        ptimedata->priority2 = priority2;
        ptimedata->domain_number = domain_number;
        ptimedata->log_sync_interval = log_sync_interval;
        ptimedata->log_announce_interval = log_announce_interval;
        ptimedata->log_pdelay_interval = log_pdelay_interval;
        ptimedata->port_number   = port_number;
        /* unlock */
        seq1->fetch_add(1);
    }

#endif
    return true;
}

void LinuxSharedMemoryIPC::ipc_down() {
    if ( master_offset_buffer != NULL ) {
        memset(master_offset_buffer, 0x0, SHM_SIZE);
    }
}

void LinuxSharedMemoryIPC::stop()
{
    if ( master_offset_buffer != NULL ) {
        memset(master_offset_buffer, 0x0, SHM_SIZE);
        munmap( master_offset_buffer, SHM_SIZE );
#ifdef ANDROID

        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
        }

        // unlink( SHM_NAME );
#else

        if (shm_fd != -1) {
            close(shm_fd);
            shm_fd = -1;
        }

        //shm_unlink(SHM_NAME);
#endif
#ifdef LE_SHARED_MEM

        if (gptp_fd != -1) {
            close(gptp_fd);
            gptp_fd = -1;
        }

#endif
#ifdef GPTP_VFIO
        vfio_ptp_device_deinit();
#endif
    }
}

bool LinuxNetworkInterfaceFactory::createInterface
( OSNetworkInterface **net_iface, InterfaceLabel *label,
  CommonTimestamper *timestamper ,bool tsc_enable)
{
    struct ifreq device;
    int err;
    struct sockaddr_ll ifsock_addr;
    struct packet_mreq mr_8021as;
    LinkLayerAddress addr;
    int ifindex;
    LinuxNetworkInterface *net_iface_l;
    if (*net_iface != NULL) {
        net_iface_l = dynamic_cast<LinuxNetworkInterface *>(*net_iface);
        delete net_iface_l;
    }

    net_iface_l = new LinuxNetworkInterface();
    if ( !net_iface_l->net_lock.init()) {
        GPTP_LOG_ERROR( "Failed to initialize network lock");
        delete net_iface_l;
        return false;
    }

    InterfaceName *ifname = dynamic_cast<InterfaceName *>(label);

    if ( ifname == NULL ) {
        GPTP_LOG_ERROR( "ifname == NULL");
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    net_iface_l->sd_general = socket( PF_PACKET, SOCK_DGRAM, 0 );

    if ( net_iface_l->sd_general == -1 ) {
        GPTP_LOG_ERROR( "failed to open general socket: %s", strerror(errno));
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    net_iface_l->sd_event = socket( PF_PACKET, SOCK_DGRAM, 0 );

    if ( net_iface_l->sd_event == -1 ) {
        GPTP_LOG_ERROR
        ( "failed to open event socket: %s ", strerror(errno));
        close(net_iface_l->sd_general);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    memset( &device, 0, sizeof(device));
    ifname->toString( device.ifr_name, IFNAMSIZ - 1 );
    err = ioctl( net_iface_l->sd_event, SIOCGIFHWADDR, &device );

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ( "Failed to get interface address: %s", strerror( errno ));
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    addr = LinkLayerAddress( (uint8_t *)&device.ifr_hwaddr.sa_data );
    net_iface_l->local_addr = addr;
    err = ioctl( net_iface_l->sd_event, SIOCGIFINDEX, &device );

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ( "Failed to get interface index: %s", strerror( errno ));
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    ifindex = device.ifr_ifindex;
    net_iface_l->ifindex = ifindex;
    memset( &mr_8021as, 0, sizeof( mr_8021as ));
    mr_8021as.mr_ifindex = ifindex;
    mr_8021as.mr_type = PACKET_MR_MULTICAST;
    mr_8021as.mr_alen = 6;
    memcpy( mr_8021as.mr_address, P8021AS_MULTICAST, mr_8021as.mr_alen );
    err = setsockopt
          ( net_iface_l->sd_event, SOL_PACKET, PACKET_ADD_MEMBERSHIP,
            &mr_8021as, sizeof( mr_8021as ));

    if ( err == -1 ) {
        GPTP_LOG_ERROR
        ( "Unable to add PTP multicast addresses to port id: %u",
          ifindex );
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    memset( &ifsock_addr, 0, sizeof( ifsock_addr ));
    ifsock_addr.sll_family = AF_PACKET;
    ifsock_addr.sll_ifindex = ifindex;
    ifsock_addr.sll_protocol = PLAT_htons( PTP_ETHERTYPE );
    err = bind
          ( net_iface_l->sd_event, (sockaddr *) &ifsock_addr,
            sizeof( ifsock_addr ));

    if ( err == -1 ) {
        GPTP_LOG_ERROR( "Call to bind() failed: %s", strerror(errno) );
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    net_iface_l->timestamper =
        dynamic_cast <LinuxTimestamper *>(timestamper);

    if (net_iface_l->timestamper == NULL) {
        GPTP_LOG_ERROR( "timestamper == NULL" );
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    if ( !net_iface_l->timestamper->post_init
            ( ifindex, net_iface_l->sd_event, &net_iface_l->net_lock, tsc_enable)) {
        GPTP_LOG_ERROR( "post_init failed\n" );
        close(net_iface_l->sd_general);
        close(net_iface_l->sd_event);
        delete net_iface_l;
        net_iface_l  = NULL;
        return false;
    }

    *net_iface = net_iface_l;
    return true;
}

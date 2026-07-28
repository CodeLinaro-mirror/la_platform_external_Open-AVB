/******************************************************************************

  Copyright (c) 2012 Intel Corporation
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

#include "ieee1588.hpp"
#include "avbts_clock.hpp"
#include "avbts_osnet.hpp"
#include "avbts_oslock.hpp"
#include "avbts_persist.hpp"
#include "gptp_cfg.hpp"
#ifdef RGPTP_ENABLED
#include "rgptp.hpp"
#endif
#ifdef ARCH_INTELCE
#include "linux_hal_intelce.hpp"
#else
#include "linux_hal_generic.hpp"
#endif

#include "linux_hal_persist_file.hpp"
#include <ctype.h>
#include <inttypes.h>
#include <signal.h>
#include <inttypes.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <linux/ptp_clock.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>

#ifdef ANDROID
#include <cutils/sockets.h>
#endif

#include "qgptp_rmgr.h"

#ifdef SYSTEMD_WATCHDOG
#include <watchdog.hpp>
#endif

#define PHY_DELAY_GB_TX_I20 184 //1G delay
#define PHY_DELAY_GB_RX_I20 382 //1G delay
#define PHY_DELAY_MB_TX_I20 1044//100M delay
#define PHY_DELAY_MB_RX_I20 2133//100M delay

#define MAX_STR_LEN 2048

#ifdef SYSTEMD
#ifdef ANDROID
#define ADDRESS     "/dev/socket/gptp_socket"
#else
#define ADDRESS     "/dev/socket/gptp/gptp_socket"
#endif // END OF ANDROID
#else
#define ADDRESS     "/tmp/gptp_socket"
#endif
#define MAX_CLIENTS_COUNT 5
#define MAX_EVENTS 1

#ifdef RGPTP_ENABLED
#define RGPTP_MIN_GPIO_PULSE_TIME_MS 125
#define RGPTP_MAX_GPIO_PULSE_TIME_MS 5000
#endif
char ifname_eth[IFNAME_SIZE] = {0};
static IEEE1588Clock *pClock = NULL;
static EtherPort *pPort = NULL;

void gPTPPersistWriteCB(char *bufPtr, uint32_t bufSize);

static int sock = 0;
static bool using_android_socket = false;
static pthread_t thread_id = 0;
static int keep_running = 0;
struct sockaddr_un sock_addr_un;
static struct sockaddr cli_addr;
static socklen_t cli_len = sizeof(cli_addr);
static int gptp_client[MAX_CLIENTS_COUNT] = {0};


// gptp logcat support
extern gptplogcat_t gptplogcat;
extern gptplogcat_t systemlogcat;
LinuxSharedMemoryIPC *ipc;

#define MAX_NSEC 1000000000

#ifdef GPTP_DSQB_ENABLED
lpm_t lpm_handle;
#endif

#ifdef ANDROID
#ifdef GPTP_CAR_POWER_POLICY
static constexpr const char* GPTP_SOCKET_PATH = "/dev/socket/gptp_power_socket";

enum PowerEvent {
    POWER_SUSPEND = 0,
    POWER_RESUME  = 1,
};

static void* powerListenerThread(void* arg) {
    int fd = (int)(intptr_t)arg;

    PowerEvent ev;

    while (true) {
        sockaddr_un sender;
        socklen_t sender_len = sizeof(sender);
        ssize_t n = recvfrom(fd, &ev, sizeof(ev), 0,
                             reinterpret_cast<sockaddr*>(&sender), &sender_len);
        if (n < 0) {
            if (errno == EINTR) {
                GPTP_LOG_WARNING("powerListenerThread: recvfrom EINTR, retrying");
                continue;
            }
            GPTP_LOG_ERROR("powerListenerThread: recvfrom failed fd=%d: %s",
                           fd, strerror(errno));
            break;
        }

        if (n == 0) {
            GPTP_LOG_ERROR("powerListenerThread: recvfrom returned 0 fd=%d", fd);
            break;
        }

        if (n != (ssize_t)sizeof(ev)) {
            GPTP_LOG_WARNING("powerListenerThread: short recv %zd of %zu bytes, discarding",
                             n, sizeof(ev));
            continue;
        }

        if (!pPort) {
            continue;
        }

        if (ev == POWER_SUSPEND) {
            GPTP_LOG_INFO("Power: SUSPEND");
            if (pPort->gPTP_lpm == false) {
                pPort->gPTP_lpm = true;
                pPort->processEvent(LINKDOWN);
            }
        } else if (ev == POWER_RESUME) {
            if (pPort->gPTP_lpm == true) {
                GPTP_LOG_INFO("Power: RESUME");
                pPort->gPTP_lpm = false;
                pPort->processEvent(LINKUP);
            }
        }
    }

    close(fd);
    return nullptr;
}

static void startPowerListener() {
    int fd = -1;

    // First try to get the socket fd from Android init (via init.rc socket declaration)
    fd = android_get_control_socket("gptp_power_socket");
    if (fd > 0) {
        GPTP_LOG_INFO("Got gptp_power_socket fd from init: %d", fd);
    } else {
        // Fallback: create and bind manually
        GPTP_LOG_WARNING("android_get_control_socket(gptp_power_socket) failed, falling back to manual bind");
        fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) {
            GPTP_LOG_ERROR("socket() failed: %s", strerror(errno));
            return;
        }

        sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strlcpy(addr.sun_path, GPTP_SOCKET_PATH, sizeof(addr.sun_path));

        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            GPTP_LOG_ERROR("bind() failed: %s", strerror(errno));
            close(fd);
            return;
        }
    }

    pthread_t tid;
    int ret = pthread_create(&tid, nullptr, powerListenerThread,
                             (void*)(intptr_t)fd);

    if (ret != 0) {
        GPTP_LOG_ERROR("pthread_create failed: %s", strerror(ret));
        close(fd);
        return;
    }

    pthread_detach(tid);
}

#endif
#endif

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

#define CLEANUP_RESOURCES() {\
                if( thread_factory != NULL ) delete thread_factory; thread_factory = NULL; \
                if( default_factory != NULL ) delete default_factory; default_factory = NULL; \
                if( timerq_factory != NULL ) delete timerq_factory; timerq_factory = NULL; \
                if( lock_factory != NULL ) delete lock_factory; lock_factory = NULL; \
                if( timer_factory != NULL ) delete timer_factory; timer_factory = NULL; \
                if( condition_factory != NULL ) delete condition_factory; condition_factory = NULL; \
                if( ipc != NULL ) delete ipc; ipc = NULL; \
                if( ifname != NULL ) delete ifname; ifname = NULL; \
                if( ipc_arg != NULL ) delete ipc_arg; ipc_arg = NULL; \
                if( timestamper != NULL ) delete timestamper; timestamper = NULL; \
                if( pClock != NULL ) delete pClock; pClock = NULL; \
                if( pGPTPPersist != NULL ) { \
                    pGPTPPersist->closeStorage(); \
                    delete pGPTPPersist; pGPTPPersist = NULL;    \
                    }\
                }


void print_usage( char *arg0 )
{
    fprintf( stderr,
             "%s <network intf name/'ini' if intf is mentioned in config ini file> [-S] [-P] [-M <filename>] "
             "[-C ] [-G <group>] [-R <priority 1>] "
             "[-D <gb_tx_delay,gb_rx_delay,mb_tx_delay,mb_rx_delay>] "
             "[-T] [-L] [-E] [-B] [-V] [-N] [-GM] [-INITSYNC <value>] [-OPERSYNC <value>] "
             "[-INITPDELAY <value>] [-OPERPDELAY <value>] [-SYNCLOSSTHRESH <value>] "
             "[-RSYNC <value>] [-RSYNC_DOMAIN <value>] [-RSYNC_RATE <value>]"
             "[-F <path to gptp_cfg.ini file>] "
             "\n",
             arg0 );
    fprintf
    ( stderr,
      "\t-S start syntonization\n"
      "\t-P pulse per second\n"
      "\t-M <filename> save/restore state\n"
      "\t-C print logs in console \n"
      "\t-G <group> group id for shared memory\n"
      "\t-R <priority 1> priority 1 value\n"
      "\t-D Phy Delay <gb_tx_delay,gb_rx_delay,mb_tx_delay,mb_rx_delay>\n"
      "\t-T force master (ignored when Automotive Profile set)\n"
      "\t-L force slave (ignored when Automotive Profile set)\n"
      "\t-E enable test mode (as defined in AVnu automotive profile)\n"
      "\t-B to bypass Interface check & wait\n"
      "\t-V enable AVnu Automotive Profile\n"
      "\t-N Neighbor prop delay threshold\n"
      "\t-GM set grandmaster for Automotive Profile\n"
      "\t-INITSYNC <value> initial sync interval (Log base 2. 0 = 1 second)\n"
      "\t-OPERSYNC <value> operational sync interval (Log base 2. 0 = 1 second)\n"
      "\t-INITPDELAY <value> initial pdelay interval (Log base 2. 0 = 1 second)\n"
      "\t-OPERPDELAY <value> operational pdelay interval (Log base 2. 0 = 1 sec)\n"
      "\t-SYNCLOSSTHRESH <value> sync loss threshold default value 6000000 ns\n"
      "\t-RSYNC <value> reverse sync enable\n"
      "\t-RSYNC_DOMAIN <value> reverse sync domain\n"
      "\t-RSYNC_RATE <value> reverse sync rate\n"
      "\t-F <path-to-ini-file>\n"
#ifdef RGPTP_ENABLED
      "\t-Y Periodic GPIO pulse time in ms\n"
#endif
    );
}

#ifdef GPTP_DSQB_ENABLED
int gptp_sys_suspend(void *data, enum PM_MODE mode)
{
    bool err = false;
    GPTP_LOG_INFO("Handling LPM(mode: %d) enter notification", mode);
    GPTP_LOG_INFO("stoping gptp daemon....");

    if (pPort->gPTP_lpm == false) {
        pPort->gPTP_lpm = true;
        err = pPort->processEvent(LINKDOWN);

        if (err == false) {
            GPTP_LOG_ERROR("failed to ds_suspend, roll back and NACK");
            pPort->gPTP_lpm = false;
            return -1;
        }
    } else {
        GPTP_LOG_WARNING("gptp_sys_suspend: already in LPM state, ignoring duplicate suspend");
    }

    return 0;
}

int gptp_sys_resume(void *data, enum PM_MODE mode)
{
    GPTP_LOG_INFO("Handling LPM(mode: %d) exit notification", mode);
    GPTP_LOG_INFO("starting gptp daemon....");

    if (pPort->gPTP_lpm == true) {
        pPort->processEvent(LINKUP);
        pPort->gPTP_lpm = false;
    } else {
        GPTP_LOG_WARNING("gptp_sys_resume: not in LPM state, ignoring duplicate resume");
    }

    return 0;
}

struct pm_ops_s gptp_lpm_ops = {
    .pm_enter = gptp_sys_suspend,
    .pm_exit = gptp_sys_resume,
};

/*! \fn int gptp_sys_register_lpm()
    \brief This function registers gptp as external client to the server/RM via snservice interface.
    \return int32_t
*/
int gptp_sys_register_lpm()
{
    int err = 0;
    GPTP_LOG_INFO("Registering lpm callbacks");
    err = pm_register("gptp", &gptp_lpm_ops, NULL, &lpm_handle);

    if (err) {
        GPTP_LOG_ERROR("LPM registration failed with err: %d", err);
        return -1;
    }

    return 0;
}

int gptp_sys_deregister_lpm()
{
    int err = 0;
    GPTP_LOG_INFO("Deregistering lpm callbacks");
    err = pm_deregister(lpm_handle);

    if (err) {
        GPTP_LOG_ERROR("LPM deregistration failed with err: %d", err);
        return -1;
    }

    return 0;
}
#endif

int watchdog_setup(OSThreadFactory *thread_factory)
{
#ifdef SYSTEMD_WATCHDOG
    SystemdWatchdogHandler *watchdog = new SystemdWatchdogHandler();
    OSThread *watchdog_thread = thread_factory->createThread();
    int watchdog_result;
    long unsigned int watchdog_interval;
    watchdog_interval = watchdog->getSystemdWatchdogInterval(&watchdog_result);

    if (watchdog_result) {
        GPTP_LOG_INFO("Watchtog interval read from service file: %lu us",
                      watchdog_interval);
        watchdog->update_interval = watchdog_interval / 2;
        GPTP_LOG_STATUS("Starting watchdog handler (Update every: %lu us)",
                        watchdog->update_interval);
        watchdog_thread->start(watchdogUpdateThreadFunction, watchdog);
        return 0;
    } else if (watchdog_result < 0) {
        GPTP_LOG_ERROR("Watchdog settings read error.");
        return -1;
    } else {
        GPTP_LOG_STATUS("Watchdog disabled");
        return 0;
    }

#else
    return 0;
#endif
}

static void *wait_for_epoll_event(void *arg)
{
    int j = 0;
    int epoll_fd, cli_fd;
    struct epoll_event ev;
    struct epoll_event *epoll_events;
    socklen_t cli_len = sizeof(cli_addr);
    epoll_fd = epoll_create(1);
    memset(gptp_client, -1, MAX_CLIENTS_COUNT * sizeof(int));

    if (epoll_fd == -1) {
        GPTP_LOG_ERROR("epoll_create() failed : %s\n", strerror(errno));
        return NULL;
    }
    int ret = pthread_setname_np(pthread_self(), "wait_for_epoll");
    if (ret != 0) {
        GPTP_LOG_ERROR("pthread_setname_np failed");
    }

    GPTP_LOG_INFO("gptpDaemonServInit: wait_for_epoll_event successful\n");
    ev.data.fd = sock;
    ev.events = EPOLLIN | EPOLLET;

    if ((epoll_ctl (epoll_fd, EPOLL_CTL_ADD, sock, &ev)) == -1) {
        GPTP_LOG_ERROR("epoll_ctl() failed : %s\n", strerror(errno));
        return NULL;
    }

    GPTP_LOG_INFO("Added fd : %d to the epoll\n", sock);
    epoll_events = (struct epoll_event *) calloc(MAX_EVENTS, sizeof(ev));

    if (NULL == epoll_events) {
        GPTP_LOG_ERROR("epoll_events alloc failed : %s\n", strerror(errno));
        close(epoll_fd);
        return NULL;
    }

    while (keep_running) {
        int n, i;
        n = epoll_wait (epoll_fd, epoll_events, MAX_EVENTS, 1000);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            GPTP_LOG_ERROR("epoll_wait() failed : %s\n", strerror(errno));
            break;
        }

        for (i = 0; i < n; i++) {
            if ((epoll_events[i].events & EPOLLERR)
                    || (epoll_events[i].events & EPOLLHUP)) {
                GPTP_LOG_INFO("Received EPOLLERR or EPOLLHUP event from client with fd : %d\n",
                              epoll_events[i].data.fd);

                for (j = 0; j < MAX_CLIENTS_COUNT; j++) {
                    if (epoll_events[i].data.fd == gptp_client[j]) {
                        GPTP_LOG_INFO("close client socket j index %d \n", j);
                        gptp_client[j] = -1;
                        break;
                    }
                }

                close(epoll_events[i].data.fd);
            }

            if (sock == epoll_events[i].data.fd) {
                cli_fd = accept (sock, (struct sockaddr *) &cli_addr, &cli_len);
                GPTP_LOG_INFO("Accepted socket connection from client with fd %d \n", cli_fd);

                if (cli_fd == -1) {
                    GPTP_LOG_ERROR("Accept socket connection error from client\n");
                    break;
                }

                for (j = 0; j < MAX_CLIENTS_COUNT; j++) {
                    if (gptp_client[j] == -1) {
                        GPTP_LOG_INFO("accept socket j index %d \n", j);
                        gptp_client[j] = cli_fd;
                        break;
                    }
                }

                ev.data.fd = cli_fd;
                ev.events = EPOLLIN | EPOLLET;

                if ((epoll_ctl (epoll_fd, EPOLL_CTL_ADD, cli_fd, &ev)) ==  -1) {
                    GPTP_LOG_ERROR("New connection epoll_ctl() failed : %s\n", strerror(errno));
                    break;
                }

                GPTP_LOG_INFO("Added client with fd : %d to the epoll\n", cli_fd);
            }
        }
    }
    close(epoll_fd);
    free(epoll_events);
    return NULL;
}

void gptpDaemonServDeInit(void)
{
    int ret = 0;
    if (!using_android_socket) {
        unlink(ADDRESS);
        close(sock);
        sock = 0;
    }
    // Signal the thread to exit
    keep_running = 0;

    // Wait for the epoll thread to exit only if it was started.
    if (thread_id != 0) {
        pthread_join(thread_id, NULL);
        thread_id = 0;
    }

    for (int i = 0 ; i < MAX_CLIENTS_COUNT; i++) {
        if(gptp_client[i] != -1) {
            close(gptp_client[i]);
            gptp_client[i] = -1;
        }
    }

    return;
}

void gptpDaemonServInit(void)
{
    socklen_t len = 0;
    int ret = 0;
    umask(S_IRGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
#ifdef ANDROID
    if (sock <= 0) {
        sock = android_get_control_socket("gptp_socket");

        if (sock > 0) {
            using_android_socket = true;
        } else {
            GPTP_LOG_ERROR("android_get_control_socket(gptp_socket) failed: %s\n", strerror(errno));
        }
    }

#endif
    keep_running = 1; //reset the flag to keep running thread

    if (sock <= 0) {
        /* Create gptp daemon socket */
        sock = socket(AF_UNIX, SOCK_STREAM, 0);

        if (sock == -1) {
            GPTP_LOG_ERROR("Socket creation failed : %s\n", strerror(errno));
            exit(1);
        }

        GPTP_LOG_INFO("Socket creation successful\n");
        fcntl(sock, F_SETFL, (fcntl (sock, F_GETFL, 0) | O_NONBLOCK));
        memset(&sock_addr_un, 0, sizeof(sockaddr_un));
        sock_addr_un.sun_family = AF_UNIX;
        snprintf(sock_addr_un.sun_path, (sizeof(sock_addr_un.sun_path) - 1), ADDRESS);
        len = sizeof(sock_addr_un);
        unlink(ADDRESS);

        if ((bind(sock, (struct sockaddr*) &sock_addr_un, len)) == -1) {
            GPTP_LOG_ERROR("bind() failed : %s\n", strerror(errno));
            close(sock);
            exit(1);
        }

        GPTP_LOG_INFO("Socket bind successful\n");
    }

    if ((listen (sock, MAX_CLIENTS_COUNT)) == -1) {
        GPTP_LOG_ERROR("listen() failed : %s", strerror(errno));
        close(sock);
        exit(1);
    }

    GPTP_LOG_INFO("Socket listen successful\n");
    ret = pthread_create(&thread_id, NULL, wait_for_epoll_event, NULL);

    if (ret != 0) {
        GPTP_LOG_ERROR("Failed to create wait_for_epoll_event: %s\n", strerror(errno));
        close(sock);
        exit(1);
    }

    return;
}


bool waitForInterface()
{
#if 0
    struct ifreq ifrq;
    int sockfd = socket(PF_PACKET, SOCK_DGRAM, 0);
    memset(&ifrq, 0, sizeof(ifrq));
    PLAT_strlcpy(ifrq.ifr_name, ifname_eth, IFNAME_SIZE);

    if (ioctl(sockfd, SIOCGIFFLAGS, &ifrq) < 0) {
        perror("couldnt call ioctl with flag SIOCGIFFLAGS");
    }

    close(sockfd);
    GPTP_LOG_DEBUG( "waitForInterface %d %d \n", ifrq.ifr_flags, IFF_UP);
    return !(ifrq.ifr_flags & IFF_UP);
#else
    FILE* fp;
    char status[5] = {0};
    char buff[1024] = {0};
    snprintf(buff, 1024, "/sys/class/net/%s/operstate", ifname_eth);
    fp = fopen(buff, "r");

    if (!fp) {
        return true;
    }

    fread(status, 1, 5, fp);
    fclose(fp);
    GPTP_LOG_DEBUG( "waitForInterface status %c%c%c%c \n", status[0], status[1],
                    status[2], status[3]);

    if ((status[0] == 'u' && status[1] == 'p') || (status[0] == 'U'
            && status[1] == 'P')) {
        return false;
    }

    return true;
#endif
}

int main(int argc, char **argv)
{
    PortInit_t portInit;
    sigset_t set;
    InterfaceName *ifname = NULL;
    int sig;
    bool syntonize = true;
    int i;
    bool pps = false;
    uint8_t priority1 = 248;
    uint8_t priority2 = 248;
    uint8_t clockClass = 248;
    bool override_portstate = false;
    PortState port_state = PTP_SLAVE;
    char *restoredata = NULL;
    char *restoredataptr = NULL;
    off_t restoredatalength = 0;
    off_t restoredatacount = 0;
    bool restorefailed = false;
    LinuxIPCArg *ipc_arg = NULL;
    EtherTimestamper *timestamper = NULL;
    bool use_config_file = false;
    char config_file_path[512];
    struct timespec timeout;
    int rc = 0;
    char reply_msg[MAX_STR_LEN];
    int bytes_written = 0;
#ifdef RGPTP_ENABLED
    bool rgptp = false;
#endif
    memset(config_file_path, 0, 512);
    GPTPPersist *pGPTPPersist = NULL;
    LinuxNetworkInterfaceFactory *default_factory = NULL;
    LinuxTimerQueueFactory *timerq_factory = NULL;
    LinuxLockFactory *lock_factory = NULL;
    LinuxTimerFactory *timer_factory = NULL;
    LinuxConditionFactory *condition_factory = NULL;
    LinuxThreadFactory *thread_factory = new LinuxThreadFactory();
    if (thread_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate thread_factory");
        return -1;
    }
    // Block SIGUSR1
    {
        sigset_t block;
        sigemptyset( &block );
        sigaddset( &block, SIGUSR1 );

        if ( pthread_sigmask( SIG_BLOCK, &block, NULL ) != 0 ) {
            GPTP_LOG_ERROR("Failed to block SIGUSR1");

            if ( thread_factory != NULL ) {
                delete thread_factory;
            }

            return -1;
        }
    }
    GPTP_LOG_REGISTER();

    if (watchdog_setup(thread_factory) != 0) {
        GPTP_LOG_ERROR("Watchdog handler setup error");
        return -1;
    }

    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset( &set, SIGTERM );
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGUSR2);
    phy_delay_map_t ether_phy_delay;
    bool input_delay = false;
    portInit.clock = NULL;
    portInit.index = 0;
    portInit.timestamper = NULL;
    portInit.net_label = NULL;
    portInit.automotive_profile = false;
    portInit.isGM = false;
    portInit.asCapable = false;
    portInit.testMode = false;
    portInit.linkUp = false;
    portInit.isSigNoSend = false;
    portInit.disableSigMsg = false;
    portInit.allowedLostResponses = DEFAULT_ALLOWED_LOST_RESPONSES;
    portInit.initialLogSyncInterval = LOG2_INTERVAL_INVALID;
    portInit.initialLogPdelayReqInterval = LOG2_INTERVAL_INVALID;
    portInit.operLogPdelayReqInterval = LOG2_INTERVAL_INVALID;
    portInit.operLogSyncInterval = LOG2_INTERVAL_INVALID;
    portInit.syncArrivalTimeDiffTolerance = NAN;
    portInit.reverseSyncEnabled = LOG2_INTERVAL_INVALID;
    portInit.reverseSyncDomain = LOG2_INTERVAL_INVALID;
    portInit.reverseSyncRate = LOG2_INTERVAL_INVALID;
    portInit.condition_factory = NULL;
    portInit.thread_factory = NULL;
    portInit.timer_factory = NULL;
    portInit.lock_factory = NULL;
    portInit.announceReceiptTimeout = 3;
    portInit.syncReceiptTimeout = 3;
    portInit.syncClocks = 0;
    portInit.syncReceiptThreshold =
        CommonPort::DEFAULT_SYNC_RECEIPT_THRESH;
    portInit.neighborPropDelayThreshold =
        CommonPort::NEIGHBOR_PROP_DELAY_THRESH;
    portInit.stbMSyncLossThreshold =
        CommonPort::STBM_SYNCLOSS_THRESH;
    portInit._peer_rate_offset = 1.0;
    portInit.sct_buffer = NULL;
    portInit.sct_shm_fd = -1;
    portInit.bypass_if_wait = false;
    portInit.wait_for_sync = false;
    portInit.tsc_enable = false;
    default_factory = new LinuxNetworkInterfaceFactory;
    if (default_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate default_factory");
        CLEANUP_RESOURCES();
        return -1;
    }
    OSNetworkInterfaceFactory::registerFactory
    (factory_name_t("default"), default_factory);
    timerq_factory = new LinuxTimerQueueFactory();
    if (timerq_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate timerq_factory");
        CLEANUP_RESOURCES();
        return -1;
    }
    lock_factory = new LinuxLockFactory();
    if (lock_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate lock_factory");
        CLEANUP_RESOURCES();
        return -1;
    }
    timer_factory = new LinuxTimerFactory();
    if (timer_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate timer_factory");
        CLEANUP_RESOURCES();
        return -1;
    }
    condition_factory = new LinuxConditionFactory();
    if (condition_factory == NULL) {
        GPTP_LOG_ERROR("Failed to allocate condition_factory");
        CLEANUP_RESOURCES();
        return -1;
    }
    ipc = new LinuxSharedMemoryIPC();
    if (ipc == NULL) {
        GPTP_LOG_ERROR("Failed to allocate LinuxSharedMemoryIPC");
        CLEANUP_RESOURCES();
        return -1;
    }

    /* Create Low level network interface object */
    if ( argc < 2 ) {
        printf( "Interface name required\n" );
        print_usage( argv[0] );
        CLEANUP_RESOURCES();
        return -1;
    }

    /* Process optional arguments */
    for ( i = 2; i < argc; ++i ) {
        if ( argv[i][0] == '-' ) {
            if ( strcmp(argv[i] + 1,  "S") == 0 ) {
                // Get syntonize directive from command line
                syntonize = true;
            } else if ( strcmp(argv[i] + 1,  "T" ) == 0 ) {
                override_portstate = true;
                port_state = PTP_MASTER;
            } else if ( strcmp(argv[i] + 1,  "L" ) == 0 ) {
                override_portstate = true;
                port_state = PTP_SLAVE;
            } else if ( strcmp(argv[i] + 1,  "C" ) == 0 ) {
                systemlogcat = GPTP_LOG_OFF;
                gptplogcat = GPTP_LOG_OFF;
            } else if ( strcmp(argv[i] + 1,  "M" )  == 0 ) {
                // Open file
                if ( i + 1 < argc ) {
                    if (pGPTPPersist == NULL) {
                        pGPTPPersist = makeLinuxGPTPPersistFile();
                    }

                    if (pGPTPPersist) {
                        pGPTPPersist->initStorage(argv[i + 1]);
                    }
                } else {
                    GPTP_LOG_ERROR( "Restore file must be specified on "
                                    "command line\n" );
                }
            } else if ( strcmp(argv[i] + 1,  "G") == 0 ) {
                if ( i + 1 < argc ) {
                    if (ipc_arg == NULL) {
                        ipc_arg = new LinuxIPCArg(argv[++i]);
                    }
                } else {
                    GPTP_LOG_ERROR( "Must specify group name on the command line\n" );
                }
            } else if ( strcmp(argv[i] + 1,  "P") == 0 ) {
                pps = true;
            } else if ( strcmp(argv[i] + 1,  "H") == 0 ) {
                print_usage( argv[0] );
                GPTP_LOG_UNREGISTER();
                CLEANUP_RESOURCES();
                return 0;
            } else if ( strcmp(argv[i] + 1,  "R") == 0 ) {
                if ( i + 1 >= argc ) {
                    GPTP_LOG_ERROR( "Priority 1 value must be specified on "
                                    "command line, using default value\n" );
                } else {
                    unsigned long tmp = strtoul( argv[i + 1], NULL, 0 );
                    ++i;

                    if ( tmp == 0 ) {
                        GPTP_LOG_ERROR( "Invalid priority 1 value, using "
                                        "default value\n" );
                    } else {
                        priority1 = (uint8_t) tmp;
                    }
                }
            } else if (strcmp(argv[i] + 1, "D") == 0) {
                int phy_delay[4];
                input_delay = true;
                int delay_count = 0;
                char *saveptr;
                char *cli_inp_delay = strtok_r(argv[i + 1], ",", &saveptr);

                while (cli_inp_delay != NULL) {
                    if (delay_count > 3) {
                        GPTP_LOG_ERROR("Too many values\n");
                        print_usage( argv[0] );
                        GPTP_LOG_UNREGISTER();
                        CLEANUP_RESOURCES();
                        return 0;
                    }

                    phy_delay[delay_count] = atoi(cli_inp_delay);
                    delay_count++;
                    cli_inp_delay = strtok_r(NULL, ",", &saveptr);
                }

                if (delay_count != 4) {
                    GPTP_LOG_ERROR("All four delay values must be specified\n");
                    print_usage( argv[0] );
                    GPTP_LOG_UNREGISTER();
                    CLEANUP_RESOURCES();
                    return 0;
                }

                ether_phy_delay[LINKSPEED_1G].set_delay
                ( phy_delay[0], phy_delay[1] );
                ether_phy_delay[LINKSPEED_100MB].set_delay
                ( phy_delay[2], phy_delay[3] );
            } else if (strcmp(argv[i] + 1, "V") == 0) {
                portInit.automotive_profile = true;
            } else if (strcmp(argv[i] + 1, "GM") == 0) {
                portInit.isGM = true;
            } else if (strcmp(argv[i] + 1, "DISSIGMSG") == 0) {
                portInit.disableSigMsg = true;
            } else if (strcmp(argv[i] + 1, "E") == 0) {
                portInit.testMode = true;
            } else if (strcmp(argv[i] + 1, "B") == 0) {
                portInit.bypass_if_wait = true;
            } else if (strcmp(argv[i] + 1, "S") == 0) {
                portInit.wait_for_sync = true;
            } else if (strcmp(argv[i] + 1, "INITSYNC") == 0) {
                portInit.initialLogSyncInterval = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "OPERSYNC") == 0) {
                portInit.operLogSyncInterval = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "RSYNC") == 0) {
                portInit.reverseSyncEnabled = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "RSYNC_DOMAIN") == 0) {
                portInit.reverseSyncDomain = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "RSYNC_RATE") == 0) {
                portInit.reverseSyncRate = atof(argv[++i]);
            } else if (strcmp(argv[i] + 1, "INITPDELAY") == 0) {
                portInit.initialLogPdelayReqInterval = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "OPERPDELAY") == 0) {
                portInit.operLogPdelayReqInterval = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "SYNC_TOLERANCE") == 0) {
                portInit.syncArrivalTimeDiffTolerance = atof(argv[++i]);
                GPTP_LOG_ERROR("portInit.syncArrivalTimeDiffTolerance: %lF\n",portInit.syncArrivalTimeDiffTolerance);
            } else if (strcmp(argv[i] + 1, "F") == 0) {
                if ( i + 1 < argc ) {
                    use_config_file = true;
                    snprintf(config_file_path, sizeof(config_file_path), "%s", argv[i + 1]);
                } else {
                    GPTP_LOG_ERROR("config file must be specified.\n");
                }
            } else if (strcmp(argv[i] + 1, "SYNCLOSSTHRESH") == 0) {
                portInit.stbMSyncLossThreshold = atoi(argv[++i]);
            } else if (strcmp(argv[i] + 1, "N") == 0) {
                if ( i + 1 < argc ) {
                    portInit.neighborPropDelayThreshold = atoi(argv[++i]);
                    GPTP_LOG_INFO("neighborPropDelayThreshold value:% " PRId64 " ",
                                  portInit.neighborPropDelayThreshold);
                }
            } else if (strcmp(argv[i] + 1, "TSC_EN") == 0) {
#ifndef ANDROID
                portInit.tsc_enable = true;
#else
                portInit.tsc_enable = false;
                GPTP_LOG_ERROR("TSC disabled on Android, not supported\n");
#endif
            }
#ifdef RGPTP_ENABLED
            else if (strcmp(argv[i] + 1, "Y") == 0) {
                rgptp = true;
                portInit.rgptpSyncTime = RGPTP_MIN_GPIO_PULSE_TIME_MS;

                if (( i + 1 < argc ) && (isdigit(*argv[i + 1]))) {
                    int sync_interval = atoi(argv[++i]);

                    if (sync_interval < RGPTP_MIN_GPIO_PULSE_TIME_MS) {
                        portInit.rgptpSyncTime = RGPTP_MIN_GPIO_PULSE_TIME_MS;
                        GPTP_LOG_INFO("rgptp - set pulse time default min value: %ums",
                                      portInit.rgptpSyncTime);
                    } else if (sync_interval > RGPTP_MAX_GPIO_PULSE_TIME_MS) {
                        portInit.rgptpSyncTime = RGPTP_MAX_GPIO_PULSE_TIME_MS;
                        GPTP_LOG_INFO("rgptp - set pulse time default max value: %ums",
                                      portInit.rgptpSyncTime);
                    } else {
                        portInit.rgptpSyncTime = sync_interval;
                        GPTP_LOG_INFO("rgptp - set pulse time value: %ums", portInit.rgptpSyncTime);
                    }
                } else {
                    GPTP_LOG_INFO("rgptp - set pulse time default value: %ums",
                                  portInit.rgptpSyncTime);
                }
            }

#endif
        }
    }

    if (strcmp(argv[1], "ini") != 0) {
        PLAT_strlcpy(ifname_eth, argv[1], IFNAME_SIZE);
        ifname = new InterfaceName( argv[1], strlen(argv[1]) );
        if (ifname == NULL) {
            GPTP_LOG_ERROR("Failed to allocate InterfaceName for argv[1]");
            CLEANUP_RESOURCES();
            return -1;
        }
    } else if (!use_config_file) {
        GPTP_LOG_ERROR( "Interface name required/ ini file is required\n" );
        print_usage( argv[0] );
        CLEANUP_RESOURCES();
        return -1;
    }

    if (!input_delay) {
        ether_phy_delay[LINKSPEED_1G].set_delay
        ( PHY_DELAY_GB_TX_I20, PHY_DELAY_GB_RX_I20 );
        ether_phy_delay[LINKSPEED_100MB].set_delay
        ( PHY_DELAY_MB_TX_I20, PHY_DELAY_MB_RX_I20 );
    }

    portInit.phy_delay = &ether_phy_delay;

    if ( ipc_arg != NULL ) {
        delete ipc_arg;
        ipc_arg = NULL;
    }

    if ( pGPTPPersist ) {
        uint32_t bufSize = 0;

        if (!pGPTPPersist->readStorage(&restoredata, &bufSize)) {
            GPTP_LOG_ERROR("Failed to stat restore file");
        }

        restoredatalength = bufSize;
        restoredatacount = restoredatalength;
        restoredataptr = (char *)restoredata;
    }

#ifdef ARCH_INTELCE
    timestamper = new LinuxTimestamperIntelCE();
#else
    timestamper = new LinuxTimestamperGeneric();
#endif
    if (timestamper == NULL) {
        GPTP_LOG_ERROR("Failed to allocate EtherTimestamper");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
        perror("pthread_sigmask()");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    // TODO: The setting of values into temporary variables should be changed to
    // just set directly into the portInit struct.
    //portInit.clock = pClock;
    portInit.index = 1;
    portInit.timestamper = timestamper;
    portInit.condition_factory = condition_factory;
    portInit.thread_factory = thread_factory;
    portInit.timer_factory = timer_factory;
    portInit.lock_factory = lock_factory;

    if (use_config_file) {
        GptpIniParser iniParser(config_file_path);

        if (iniParser.parserError() < 0) {
            GPTP_LOG_ERROR("Can't parse ini file. Aborting file reading..\nExiting gptp daemon.");
            GPTP_LOG_UNREGISTER();
            CLEANUP_RESOURCES();
            return -1;
        } else {
            GPTP_LOG_INFO("priority1 = %d", iniParser.getPriority1());
            GPTP_LOG_INFO("announceReceiptTimeout: %d",
                          iniParser.getAnnounceReceiptTimeout());
            GPTP_LOG_INFO("syncReceiptTimeout: %d", iniParser.getSyncReceiptTimeout());
            iniParser.print_phy_delay();
            GPTP_LOG_INFO("neighborPropDelayThresh: %ld",
                          iniParser.getNeighborPropDelayThresh());
            GPTP_LOG_INFO("stbMSyncLossThreshold: %ld",
                          iniParser.getStbMSyncLossThreshold());
            GPTP_LOG_INFO("syncReceiptThreshold: %d", iniParser.getSyncReceiptThresh());
            GPTP_LOG_INFO("initialLogSyncInterval: %d",
                          iniParser.getInitialLogSyncInterval());
            GPTP_LOG_INFO("initialLogPdelayReqInterval: %d",
                          iniParser.getInitialLogPdelayReqInterval());
            priority1 =  iniParser.getPriority1();
            priority2 =  iniParser.getPriority2();
            clockClass = iniParser.getclockClass();
            port_state = iniParser.getPortState();
            portInit.bypass_if_wait = iniParser.getIsIfCheckBypass();
            portInit.wait_for_sync = iniParser.getwaitForSync();

            if (strcmp(argv[1], "ini") == 0) {
                std::string if_name = iniParser.getIfaceName();
                PLAT_strlcpy(ifname_eth, if_name.c_str(), IFNAME_SIZE);
                ifname = new InterfaceName( ifname_eth, strlen(ifname_eth) );
                if (ifname == NULL) {
                    GPTP_LOG_ERROR("Failed to allocate InterfaceName from ini config");
                    GPTP_LOG_UNREGISTER();
                    CLEANUP_RESOURCES();
                    return -1;
                }
            }

            if (!portInit.testMode && iniParser.getDebugLog() != 0) {
                portInit.testMode = true;
            }

            if (port_state == PTP_MASTER) {
                override_portstate = true;
                GPTP_LOG_INFO("Configuring port state to master\n");
            } else {
                override_portstate = true;
                GPTP_LOG_INFO("Configuring port state to Slave\n");
            }

            portInit.syncReceiptTimeout = iniParser.getSyncReceiptTimeout();
            portInit.announceReceiptTimeout = iniParser.getAnnounceReceiptTimeout();
            portInit.operLogSyncInterval = iniParser.getOperLogSyncInterval();
            portInit.operLogPdelayReqInterval = iniParser.getOperLogPdelayReqInterval();
            portInit.syncArrivalTimeDiffTolerance = iniParser.getSyncArrivalTimeDiffTolerance();
            portInit.reverseSyncEnabled = iniParser.getIsRsync();
            portInit.disableSigMsg = iniParser.getIsSigMsgDisabled();
            portInit.reverseSyncDomain = iniParser.getRSyncDomain();
            portInit.reverseSyncRate = iniParser.getRSyncRate();
            portInit.automotive_profile = iniParser.getAutomotiveProfile();
            portInit.isGM = iniParser.getIsGM();
            portInit.syncClocks = iniParser.getSyncClocks();
            portInit.asCapable = iniParser.getAsCapable();
#ifndef ANDROID
            portInit.tsc_enable = iniParser.getTscEnable();
#else
            portInit.tsc_enable = false;
#endif
            GPTP_LOG_INFO("syncClocks: %d", portInit.syncClocks);
            GPTP_LOG_INFO("automotive profile %s isGM %s\n",
                          ((portInit.automotive_profile) ? "True" : "False"),
                          ((portInit.isGM) ? "True" : "False"));
            GPTP_LOG_INFO("priority1 %d and priority2 %d clockClass %d srt %d art %d\n",
                          priority1, priority2, clockClass, portInit.syncReceiptTimeout,
                          portInit.announceReceiptTimeout);
            /* If using config file, set the neighborPropDelayThresh.
             * Otherwise it will use its default value (800ns) */
            portInit.neighborPropDelayThreshold =
                iniParser.getNeighborPropDelayThresh();
            /* If using config file, set the stbMSyncLossThreshold.
             * Otherwise it will use its default value (6000000ns) */
            portInit.stbMSyncLossThreshold =
                iniParser.getStbMSyncLossThreshold();
            /* If using config file, set the syncReceiptThreshold, otherwise
             * it will use the default value (SYNC_RECEIPT_THRESH)
             */
            portInit.syncReceiptThreshold =
                iniParser.getSyncReceiptThresh();
            /* If using config file, set the initialLogSyncInterval.
             * Otherwise it will use its default value -5 */
            portInit.initialLogSyncInterval =
                iniParser.getInitialLogSyncInterval();
            /* If using config file, set the initialLogPdelayReqInterval.
             * Otherwise it will use its default value 0 */
            portInit.initialLogPdelayReqInterval =
                iniParser.getInitialLogPdelayReqInterval();

            /*Only overwrites phy_delay default values if not input_delay switch enabled*/
            if (!input_delay) {
                ether_phy_delay = iniParser.getPhyDelay();
            }
        }
    }

    portInit.net_label = ifname;

    if ( !ipc->init( ipc_arg, portInit.reverseSyncEnabled,
                     portInit.reverseSyncDomain, portInit.reverseSyncRate, portInit.wait_for_sync) ) {
        delete ipc;
        ipc = NULL;
        GPTP_LOG_ERROR( "ipc init failed\n" );
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    qgptp_rmgr_init(&portInit.sct_shm_fd, &portInit.sct_buffer);

    if ((strcmp(ifname_eth, "eth0") != 0) && (strcmp(ifname_eth, "eth1") != 0) && (strcmp(ifname_eth, "eth2") != 0)) {
        GPTP_LOG_INFO( "Valid Interface name required\n" );
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    if (!portInit.bypass_if_wait) {
        timeout.tv_sec = 1;
        timeout.tv_nsec = 0;
        GPTP_LOG_INFO( "waiting for eth interface to be up.. \n");

        while (waitForInterface()) {
            sig = sigtimedwait(&set, NULL, &timeout);

            if (sig == SIGINT || sig == SIGTERM || sig == SIGHUP || sig == SIGUSR2 ) {
                perror("sigtimedwait()");
                GPTP_LOG_UNREGISTER();
                CLEANUP_RESOURCES();
                return 0;
            }

            GPTP_LOG_DEBUG( "waitForInterface %d \n", sig);
        }

        GPTP_LOG_INFO( "eth interface is up.. \n");
        sig = 0;
    } else {
        GPTP_LOG_INFO( "Bypass Ethernet check.. \n");
    }

    pClock = new IEEE1588Clock
    ( false, syntonize, priority1, priority2, clockClass, timerq_factory, ipc,
      lock_factory );
    if (pClock == NULL) {
        GPTP_LOG_ERROR("Failed to allocate IEEE1588Clock");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    if ( restoredataptr != NULL ) {
        restorefailed =
            !pClock->restoreSerializedState( restoredataptr, &restoredatacount );
        restoredataptr = ((char *)restoredata) + (restoredatalength - restoredatacount);
    }

    portInit.clock = pClock;
#ifdef PTP_SW_QTIMER
    {
        int64_t qtimer_to_mono_to_offset = 0;
        int64_t qtimer_to_mono_to_offset_min = INT64_MAX;
        int64_t qtimer_to_mono_to_offset_max = INT64_MIN;
        int64_t avg_offset = 0;

        // Find average delta between qtimer and system time
        for (int i = 0; i < 20; ++i ) {
            struct timespec mono;
            struct ptp_clock_time mono_pct;
            struct ptp_clock_time qtimer_pct;
            uint64_t qTimerCount = 0, qTimerFreq = 0, qTimerNanosSec = 0,
                     qTimerNanosNSec = 0;
            int64_t offset;
            clock_gettime(CLOCK_MONOTONIC, &mono);
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
            qtimer_pct.sec = qTimerNanosSec;
            qtimer_pct.nsec = qTimerNanosNSec;
            mono_pct.sec = mono.tv_sec;
            mono_pct.nsec = mono.tv_nsec;
            offset = pctns(pct_diff(&qtimer_pct, &mono_pct));
            qtimer_to_mono_to_offset += offset;

            if (offset < qtimer_to_mono_to_offset_min) {
                qtimer_to_mono_to_offset_min = offset;
            } else if (offset > qtimer_to_mono_to_offset_max) {
                qtimer_to_mono_to_offset_max = offset;
            }
        }

        avg_offset = (qtimer_to_mono_to_offset / 20);
        GPTP_LOG_WARNING("qtimer_to_mono_offset -- min:%ld max:%ld new avg:% " PRId64
                         " ",
                         qtimer_to_mono_to_offset_min, qtimer_to_mono_to_offset_max, avg_offset);

        if (ipc) {
            ipc->updateQtimeToMonoOffset(avg_offset);
        }
    }
#endif
    pPort = new EtherPort(&portInit);
    if (pPort == NULL) {
        GPTP_LOG_ERROR("Failed to allocate EtherPort");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }
    GPTP_LOG_ERROR("pPort TSC enable flag: %d\n", pPort->getTSC());
    if (!pPort->init_port()) {
        GPTP_LOG_ERROR("failed to initialize port");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }

    if ( restoredataptr != NULL ) {
        if ( !restorefailed ) {
            restorefailed = !pPort->restoreSerializedState( restoredataptr,
                            &restoredatacount );
            GPTP_LOG_INFO("Persistent port data restored: asCapable:%d, port_state:%d, one_way_delay:%lld, operLogPdelayReqInterval:%d, neighborRateRatio:%Lf\n",
                          pPort->getAsCapable(), pPort->getPortState(), pPort->getLinkDelay(),
                          pPort->getoperLogPdelayReqInterval(), pPort->getPeerRateOffset());
        }

        restoredataptr = ((char *)restoredata) + (restoredatalength - restoredatacount);
    }

    if (portInit.automotive_profile) {
        if (portInit.isGM) {
            port_state = PTP_MASTER;
        } else {
            port_state = PTP_SLAVE;
        }

        override_portstate = true;
    }

    if ( override_portstate ) {
        pPort->setPortState( port_state );
        pPort->setAsCapable( true );
    }

    // Start PPS if requested
    if ( pps ) {
        if ( !timestamper->HWTimestamper_PPS_start()) {
            GPTP_LOG_ERROR("Failed to start pulse per second I/O");
        }
    }

    // Configure persistent write
    if (pGPTPPersist) {
        off_t len = 0;
        restoredatacount = 0;
        pClock->serializeState(NULL, &len);
        restoredatacount += len;
        pPort->serializeState(NULL, &len);
        restoredatacount += len;
        pGPTPPersist->setWriteSize((uint32_t)restoredatacount);
        pGPTPPersist->registerWriteCB(gPTPPersistWriteCB);
    }

    gptpDaemonServInit();

#ifdef GPTP_CAR_POWER_POLICY
    // Start listening for power events
    startPowerListener();
#endif

#ifdef GPTP_DSQB_ENABLED
    /*Register gptp System for lpm*/
    rc = gptp_sys_register_lpm();

    if (rc) {
        GPTP_LOG_ERROR("gptp_sys_register_lpm failed");
        GPTP_LOG_UNREGISTER();
        CLEANUP_RESOURCES();
        return -1;
    }
#endif

    GPTP_LOG_INFO("gPTP starting...");
    pPort->processEvent(POWERUP);
    pPort->gPTP_lpm = false;
#ifdef RGPTP_ENABLED

    if ( rgptp ) {
        rgptpInit(&portInit);
    }

#endif

    do {
        sig = 0;

        if (sigwait(&set, &sig) != 0) {
            perror("sigwait()");
            GPTP_LOG_UNREGISTER();
            CLEANUP_RESOURCES();
            return -1;
        }

        if (sig == SIGHUP || sig == SIGINT) {
            if (pGPTPPersist) {
                // If port is either master or slave, save clock and then port state
                if (pPort->getPortState() == PTP_MASTER || pPort->getPortState() == PTP_SLAVE) {
                    pGPTPPersist->triggerWriteStorage();
                }
            }
        }

        if (sig == SIGUSR2) {
            pPort->logIEEEPortCounters();
            bytes_written = get_gptp_stats(reply_msg, 0);
            GPTP_LOG_STATUS("bytes_written = %d", bytes_written);
            char *saveptr;
            char *line = strtok_r(reply_msg, "\n", &saveptr);
            while (line != NULL) {
                GPTP_LOG_STATUS("%s", line);
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }
    } while (sig == SIGHUP || sig == SIGUSR2);

    GPTP_LOG_ERROR("Exiting on %d", sig);

    if (pGPTPPersist) {
        pGPTPPersist->closeStorage();
    }

    gptpDaemonServDeInit();

    if (pPort) {
        pPort->processEvent(POWERDOWN);
        qgptp_rmgr_deinit();
        delete pPort;
        pPort = NULL;
    }

    // Stop PPS if previously started
    if ( pps ) {
        if ( !timestamper->HWTimestamper_PPS_stop()) {
            GPTP_LOG_ERROR("Failed to stop pulse per second I/O");
        }
    }

    if ( ipc ) {
#ifdef LE_SHARED_MEM
        ipc->updateSyncStatus(false, PTP_DISABLED);
#endif
        delete ipc;
        ipc = NULL;
    }
#ifdef GPTP_DSQB_ENABLED
    gptp_sys_deregister_lpm();
#endif

#ifdef RGPTP_ENABLED

    if ( rgptp ) {
        rgptpDeInit();
    }

#endif
    GPTP_LOG_UNREGISTER();
    CLEANUP_RESOURCES();
    return 0;
}

void gPTPPersistWriteCB(char *bufPtr, uint32_t bufSize)
{
    if (pClock == NULL || pPort == NULL) {
        GPTP_LOG_ERROR("pClock or pPort is NULL in gPTPPersistWriteCB");
        return;
    }
    if (bufPtr == NULL) {
        GPTP_LOG_ERROR("bufPtr is NULL in gPTPPersistWriteCB");
        return;
    }
    off_t restoredatalength = bufSize;
    off_t restoredatacount = restoredatalength;
    char *restoredataptr = NULL;
    GPTP_LOG_INFO("Signal received to write restore data");
    restoredataptr = (char *)bufPtr;
    pClock->serializeState(restoredataptr, &restoredatacount);
    restoredataptr = ((char *)bufPtr) + (restoredatalength - restoredatacount);
    pPort->serializeState(restoredataptr, &restoredatacount);
    restoredataptr = ((char *)bufPtr) + (restoredatalength - restoredatacount);
}

/*************************************************************************************************************
Copyright (c) 2012-2016, Harman International Industries, Incorporated
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
*************************************************************************************************************/

/******************************************************************************

Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

#include <gptp_log.hpp>

#include <stdio.h>
#include <stdarg.h>
#include <platform.hpp>
#include <syslog.h>
#include <sys/syscall.h>
#include <unistd.h>
// MS VC++ 2013 has C++11 but not C11 support, use this to get millisecond resolution
#include <chrono>
#include <string.h>

#ifdef DLT_AVAILABLE
DLT_DECLARE_CONTEXT(dlt_con_gptp);
#endif

void gptplogRegister(void)
{
#ifdef DLT_AVAILABLE

    DLT_REGISTER_APP("GPTP", "OpenAVB gPTP");
    DLT_REGISTER_CONTEXT(dlt_con_gptp, "GNRL", "General Context");
#endif
}

void gptplogUnregister(void)
{
#ifdef DLT_AVAILABLE
    DLT_UNREGISTER_CONTEXT(dlt_con_gptp);
    DLT_UNREGISTER_APP();
#endif
}

// logcat support
#ifdef ANDROID
gptplogcat_t gptplogcat = GPTP_LOG_ON;
#else
gptplogcat_t gptplogcat = GPTP_LOG_OFF;
#endif

// DLT support
#ifdef DLT_AVAILABLE
gptplogcat_t gptpdlt = GPTP_LOG_ON;
#else
gptplogcat_t gptpdlt = GPTP_LOG_OFF;
#endif

gptplogcat_t systemlogcat = GPTP_LOG_ON;

LogLimit_t loglimit;

#define gettid() syscall(SYS_gettid)

void gptpLog(GPTP_LOG_LEVEL level, const char *tag, const char *path, int line,
             const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
#ifdef ANDROID

    if (gptplogcat) {
        LOGE(level, "[%s:%d] %s", path, line, msg);
    }

#elif defined(DLT_AVAILABLE)
    if (gptpdlt) {
        DLT_LOG(dlt_con_gptp, (DltLogLevelType)level, DLT_INT(gettid()), DLT_STRING(path), DLT_INT(line), DLT_STRING(msg));
    }
#else

    if (systemlogcat) {
        syslog(level, "[%ld:%s:%d] %s\n", gettid(), path, line, msg);
    }

#endif
    else {
        std::chrono::system_clock::time_point cNow = std::chrono::system_clock::now();
        time_t tNow = std::chrono::system_clock::to_time_t(cNow);
        struct tm tmNow;
        PLAT_localtime(&tNow, &tmNow);
        std::chrono::system_clock::duration roundNow = cNow -
                std::chrono::system_clock::from_time_t(tNow);
        long int millis = (long int)
                          std::chrono::duration_cast<std::chrono::milliseconds>(roundNow).count();
        fprintf(stderr, "%s:GPTP:[%2.2d:%2.2d:%2.2d:%3.3ld] [%ld:%s:%d] %s\n",
                tag, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, millis, gettid(), path, line,
                msg);
    }
}

void gptpLogMs(GPTP_LOG_LEVEL level, const char *tag, const char *path,
               int line,
               const char *fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
#ifdef ANDROID

    if (gptplogcat) {
        LOGE(level, "[%s:%d] %s", path, line, msg);
    }

#else

    if (systemlogcat) {
        std::chrono::system_clock::time_point cNow = std::chrono::system_clock::now();
        time_t tNow = std::chrono::system_clock::to_time_t(cNow);
        struct tm tmNow;
        PLAT_localtime(&tNow, &tmNow);
        std::chrono::system_clock::duration roundNow = cNow -
                std::chrono::system_clock::from_time_t(tNow);
        long int millis = (long int)
                          std::chrono::duration_cast<std::chrono::milliseconds>(roundNow).count();
        syslog(level, "[%2.2d:%2.2d:%2.2d:%3.3ld] [%ld:%s:%d] %s\n", tag,
               tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, millis, gettid(), path, line, msg);
    }

#endif
    else {
        std::chrono::system_clock::time_point cNow = std::chrono::system_clock::now();
        time_t tNow = std::chrono::system_clock::to_time_t(cNow);
        struct tm tmNow;
        PLAT_localtime(&tNow, &tmNow);
        std::chrono::system_clock::duration roundNow = cNow -
                std::chrono::system_clock::from_time_t(tNow);
        long int millis = (long int)
                          std::chrono::duration_cast<std::chrono::milliseconds>(roundNow).count();
        fprintf(stderr, "%s:GPTP:[%2.2d:%2.2d:%2.2d:%3.3ld] [%ld:%s:%d] %s\n",
                tag, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, millis, gettid(), path, line,
                msg);
    }
}

/**
 * @brief Reset the log limit
 * @param void
 * @return void
 */
void reset_log_limit(gptp_log_type_t type) {
    switch (type) {
        case SYNC_LOG :
                        {
                            loglimit.sync = 0;
                        }
                        break;
        case PDELAY_LOG :
                        {
                            loglimit.pDelay = 0;
                        }
                        break;
        case FOLLOW_UP_LOG :
                        {
                            loglimit.followup = 0;
                        }
                        break;
        case TX_TIMESTAMP_LOG:
                        {
                            loglimit.tx_timestamp = 0;
                        }
                        break;
        case RX_TIMESTAMP_LOG:
                        {
                            loglimit.rx_timestamp = 0;
                        }
                        break;
        case SEND_PORT_LOG:
                        {
                            loglimit.send_port = 0;
                        }
                        break;
        case RESET_ALL_LOG:
        default :
                        {
                            memset(&loglimit, '\0', sizeof(loglimit));
                        }
    }
}

/**
 * @brief check is log in the log_limit
 * @param gptp_log_type_t
 * @return true: log in limit, false: if not in limit
 */

bool is_in_log_limit(gptp_log_type_t type) {
    switch (type) {
        case SYNC_LOG :
                        {
                            if (loglimit.sync >= 0 && loglimit.sync < GPTP_MAX_SYNC_LOG) {
                                loglimit.sync++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        case PDELAY_LOG :
                        {
                            if (loglimit.pDelay >= 0 && loglimit.pDelay < GPTP_MAX_PDELAY_LOG) {
                                loglimit.pDelay++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        case FOLLOW_UP_LOG :
                        {
                            if (loglimit.followup >= 0 && loglimit.followup < GPTP_MAX_FOLLOWUP_LOG) {
                                loglimit.followup++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        case TX_TIMESTAMP_LOG:
                        {
                            if (loglimit.tx_timestamp >= 0 && loglimit.tx_timestamp < GPTP_MAX_TX_TIMESTAMP_LOG) {
                                loglimit.tx_timestamp++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        case RX_TIMESTAMP_LOG:
                        {
                            if (loglimit.rx_timestamp >= 0 && loglimit.rx_timestamp < GPTP_MAX_RX_TIMESTAMP_LOG) {
                                loglimit.rx_timestamp++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        case SEND_PORT_LOG:
                        {
                            if (loglimit.send_port >= 0 && loglimit.send_port < GPTP_MAX_SEND_PORT_LOG) {
                                loglimit.send_port++;
                                return true;
                            } else {
                                return false;
                            }
                        }
                        break;
        default :
                        return -1;
    }
}

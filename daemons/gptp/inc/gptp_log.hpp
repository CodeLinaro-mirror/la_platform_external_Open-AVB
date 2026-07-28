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

/************************************************************************************************************
Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear
*************************************************************************************************************/

#ifndef GPTP_LOG_HPP
#define GPTP_LOG_HPP

/**@file*/

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <syslog.h>
#include <stdint.h>

#ifdef GENIVI_DLT
#include "dlt.h"
#endif
#ifdef DLT_AVAILABLE
#include <dlt/dlt.h>
#include <unistd.h>
#endif

#ifdef ANDROID
#define LOG_TAG "gPTP"
#include <utils/Log.h>
#define LOGE(level,tag, ...) __android_log_print (level,"qgptp", tag, __VA_ARGS__)
#else
#define LOGE(level,tag, ...)
#endif

#define GPTP_LOG_CRITICAL_ON        1
#define GPTP_LOG_ERROR_ON           1
#define GPTP_LOG_EXCEPTION_ON       1
#define GPTP_LOG_WARNING_ON         1
#define GPTP_LOG_INFO_ON            1
#define GPTP_LOG_STATUS_ON          1
#ifdef DLT_AVAILABLE
#define GPTP_LOG_DEBUG_ON         1
#define GPTP_LOG_VERBOSE_ON       1
#endif

#ifndef ANDROID
#ifdef DLT_AVAILABLE

typedef enum {
    GPTP_LOG_LVL_CRITICAL = DLT_LOG_FATAL,
    GPTP_LOG_LVL_ERROR = DLT_LOG_ERROR,
    GPTP_LOG_LVL_EXCEPTION = DLT_LOG_ERROR,
    GPTP_LOG_LVL_WARNING = DLT_LOG_WARN,
    GPTP_LOG_LVL_INFO = DLT_LOG_INFO,
    GPTP_LOG_LVL_STATUS = DLT_LOG_INFO,
    GPTP_LOG_LVL_DEBUG = DLT_LOG_DEBUG,
    GPTP_LOG_LVL_VERBOSE = DLT_LOG_VERBOSE,
} GPTP_LOG_LEVEL;
#else
typedef enum {
    GPTP_LOG_LVL_CRITICAL,
    GPTP_LOG_LVL_ERROR,
    GPTP_LOG_LVL_EXCEPTION,
    GPTP_LOG_LVL_WARNING,
    GPTP_LOG_LVL_INFO,
    GPTP_LOG_LVL_STATUS,
    GPTP_LOG_LVL_DEBUG,
    GPTP_LOG_LVL_VERBOSE,
} GPTP_LOG_LEVEL;
#endif //DLT_AVAILABLE
#else

typedef enum {
    GPTP_LOG_LVL_CRITICAL =  ANDROID_LOG_FATAL,
    GPTP_LOG_LVL_ERROR =  ANDROID_LOG_ERROR,
    GPTP_LOG_LVL_EXCEPTION =  ANDROID_LOG_ERROR,
    GPTP_LOG_LVL_WARNING =  ANDROID_LOG_WARN,
    GPTP_LOG_LVL_INFO =  ANDROID_LOG_INFO,
    GPTP_LOG_LVL_STATUS =  ANDROID_LOG_INFO,
    GPTP_LOG_LVL_DEBUG =  ANDROID_LOG_DEBUG,
    GPTP_LOG_LVL_VERBOSE =  ANDROID_LOG_VERBOSE,
} GPTP_LOG_LEVEL;

#endif

typedef enum {
    GPTP_LOG_OFF,
    GPTP_LOG_ON
} gptplogcat_t;

/**
 * Log based on ptp message
 */
typedef enum {
    SYNC_LOG,
    PDELAY_LOG,
    FOLLOW_UP_LOG,
    TX_TIMESTAMP_LOG,
    RX_TIMESTAMP_LOG,
    SEND_PORT_LOG,
    RESET_ALL_LOG,
} gptp_log_type_t;

/**
 * Counters to Limit the logs
 */
typedef struct {
    uint32_t sync;
    uint32_t pDelay;
    uint32_t followup;
    uint32_t tx_timestamp;
    uint32_t rx_timestamp;
    uint32_t send_port;
} LogLimit_t;

#define GPTP_MAX_SYNC_LOG               (3)
#define GPTP_MAX_PDELAY_LOG             (3)
#define GPTP_MAX_FOLLOWUP_LOG           (3)
#define GPTP_MAX_TX_TIMESTAMP_LOG       (3)
#define GPTP_MAX_RX_TIMESTAMP_LOG       (3)
#define GPTP_MAX_SEND_PORT_LOG          (3)

void gptplogRegister(void);
void gptplogUnregister(void);
void gptpLog(GPTP_LOG_LEVEL level, const char *tag, const char *path, int line,
             const char *fmt, ...);

void reset_log_limit(gptp_log_type_t type);
bool is_in_log_limit(gptp_log_type_t type);


#define GPTP_LOG_REGISTER() gptplogRegister()

#define GPTP_LOG_UNREGISTER() gptplogUnregister()

#ifdef GPTP_LOG_CRITICAL_ON
#define GPTP_LOG_CRITICAL(fmt,...) gptpLog(GPTP_LOG_LVL_CRITICAL, "CRITICAL ",__func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_CRITICAL(fmt,...)
#endif

#ifdef GPTP_LOG_ERROR_ON
#define GPTP_LOG_ERROR(fmt,...) gptpLog(GPTP_LOG_LVL_ERROR, "ERROR    ", __func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_ERROR(fmt,...)
#endif

#ifdef GPTP_LOG_EXCEPTION_ON
#define GPTP_LOG_EXCEPTION(fmt,...) gptpLog(GPTP_LOG_LVL_EXCEPTION, "EXCEPTION", __func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_EXCEPTION(fmt,...)
#endif

#ifdef GPTP_LOG_WARNING_ON
#define GPTP_LOG_WARNING(fmt,...) gptpLog(GPTP_LOG_LVL_WARNING, "WARNING  ", __func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_WARNING(fmt,...)
#endif

#ifdef GPTP_LOG_INFO_ON
#define GPTP_LOG_INFO(fmt,...) gptpLog(GPTP_LOG_LVL_INFO, "INFO     ", __func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_INFO(fmt,...)
#endif

#ifdef GPTP_LOG_STATUS_ON
#define GPTP_LOG_STATUS(fmt,...) gptpLog(GPTP_LOG_LVL_STATUS, "STATUS   ", __func__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_STATUS(fmt,...)
#endif

#ifdef GPTP_LOG_DEBUG_ON
#define GPTP_LOG_DEBUG(fmt,...) gptpLog(GPTP_LOG_LVL_DEBUG, "DEBUG    ", __FILE__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_DEBUG(fmt,...)
#endif

#ifdef GPTP_LOG_VERBOSE_ON
#define GPTP_LOG_VERBOSE(fmt,...) gptpLog(GPTP_LOG_LVL_VERBOSE, "VERBOSE  ", __FILE__, __LINE__, fmt, ## __VA_ARGS__)
#else
#define GPTP_LOG_VERBOSE(fmt,...)
#endif

#ifdef LOG_LIMIT
#define GPTP_LOG_LIMIT_EXCEPTION(log_type, fmt,...) \
        if (is_in_log_limit(log_type)) { \
            GPTP_LOG_EXCEPTION(fmt,## __VA_ARGS__);  \
        } \

#else
#define GPTP_LOG_LIMIT_EXCEPTION(log_type, fmt,...) GPTP_LOG_EXCEPTION(fmt,## __VA_ARGS__)
#endif

#ifdef LOG_LIMIT
#define GPTP_LOG_LIMIT_ERROR(log_type, fmt,...) \
    if (is_in_log_limit(log_type)) { \
        GPTP_LOG_ERROR(fmt,## __VA_ARGS__);  \
    } \

#else
#define GPTP_LOG_LIMIT_ERROR(log_type, fmt,...) GPTP_LOG_ERROR(fmt,## __VA_ARGS__)
#endif

#endif

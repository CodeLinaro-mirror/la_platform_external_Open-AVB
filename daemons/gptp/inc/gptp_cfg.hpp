/*************************************************************************************************************
Copyright (c) 2015, Coveloz Consulting Ltda
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
*************************************************************************************************************/

/******************************************************************************

Changes from Qualcomm Innovation Center, Inc. are provided under the following license:

Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

/**
 * @file
 * MODULE SUMMARY : Reads the .ini file and parses it into information
 * to be used on daemon_cl
 */

#include <string>

#include "ini.h"
#include <limits.h>
#include <common_port.hpp>

const uint32_t LINKSPEED_10G =      10000000;
const uint32_t LINKSPEED_5G =       5000000;
const uint32_t LINKSPEED_2_5G =     2500000;
const uint32_t LINKSPEED_1G =       1000000;
const uint32_t LINKSPEED_100MB =    100000;
const uint32_t INVALID_LINKSPEED =  UINT_MAX;

/**
 * @brief Returns name given numeric link speed
 * @return NULL if speed/name isn't found
 */
const char *findNameBySpeed( uint32_t speed );

/**
 * @brief Provides the gptp interface for
 * the iniParser external module
 */
class GptpIniParser
{
    public:

        /**
         * @brief Container with the information to get from the .ini file
         */
        typedef struct {
            /*ptp data set*/
            uint8_t priority1;
            uint8_t priority2;
            uint8_t clockClass;
            uint8_t debugLog = 0;
            uint8_t syncClocks = 0;

            /*port data set*/
            unsigned int announceReceiptTimeout;
            unsigned int syncReceiptTimeout;
            unsigned int
            syncReceiptThresh;     //!< Number of wrong sync messages that will trigger a switch to master
            int64_t neighborPropDelayThresh;
            int64_t stbMSyncLossThreshold;
            unsigned int seqIdAsCapableThresh;
            uint16_t lostPdelayRespThresh;
            int8_t initialLogSyncInterval;
            int8_t initialLogPdelayReqInterval;
            int8_t operLogSyncInterval;
            int8_t operLogPdelayReqInterval;
            double syncArrivalTimeDiffTolerance;
            int8_t reverseSyncEnabled;
            int8_t reverseSyncDomain;
            double reverseSyncRate;
            bool automotive_profile;
            bool asCapable;
            bool isGm;
            bool disableSigMsg;
            bool bypassIfWait = false;
            bool waitForSync = false;
            bool tsc_enable = false;
            PortState port_state;

            /*ethernet adapter data set*/
            std::string ifname;
            phy_delay_map_t phy_delay;
        } gptp_cfg_t;

        /*public methods*/
        GptpIniParser(std::string ini_path);
        ~GptpIniParser();

        /**
         * @brief  Reads the parser Error value
         * @param  void
         * @return Parser Error
         */
        int parserError(void);

        /**
         * @brief  Reads priority1 config value
         * @param  void
         * @return priority1
         */
        uint8_t getPriority1(void)
        {
            return _config.priority1;
        }
        /**
         * @brief  Reads priority2 config value
         * @param  void
         * @return priority2
         */
        uint8_t getPriority2(void)
        {
            return _config.priority2;
        }
        /**
         * @brief  Reads debugLog config value
         * @param  void
         * @return debugLog
         */

        uint8_t getDebugLog(void)
        {
            return _config.debugLog;
        }

        /**
         * @brief  Reads syncClocks config value
         * @param  void
         * @return syncClocks
         */

        uint8_t getSyncClocks(void)
        {
            return _config.syncClocks;
        }


        /**
         * @brief  Reads clockClass config value
         * @param  void
         * @return clockclass
         */
        uint8_t getclockClass(void)
        {
            return _config.clockClass;
        }

        /**
         * @brief  Reads the announceReceiptTimeout configuration value
         * @param  void
         * @return announceRecepitTimeout value from .ini file
         */
        unsigned int getAnnounceReceiptTimeout(void)
        {
            return _config.announceReceiptTimeout;
        }

        /**
         * @brief  Reads the syncRecepitTimeout configuration value
         * @param  void
         * @return syncRecepitTimeout value from the .ini file
         */
        unsigned int getSyncReceiptTimeout(void)
        {
            return _config.syncReceiptTimeout;
        }

        /**
         * @brief  Reads the PHY DELAY values from the configuration file
         * @param  void
         * @return PHY delay map structure
         */
        const phy_delay_map_t getPhyDelay(void)
        {
            return _config.phy_delay;
        }

        /**
         * @brief  Reads the neighbohr propagation delay threshold from the configuration file
         * @param  void
         * @return neighborPropDelayThresh value from the .ini file
         */
        int64_t getNeighborPropDelayThresh(void)
        {
            return _config.neighborPropDelayThresh;
        }

        /**
         * @brief  Reads the StbMSyncLossThreshold from the configuration file
         * @param  void
         * @return StbMSyncLossThreshold value from the .ini file
         */
        int64_t getStbMSyncLossThreshold(void)
        {
            return _config.stbMSyncLossThreshold;
        }

        /**
         * @brief  Reads the sync receipt threshold from the configuration file
         * @return syncRecepitThresh value from the .ini file
         */
        unsigned int getSyncReceiptThresh(void)
        {
            return _config.syncReceiptThresh;
        }
        /**
         * @brief  Reads the initial log sync interval from the configuration file
         * @return initialLogSyncInterval value from the .ini file
         */
        int8_t getInitialLogSyncInterval(void)
        {
            return _config.initialLogSyncInterval;
        }
        /**
         * @brief  Reads the initial log pdelay interval from the configuration file
         * @return initialLogPdelayReqInterval value from the .ini file
         */
        int8_t getInitialLogPdelayReqInterval(void)
        {
            return _config.initialLogPdelayReqInterval;
        }
        /**
         * @brief  Reads the operational log sync interval from the configuration file
         * @return operLogSyncInterval value from the .ini file
         */
        int8_t getOperLogSyncInterval(void)
        {
            return _config.operLogSyncInterval;
        }
        /**
         * @brief  Reads the operational log pdelay req interval from the configuration file
         * @return operlogpdelayreqInterval value from the .ini file
         */
        int8_t getOperLogPdelayReqInterval(void)
        {
            return _config.operLogPdelayReqInterval;
        }
        /**
         * @brief  Reads the incoming successive sync interval time difference tolerance value from the configuration file
         * @return syncArrivalTimeDiffTolerance value from the .ini file
         */
        double getSyncArrivalTimeDiffTolerance(void)
        {
            return _config.syncArrivalTimeDiffTolerance;
        }

        bool getIsRsync(void)
        {
            return _config.reverseSyncEnabled;
        }

        int8_t getRSyncDomain(void)
        {
            return _config.reverseSyncDomain;
        }
        double getRSyncRate(void)
        {
            return _config.reverseSyncRate;
        }

        bool getIsGM(void)
        {
            return _config.isGm;
        }
        bool getIsSigMsgDisabled(void)
        {
            return _config.disableSigMsg;
        }
        bool getAsCapable(void)
        {
            return _config.asCapable;
        }
        bool getAutomotiveProfile(void)
        {
            return _config.automotive_profile;
        }

        bool getIsIfCheckBypass()
        {
            return _config.bypassIfWait;
        }

        bool getwaitForSync()
        {
            return _config.waitForSync;
        }

        bool getTscEnable()
        {
            return _config.tsc_enable;
        }

        std::string getIfaceName()
        {
            return _config.ifname;
        }

        PortState getPortState(void)
        {
            return _config.port_state;
        }

        /**
         * @brief Dump PHY delays to screen
         */
        void print_phy_delay( void );

    private:
        int _error;
        gptp_cfg_t _config;

        static int iniCallBack(void *user, const char *section, const char *name,
                               const char *value);
        static bool parseMatch(const char *s1, const char *s2);
};


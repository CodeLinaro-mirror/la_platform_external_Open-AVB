#
#  Copyright (c) 2012 Intel Corporation
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#
#   1. Redistributions of source code must retain the above copyright notice,
#      this list of conditions and the following disclaimer.
#
#   2. Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#   3. Neither the name of the Intel Corporation nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#  Changes from Qualcomm Technologies, Inc. are provided under the following license:
#  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
#  SPDX-License-Identifier: BSD-3-Clause-Clear

ALTERNATE_LINUX_INCPATH=$(HOME)/header/include/

CFLAGS_G = -Wall -g -fsigned-char -I. -I./inc \
	-I$(ALTERNATE_LINUX_INCPATH) -DUSE_GLIB -DPTP_SW_QTIMER=1 -DPTP_SW_BOOTTIME=1 `pkg-config --cflags glib-2.0`

# Check to see if PTP cross-timestamping is supported in hardware/driver
# and, if so, add flag to compile in support

PRECISE_TIME_TEST = "\#include <linux/ptp_clock.h>\\n\
\#ifdef PTP_SYS_OFFSET_PRECISE\\nint main(){return 0;}\\n\#endif"

HAS_PRECISE_TIME = $(shell echo $(PRECISE_TIME_TEST) | $(CC) -xc - \
-I$(ALTERNATE_LINUX_INCPATH) -o /dev/null > /dev/null 2>&1 ; echo $$?)

ifeq ($(HAS_PRECISE_TIME),0)
       CFLAGS_G += -DPTP_HW_CROSSTSTAMP
endif

ifeq ($(RGPTP_ENABLED),1)
	CFLAGS_G += -DRGPTP_ENABLED
endif

CPPFLAGS_G = $(CFLAGS_G) -std=c++0x -Wnon-virtual-dtor
LDFLAGS_G = -lpthread -lrt `pkg-config --libs glib-2.0`
INC_DIR = inc
SRC_DIR = src
OBJ_DIR = obj

OBJ_FILES = $(OBJ_DIR)/ptp_message.o\
		 $(OBJ_DIR)/ap_message.o\
		 $(OBJ_DIR)/avbts_osnet.o\
		 $(OBJ_DIR)/ether_port.o\
		 $(OBJ_DIR)/common_port.o\
		 $(OBJ_DIR)/ieee1588clock.o \
		 $(OBJ_DIR)/linux_hal_common.o\
		 $(OBJ_DIR)/linux_hal_persist_file.o\
		 $(OBJ_DIR)/gptp_log.o\
		 $(OBJ_DIR)/platform.o \
		 $(OBJ_DIR)/qgptp_rmgr.o \
		 $(OBJ_DIR)/ini.o \
		 $(OBJ_DIR)/gptp_cfg.o \
		 $(OBJ_DIR)/linux_hal_generic.o \
		 $(OBJ_DIR)/linux_hal_generic_adj.o

HEADER_FILES = $(INC_DIR)/ether_port.hpp\
		$(INC_DIR)/common_port.hpp\
		$(INC_DIR)/avbts_ostimerq.hpp\
		$(INC_DIR)/avbts_ostimer.hpp\
		$(INC_DIR)/avbts_osthread.hpp\
		$(INC_DIR)/avbts_osnet.hpp\
		$(INC_DIR)/avbts_oslock.hpp\
		$(INC_DIR)/avbts_osipc.hpp\
		$(INC_DIR)/avbts_oscondition.hpp\
		$(INC_DIR)/avbts_message.hpp\
		$(INC_DIR)/avbts_clock.hpp\
		$(INC_DIR)/avbts_persist.hpp\
		$(INC_DIR)/avbap_message.hpp\
		$(INC_DIR)/ieee1588.hpp\
		$(INC_DIR)/ipcdef.hpp\
		$(INC_DIR)/ini.h\
		$(INC_DIR)/gptp_cfg.hpp\
		$(INC_DIR)/gptp_log.hpp\
		$(INC_DIR)/linux_ipc.hpp\
		$(INC_DIR)/qgptp_rmgr.h\
		$(INC_DIR)/linux_hal_common.hpp\
		$(INC_DIR)/linux_hal_persist_file.hpp\
		$(INC_DIR)/platform.hpp \
		$(INC_DIR)/linux_hal_generic.hpp


ifeq ($(RGPTP_ENABLED),1)
	INCPATHS := -I${PKG_CONFIG_SYSROOT_DIR}/usr/include/qmi-framework-vendor
	INCPATHS += -I${PKG_CONFIG_SYSROOT_DIR}/usr/include/qmi-framework
	LDFLAGS_G += -lqrtr
	OBJ_FILES += $(OBJ_DIR)/rgptp.o
	HEADER_FILES += $(INC_DIR)/rgptp.hpp
	CFLAGS_G += -DGPIO_CHIP
endif

ifeq ($(SYSTEMD_SUPPORT_INCLUDED),1)
	CFLAGS_G += -DSYSTEMD
endif

ifeq ($(GPTP_AUTO_START),1)
	CFLAGS_G += -DGPTP_AUTO_START
endif

ifeq ($(LE_SHARED_MEM),1)
    CFLAGS_G += -DLE_SHARED_MEM
    CPPFLAGS_G += -DLE_SHARED_MEM
endif

ifeq ($(GPTP_VFIO),1)
    CFLAGS_G += -DGPTP_VFIO
    CPPFLAGS_G += -DGPTP_VFIO
    CFLAGS_G += -DUSE_CARVEOUT_GPTP
    CPPFLAGS_G += -DUSE_CARVEOUT_GPTP
    LDFLAGS_G += -lvfioptp
endif

ifeq ($(GPTP_DSQB_ENABLED),1)
    CFLAGS_G += -DGPTP_DSQB_ENABLED
    CPPFLAGS_G += -DGPTP_DSQB_ENABLED
    LDFLAGS_G += -lpm-client
endif

ifeq ($(TSC_FEATURE),1)
    CFLAGS_G += -DTSC_FEATURE
    CPPFLAGS_G += -DTSC_FEATURE
endif

LDFLAGS_G += -lpthread -lrt

CFLAGS_G += -DLOG_LIMIT
CPPFLAGS_G += -DLOG_LIMIT

CFLAGS = $(CFLAGS_G)
CPPFLAGS = $(CPPFLAGS_G)
LDFLAGS = $(LDFLAGS_G)

all: $(OBJ_DIR)/daemon_cl

rebuild: clean all

$(OBJ_DIR)/daemon_cl: $(SRC_DIR)/daemon_cl.cpp $(OBJ_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OBJ_FILES) $(SRC_DIR)/daemon_cl.cpp -o $(OBJ_DIR)/qgptp $(LDFLAGS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OBJ_FILES) $(SRC_DIR)/daemon_cl.cpp -o $(OBJ_DIR)/daemon_cl $(LDFLAGS)

ifeq ($(RGPTP_ENABLED),1)
$(OBJ_DIR)/rgptp.o: $(SRC_DIR)/rgptp.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/rgptp.cpp -o $(OBJ_DIR)/rgptp.o $(INCPATHS)
endif

$(OBJ_DIR)/qgptp_rmgr.o: $(SRC_DIR)/qgptp_rmgr.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/qgptp_rmgr.cpp -o $(OBJ_DIR)/qgptp_rmgr.o $(INCPATHS)

$(OBJ_DIR)/linux_hal_common.o: $(SRC_DIR)/linux_hal_common.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/linux_hal_common.cpp -o $(OBJ_DIR)/linux_hal_common.o

$(OBJ_DIR)/platform.o: $(SRC_DIR)/platform.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/platform.cpp -o $(OBJ_DIR)/platform.o

$(OBJ_DIR)/linux_hal_generic.o: $(SRC_DIR)/linux_hal_generic.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/linux_hal_generic.cpp -o $(OBJ_DIR)/linux_hal_generic.o

$(OBJ_DIR)/linux_hal_generic_adj.o: $(SRC_DIR)/linux_hal_generic_adj.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/linux_hal_generic_adj.cpp -o $(OBJ_DIR)/linux_hal_generic_adj.o

$(OBJ_DIR)/ether_port.o: $(SRC_DIR)/ether_port.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/common_port.o: $(SRC_DIR)/common_port.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/ieee1588clock.o: $(SRC_DIR)/ieee1588clock.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/ieee1588clock.cpp -o $(OBJ_DIR)/ieee1588clock.o

$(OBJ_DIR)/ptp_message.o: $(SRC_DIR)/ptp_message.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/ptp_message.cpp -o $(OBJ_DIR)/ptp_message.o

$(OBJ_DIR)/ap_message.o: $(SRC_DIR)/ap_message.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/ap_message.cpp -o $(OBJ_DIR)/ap_message.o

$(OBJ_DIR)/avbts_osnet.o: $(SRC_DIR)/avbts_osnet.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/avbts_osnet.cpp -o $(OBJ_DIR)/avbts_osnet.o

$(OBJ_DIR)/linux_hal_persist_file.o: $(SRC_DIR)/linux_hal_persist_file.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/linux_hal_persist_file.cpp -o $(OBJ_DIR)/linux_hal_persist_file.o

$(OBJ_DIR)/gptp_log.o: $(SRC_DIR)/gptp_log.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/gptp_log.cpp -o $(OBJ_DIR)/gptp_log.o

$(OBJ_DIR)/gptp_cfg.o: $(SRC_DIR)/gptp_cfg.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/gptp_cfg.cpp -o $(OBJ_DIR)/gptp_cfg.o

$(OBJ_DIR)/ini.o: $(SRC_DIR)/ini.c $(HEADER_FILES)
	$(CC) $(CFLAGS) -c $(SRC_DIR)/ini.c -o $(OBJ_DIR)/ini.o

$(OBJ_DIR)/watchdog.o: $(SRC_DIR)/watchdog.cpp $(HEADER_FILES)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(SRC_DIR)/watchdog.cpp -o $(OBJ_DIR)/watchdog.o
clean:
	$(RM) *~ $(OBJ_DIR)/*.o  $(OBJ_DIR)/qgptp
	$(RM) *~ $(OBJ_DIR)/*.o  $(OBJ_DIR)/daemon_cl

.PHONY: FORCE

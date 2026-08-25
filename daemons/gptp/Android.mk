# Set LOCAL_PATH to the current directory of this Android.mk
LOCAL_PATH := $(call my-dir)

# Clear previous variable definitions
include $(CLEAR_VARS)

LOCAL_CFLAGS := -DWITHOUT_IFADDRS -Wno-unused-parameter -frtti -Wno-unused-private-field
LOCAL_CFLAGS += -DPTP_SW_QTIMER=1 -DSYSTEMD

LOCAL_C_INCLUDES := $(LOCAL_PATH)/inc

LOCAL_SHARED_LIBRARIES += liblog libutils libcutils

LOCAL_SRC_FILES := src/daemon_cl.cpp \
                   src/ptp_message.cpp \
                   src/avbts_osnet.cpp \
                   src/ap_message.cpp \
                   src/common_port.cpp \
                   src/ether_port.cpp \
                   src/ieee1588clock.cpp \
                   src/gptp_cfg.cpp \
                   src/gptp_log.cpp \
                   src/ini.c \
                   src/linux_hal_common.cpp \
                   src/linux_hal_persist_file.cpp \
                   src/platform.cpp \
                   src/linux_hal_generic.cpp \
                   src/linux_hal_generic_adj.cpp \
                   src/qgptp_rmgr.cpp

LOCAL_MODULE := qgptp_legacy

ifeq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
LOCAL_INIT_RC := etc/gptp_daemon.rc
endif

ifeq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_INIT_RC := etc/gptp_daemon.rc
endif

ifeq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_INIT_RC := etc/gptp_daemon.rc
endif

LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/bin

include $(BUILD_EXECUTABLE)

# ---------------------------------------------------------------------
# Android.mk for installing gptp configuration file to vendor/etc
# with board-specific overrides
# ---------------------------------------------------------------------

# Clear previous variable definitions
include $(CLEAR_VARS)

# Module name (logical identifier for this prebuilt file)
LOCAL_MODULE := gptp_cfg_legacy.ini

# Class for configuration files (ETC means it goes under /etc)
LOCAL_MODULE_CLASS := ETC

# Destination path on the device (vendor partition)
# This ensures the file is installed under /vendor/etc
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/etc

ifeq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
LOCAL_SRC_FILES := etc/gptp_cfg_au.ini
else ifeq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_SRC_FILES := etc/gptp_cfg_gvm.ini
else ifeq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_SRC_FILES := etc/gptp_cfg_gvm.ini
else
# Default case: use the module name (gptp_cfg.ini)
LOCAL_SRC_FILES := etc/$(LOCAL_MODULE)
endif

# Mark this as a prebuilt file
include $(BUILD_PREBUILT)

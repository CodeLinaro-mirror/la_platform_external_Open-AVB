LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        gptp_helper.cpp\

LOCAL_SHARED_LIBRARIES := libcutils liblog

LOCAL_C_INCLUDES += \
        $(LOCAL_PATH) \
		external/open-avb/daemons/gptp/inc

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_C_INCLUDES)

LOCAL_CFLAGS += -Wno-multichar -Werror -Wall -Wno-unused-parameter
ifeq ($(call is-board-platform,msmnile),true)
ifeq ($(TARGET_BOARD_SUFFIX),_gvmq)
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1

LOCAL_SHARED_LIBRARIES += libuhab libion
endif
endif

ifeq ($(call is-board-platform,msmnile),true)
ifeq ($(TARGET_BOARD_SUFFIX),_gvm)
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1
LOCAL_SHARED_LIBRARIES += libuhab libion
endif
endif

ifeq ($(TARGET_BOARD_SUFFIX),_gvm)
ifneq ($(TARGET_BOARD_DERIVATIVE_SUFFIX),_cdccomm)
ifneq (gen5_gvm_gy, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
ifneq (gen5_gvm, $(TARGET_BOARD_PLATFORM)$(TARGET_BOARD_SUFFIX))
LOCAL_CFLAGS += -DAVB_FEATURE_GVM_MODE=1
LOCAL_SHARED_LIBRARIES += libuhab libion
endif
endif
endif
endif

LOCAL_CFLAGS += -DANDROID -DSYSTEMD -DLOG_LIMIT

LOCAL_CLANG := true

LOCAL_MODULE_TAGS := optional

LOCAL_MODULE:= libgptp

LOCAL_MODULE_PATH_64 := $(TARGET_OUT_VENDOR)/lib64

ifneq ($(wildcard $(LOCAL_PATH)/Android.bp),)
else
include $(BUILD_SHARED_LIBRARY)
endif


.PHONY : clean

CPPFLAGS+= -fpic -g -I../../daemons/gptp/inc
ifeq ($(GPTP_AUTO_START),1)
	CPPFLAGS += -DGPTP_AUTO_START
endif
ifeq ($(LE_GVM),1)
	CPPFLAGS += -DLE_GVM
endif
ifeq ($(AVB_FEATURE_GVM_MODE),1)
	CPPFLAGS += -DAVB_FEATURE_GVM_MODE
endif
ifeq ($(SYSTEMD_SUPPORT_INCLUDED),1)
	CPPFLAGS += -DSYSTEMD
endif
ifeq ($(RGPTP_CLNT_ENABLED),1)
	CPPFLAGS += -DRGPTP_CLNT_ENABLED
endif
ifeq ($(TSC_FEATURE),1)
	CFLAGS += -DTSC_FEATURE
	CPPFLAGS += -DTSC_FEATURE
endif
LDFLAGS+= -shared -lrt -lpthread

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	LDFLAGS+= -luhab
endif
ifeq ($(GPTP_VFIO),1)
	CPPFLAGS += -DGPTP_VFIO
	LDFLAGS += -lvfioptp
endif

CPPFLAGS += -DLOG_LIMIT

# Define variables for CLOG (Common Log) availability
CLOG_CFLAGS := $(shell pkg-config --cflags libclog 2>/dev/null)
CLOG_LDFLAGS := $(shell pkg-config --libs libclog 2>/dev/null)
$(info CLOG_CFLAGS: $(CLOG_CFLAGS))
$(info CLOG_LDFLAGS: $(CLOG_LDFLAGS))

CLOG_AVAILABLE := 0
ifneq ($(CLOG_CFLAGS),)
ifneq ($(CLOG_LDFLAGS),)
    $(info CLOG_CFLAGS: $(CLOG_CFLAGS))
    $(info CLOG_LDFLAGS: $(CLOG_LDFLAGS))
    CLOG_AVAILABLE := 1
    CFLAGS += -DUSE_CLOG
    CFLAGS += $(CLOG_CFLAGS)
    CPPFLAGS += -DUSE_CLOG
    CPPFLAGS += $(CLOG_CFLAGS)
    LDFLAGS += $(CLOG_LDFLAGS)
    $(info CPPFLAGS: $(CPPFLAGS))
    $(info LDFLAGS: $(LDFLAGS))
endif
endif

# Define variables for DLT availability
# DLT is used when CLOG is not available
ifeq ($(CLOG_AVAILABLE),0)
    DLT_CFLAGS := $(shell pkg-config --cflags automotive-dlt)
    DLT_LDFLAGS := $(shell pkg-config --libs automotive-dlt)
    $(info DLT_CFLAGS: $(DLT_CFLAGS))
    $(info DLT_LDFLAGS: $(DLT_LDFLAGS))

    DLT_AVAILABLE := 0
    ifneq ($(DLT_CFLAGS),)
    ifneq ($(DLT_LDFLAGS),)
        $(info DLT_CFLAGS: $(DLT_CFLAGS))
        $(info DLT_LDFLAGS: $(DLT_LDFLAGS))
        DLT_AVAILABLE := 1
        CFLAGS += -DUSE_DLT
        CFLAGS += $(DLT_CFLAGS)
        CPPFLAGS += -DUSE_DLT
        CPPFLAGS += $(DLT_CFLAGS)
        LDFLAGS += $(DLT_LDFLAGS) -lstdc++
        $(info CPPFLAGS_G: $(CPPFLAGS))
        $(info LDFLAGS_G: $(LDFLAGS))
    endif
    endif
endif

SOURCES= gptp_helper.cpp
OBJECTS= gptp_helper.o
TARGET= libgptp.so 

all: $(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)

$(TARGET) : $(OBJECTS)
	$(CC) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(OBJECTS) -o $@ $(LDFLAGS)

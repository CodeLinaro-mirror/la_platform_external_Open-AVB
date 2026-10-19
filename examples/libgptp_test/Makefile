.PHONY : clean

BUILD_DIR=.
OPT=-g
CFLAGS+=$(OPT) -Wall -Wextra -Wno-parentheses
INCFLAGS=-I../../lib/libgptp
LDLIBS= -lrt -lm -lpthread -lgptp
LDFLAGS+=-L../../lib/libgptp

ifeq ($(GPTP_AUTO_START),1)
	CFLAGS += -DGPTP_AUTO_START
	CPPFLAGS += -DGPTP_AUTO_START
endif

ifeq ($(LE_GVM),1)
	CFLAGS += -DLE_GVM
	CPPFLAGS += -DLE_GVM
endif

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	CFLAGS += -DAVB_FEATURE_GVM_MODE
	CPPFLAGS += -DAVB_FEATURE_GVM_MODE
endif

ifeq ($(AVB_FEATURE_GVM_MODE),1)
	LDLIBS+= -luhab
endif

ifeq ($(RGPTP_CLNT_ENABLED),1)
	CFLAGS += -DRGPTP_CLNT_ENABLED
	CPPFLAGS += -DRGPTP_CLNT_ENABLED
endif

ifeq ($(GPTP_VFIO),1)
	CFLAGS += -DGPTP_VFIO
	CPPFLAGS += -DGPTP_VFIO
endif

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

# Define variables for DLT availability.
# DLT is used only when CLOG is not available.
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
        LDFLAGS += $(DLT_LDFLAGS)
        $(info CPPFLAGS_G: $(CPPFLAGS))
        $(info LDFLAGS_G: $(LDFLAGS))
    endif
    endif
endif

all: libgptp_test
libgptp_test: libgptp_test.cpp
	$(CC) -o $(BUILD_DIR)/libgptp_test $(INCFLAGS) libgptp_test.cpp $(CFLAGS) $(CXXFLAGS) $(LDLIBS) $(LDFLAGS)

clean:
	$(RM) libgptp_test
	$(RM) `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core -o -name "*.orig"`

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

# Define variables
DLT_CFLAGS := $(shell pkg-config --cflags automotive-dlt)
DLT_LDFLAGS := $(shell pkg-config --libs automotive-dlt)
$(info DLT_CFLAGS: $(DLT_CFLAGS))
$(info DLT_LDFLAGS: $(DLT_LDFLAGS))

# Define a flag for DLT availability
DLT_AVAILABLE := 0
ifneq ($(DLT_CFLAGS),)
ifneq ($(DLT_LDFLAGS),)
$(info DLT_CFLAGS: $(DLT_CFLAGS))
$(info DLT_LDFLAGS: $(DLT_LDFLAGS))
DLT_AVAILABLE := 1
CFLAGS += -DDLT_AVAILABLE
CFLAGS += $(DLT_CFLAGS)
CPPFLAGS += -DDLT_AVAILABLE
CPPFLAGS += $(DLT_CFLAGS)
LDFLAGS += $(DLT_LDFLAGS)
$(info CPPFLAGS_G: $(CPPFLAGS))
$(info LDFLAGS_G: $(LDFLAGS))
endif
endif

all: libgptp_test
libgptp_test: libgptp_test.cpp
	$(CC) -o $(BUILD_DIR)/libgptp_test $(INCFLAGS) libgptp_test.cpp $(CFLAGS) $(CXXFLAGS) $(LDLIBS) $(LDFLAGS)

clean:
	$(RM) libgptp_test
	$(RM) `find . -name "*~" -o -name "*.[oa]" -o -name "\#*\#" -o -name TAGS -o -name core -o -name "*.orig"`

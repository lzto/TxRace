LEVEL = ../..

CXXFLAGS += -I$(PROJ_SRC_ROOT)/../intel-toolset/
LDFLAGS += $(PROJ_SRC_ROOT)/../intel-toolset/intel-lbr/liblbr.a

LIBRARYNAME = TxHooks

BYTECODE_LIBRARY = 1

DIRS=tsan-compiler-rt donot-rt prof-rt intel-lbr

include $(LEVEL)/Makefile.common

CFLAGS := $(CFLAGS) -Wall -Wstrict-prototypes -DUSE_TSD_DATA_HACK -D_REENTRANT -DTHREAD_STATS=1  -DMSPACES=1 -DUSE_DL_PREFIX=1 -Itsan-compiler-rt/rtl/ -Itsan-compiler-rt/rtl/interception -Itsan-compiler-rt/rtl/sanitizer_common

CXXFLAGS := $(CXXFLAGS) -Wall -Wstrict-prototypes -DUSE_TSD_DATA_HACK -D_REENTRANT -DTHREAD_STATS=1  -DMSPACES=1 -DUSE_DL_PREFIX=1 -Itsan-compiler-rt/rtl/ -Itsan-compiler-rt/rtl/interception -Itsan-compiler-rt/rtl/sanitizer_common


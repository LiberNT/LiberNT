# Builds ntfslib.ko: the NTFS core as one prebuilt object, the VFS glue through kbuild.
#   make KDIR=/lib/modules/$(uname -r)/build OUT=<empty build directory>
KDIR ?= /lib/modules/$(shell uname -r)/build
HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT := $(abspath $(HERE)/../..)
OUT ?= $(HERE)/build

CORE_NAMES := \
	btree/directory/directory.cpp \
	btree/directory/editdir.cpp \
	btree/directory/find.cpp \
	btree/directory/get.cpp \
	btree/btree.cpp \
	check/check.cpp \
	capi/btree.cpp \
	capi/filerecord.cpp \
	capi/lfs.cpp \
	capi/mft.cpp \
	capi/volume.cpp \
	filerecord/attributes/addattr.cpp \
	filerecord/attributes/findattr.cpp \
	filerecord/read/allocation.cpp \
	filerecord/read/compression.cpp \
	filerecord/read/copy.cpp \
	filerecord/read/ea.cpp \
	filerecord/read/reparse.cpp \
	filerecord/read/security.cpp \
	filerecord/read/streams.cpp \
	filerecord/read/wof.cpp \
	filerecord/write/basic.cpp \
	filerecord/write/create.cpp \
	filerecord/write/ea.cpp \
	filerecord/write/filedata.cpp \
	filerecord/write/filename.cpp \
	filerecord/write/move.cpp \
	filerecord/write/objectid.cpp \
	filerecord/write/reparse.cpp \
	filerecord/write/streamrename.cpp \
	filerecord/filerecord.cpp \
	filerecord/fixup.cpp \
	lfs/lfs.cpp \
	lfs/recovery.cpp \
	mft/get.cpp \
	mft/create.cpp \
	mft/mft.cpp \
	mft/namespace.cpp \
	NtfsIsNameInExpression.cpp \
	volume/metadata/attrdef.cpp \
	volume/metadata/bitmap.cpp \
	volume/metadata/upcase.cpp \
	volume/metadata/volume.cpp \
	volume/disk.cpp \
	volume/volume.cpp \
	util.cpp
CORE_SOURCES := $(addprefix $(ROOT)/src/,$(CORE_NAMES))
CORE_FLAGS := -std=gnu++17 -O2 -DNDEBUG -DNTFSLIB_PORTABLE=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
	-I$(ROOT)/include/internal -I$(ROOT)/include/public -I$(HERE) \
	-fshort-wchar -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector -fno-pic -fno-pie \
	-fno-omit-frame-pointer -fno-optimize-sibling-calls -mno-omit-leaf-frame-pointer \
	-mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -mno-80387 \
	-mindirect-branch=thunk-extern -mindirect-branch-register -mfunction-return=thunk-extern -fno-jump-tables \
	-fcheck-new -fno-strict-aliasing -fcf-protection=none -fno-delete-null-pointer-checks \
	-Wall -Wno-multichar -Wno-unused-parameter
CORE_OBJECTS := $(patsubst $(ROOT)/%.cpp,$(OUT)/core/%.o,$(CORE_SOURCES)) $(OUT)/core/shim.o

all: $(OUT)/ntfslib.ko

$(OUT)/core/%.o: $(ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -MMD -MP -c $< -o $@

$(OUT)/core/shim.o: $(HERE)/shim.cpp $(HERE)/ntfslib_k.h
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_FLAGS) -MMD -MP -c $< -o $@

-include $(CORE_OBJECTS:.o=.d)

$(OUT)/ntfslib_core.o_shipped: $(CORE_OBJECTS)
	$(LD) -r -o $@ $^

$(OUT)/ntfslib.ko: $(OUT)/ntfslib_core.o_shipped $(HERE)/vfs.c $(HERE)/ntfslib_k.h $(HERE)/Kbuild
	cp $(HERE)/vfs.c $(HERE)/ntfslib_k.h $(OUT)/
	cp $(HERE)/Kbuild $(OUT)/Kbuild
	$(MAKE) -C $(KDIR) M=$(OUT) modules

.PHONY: all

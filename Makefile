# miniglv3d-clean -- builds libminiglv3d.a, the MiniGLV3D driver archive that
# minigl-shared-library links into minigl.library (installed there as
# third_party/pistorm3d/libminiglv3d.a).
#
# This is MiniGLV3D/build_lib_gcc_nolog.sh as a Makefile: the same 27 sources,
# the same flags per group, the same archive member order, the same toolchain.
# The tree holds only the files that build reads -- 27 .c and 25 .h, the list
# gcc -MM gives for the script's own compile commands -- as committed in the
# MiniGLV3D git.
#
# Usage, from WSL (the toolchain lives there):
#     make -C <this directory>
#     make -C <this directory> OPT=-O3     (the script's optional $1)
#     make -C <this directory> clean
#
# LOCATION INDEPENDENT, AND THE ONE THING THAT COSTS: every path handed to the
# compiler is relative to this directory, so the output does not depend on
# where the repo lives. That matters because m68k-amigaos-gcc writes each
# object's -o argument, VERBATIM, into the object as its HUNK_UNIT name (the
# first bytes of the .o). build_lib_gcc_nolog.sh passes an ABSOLUTE -o,
# /mnt/d/v3d_driver/MiniGLV3D/obj_lib_gcc_nolog/<f>.o, so its archive carries
# that path 27 times, where this one carries obj_lib_gcc_nolog/<f>.o.
# Everything after the unit name is identical. To reproduce the script's
# archive byte for byte, give it the script's object directory:
#     make -C <this directory> -B OBJDIR=/mnt/d/v3d_driver/MiniGLV3D/obj_lib_gcc_nolog
# (-B because that directory already holds objects.) The unit name does not
# reach minigl.library -- the linker drops it -- so it changes this archive's
# bytes only, never the library's.

# Every path below is relative to this directory; refuse to run from anywhere
# else rather than silently build nothing or the wrong thing.
HERE := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
ifneq ($(HERE),$(CURDIR))
$(error run as: make -C $(HERE))
endif

# The script's toolchain. A different GCC or binutils build is a different
# compiler: byte identity holds for this one only.
PREFIX ?= $(HOME)/amiga-gcc-install
CC     := $(PREFIX)/bin/m68k-amigaos-gcc
AR     := $(PREFIX)/bin/m68k-amigaos-ar
export PATH := $(PREFIX)/bin:$(PATH)

# Flags exactly as build_lib_gcc_nolog.sh sets them. -fno-strict-aliasing is
# REQUIRED (strict aliasing deletes v3d_commands.c's swivel-pattern writes);
# -fno-builtin-cos/-sin stops GCC fusing cos+sin into a cexp() nothing provides.
OPT       ?= -O2
OPTCFLAGS := -fno-strict-aliasing -fno-builtin-cos -fno-builtin-sin -finline-functions -DMGLV3D_NO_LOGGING
CPUFLAGS  := -mcpu=68020 -m68881 -mcrt=clib2
INCFLAGS  := -Igl/include -Ibackend/include

OBJDIR ?= obj_lib_gcc_nolog
LIB    ?= libminiglv3d.a

GL_SRC   := aclip context draw fog glu hclip init matrix others texture \
            vertexarray vertexbuffer_min vertexelements viewport
HW_SRC   := v3d_assembler v3d_clbuf v3d_commands v3d_context v3d_device v3d_frame v3d_hw \
            v3d_mem_allocvec v3d_submit_timeout v3d_texture

GL_OBJS   := $(GL_SRC:%=$(OBJDIR)/%.o)
HW_OBJS   := $(HW_SRC:%=$(OBJDIR)/%.o)
DBG_OBJ   := $(OBJDIR)/v3d_debug.o

# Member order is part of the archive's bytes: this is the script's order.
OBJS := $(GL_OBJS) $(HW_OBJS) $(DBG_OBJ)

HEADERS := $(wildcard gl/include/mgl/*.h gl/src/*.h backend/include/*.h backend/hw/*.h)

.PHONY: all clean
all: $(LIB)

$(GL_OBJS): $(OBJDIR)/%.o: gl/src/%.c $(HEADERS) Makefile | $(OBJDIR)
	$(CC) -std=c99 $(OPT) $(OPTCFLAGS) $(CPUFLAGS) $(INCFLAGS) -c $< -o $@

$(HW_OBJS): $(OBJDIR)/%.o: backend/hw/%.c $(HEADERS) Makefile | $(OBJDIR)
	$(CC) -std=c99 $(OPT) $(OPTCFLAGS) $(CPUFLAGS) $(INCFLAGS) -c $< -o $@

# v3d_debug.c alone gets -DDEBUG: its kprintf body must be real at link time.
$(DBG_OBJ): backend/hw/v3d_debug.c $(HEADERS) Makefile | $(OBJDIR)
	$(CC) -std=c99 $(OPT) $(OPTCFLAGS) $(CPUFLAGS) -DDEBUG $(INCFLAGS) -c $< -o $@

# Built under a never-used name and renamed into place, as the script does:
# `ar rcs` onto a leftover archive MERGES into it instead of replacing it.
$(LIB): $(OBJS)
	rm -f $@.tmp$$$$ && $(AR) rcs $@.tmp$$$$ $(OBJS) && mv -f $@.tmp$$$$ $@

$(OBJDIR):
	mkdir -p $@

# Removes exactly what this Makefile builds, never the directory tree: OBJDIR
# can be pointed at another tree's object directory.
clean:
	rm -f $(OBJS) $(LIB)
	rmdir $(OBJDIR) 2>/dev/null || true

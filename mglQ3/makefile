CC := m68k-amigaos-gcc
AR := m68k-amigaos-ar
RANLIB := m68k-amigaos-ranlib
CPU ?= 68040
OBJDIR ?= obj_gcc
LIBNAME ?= libmgl-q3.a

COMMON_CFLAGS = -std=c99 -mcpu=$(CPU) -mhard-float -mcrt=clib2 \
	-O2 -ffast-math -fomit-frame-pointer -fno-strict-aliasing \
	-fno-strict-overflow -fsigned-char -D__M68K__
CFLAGS = $(COMMON_CFLAGS) -Iinclude -Wall -Wextra

LIB_SOURCES = src/context.c src/vertexbuffer.c src/draw.c \
	src/texture.c src/fog.c src/hclip.c src/init.c src/glu.c \
	src/aclip.c src/vertexelements.c src/vertexarray.c \
	src/others.c src/matrix.c
LIB_OBJS = $(patsubst src/%.c,$(OBJDIR)/%.o,$(LIB_SOURCES))

.PHONY: all clean
all: $(LIBNAME)

$(OBJDIR):
	mkdir -p $@

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBNAME): $(LIB_OBJS)
	mkdir -p $(dir $@)
	$(AR) rcs $@ $(LIB_OBJS)
	$(RANLIB) $@

clean:
	rm -rf $(OBJDIR)
	rm -f $(LIBNAME)

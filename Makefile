export SHELL = /bin/sh

export srcdir = ./src
debugdir = ./Debug
releasedir = ./Release
ifeq ($(MAKECMDGOALS),debug)
	export bin = $(debugdir)
else
	export bin = $(releasedir)
endif

export CC = gcc
export RM = rm -f
export MKDIR = mkdir -p

export LIBS = -lpthread
export CFLAGS = -Wall -fPIC -D_LARGEFILE64_SOURCE

export soname = librtpstore.so.0
export libname = $(soname).0.1
export LDFLAGS = -shared -Wl,-soname,$(soname)

export C_SRC = \
$(srcdir)/log.c \
$(srcdir)/rtp_foutput.c \
$(srcdir)/rtp_manager.c \
$(srcdir)/rtp_network.c \
$(srcdir)/rtp_stream_thread.c

export C_OBJ = \
$(bin)/log.o \
$(bin)/rtp_foutput.o \
$(bin)/rtp_manager.o \
$(bin)/rtp_network.o \
$(bin)/rtp_stream_thread.o

export C_DEPS = $(C_SRC:$(srcdir)%.c=$(bin)%.d)

.PHONY: all 
all: build

.PHONY: debug
debug: build

.PHONY: clean
clean:

.PHONY: build
build: mkdirs $(C_DEPS)
	$(MAKE) -f build.mk --no-print-directory $(MAKECMDGOALS)

.PHONY: clean
clean:
	$(RM) $(debugdir)/*.[od] $(releasedir)/*.[od] \
		$(debugdir)/$(libname) $(releasedir)/$(libname)

mkdirs:
	$(MKDIR) $(bin)

$(bin)/%.d: $(srcdir)/%.c
	$(RM) $@; \
	$(CC) -MM $< > $@

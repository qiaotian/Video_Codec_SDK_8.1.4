################################################################################
#
# Copyright 1993-2017 NVIDIA Corporation.  All rights reserved.
#
# Please refer to the NVIDIA end user license agreement (EULA) associated
# with this source code for terms and conditions that govern your use of
# this software. Any use, reproduction, disclosure, or distribution of
# this software and related documentation outside the terms of the EULA
# is strictly prohibited.
#
################################################################################
#
# Makefile project only supported on Mac OSX and Linux Platforms)
#
################################################################################

# OS Name (Linux or Darwin)
OSUPPER = $(shell uname -s 2>/dev/null | tr [:lower:] [:upper:])
OSLOWER = $(shell uname -s 2>/dev/null | tr [:upper:] [:lower:])

# Flags to detect 32-bit or 64-bit OS platform
OS_SIZE = $(shell uname -m | sed -e "s/i.86/32/" -e "s/x86_64/64/")
OS_ARCH = $(shell uname -m | sed -e "s/i386/i686/")

# These flags will override any settings
ifeq ($(i386),1)
	OS_SIZE = 32
	OS_ARCH = i686
endif

ifeq ($(x86_64),1)
	OS_SIZE = 64
	OS_ARCH = x86_64
endif

# Flags to detect either a Linux system (linux) or Mac OSX (darwin)
DARWIN = $(strip $(findstring DARWIN, $(OSUPPER)))

# Common binaries
GCC             ?= g++

# OS-specific build flags
ifneq ($(DARWIN),)
      LDFLAGS   := -Xlinker -rpath
      CCFLAGS   := -arch $(OS_ARCH)
else
  ifeq ($(OS_SIZE),32)
      LDFLAGS   += -L/usr/lib64 -lnvidia-encode -ldl -lGL -lglut
      CCFLAGS   := -m32
  else
      LDFLAGS   += -L/usr/lib64 -lnvidia-encode -ldl -lGL -lglut
      CCFLAGS   := -m64
  endif
endif

# Debug build flags
ifeq ($(dbg),1)
      CCFLAGS   += -g
      TARGET    := debug
else
      TARGET    := release
endif

# Common includes and paths
INCLUDES      := -I../common/inc

# Target rules
all: build

build: NvEncoderGLInterop

NvHWEncoder.o: ../common/src/NvHWEncoder.cpp ../common/inc/NvHWEncoder.h
	$(GCC) $(CCFLAGS) $(EXTRA_CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderGLInterop.o: NvEncoderGLInterop.cpp ../common/inc/nvEncodeAPI.h NvEncoderGLInterop.h
	$(GCC) $(CCFLAGS) $(EXTRA_CCFLAGS) $(INCLUDES) -o $@ -c $<

NvEncoderGLInterop: NvHWEncoder.o NvEncoderGLInterop.o
	$(GCC) $(CCFLAGS) -o $@ $+ $(LDFLAGS) $(EXTRA_LDFLAGS)

run: build
	./NvEncoderGLInterop

clean:
	rm -rf NvEncoderGLInterop NvHWEncoder.o NvEncoderGLInterop.o

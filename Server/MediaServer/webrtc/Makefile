# Copyright (C) 2019 The Qpidnetwork
# WebRTC Module Makefile
#
# Created on: 2019/6/13
# Author: Max.Chiu
# Email: Kingsleyyau@gmail.com
#

export MAKE	:=	make

ifeq ($(RELEASE), 1)
CXXFLAGS = -O2 
else
CXXFLAGS = -O2 -g
endif

CXXFLAGS 	+=	-DLINUX -DFILE_JNI_LOG
CXXFLAGS 	+=	-Wall -fmessage-length=0 -Wno-unused-variable -Wno-unused-but-set-variable -D__STDC_CONSTANT_MACROS
CXXFLAGS 	+=	-I. -I.. -I../third_party -I../third_party/libsdp/include \
				-I../third_party/libnice/nice -I../third_party/libnice/agent \
				-I/usr/include/glib-2.0/ -I/usr/lib64/glib-2.0/include/ \
				-I$(OPENSSL_INCLUDE)
				
OBJS 		+=	WebRTC.o IceClient.o
		
TARGET 		=	libwebrtc.a

.PHONY: all clean $(TARGET)

$(TARGET):	$(OBJS)
	$(AR) cr $@ $(OBJS)
	@echo '################################################################'
	@echo ''
	@echo '# Bulid libwebrtc.a completed!'
	@echo ''
	@echo '################################################################'

all:	$(TARGET) 

clean:
	rm -f $(OBJS) $(TARGET)
# Copyright (C) 2015 The MediaServer
# AsyncIOServer module Makefile
#
# Created on: 2019/6/13
# Author: Max.Chiu
# Email: Kingsleyyau@gmail.com
#

export MAKE	:=	make

ifeq ($(RELEASE), 1)
CXXFLAGS = -O3 
else
CXXFLAGS = -O2 -g #-D_CHECK_MEMORY_LEAK
endif

CXXFLAGS +=-std=c++11
CXXFLAGS +=	-Wall -fmessage-length=0 -Wno-unused-variable -Wno-unused-but-set-variable
CXXFLAGS +=	-I. -I.. -I../third_party -I../include -I../third_party/libev

OBJS 		=	TcpServer.o AsyncIOServer.o MainLoop.o
		
TARGET =	libserver.a

.PHONY: all deps $(TARGET)

$(TARGET):	$(OBJS)
	$(AR) cr $@ $(OBJS)
	@echo '################################################################'
	@echo ''
	@echo '# Bulid libserver.a completed!'
	@echo ''
	@echo '################################################################'
	
all:	$(TARGET) 

clean:
	rm -f $(OBJS) $(TARGET)

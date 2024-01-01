PROG=smartmeter
SRCS=main.cpp eventqueue/equeue/equeue.c eventqueue/equeue/equeue_posix.c eventqueue/equeue/eventqueue.cpp
MAN=
PREFIX?=/usr/local
INCLUDES= -I. \
	-I eventqueue \
	-I eventqueue/equeue \
	-I/usr/include
LDADD=-ljsoncpp -lmosquitto -pthread -lmodbus -lstdc++

.include <init.mk>
	@echo Configuring for "${TARGET_OSNAME}"


.if "${TARGET_OSNAME}" == "Linux"
INCLUDES+=-I/usr/include/modbus\
			  -I/usr/include/jsoncpp
LDADD+=-lstdc++
.elif "${TARGET_OSNAME}" == "FreeBSD"
INCLUDES+=-I/usr/local/include \
		  -I/usr/local/include/modbus \
		  -I/usr/local/include/jsoncpp
.endif

CFLAGS+=${INCLUDES}

CXXFLAGS+=${INCLUDES} -std=c++17

LDFLAGS+=-L/usr/lib -L/usr/local/lib
COMPILER_FEATURES=c++17

all: ${PROG_CXX}

install: all
	@echo Install

.include <prog.mk>

PROG_CXX=smartmeter
SRCS=main.cpp eventqueue/equeue/equeue.c eventqueue/equeue/equeue_posix.c eventqueue/equeue/eventqueue.cpp
MAN=
PREFIX?=/usr/local

INCLUDES= -I. \
	-I eventqueue \
	-I eventqueue/equeue \
	-I/usr/include \
	-I/usr/local/include \
	-I/usr/local/include/modbus \
	-I/usr/local/include/jsoncpp \

CFLAGS+=${INCLUDES}

CXXFLAGS+=${INCLUDES} -std=c++17

LDFLAGS+=-L/usr/lib \
	 -L/usr/local/lib \

LDADD=-ljsoncpp -lmosquitto -pthread -lmodbus
COMPILER_FEATURES=c++17

all: ${PROG_CXX}

install: all
	@echo Install

.include <bsd.prog.mk>

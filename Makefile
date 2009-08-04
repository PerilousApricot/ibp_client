# Hook for extra -I options, etc.
EXTRA_CFLAGS = -O2 

#Change this to where your IBP is located
PREFIX=/workspace/local
#PREFIX=/c/code/mingw/local
#PREFIX=/cygdrive/c/code/local

#For Phoebus support uncomment these lines
#PHOEBUS = -D_ENABLE_PHOEBUS -DHAVE_STDINT_H
#PHOEBUS_LIB = -llsl_client -ldl
#PHOEBUS_OBJS = net_phoebus.o net_fd.o
#No phoebus support
PHOEBUS = 
PHOEBUS_LIB = 
PHOEBUS_OBJS = net_phoebus.o

target=
FLAVOR=-D_LINUX64BIT
CFLAGS = -g -fPIC -Wall -I${IBP_INC} -I. ${FLAVOR} ${PHOEBUS} -D_ENABLE_DEBUG -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_THREAD_SAFE ${EXTRA_CFLAGS}
#FLAVOR=-D_MINGW32BIT
#CFLAGS = -g  -Wall -I${IBP_INC} -I. ${FLAVOR} ${PHOEBUS} -D_ENABLE_DEBUG -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_THREAD_SAFE ${EXTRA_CFLAGS}
LDINC = ld -i -o


CC = gcc
#INC_DIR= -I. -I${PREFIX}/include -I${PREFIX}/include/glib-2.0
LIBS = -L${PREFIX}/lib -lapr-1 ${PHOEBUS_LIB} -lpthread -lm 
#LIBS = -L${PREFIX}/lib -lapr-1 ${PHOEBUS_LIB} -lrpcrt4 -lshell32 -lws2_32 -ladvapi32 -lkernel32 -lmsvcrt
INC_DIR= -I. -I${PREFIX}/include -I${PREFIX}/include/apr-1
LIB_LIBS = 

NETWORK_OBJS = \
         network.o \
         net_sock.o \
         net_1_ssl.o \
         net_2_ssl.o \
         ${PHOEBUS_OBJS}

IBP_OBJS = \
    hconnection.o \
    oplist.o \
    opque.o \
    ibp_oplist.o \
    ibp_config.o \
    hportal.o \
    ibp_op.o \
    ibp_misc.o \
    ibp_types.o \
    ibp_sync.o \
    ibp_errno.o \
    ibp_client_version.o \
    string_token.o \
    iovec_sync.o \
    dns_cache.o \
    log.o \
    debug.o \
    stack.o \
    iniparse.o \
    phoebus.o \
    ${NETWORK_OBJS}


IBP_LIB = libibp.a
IBP_PERF_OBJS = ibp_perf.o ${IBP_LIB}
IBP_COPYPERF_OBJS = ibp_copyperf.o ${IBP_LIB}
IBP_TEST_OBJS = ibp_test.o ${IBP_LIB}
IBP_TOOL_OBJS = ibp_tool.o ${IBP_LIB}

TARGETS = ibp_perf ibp_copyperf ibp_test ibp_tool ${IBP_LIB} libibp.so
 
SRCS=*.c 

all: depend ${TARGETS}
#all: ${TARGETS}

ibp_perf: ${IBP_PERF_OBJS}
	${CC} -g ${CFLAGS} ${INC_DIR} $^ -o $@ ${LIBS}
ibp_copyperf: ${IBP_COPYPERF_OBJS}
	${CC} -g ${CFLAGS} ${INC_DIR} $^ -o $@ ${LIBS}
ibp_test: ${IBP_TEST_OBJS}
	${CC} -g ${CFLAGS} ${INC_DIR} $^ -o $@ ${LIBS}
ibp_tool: ${IBP_TOOL_OBJS}
	${CC} -g ${CFLAGS} ${INC_DIR} $^ -o $@ ${LIBS}
libibp.a: ${IBP_OBJS}
	ar rcs libibp.a ${IBP_OBJS}
libibp.so: ${IBP_OBJS}
	gcc -shared -W1,-soname,libibp.so -o libibp.so ${IBP_OBJS} ${LIB_LIBS}

#===========================================
#

clean:
	rm -f *.o ${TARGETS}

%.o: %.c
	${CC} ${CFLAGS} ${INC_DIR} -c $< -o $@

TAGS:
	etags `find . -name "*.[h|cc]"`

.depend:
	touch .depend

depend:
	$(RM) .depend
	./make_version $(INC_DIR) $(CFLAGS)
	makedepend -f- -- $(INC_DIR) $(CFLAGS) -- $(SRCS) > .depend 2>/dev/null

# now add a line to include the dependency list.
include .depend

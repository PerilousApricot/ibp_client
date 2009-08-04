# Hook for extra -I options, etc.
EXTRA_CFLAGS = -O2 

#Change this to where your IBP is located
PREFIX=/workspace/local

#For Phoebus support uncomment these lines
PHOEBUS = -D_ENABLE_PHOEBUS -DHAVE_STDINT_H
PHOEBUS_LIB = -llsl_client -ldl
#No phoebus support
#PHOEBUS = 
#PHOEBUS_LIB = 

target=
FLAVOR=-D_LINUX64BIT
CFLAGS = -g -fPIC -Wall -I${IBP_INC} -I. ${FLAVOR} ${PHOEBUS} -D_ENABLE_DEBUG -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_THREAD_SAFE ${EXTRA_CFLAGS}
LDINC = ld -i -o


CC = gcc
#LIBS = -L${IBP_LIB} -libp -lpthread
LIBS = -L${PREFIX}/lib -lglib-2.0 ${PHOEBUS_LIB} -lpthread -lm
INC_DIR= -I. -I${PREFIX}/include -I${PREFIX}/include/glib-2.0

NETWORK_OBJS = \
         network.o \
         net_sock.o \
         net_1_ssl.o \
         net_2_ssl.o \
         net_fd.o \
         net_phoebus.o

IBP_OBJS = \
    hconnection.o \
    oplist.o \
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
	gcc -shared -W1,-soname,libibp.so -o libibp.so ${IBP_OBJS}

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

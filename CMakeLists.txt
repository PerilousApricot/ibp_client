cmake_minimum_required(VERSION 2.6)

# make a project
project( IBP C )

# get dependancies
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
#find_package(BerkeleyDB REQUIRED)
#find_package(GLIB2 REQUIRED)
find_package(OpenSSL REQUIRED)
#find_package(Protobufc REQUIRED)
find_package(APR REQUIRED)
# find_package(APRUtil)

include_directories(${BDB_INCLUDE_DIR} ${APR_INCLUDE_DIR} ${APRUTIL_INCLUDES}
${OPENSSL_INCLUDE_DIR} "ceph/eobfs" "ceph" ${PROTOBUF_INCLUDE_DIR})

SET(LIBS ${LIBS} ${BDB_LIBRARIES} ${APR_LIBRARIES} ${OPENSSL_LIBRARIES}
${CRYPTO_LIBRARIES} ${PROTOBUF_LIBRARY} ${APRUTIL_LIBS} pthread ${APR_LIBRARY}
m)

message("aprlibs is ${APR_LIBRARIES} ${APR_LIBRARY}")
# config options
IF( ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64" )
set( IS64BIT 1 )
ENDIF( ${CMAKE_SYSTEM_PROCESSOR} MATCHES "x86_64" )

OPTION( _ENABLE_PHOEBUS "Enable Phoebus support" OFF )
CONFIGURE_FILE( ${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_SOURCE_DIR}/config.h )

# common objects
SET(NETWORK_OBJS network net_sock net_1_ssl net_2_ssl net_phoebus )

SET(IBP_OBJS 
    hconnection 
    oplist 
    opque 
    ibp_oplist 
    ibp_config 
    hportal 
    ibp_op 
    ibp_misc 
    ibp_types 
    ibp_sync 
    ibp_errno 
    ibp_client_version 
    string_token 
    iovec_sync 
    dns_cache 
    log 
    debug 
    stack
    iniparse 
    phoebus
    ${NETWORK_OBJS}
 )


message("proto src is ${PROTO_SRC}")
# various executables
ADD_EXECUTABLE( ibp_perf ibp_perf ${IBP_OBJS} )
ADD_EXECUTABLE( ibp_copyperf ibp_copyperf ${IBP_OBJS} )
ADD_EXECUTABLE( ibp_test ibp_test ${IBP_OBJS} )
ADD_EXECUTABLE( ibp_tool ibp_tool ${IBP_OBJS} )
ADD_LIBRARY( ibp SHARED ${IBP_OBJS})
ADD_LIBRARY( ibp-static STATIC ${IBP_OBJS})
SET_TARGET_PROPERTIES( ibp-static PROPERTIES OUTPUT_NAME "ibp" )
SET_TARGET_PROPERTIES(ibp PROPERTIES CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(ibp-static PROPERTIES CLEAN_DIRECT_OUTPUT 1)


TARGET_LINK_LIBRARIES( ibp_perf ibp ${LIBS})
TARGET_LINK_LIBRARIES( ibp_test ibp ${LIBS})
TARGET_LINK_LIBRARIES( ibp_copyperf ibp ${LIBS})
TARGET_LINK_LIBRARIES( ibp_tool ibp ${LIBS})


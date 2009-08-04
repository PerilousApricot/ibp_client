/*
Advanced Computing Center for Research and Education Proprietary License
Version 1.0 (April 2006)

Copyright (c) 2006, Advanced Computing Center for Research and Education,
 Vanderbilt University, All rights reserved.

This Work is the sole and exclusive property of the Advanced Computing Center
for Research and Education department at Vanderbilt University.  No right to
disclose or otherwise disseminate any of the information contained herein is
granted by virtue of your possession of this software except in accordance with
the terms and conditions of a separate License Agreement entered into with
Vanderbilt University.

THE AUTHOR OR COPYRIGHT HOLDERS PROVIDES THE "WORK" ON AN "AS IS" BASIS,
WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT
LIMITED TO THE WARRANTIES OF MERCHANTABILITY, TITLE, FITNESS FOR A PARTICULAR
PURPOSE, AND NON-INFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Vanderbilt University
Advanced Computing Center for Research and Education
230 Appleton Place
Nashville, TN 37203
http://www.accre.vanderbilt.edu
*/ 

//*************************************************************
// ibp_op.h - Header defining I/O structs and operations
//*************************************************************

#ifndef __IBP_OP_H_
#define __IBP_OP_H_

#include "stack.h"
#include "network.h"
//#include "ibp_config.h"
#include "oplist.h"
#include "host_portal.h"
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ERR_RETRY_DEADSOCKET 0 //** Used as another IBP error
#define IBP_READ IBP_LOAD
#define IBP_ST_STATS   4     //** Get depot stats
#define IBP_ST_VERSION 5     //** This is for the get_version() command
#define IBP_ST_RES   3         //** Used to get the list or resources from the depot
#define MAX_KEY_SIZE 256

typedef struct {
   int tcpsize;         //** TCP R/W buffer size.  If 0 then OS default is used
   int min_idle;        //** Connection minimum idle time before disconnecting
   int min_threads;     //** Min and max threads allowed to a depot
   int max_threads;     //** Max number of simultaneous connection to a depot
   int max_connections; //** Max number of connections across all connections
   int new_command;     //** byte "cost" of just the command portion excluding any data transfer
   int64_t max_workload;    //** Max workload allowed in a given connection
   int wait_stable_time; //** Time to wait before opening a new connection for a heavily loaded depot
   int abort_conn_attempts; //** If this many failed connection requests occur in a row we abort
   int check_connection_interval;  //**# of secs to wait between checks if we need more connections to a depot
   int max_retry;        //** Max number of times to retry a command before failing.. only for dead socket retries
   ibp_connect_context_t cc[IBP_MAX_NUM_CMDS+1];  //** Default connection contexts for EACH command
} ibp_config_t;

extern Hportal_context_t *_hpc_config;
extern ibp_config_t *_ibp_config;

typedef struct {  //** Read/Write operation 
   ibp_cap_t *cap;
   char       key[MAX_KEY_SIZE];
   char       typekey[MAX_KEY_SIZE];
   char *buf;
   int offset;
   int size;
   void *arg;
   int (*next_block)(int, void *, int *, char **);
//   int counter;
} ibp_op_rw_t;

typedef struct { //** MERGE allocoation op
   char mkey[MAX_KEY_SIZE];      //** Master key
   char mtypekey[MAX_KEY_SIZE];  
   char ckey[MAX_KEY_SIZE];      //** Child key
   char ctypekey[MAX_KEY_SIZE]; 
} ibp_op_merge_alloc_t;

typedef struct {  //**Allocate operation
   int size;
   int offset;                         //** ibp_alias_allocate
   int duration;                       //** ibp_alias_allocate
   char       key[MAX_KEY_SIZE];      //** ibp_rename/alias_allocate
   char       typekey[MAX_KEY_SIZE];  //** ibp_rename/alias_allocate
   ibp_cap_t *mcap;         //** This is just used for ibp_rename/ibp_split_allocate
   ibp_capset_t *caps;
   ibp_depot_t *depot;
   ibp_attributes_t *attr;
} ibp_op_alloc_t;

typedef struct {  //** modify count and PROBE  operation
   int       cmd;    //** IBP_MANAGE or IBP_ALIAS_MANAGE
   ibp_cap_t *cap;
   char       mkey[MAX_KEY_SIZE];     //** USed for ALIAS_MANAGE
   char       mtypekey[MAX_KEY_SIZE]; //** USed for ALIAS_MANAGE
   char       key[MAX_KEY_SIZE];
   char       typekey[MAX_KEY_SIZE];
   int        mode;
   int        captype;
   ibp_capstatus_t *probe;
   ibp_alias_capstatus_t *alias_probe;
} ibp_op_probe_t;

typedef struct {  //** modify Allocation operation
   ibp_cap_t *cap;
   char       mkey[MAX_KEY_SIZE];     //** USed for ALIAS_MANAGE
   char       mtypekey[MAX_KEY_SIZE]; //** USed for ALIAS_MANAGE
   char       key[MAX_KEY_SIZE];
   char       typekey[MAX_KEY_SIZE];
   size_t     offset;    //** IBP_ALIAS_MANAGE
   size_t     size;
   time_t     duration;
   int        reliability;
} ibp_op_modify_alloc_t;

typedef struct {  //** depot depot copy operations
   char      *path;       //** Phoebus path or NULL for default
   ibp_cap_t *srccap;
   ibp_cap_t *destcap;
   char       src_key[MAX_KEY_SIZE];
   char       src_typekey[MAX_KEY_SIZE];
   int        src_offset;
   int        dest_offset;
   int        len;
   int        dest_timeout;
   int        dest_client_timeout;
   int        ibp_command;
   int        ctype;
} ibp_op_copy_t;

typedef struct {  //** Modify a depot/RID settings
   ibp_depot_t *depot;
   char *password;
   size_t max_hard;
   size_t max_soft;
   time_t max_duration;
} ibp_op_depot_modify_t;

typedef struct {  //** Modify a depot/RID settings
   ibp_depot_t *depot;
   char *password;
   ibp_depotinfo_t *di;
} ibp_op_depot_inq_t;

typedef struct {  //** Get the depot version information
  ibp_depot_t *depot;
  char *buffer;
  int buffer_size;
} ibp_op_version_t;

typedef struct {  //** Get a list of RID's for a depot
  ibp_depot_t *depot;
  ibp_ridlist_t *rlist; 
} ibp_op_rid_inq_t;

typedef struct _ibp_op_s { //** Individual IO operation
   Hportal_op_t hop;
   oplist_base_op_t bop;
   int primary_cmd;//** Primary sync IBP command family
   int sub_cmd;    //** sub command, if applicable
   union {         //** Holds the individual commands options
     ibp_op_alloc_t  alloc_op;
     ibp_op_merge_alloc_t  merge_op;
     ibp_op_probe_t  probe_op;
     ibp_op_rw_t     rw_op;
     ibp_op_copy_t   copy_op;
     ibp_op_depot_modify_t depot_modify_op;
     ibp_op_depot_inq_t depot_inq_op;
     ibp_op_modify_alloc_t mod_alloc_op;
     ibp_op_rid_inq_t   rid_op; 
     ibp_op_version_t   ver_op;
   };
} ibp_op_t;

//** ibp_op.c **
ibp_op_t *new_ibp_op();
void init_ibp_base_op(ibp_op_t *op, char *logstr, int timeout, int workload, char *hostport, 
     int cmp_size, int primary_cmd, int sub_cmd, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_rw_op(int rw_type, ibp_cap_t *cap, int offset, int size,                           
     int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_rw_op(ibp_op_t *op, int rw_type, ibp_cap_t *cap, int offset, int size,
     int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_user_read_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_user_read_op(ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_read_op(ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_read_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_user_write_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_user_write_op(ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_write_op(ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_write_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_append_op(ibp_cap_t *cap, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_append_op(ibp_op_t *op, ibp_cap_t *cap, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alloc_op(ibp_capset_t *caps, int size, ibp_depot_t *depot, ibp_attributes_t *attr, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alloc_op(ibp_op_t *op, ibp_capset_t *caps, int size, ibp_depot_t *depot, ibp_attributes_t *attr, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_split_alloc_op(ibp_op_t *op, ibp_cap_t *mcap, ibp_capset_t *caps, int size, 
       ibp_attributes_t *attr, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_merge_alloc_op(ibp_cap_t *mcap, ibp_cap_t *ccap,
       int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_merge_alloc_op(ibp_op_t *op, ibp_cap_t *mcap, ibp_cap_t *ccap, int timeout, 
    oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_split_alloc_op(ibp_cap_t *mcap, ibp_capset_t *caps, int size,
       ibp_attributes_t *attr, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alias_alloc_op(ibp_op_t *op, ibp_capset_t *caps, ibp_cap_t *mcap, int offset, int size,
   int duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alias_alloc_op(ibp_capset_t *caps, ibp_cap_t *mcap, int offset, int size,
   int duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_rename_op(ibp_capset_t *caps, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_rename_op(ibp_op_t *op, ibp_capset_t *caps, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_remove_op(ibp_cap_t *cap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_remove_op(ibp_op_t *op, ibp_cap_t *cap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alias_remove_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alias_remove_op(ibp_cap_t *cap, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_modify_count_op(ibp_cap_t *cap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_modify_count_op(ibp_op_t *op, ibp_cap_t *cap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alias_modify_count_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alias_modify_count_op(ibp_cap_t *cap, ibp_cap_t *mcap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_modify_alloc_op(ibp_op_t *op, ibp_cap_t *cap, size_t size, time_t duration, int reliability, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_modify_alloc_op(ibp_cap_t *cap, size_t size, time_t duration, int reliability, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_probe_op(ibp_cap_t *cap, ibp_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_probe_op(ibp_op_t *op, ibp_cap_t *cap, ibp_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alias_probe_op(ibp_op_t *op, ibp_cap_t *cap, ibp_alias_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alias_probe_op(ibp_cap_t *cap, ibp_alias_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_copyappend_op(int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap, int src_offset, int size,
        int src_timeout, int  dest_timeout, int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_copyappend_op(ibp_op_t *op, int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap, int src_offset, int size,
        int src_timeout, int  dest_timeout, int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_copy_op(ibp_op_t *op, int mode, int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap,
        int src_offset, int dest_offset, int size, int src_timeout, int  dest_timeout, 
        int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_copy_op(int mode, int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap,
        int src_offset, int dest_offset, int size, int src_timeout,
        int  dest_timeout, int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_depot_modify_op(ibp_op_t *op, ibp_depot_t *depot, char *password, size_t hard, size_t soft,
      time_t duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_depot_modify_op(ibp_depot_t *depot, char *password, size_t hard, size_t soft,
      time_t duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_alias_modify_alloc_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, size_t offset, size_t size, time_t duration,
     int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_alias_modify_alloc_op(ibp_cap_t *cap, ibp_cap_t *mcap, size_t offset, size_t size, time_t duration,
     int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_depot_inq_op(ibp_op_t *op, ibp_depot_t *depot, char *password, ibp_depotinfo_t *di, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_depot_inq_op(ibp_depot_t *depot, char *password, ibp_depotinfo_t *di, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_version_op(ibp_op_t *op, ibp_depot_t *depot, char *buffer, int buffer_size, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_version_op(ibp_depot_t *depot, char *buffer, int buffer_size, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void set_ibp_query_resources_op(ibp_op_t *op, ibp_depot_t *depot, ibp_ridlist_t *rlist, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
ibp_op_t *new_ibp_query_resources_op(ibp_depot_t *depot, ibp_ridlist_t *rlist, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc);
void free_ibp_op(ibp_op_t *iop);
void finalize_ibp_op(ibp_op_t *iop);
int ibp_op_status(ibp_op_t *op);
int ibp_op_id(ibp_op_t *op);

//** ibp_oplist.c **
oplist_t *new_ibp_oplist(oplist_app_notify_t *an);
void init_ibp_oplist(oplist_t *iol, oplist_app_notify_t *an);
int add_ibp_oplist(oplist_t *iolist, ibp_op_t *iop);
ibp_op_t *ibp_get_failed_op(oplist_t *oplist);
ibp_op_t *ibp_waitany(oplist_t *iolist);

//** ibp_config.c **
void ibp_set_abort_attempts(int n);
int  ibp_get_abort_attempts();
void ibp_set_tcpsize(int n);
int  ibp_get_tcpsize();
void ibp_set_min_depot_threads(int n);
int  ibp_get_min_depot_threads();
void ibp_set_max_depot_threads(int n);
int  ibp_get_max_depot_threads();
void ibp_set_max_connections(int n);
int  ibp_get_max_connections();
void ibp_set_command_weight(int n);
int  ibp_get_command_weight();
void ibp_set_max_thread_workload(int n);
int  ibp_get_max_thread_workload();
void ibp_set_wait_stable_time(int n);
int  ibp_get_wait_stable_time();
void ibp_set_check_interval(int n);
int  ibp_get_check_interval();
void ibp_set_max_retry(int n);
int  ibp_get_max_retry();
int ibp_load_config(char *fname);
void set_ibp_config(ibp_config_t *cfg);
void default_ibp_config();
void ibp_init();
void ibp_finalize();

//*** ibp_sync.c ***
int ibp_sync_command(ibp_op_t *op);
unsigned long int IBP_phoebus_copy(char *path, ibp_cap_t *srccap, ibp_cap_t *destcap, ibp_timer_t  *src_timer, ibp_timer_t *dest_timer,
        unsigned long int size, unsigned long int offset);

//**** ibp_client_version.c *******
char *ibp_client_version();

#ifdef __cplusplus
}
#endif


#endif


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

#ifndef __HOST_PORTAL_H_
#define __HOST_PORTAL_H_

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <apr_hash.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include "fmttypes.h"
#include "network.h"
#include "oplist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HP_COMPACT_TIME 10   //** How often to run the garbage collector


typedef struct {   //** Hportal operation
   char *hostport; //** Depot hostname:port:type:...  Unique string for host/connect_context
   void *connect_context;   //** Private information needed to make a host connection
   int  cmp_size;  //** Used for ordering commands within the same host
   int timeout;    //** Command timeout
   int64_t workload;   //** Workload for measuring channel usage
   int max_workload; //** Max workload for a connection
   int retry_count;//** Number of times retried
   int (*send_command)(void *op, NetStream_t *ns);  //**Send command routine
   int (*send_phase)(void *op, NetStream_t *ns);    //**Handle "sending" side of command
   int (*recv_phase)(void *op, NetStream_t *ns);    //**Handle "receiving" half of command
   int (*destroy_command)(void *op);                //**Destroys the data structure
   time_t start_time;
   time_t end_time;
}  Hportal_op_t;


typedef struct {  //** Hportal specific implementation
  int hp_ok;                //** Ok Value
  int hp_retry_dead_socket; //** Error occured but Ok to retry.  Close socket
  int hp_timeout;           //** Command timed out
  int hp_generic_err;       //** Generic internal error
  int dead_connection;      //** Dead connection error
  int hp_invalid_host;      //** Can't resolve hostname
  int hp_cant_connect;      //** Can't connect to the host
  oplist_base_op_t *(*get_base_op)(void *);  //** Returns the oplist base op
  Hportal_op_t *(*get_hp_op)(void *);   //** Returns the hportal_op 
  void *(*dup_connect_context)(void *connect_context);  //** Duplicates a ccon
  void (*destroy_connect_context)(void *connect_context);
  int (*host_connect)(NetStream_t *ns, void *connect_context, char *host, int port, Net_timeout_t timeout);
  void (*host_close_connection)(NetStream_t *ns);
} Hportal_impl_t;

typedef struct {             //** Handle for maintaining all the ecopy connections
  apr_thread_mutex_t *lock;
  apr_hash_t *table;         //** Table containing the depot_portal structs
  apr_pool_t *pool;          //** Memory pool for hash table
  int running_threads;       //** currently running # of connections
  int max_connections;       //** Max aggregate allowed number of threads
  int min_threads;           //** Max allowed number of threads/host
  int max_threads;           //** Max allowed number of threads/host
  int64_t max_workload;      //** Max allowed workload before spawning another connection
  int compact_interval;      //** Interval between garbage collections calls
  int wait_stable_time;      //** time to wait before adding connections for unstable hosts
  int abort_conn_attempts;   //** If this many failed connection requests occur in a row we abort
  int check_connection_interval; //** Max time to wait for a thread to check for a close
  int min_idle;              //** Idle time before closing connection
  int max_retry;             //** Default max number of times to retry an op
  int count;                 //** Internal Counter 
  time_t   next_check;       //** Time for next compact_dportal call
  Net_timeout_t dt;          //** Default wait time
  Hportal_impl_t *imp;       //** Actual implementaion for application
} Hportal_context_t;


typedef struct {      //** Hportal stack operation
  oplist_t *oplist;
  void *op;
} Hportal_stack_op_t;

typedef struct {       //** Contains information about the depot including all connections
  char skey[512];         //** Search key used for lookups its "host:port:type:..." Same as for the op
  char host[512];         //** Hostname
  int port;               //** port 
  int invalid_host;       //** Flag that this host is not resolvable
  int64_t workload;       //** Amount of work left in the feeder que
  int64_t cmds_processed; //** Number of commands processed
  int failed_conn_attempts;     //** Failed net_connects()
  int successful_conn_attempts; //** Successful net_connects()
  int abort_conn_attempts; //** IF this many failed connection requests occur in a row we abort
  int n_conn;             //** Number of current depot connections
  int stable_conn;        //** Last count of "stable" connections
  int max_conn;           //** Max allowed connections, normally global_config->max_threads
  int min_conn;           //** Max allowed connections, normally global_config->min_threads 
  time_t pause_until;     //** Forces the system to wait, if needed, before making new conn
  Stack_t *conn_list;     //** List of connections
  Stack_t *que;           //** Task que
  Stack_t *closed_que;    //** List of closed but not reaped connections
  Stack_t *sync_list;     //** List of dedicated dportal/dc for the traditional IBP sync calls
  apr_thread_mutex_t *lock;  //** shared lock
  apr_thread_cond_t *cond;  
  apr_pool_t *mpool;
  void *connect_context;   //** Private information needed to make a host connection
  Hportal_context_t *context;  //** Specific Hportal implementaion
} Host_portal_t;

typedef struct {            //** Individual depot connection in conn_list
   int cmd_count;
   int curr_workload;
   int shutdown_request;
   int net_connect_status;
   time_t last_used;          //** Time the last command completed
   NetStream_t *ns;           //** Socket 
   Stack_t *pending_stack;    //** Local task que. An op  is mpoved from the parent que to here
   Stack_ele_t *my_pos;       //** My position int the dp conn list
   Hportal_stack_op_t *curr_op;   //** Sending phase op that could have failed
   Host_portal_t *hp;         //** Pointerto parent depot portal with the todo list
//   apr_thread_t *thread;    //** Thread data
   apr_thread_mutex_t *lock;      //** shared lock
   apr_thread_cond_t *send_cond;  
   apr_thread_cond_t *recv_cond;
   apr_thread_t *send_thread; //** Sending thread
   apr_thread_t *recv_thread; //** recving thread
   apr_pool_t   *mpool;       //** MEmory pool for 
} Host_connection_t;


extern Net_timeout_t global_dt;

//** Routines from hportal.c
#define hportal_trylock(hp)   apr_thread_mutex_trylock(hp->lock)
#define hportal_lock(hp)   apr_thread_mutex_lock(hp->lock)
#define hportal_unlock(hp) apr_thread_mutex_unlock(hp->lock)
#define hportal_signal(hp) apr_thread_cond_broadcast(hp->cond)

void hportal_wait(Host_portal_t *hp, int dt);
int get_hpc_thread_count(Hportal_context_t *hpc);
void modify_hpc_thread_count(Hportal_context_t *hpc, int n);
Host_portal_t *create_hportal(Hportal_context_t *hpc, void *connect_context, char *hostport, int min_conn, int max_conn);
///???void Host_dportal(Host_portal_t *hp);
Hportal_context_t *create_hportal_context(Hportal_impl_t *hpi);
void destroy_hportal_context(Hportal_context_t *hpc);
void finalize_hportal_context(Hportal_context_t *hpc);
Hportal_stack_op_t *new_hportal_op(oplist_t *oplist, void *op);
Hportal_stack_op_t *_get_hportal_op(Host_portal_t *hp);
void destroy_hportal_op(Hportal_stack_op_t *hpo);
void shutdown_hportal(Hportal_context_t *hpc);
void compact_dportals(Hportal_context_t *hpc);
void _hp_fail_tasks(Host_portal_t *hp, int err_code);
void check_hportal_connections(Host_portal_t *hp);
Host_portal_t *submit_hportal_sync(Hportal_context_t *hpc, oplist_t *oplist, void *op);
int submit_hportal(Host_portal_t *dp, oplist_t *oplist, void *op, int addtotop);
int submit_hp_op(Hportal_context_t *hpc, oplist_t *oplist, void *op);

//** Routines for hconnection.c
#define trylock_hc(a) apr_thread_mutex_trylock(a->lock)
#define lock_hc(a) apr_thread_mutex_lock(a->lock)
#define unlock_hc(a) apr_thread_mutex_unlock(a->lock)
#define hc_send_signal(hc) apr_thread_cond_signal(hc->send_cond)
#define hc_recv_signal(hc) apr_thread_cond_signal(hc->recv_cond)

Host_connection_t *new_host_connection();
void destroy_host_connection(Host_connection_t *hc);
void close_hc(Host_connection_t *dc);
int create_host_connection(Host_portal_t *hp);

#ifdef __cplusplus
}
#endif

#endif


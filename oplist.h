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
// oplist.h - Header defining I/O structs and operations
//*************************************************************

#ifndef __OPLIST_H_
#define __OPLIST_H_

#include <pthread.h>
#include "stack.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPLIST_AUTO_NONE     0      //** User has to manually free the data
#define OPLIST_AUTO_FINALIZE 1      //** Auto "finalize" oplist when finished
#define OPLIST_AUTO_FREE     2      //** Auto "free" oplist when finished

struct oplist_s;

typedef struct {   //** Used for application level callback
   void *data;
   void (*notify)(void *data);
} oplist_app_notify_t;

typedef struct {   //** Base info for each op
   int id;       
   int status;
   oplist_app_notify_t *an;
} oplist_base_op_t;

typedef struct oplist_s oplist_t;

typedef struct {     //** routines needed for implmentingthe op/oplist framework
   int ok_status;         //** Return value considered successful
   int blank_status;      //** Value to assign a "blank" status. Used for initialiing an op
   void *arg;             //** Container for private data if needed
   oplist_base_op_t *(*get_base_op)(void *op);
//   void (*op_modify_ref)(void *op, int inc);
//   int (*op_get_ref)(void *op);
   void (*op_finalize)(void *op);
   void (*op_free)(void *op);
//   void (*op_set_status)(void *op, int status);
//   int  (*op_get_status)(void *op);
//   void (*op_set_id)(void *op, int id);
//   void (*op_mark_completed)(void *op, int status);
   void (*oplist_sort_tasks)(oplist_t *oplist);        //**optional
   void (*notify)(oplist_t *oplist, void *op);  
//   void (*op_notify)(oplist_t *oplist, void *op);     
   void (*submit_op)(oplist_t *oplist, void *op);
} oplist_implementation_t;

struct oplist_s {
   Stack_t *list;         //** List of tasks
   Stack_t *finished;     //** Tasks that have completed and not yet processed
   Stack_t *failed;       //** All tasks that fail are also placed here
   oplist_app_notify_t *an; //**Optional app notify obj for oplist
   int id;                //** This oplist's id
   int count_id;          //** Used for assigning ID's to ops
   int nleft;             //** Number of tasks left to be processed
   int started_execution; //** If 1 the tasks have already been submitted for execution
   int free_mode;         //** How to free the oplist data when complete
   int finished_submission; //** No more tasks will be submitted so it's safe to free the data when finished
   pthread_mutex_t lock;  //** shared lock
   pthread_cond_t cond;   //** shared condition variable
   oplist_implementation_t *imp;
};

#define lock_oplist(opl)   pthread_mutex_lock(&(opl->lock))
#define unlock_oplist(opl) pthread_mutex_unlock(&(opl->lock))

oplist_t *new_oplist(oplist_implementation_t *imp, oplist_app_notify_t *an);
void init_oplist(oplist_t *iol, oplist_implementation_t *imp, oplist_app_notify_t *an);
int oplist_nfailed(oplist_t *oplist);
void free_oplist(oplist_t *oplist);
void finalize_oplist(oplist_t *oplist, int op_mode);
int add_oplist(oplist_t *iolist, void *iop);
void *oplist_get_failed_op(oplist_t *oplist);
int oplist_nfailed(oplist_t *oplist);
int oplist_tasks_left(oplist_t *oplist);
int oplist_waitall(oplist_t *iolist);
void *oplist_waitany(oplist_t *iolist);
void oplist_start_execution(oplist_t *oplist);
void oplist_finished_submission(oplist_t *oplist, int free_mode);
void oplist_mark_completed(oplist_t *oplist, void *op, int status);
void init_oplist_system();
void destroy_oplist_system();

void bop_set_status(oplist_base_op_t *bop, int status);
int bop_get_status(oplist_base_op_t *bop);
void bop_set_id(oplist_base_op_t *bop, int id);
int bop_get_id(oplist_base_op_t *bop);
void bop_set_notify(oplist_base_op_t *bop, oplist_app_notify_t *an);
oplist_app_notify_t *bop_get_notify(oplist_base_op_t *bop);
void app_notify_set(oplist_app_notify_t *an, void (*notify)(void *data), void *data);
void app_notify_execute(oplist_app_notify_t *an);
void bop_init(oplist_base_op_t *bop, int id, int status, oplist_app_notify_t *an);
#ifdef __cplusplus
}
#endif


#endif


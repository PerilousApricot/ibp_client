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
// opque.h - Header defining I/O structs and operations for
//     collections of oplists
//*************************************************************

#ifndef __OPQUE_H_
#define __OPQUE_H_

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include "stack.h"
#include "oplist.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OPLIST_AUTO_NONE     0      //** User has to manually free the data
#define OPLIST_AUTO_FINALIZE 1      //** Auto "finalize" oplist when finished
#define OPLIST_AUTO_FREE     2      //** Auto "free" oplist when finished

struct opque_s;

struct opque_s {
   Stack_t *list;         //** List of tasks
   Stack_t *finished;     //** lists that have completed and not yet processed
   Stack_t *failed;       //** All lists that fail are also placed here
   Stack_t *oplist_an;    //**Callback for each oplist
   oplist_app_notify_t *an; //**Optional app notify obj for opque
   int id;                //** This opque's id
   int count_id;          //** Used for assigning ID's to ops
   int nleft;             //** Number of lists left to be processed
   apr_thread_mutex_t *lock;  //** shared lock
   apr_thread_cond_t *cond;   //** shared condition variable
};

typedef struct opque_s opque_t;

typedef struct {
  oplist_app_notify_t an;
  oplist_t *iol;
  opque_t *q;
} opque_an_t;

#define lock_opque(q)   apr_thread_mutex_lock((q)->lock)
#define unlock_opque(q) apr_thread_mutex_unlock((q)->lock)

opque_t *new_opque(oplist_app_notify_t *an);
void init_opque(opque_t *que, oplist_app_notify_t *an);
int opque_nfailed(opque_t *que);
void free_opque(opque_t *que);
void finalize_opque(opque_t *que);
int add_opque(opque_t *que, oplist_t *oplist);
oplist_t *opque_get_failed(opque_t *que);
int opque_nfailed(opque_t *que);
void opque_notify_append(opque_t *q, oplist_app_notify_t *an);
int opque_tasks_left(opque_t *que);
int opque_waitall(opque_t *que);
oplist_t *opque_waitany(opque_t *que);

#ifdef __cplusplus
}
#endif


#endif


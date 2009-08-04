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

#include <assert.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "opque.h"

//*************************************************************
// _opque_cb - Global callback for all opque's
//*************************************************************

void _opque_cb(void *v)
{
  int n;
  opque_an_t *qan = (opque_an_t *)v;

  lock_opque(qan->q);  

  push(qan->q->finished, qan->iol);  //** It always goes o nthe finished list

  n = oplist_nfailed(qan->iol);
  if (n != 0) push(qan->q->failed, qan->iol); //** Push it on the failed list if needed

  qan->q->nleft--;  
  if (qan->q->nleft <= 0) {  //** we're finished
     apr_thread_cond_broadcast(qan->q->cond);
  }

  unlock_opque(qan->q);  
}



//*************************************************************
// init_opque - Initializes a que list container
//*************************************************************

void init_opque(opque_t *que, oplist_app_notify_t *an)
{
  apr_thread_mutex_lock(_oplist_lock);
  que->id = _oplist_counter;
  _oplist_counter++;
  apr_thread_mutex_unlock(_oplist_lock);
  
  apr_thread_mutex_create(&(que->lock), APR_THREAD_MUTEX_DEFAULT,_oplist_pool);
  apr_thread_cond_create(&(que->cond), _oplist_pool);
  que->list = new_stack();
  que->finished = new_stack();
  que->failed = new_stack();
  que->oplist_an = new_stack();
  que->count_id = 0;
  que->nleft = 0;
  que->an = NULL; app_notify_append(que->an, an);
  
}

//*************************************************************
// new_opque - Generates a new que container
//*************************************************************

opque_t *new_opque(oplist_app_notify_t *an)
{
  opque_t *q = (opque_t *)malloc(sizeof(opque_t));
  if (q == NULL) {
     log_printf(0, "new_opque: malloc failed!\n");
     return(NULL);
  }

  init_opque(q, an);

  return(q);
}

//*************************************************************
//  teardown_opque - tears down the que container
//*************************************************************

void teardown_opque(opque_t *q)
{
  log_printf(15, "teardown_opque: opque=%d size(list)=%d size(finished)=%d size(failed)=%d\n",
       q->id, stack_size(q->list), stack_size(q->finished), stack_size(q->failed));

  free_stack(q->list, 0);
  free_stack(q->finished, 0);
  free_stack(q->failed, 0);
  free_stack(q->oplist_an, 0);
  apr_thread_mutex_destroy(q->lock);
  apr_thread_cond_destroy(q->cond);
}

//*************************************************************
//  free_opque - Frees the que container
//*************************************************************

void free_opque(opque_t *q)
{
  teardown_opque(q);
  free(q);  
}

//*************************************************************
//  finalize_opque - Destroys the que container
//     but does NOT free the que memory
//*************************************************************

void finalize_opque(opque_t *q)
{
  teardown_opque(q);

}

//*************************************************************
// add_opque - Adds a task list to the que
//*************************************************************

int add_opque(opque_t *q, oplist_t *iol)
{
  opque_an_t *qan;

  //** Create the oplist callback **
  assert((qan = (opque_an_t *)malloc(sizeof(opque_an_t))) != NULL);
  app_notify_set(&(qan->an), _opque_cb, (void *)qan); //** Set the global callback for the list

  lock_opque(q);

  log_printf(15, "add_opque: q=%d oplist=%d\n", q->id, iol->id);

  //** Add the CB to the oplist
  qan->iol = iol;
  qan->q = q;
  push(q->oplist_an, (void *)qan);       

  q->nleft++;
  move_to_bottom(q->list);
  insert_below(q->list, (void *)iol);

  unlock_opque(q);

  oplist_notify_append(iol, &(qan->an));  //** Lastly append the callback

  return(0);
}

//*************************************************************
// opque_get_failed - returns a failed task list from the provided que
//      or NULL if none exist.
//*************************************************************

oplist_t *opque_get_failed(opque_t *q)
{
  void *iol;

  lock_opque(q);
  iol = pop(q->failed);
  unlock_opque(q);

  return(iol);
}

//*************************************************************
// opque_nfailed- Returns the # of errors left in the 
//    failed que
//*************************************************************

int opque_nfailed(opque_t *q)
{
  int nf;
  lock_opque(q);
  nf = stack_size(q->failed);
  unlock_opque(q);

  return(nf);
}

//*************************************************************
// opque_tasks_left - Returns the number of tasks remaining
//*************************************************************

int opque_tasks_left(opque_t *q)
{
  int n;

  lock_opque(q);
  n = q->nleft;
  unlock_opque(q);

  return(n);
}

//*************************************************************
//  opque_notify_append - Adds a callback to the opque
//*************************************************************

void opque_notify_append(opque_t *q, oplist_app_notify_t *an)
{
  app_notify_append(q->an, an);   
}


//*************************************************************
// opque_waitany - waits until any given task completes and
//   returns the operation.
//*************************************************************

oplist_t *opque_waitany(opque_t *q)
{
  void *opl;

  lock_opque(q);

//  opl = (oplist_t *)pop(q->finished);
//  if ((q->nleft == 0) && (opl == NULL)) { //** Nothing left to do so exit
//     unlock_opque(q);
//     return(NULL);
//  }      

  while (((opl = (oplist_t *)pop(q->finished)) == NULL) && (q->nleft > 0)) {
     apr_thread_cond_wait(q->cond, q->lock); //** Sleep until something completes
  }

  unlock_opque(q);

  return(opl);
}

//*************************************************************
// opque_waitall - waits until all the tasks are completed
//    It returns op_status if all the tasks completed without problems or
//    with the last error otherwise.
//*************************************************************

int opque_waitall(opque_t *q)
{
  oplist_t *opl;

  lock_opque(q);
  while (q->nleft > 0) {
    unlock_opque(q);

    opl = opque_waitany(q);
    
    lock_opque(q);
  }

  return(stack_size(q->failed));
}


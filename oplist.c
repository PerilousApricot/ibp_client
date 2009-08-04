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

#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "oplist.h"

int _oplist_counter = -1;
apr_thread_mutex_t *_oplist_lock = NULL;
apr_pool_t *_oplist_pool = NULL;

//*************************************************************
// init_oplist - Init's the oplist counter and lock
//*************************************************************

void init_oplist_system()
{
  if (_oplist_counter == -1) {   //** Only init if needed
     _oplist_counter = 0;
     assert(apr_pool_create(&_oplist_pool, NULL) == APR_SUCCESS);
     assert(apr_thread_mutex_create(&_oplist_lock, APR_THREAD_MUTEX_DEFAULT, _oplist_pool) == APR_SUCCESS);
  }
}

//*************************************************************
// destroy_oplist - Destroy's the oplist counter and lock
//*************************************************************

void destroy_oplist_system()
{
  if (_oplist_counter >= 0) {    
     apr_thread_mutex_destroy(_oplist_lock);
     apr_pool_destroy(_oplist_pool);
     _oplist_counter = -2;
  }
}

//*************************************************************
// init_oplist - Initializes a task list container
//*************************************************************

void init_oplist(oplist_t *oplist, oplist_implementation_t *imp, oplist_app_notify_t *an)
{
  apr_thread_mutex_lock(_oplist_lock);
  oplist->id = _oplist_counter;
  _oplist_counter++;
  apr_thread_mutex_unlock(_oplist_lock);
  
  apr_thread_mutex_create(&(oplist->lock), APR_THREAD_MUTEX_DEFAULT,_oplist_pool);
  apr_thread_cond_create(&(oplist->cond), _oplist_pool);
  oplist->list = new_stack();
  oplist->finished = new_stack();
  oplist->failed = new_stack();
  oplist->count_id = 0;
  oplist->nleft = 0;
  oplist->started_execution = 0;
  oplist->imp = imp;
  oplist->an = NULL; app_notify_append(oplist->an,  an);
  oplist->free_mode = OPLIST_AUTO_NONE;
  oplist->finished_submission = 0;
}

//*************************************************************
// new_oplist - Generates a new task list
//*************************************************************

oplist_t *new_oplist(oplist_implementation_t *imp, oplist_app_notify_t *an)
{
  oplist_t *iol = (oplist_t *)malloc(sizeof(oplist_t));
  if (iol == NULL) {
     log_printf(0, "new_oplist: malloc failed!\n");
     return(NULL);
  }

  init_oplist(iol, imp, an);

  return(iol);
}

//*************************************************************
// free_oplist_stack - Frees an oplist
//*************************************************************

void free_oplist_stack(oplist_t *oplist, Stack_t *stack, int op_mode)
{
  void *iop;

  iop = pop(stack);
  while (iop != NULL) {
//     log_printf(15, "free_oplist_stack: op=%d op->ref_count=%d\n", iop->id, iop->ref_count);
     if (op_mode == OPLIST_AUTO_FINALIZE) {
        oplist->imp->op_finalize(iop);
     } else if (op_mode == OPLIST_AUTO_FREE) {
        oplist->imp->op_free(iop);    
     }
     iop = pop(stack);
  }

  free_stack(stack, 0);
}

//*************************************************************
//  teardown_oplist - tearsdown the list of I/O operations
//*************************************************************

void teardown_oplist(oplist_t *iolist, int op_mode)
{
  log_printf(15, "teardown_oplist: oplist=%d size(list)=%d size(finished)=%d size(failed)=%d\n",
       iolist->id, stack_size(iolist->list), stack_size(iolist->finished), stack_size(iolist->failed));

  free_oplist_stack(iolist, iolist->list, op_mode);

  free_stack(iolist->finished, 0);
  free_stack(iolist->failed, 0);
  apr_thread_mutex_destroy(iolist->lock);
  apr_thread_cond_destroy(iolist->cond);
}

//*************************************************************
//  free_oplist - Frees the list of I/O operations
//*************************************************************

void free_oplist(oplist_t *iolist)
{
  teardown_oplist(iolist, OPLIST_AUTO_FREE);
  free(iolist);  
}

//*************************************************************
//  finalize_oplist - Destroys the list of I/O operations
//     but does NOT free the iolist memory
//*************************************************************

void finalize_oplist(oplist_t *iolist, int op_mode)
{
  teardown_oplist(iolist, op_mode);

}

//*************************************************************
// add_oplist - Adds an operation to the iolist
//*************************************************************

int add_oplist(oplist_t *iolist, void *iop)
{
  int id;
  oplist_base_op_t *bop = iolist->imp->get_base_op(iop);

  lock_oplist(iolist);
  id = iolist->count_id;
  bop->id = iolist->count_id;
  iolist->count_id++;
  iolist->nleft++;
  bop->status = iolist->imp->blank_status;

  log_printf(15, "add_oplist: oplist=%d op=%d\n", iolist->id, id);
  move_to_bottom(iolist->list);
  insert_below(iolist->list, iop);

  //** Submit it for execution if needed **
  if (iolist->started_execution == 1) {
     iolist->imp->submit_op(iolist, iop);
  }

  unlock_oplist(iolist);

  return(0);
}

//*************************************************************
// oplist_notify_append - Adds a callback to the oplist chain
//*************************************************************

void oplist_notify_append(oplist_t *opl, oplist_app_notify_t *an)
{
  lock_oplist(opl);
  app_notify_append(opl->an, an);
  unlock_oplist(opl);
}


//*************************************************************
// oplist_mark_completed - Marks a task as complete and
//    notify the oplist
//*************************************************************

void oplist_mark_completed(oplist_t *oplist, void *op, int status)
{
  int nleft, finished;
  oplist_base_op_t *bop = oplist->imp->get_base_op(op);

  bop->status = status;
  
  lock_oplist(oplist);  
  oplist->nleft--;
  nleft = oplist->nleft;
  finished = oplist->finished_submission;
  push(oplist->finished, (void *)op);

  if (status != oplist->imp->ok_status) push(oplist->failed, op);

  unlock_oplist(oplist);  

  //** trigger the callbacks -- They should *not* destroy/free the oplist
  app_notify_execute(bop->an);      //** Callback for OP
  app_notify_execute(oplist->an);   //** Callback for oplist  

  if (oplist->imp->notify != NULL) {   //** Callback for implementation
     oplist->imp->notify(oplist, op);
  }

  //** Lastly trigger the signal.  The calling routine may then destroy/free the oplist**
  lock_oplist(oplist);  
  apr_thread_cond_signal(oplist->cond);
  unlock_oplist(oplist);  

  if ((nleft == 0) && (finished == 1)) {  //** clean up
     switch (oplist->free_mode) {
        case OPLIST_AUTO_FINALIZE:
            finalize_oplist(oplist, OPLIST_AUTO_FINALIZE);
            break;
        case OPLIST_AUTO_FREE:
            free_oplist(oplist);
            break;
     }

  }
}


//*************************************************************
// oplist_get_failed_op - returns a failed op from the provided oplist
//      or NULL if none exist.
//*************************************************************

void *oplist_get_failed_op(oplist_t *oplist)
{
  void *op;

  lock_oplist(oplist);
  op = pop(oplist->failed);
  unlock_oplist(oplist);

  return(op);
}

//*************************************************************
// oplist_nfailed- Returns the # of errors left in the 
//    failed que
//*************************************************************

int oplist_nfailed(oplist_t *oplist)
{
  int nf;
  lock_oplist(oplist);
  nf = stack_size(oplist->failed);
  unlock_oplist(oplist);

  return(nf);
}

//*************************************************************
// oplist_tasks_left - Returns the number of tasks remaining
//*************************************************************

int oplist_tasks_left(oplist_t *oplist)
{
  int n;

  lock_oplist(oplist);
  n = oplist->nleft;
  unlock_oplist(oplist);

  return(n);
}


//*************************************************************
// oplist_waitall - waits until all the tasks are completed
//    It returns op_status if all the tasks completed without problems or
//    with the last error otherwise.
//*************************************************************

int oplist_waitall(oplist_t *oplist)
{
  int err = oplist->imp->ok_status;
  void *op;
  oplist_base_op_t *bop;

  lock_oplist(oplist);

  do {   //** This is a do loop cause I always want to scan through the task list for errors
     //** Sleep until there are more tasks
     if (oplist->nleft > 0) apr_thread_cond_wait(oplist->cond, oplist->lock); 

     //** Pop all the finished tasks off the list **
     while ((op = pop(oplist->finished)) != NULL) { 
        bop = oplist->imp->get_base_op(op);
        if (bop->status != oplist->imp->ok_status) err = bop->status;
     }     
  } while (oplist->nleft > 0);

  unlock_oplist(oplist);

  return(err);
}

//*************************************************************
// oplist_waitany - waits until any given task completes and
//   returns the operation.
//*************************************************************

void *oplist_waitany(oplist_t *iolist)
{
  void *op;

  lock_oplist(iolist);

  op = pop(iolist->finished);
  if ((iolist->nleft == 0) && (op == NULL)) { //** Nothing left to do so exit
     unlock_oplist(iolist);
     return(NULL);
  }      

  while ((op = pop(iolist->finished)) == NULL) {
     apr_thread_cond_wait(iolist->cond, iolist->lock); //** Sleep until something completes
  }

  unlock_oplist(iolist);

  return(op);
}


//*************************************************************
// oplist_start_execution - Starts executing a series of tasks
//*************************************************************

void oplist_start_execution(oplist_t *oplist)
{
  int n, i;
  void *op;

  lock_oplist(oplist);

  if (oplist->imp->oplist_sort_tasks != NULL) oplist->imp->oplist_sort_tasks(oplist);

  oplist->started_execution = 1;

  n = stack_size(oplist->list);
  move_to_top(oplist->list);
  for (i=0; i<n; i++) {
     op = get_ele_data(oplist->list);
     oplist->imp->submit_op(oplist, op);

     move_down(oplist->list);
  }

  unlock_oplist(oplist);   
}

//*************************************************************
// oplist_finished_submission - Mark list to stop acceping
//     tasks and free oplist opun completion based on free_mode
//*************************************************************

void oplist_finished_submission(oplist_t *oplist, int free_mode)
{
  int nleft;

  lock_oplist(oplist);
  oplist->finished_submission = 1;
  oplist->free_mode = free_mode;
  nleft = oplist->nleft;
  
  unlock_oplist(oplist);

  if (nleft == 0) {
     switch (free_mode) {
        case OPLIST_AUTO_FINALIZE:
            finalize_oplist(oplist, OPLIST_AUTO_FINALIZE);
            break;
        case OPLIST_AUTO_FREE:
            free_oplist(oplist);
            break;
     }
  }

}

//*************************************************************
//*************************************************************

//*************************************************************
//  oplist_base_op_t and oplist_app_notify_t manipulation functions
//*************************************************************

//*************************************************************
//*************************************************************

void bop_set_status(oplist_base_op_t *bop, int status)
{
  bop->status = status;
}

//*************************************************************

int bop_get_status(oplist_base_op_t *bop)
{
  return(bop->status);
}

//*************************************************************

void bop_set_id(oplist_base_op_t *bop, int id)
{
  bop->id = id;
}

//*************************************************************

int bop_get_id(oplist_base_op_t *bop)
{
  return(bop->id);
}

//*************************************************************

void bop_set_notify(oplist_base_op_t *bop, oplist_app_notify_t *an)
{
  bop->an = an;
}

//*************************************************************

oplist_app_notify_t  *bop_get_notify(oplist_base_op_t *bop)
{
  return(bop->an);
}

//*************************************************************

void app_notify_set(oplist_app_notify_t *an, void (*notify)(void *), void *data)
{
   an->data = data;
   an->notify = notify;
}

//*************************************************************

void app_notify_append(oplist_app_notify_t *root_an, oplist_app_notify_t *an)
{
   if (an == NULL) return;  //** Nothing to add so exit

   if (root_an->tail == NULL) {
      if (root_an->next != NULL) {
         log_printf(0, "app_notify_append: Error root_an->tail == NULL but root_an->next != NULL");
         abort();
      }

      root_an->tail = an;
      root_an->next = an;
   } else {
     root_an->tail->next = an;
     root_an->tail = an;
   }
}

//*************************************************************

void app_notify_single_execute(oplist_app_notify_t *an)
{
  if (an != NULL) {
     if (an->notify != NULL) {
        an->notify(an->data);
     }
  }
}

//*************************************************************

void app_notify_execute(oplist_app_notify_t *an)
{
  oplist_app_notify_t *a = an;

  while (a != NULL) {
     app_notify_single_execute(a);
     a = a->next;
  }
}


//*************************************************************

void bop_init(oplist_base_op_t *bop, int id, int status, oplist_app_notify_t *an)
{
  bop->id = id;
  bop->status = status;
  bop->an = an;
}


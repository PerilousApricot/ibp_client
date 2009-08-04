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

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include "ibp.h"
#include "ibp_misc.h"
#include "host_portal.h"
#include "log.h"

//=============================================================

//*************************************************************
// iovec_sync_thread - Actual routine that performs the I/O
//*************************************************************

void *iovec_sync_thread(apr_thread_t *th, void *data)
{
  oplist_t *oplist = (oplist_t *)data;
  ibp_op_t *op;
  int finished, cmd_count, nbytes;
  ibp_timer_t timer, timer2;
  ibp_capstatus_t astat;
  ibp_capset_t *capset;
  ibp_op_rw_t *rw_op;
  ibp_op_copy_t *copy_op;
  ibp_op_alloc_t *alloc_op;
  ibp_op_probe_t *probe_op;
  ibp_op_depot_modify_t *dm_op;
  ibp_op_depot_inq_t *di_op;
  int err;

  memset(&astat, 0, sizeof(ibp_capstatus_t));

  cmd_count = 0;
  finished = 0;

  while (finished == 0) {
     lock_oplist(oplist);
     op = (ibp_op_t *)get_ele_data(oplist->list);      
     move_down(oplist->list);
     unlock_oplist(oplist);

     if (op == NULL) {
        finished = 1;
     } else {
        cmd_count++;
        timer.ClientTimeout = op->hop.timeout;
        timer.ServerSync = op->hop.timeout;

        switch (op->primary_cmd) {
           case IBP_LOAD:
              rw_op = &(op->rw_op);
              nbytes = IBP_load(rw_op->cap, &timer, rw_op->buf, rw_op->size, rw_op->offset);
              if (nbytes != rw_op->size) {
                 log_printf(0, "iovec_sync_thread: IBP_load error!  nbytes=%d error=%d cap=%s\n", nbytes, IBP_errno, rw_op->cap);
              }
              break;
           case IBP_WRITE:
              rw_op = &(op->rw_op);
              nbytes = IBP_write(rw_op->cap, &timer, rw_op->buf, rw_op->size, rw_op->offset);
              if (nbytes != rw_op->size) {
                 log_printf(0, "iovec_sync_thread: IBP_write error!  nbytes=%d error=%d\n", nbytes, IBP_errno);
              }
              break;
           case IBP_STORE:
              rw_op = &(op->rw_op);
              nbytes = IBP_store(rw_op->cap, &timer, rw_op->buf, rw_op->size);
              if (nbytes != rw_op->size) {
                 log_printf(0, "iovec_sync_thread: IBP_store error!  nbytes=%d error=%d\n", nbytes, IBP_errno);
              }
              break;
           case IBP_SEND:
              copy_op = &(op->copy_op);
              timer2.ClientTimeout = copy_op->dest_client_timeout;
              timer2.ServerSync = copy_op->dest_timeout;

              nbytes = IBP_copy(copy_op->srccap, copy_op->destcap, &timer, &timer2, copy_op->len, copy_op->src_offset);
              if (nbytes != copy_op->len) {
                 log_printf(0, "iovec_sync_thread: IBP_write error!  nbytes=%d error=%d\n", nbytes, IBP_errno);
              }
              break;
           case IBP_ALLOCATE:
              alloc_op = &(op->alloc_op);
              if ((capset = IBP_allocate(alloc_op->depot, &timer, alloc_op->size, alloc_op->attr)) == NULL) {
                 log_printf(0, "iovec_sync_thread: ibp_allocate error! * ibp_errno=%d\n", IBP_errno); 
              } else {
                memcpy(alloc_op->caps, capset, sizeof(ibp_capset_t));
                free(capset);
              }
              break;
           case IBP_MANAGE:
              probe_op = &(op->probe_op);
              err = IBP_manage(probe_op->cap, &timer, op->sub_cmd, IBP_READCAP, &astat);
              if (err != 0) {
                 log_printf(0, "iovec_sync_thread: IBP_manage error!  return=%d error=%d\n", err, IBP_errno);
              }
              break;
           case IBP_STATUS:
              if (op->sub_cmd == IBP_ST_INQ) {
                 dm_op = &(op->depot_modify_op);
                 IBP_status(dm_op->depot, op->sub_cmd, &timer, dm_op->password, 
                         dm_op->max_hard, dm_op->max_soft, dm_op->max_duration);
              } else {
                 di_op = &(op->depot_inq_op);
                 di_op->di = IBP_status(di_op->depot, op->sub_cmd, &timer, di_op->password, 0, 0, 0);
              }
              if (IBP_errno != IBP_OK) {
                 log_printf(0, "iovec_sync_thread: IBP_status error!  error=%d\n", IBP_errno);
              }
           default:
             log_printf(0, "iovec_sync_thread: Unknown command: %d sub_cmd=%d \n", op->primary_cmd, op->sub_cmd);
             IBP_errno = IBP_E_INTERNAL;
        }

       oplist_mark_completed(oplist, op, IBP_errno);
     }

//     log_printf(15, "iovec_sync_thread: cmd_count=%d\n", cmd_count);
  }

  log_printf(1, "iovec_sync_thread: Total commands processed: %d\n", cmd_count);

  apr_thread_exit(th, 0);
  return(NULL);
}

//*************************************************************
// ibp_sync_execute - Handles the sync iovec operations
//*************************************************************

int ibp_sync_execute(oplist_t *oplist, int nthreads)
{
  apr_thread_t *thread[nthreads];
  apr_pool_t *mpool;
  apr_status_t dummy;
  int i;

  log_printf(15, "ibp_sync_execute: Start! ncommands=%d\n", stack_size(oplist->list));
  lock_oplist(oplist);
  sort_oplist(oplist);   //** Sort the work
  move_to_top(oplist->list);
  unlock_oplist(oplist); 

  apr_pool_create(&mpool, NULL);  //** Create the memory pool
    //** launch the threads **
  for (i=0; i<nthreads; i++) {
     apr_thread_create(&(thread[i]), NULL, iovec_sync_thread, (void *)oplist, mpool);
  }

  //** Wait for them to complete **
  for (i=0; i<nthreads; i++) {
     apr_thread_join(&dummy, thread[i]);
  }

  apr_pool_destroy(mpool);  //** Destroy the pool

  if (oplist_nfailed(oplist) == 0) {
     return(IBP_OK);
  } else {
     return(IBP_E_GENERIC);
  }
}


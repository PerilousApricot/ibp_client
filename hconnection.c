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

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include <apr_thread_mutex.h>
#include <apr_thread_cond.h>
#include "host_portal.h"
#include "log.h"
#include "network.h"

//*************************************************************************
// new_host_connection - Allocates space for a new connection
//*************************************************************************

Host_connection_t *new_host_connection(apr_pool_t *mpool)
{
  Host_connection_t *hc;
  assert((hc = (Host_connection_t *)malloc(sizeof(Host_connection_t))) != NULL);

  hc->mpool = mpool;
  apr_thread_mutex_create(&(hc->lock), APR_THREAD_MUTEX_DEFAULT, mpool);
  apr_thread_cond_create(&(hc->send_cond), mpool);
  apr_thread_cond_create(&(hc->recv_cond), mpool);
  hc->pending_stack = new_stack();
  hc->cmd_count = 0;
  hc->curr_workload = 0;
  hc->shutdown_request = 0;
  hc->ns = new_netstream();
  hc->hp = NULL;
  hc->curr_op = NULL;
  hc->last_used = 0;

  return(hc);
}

//*************************************************************************
// destroy_host_connection - Frees space allocated to a depot connection
//*************************************************************************

void destroy_host_connection(Host_connection_t *hc)
{
  destroy_netstream(hc->ns);
  free_stack(hc->pending_stack, 0);
  apr_thread_mutex_destroy(hc->lock);
  apr_thread_cond_destroy(hc->send_cond);
  apr_thread_cond_destroy(hc->recv_cond);
  apr_pool_destroy(hc->mpool);
  free(hc);
}

//*************************************************************************
// close_hc - Closes a depot connection
//*************************************************************************

void close_hc(Host_connection_t *hc)
{
  apr_status_t value;

  //** Trigger the send thread to shutdown which also closes the recv thread
  log_printf(15, "close_hc: Closing ns=%d\n", ns_getid(hc->ns));
  lock_hc(hc);
  hc->shutdown_request = 1;
  unlock_hc(hc);

  //** There are 2 types of waits: 1)empty hportal que 2)local que full
  //**(1) This wakes up everybody on the depot:(
  hportal_lock(hc->hp); hportal_signal(hc->hp); hportal_unlock(hc->hp);
  //**(2) local que is full
  lock_hc(hc); hc_send_signal(hc); unlock_hc(hc);
  hportal_lock(hc->hp); hportal_signal(hc->hp); hportal_unlock(hc->hp);

  //** Wait until the recv thread completes
  apr_thread_join(&value, hc->recv_thread);

  //** Now clean up the closed que being careful not to "join" the hc thread
  Host_connection_t *hc2;

   while ((hc2 = (Host_connection_t *)pop(hc->hp->closed_que)) != NULL) {
     if (hc2 != hc) {
        apr_thread_join(&value, hc2->recv_thread);
        destroy_host_connection(hc2);
     }
   }

  //** finally free the original hc **
  destroy_host_connection(hc);
}

//*************************************************************
// check_workload - Waits until the workload is acceptable
//   before continuing.  It returns the size of the pending stack
//*************************************************************

int check_workload(Host_connection_t *hc)
{
  int psize;

  lock_hc(hc);
  while ((hc->curr_workload > hc->hp->context->max_workload) && (hc->shutdown_request == 0)) {
     log_printf(15, "check_workload: *workload loop* shutdown_request=%d stack_size=%d curr_workload=%d\n", hc->shutdown_request, stack_size(hc->pending_stack), hc->curr_workload); 
     apr_thread_cond_wait(hc->send_cond, hc->lock); 
  }
     
  psize = stack_size(hc->pending_stack);
  unlock_hc(hc);

  return(psize);
}

//*************************************************************
// empty_work_que - Pauses until all pending commands have
//   completed processing.
//*************************************************************

void empty_work_que(Host_connection_t *hc)
{
  lock_hc(hc);
  while (stack_size(hc->pending_stack) != 0) {
      log_printf(15, "empty_work_que: shutdown_request=%d stack_size=%d curr_workload=%d\n", hc->shutdown_request, stack_size(hc->pending_stack), hc->curr_workload);        
      apr_thread_cond_signal(hc->recv_cond);
      apr_thread_cond_wait(hc->send_cond, hc->lock); 
  }
  unlock_hc(hc);
}

//*************************************************************
// empty_hp_que - Empties the work que.  Failing all tasks
//*************************************************************

void empty_hp_que(Host_portal_t *hp, int err_code)
{
  hportal_lock(hp);
  _hp_fail_tasks(hp, err_code);
  hportal_unlock(hp);
}

//*************************************************************
// wait_for_work - Pauses the recv thread until new work is 
//   available or finished.
//*************************************************************

void recv_wait_for_work(Host_connection_t *hc)
{
  lock_hc(hc);
  while ((hc->shutdown_request == 0) && (stack_size(hc->pending_stack) == 0)) {
     hc_send_signal(hc);
     apr_thread_cond_wait(hc->recv_cond, hc->lock);
  }
  unlock_hc(hc);
}

//*************************************************************
// hc_send_thread - Handles the sending phase of a command
//*************************************************************

void *hc_send_thread(apr_thread_t *th, void *data)
{
  Host_connection_t *hc = (Host_connection_t *)data;
  Host_portal_t *hp = hc->hp;  
  NetStream_t *ns = hc->ns;
  Hportal_context_t *hpc = hp->context;
  Hportal_op_t *hop;
  Hportal_stack_op_t *hsop;
  Net_timeout_t dt;
  int finished, psize;
  int dtime;

  //** check if the host is invalid and if so flush the work que
  if (hp->invalid_host == 1) { 
     log_printf(15, "hc_send_thread: Invalid host to host=%s:%d.  Emptying Que\n", hp->host, hp->port);
     empty_hp_que(hp, hpc->imp->hp_invalid_host);
     hc->net_connect_status = 1;
  } else {  //** Make the connection
     set_net_timeout(&dt, 5, 0);
     hc->net_connect_status = hpc->imp->host_connect(ns, hp->connect_context, hp->host, hp->port, dt);
     if (hc->net_connect_status != 0) {
        log_printf(5, "hc_send_thread:  Can't connect to %s:%d!, ns=%d\n", hp->host, hp->port, ns_getid(ns));
     }
  }

  log_printf(15, "hc_send_thread: New connection to host=%s:%d ns=%d\n", hp->host, hp->port, ns_getid(ns));
  

  //** Store my position in the conn_list **
  hportal_lock(hp);
  if (hc->net_connect_status == 0) {
     hp->successful_conn_attempts++;
     hp->failed_conn_attempts = 0;  //** Reset the failed attempts
  } else {
     log_printf(1, "hc_send_thread: ns=%d failing all commands failed_conn_attempts=%d\n", ns_getid(ns), hp->failed_conn_attempts);
     hp->failed_conn_attempts++;
  }
  push(hp->conn_list, (void *)hc);
  hc->my_pos = get_ptr(hp->conn_list);
  hportal_unlock(hp);

  //** Now we start the main loop  
  hsop = NULL; hop = NULL;
  finished = hpc->imp->hp_ok;
  set_net_timeout(&dt, 1, 0);

  if (hc->net_connect_status != 0) finished = hpc->imp->dead_connection;  //** If connect() failed err out
  while (finished == hpc->imp->hp_ok) {
     //** Wait for the load to go down if needed **
     psize = check_workload(hc);

     //** Now get the next command **
     hportal_lock(hp);
     
     hsop = _get_hportal_op(hp);
     if (hsop == NULL) { 
        lock_hc(hc);
        dtime = hpc->min_idle + (hc->last_used - time(NULL));
        unlock_hc(hc);
        if (dtime > 1) dtime = 1;  //** Sometimes the signals don't quite make it
        log_printf(15, "hc_send_thread: No commands so sleeping.. ns=%d time=" TT " max_wait=%d\n", ns_getid(ns), time(NULL), dtime);
        hportal_wait(hp, dtime);    //** Wait for a new task
        hportal_unlock(hp);
     } else { //** Got one so let's process it
        hportal_unlock(hp); 
        hop = hpc->imp->get_hp_op(hsop->op);

        log_printf(15, "hc_send_thread: Processing new command.. ns=%d\n", ns_getid(ns));

        hop->start_time = time(NULL);  //** This is changed in the recv phase also
        hop->end_time = hop->start_time + hop->timeout;
        if (hop->send_command != NULL) finished = hop->send_command(hsop->op, ns);
        if (finished == hpc->imp->hp_ok) {
           lock_hc(hc);
           hc->last_used = time(NULL);  //** Update  the time.  The recv thread does this also
           hc->curr_workload += hop->workload;  //** Inc the current workload
           unlock_hc(hc);

           if (hop->send_phase != NULL) finished = hop->send_phase(hsop->op, ns);

           if (finished == hpc->imp->hp_ok) {
              lock_hc(hc);
              hc->last_used = time(NULL);  //** Update  the time.  The recv thread does this also
              push(hc->pending_stack, (void *)hsop);  //** Push onto recving stack
              hc_recv_signal(hc); //** and notify recv thread
              unlock_hc(hc);
              hsop = NULL;  //** It's on the stack so don't accidentally add it twice if there's a problem
              hop = NULL;
           }
        }
     }

     lock_hc(hc);

     if (stack_size(hc->pending_stack) == 0) {
        dtime = time(NULL) - hc->last_used; //** Exit if not busy
        if (dtime >= hpc->min_idle) {
           hc->shutdown_request = 1;
           log_printf(15, "hc_send_thread: ns=%d min_idle(%d) reached.  Shutting down! dtime=%d\n", 
              ns_getid(ns), hpc->min_idle, dtime);
        }
     }

     if (hc->shutdown_request == 1) finished = hpc->imp->hp_generic_err;
     log_printf(15, "hc_send_thread: ns=%d shutdown=%d stack_size=%d curr_workload=%d time=" TT " last_used=" TT "\n", ns_getid(ns), 
             hc->shutdown_request, stack_size(hc->pending_stack), hc->curr_workload, time(NULL), hc->last_used);
     unlock_hc(hc);
  }

  //** Make sure and trigger the recv if their was a problem **
  lock_hc(hc);
  hc->curr_op = hsop;  //** Make sure the current op doesn't get lost if needed

  log_printf(15, "hc_send_thread: Exiting! (ns=%d, host=%s:%d)\n", ns_getid(ns), hp->host, hp->port);

  hc->shutdown_request = 1;
  apr_thread_cond_signal(hc->recv_cond);
  unlock_hc(hc);

  //*** The recv side handles the removal from the hportal structure ***
  modify_hpc_thread_count(hpc, -1);

  apr_thread_exit(th, 0);
  return(NULL);
}

//*************************************************************
// hc_recv_thread - Handles the recving phase of a command
//*************************************************************

void *hc_recv_thread(apr_thread_t *th, void *data)
{
  Host_connection_t *hc = (Host_connection_t *)data;
  NetStream_t *ns = hc->ns;
  Host_portal_t *hp = hc->hp;  
  Hportal_context_t *hpc = hp->context;
  apr_status_t value;

  int64_t start_cmds_processed, cmds_processed;  
  time_t check_time;
  int finished, status;
  Net_timeout_t dt;  
  Hportal_stack_op_t *hsop;  
  Hportal_op_t *hop;  

  log_printf(15, "hc_recv_thread: New thread started! ns=%d\n", ns_getid(ns));

  //** Get the initial cmd count -- Used at the end to decide if retry
  hportal_lock(hp);
  start_cmds_processed = hp->cmds_processed; 
  hportal_unlock(hp);

  set_net_timeout(&dt, 1, 0);

  finished = 0;
  check_time = time(NULL) + hpc->check_connection_interval;

  while (finished == 0) {
     lock_hc(hc);
     move_to_bottom(hc->pending_stack);//** Get the next recv command
     hsop = (Hportal_stack_op_t *)get_ele_data(hc->pending_stack);
     unlock_hc(hc);

     if (hsop != NULL) {
        hop = hpc->imp->get_hp_op(hsop->op);

        status = hpc->imp->hp_ok;        
        hop->start_time = time(NULL);  //**Start the timer
        hop->end_time = hop->start_time + hop->timeout;

        if (hop->recv_phase != NULL) status = hop->recv_phase(hsop->op, ns);        

        //** dec the current workload
        lock_hc(hc);
        hc->last_used = time(NULL);
        hc->curr_workload -= hop->workload;  
        move_to_bottom(hc->pending_stack);
        delete_current(hc->pending_stack, 1, 0);
        hc_send_signal(hc);  //** Wake up send_thread if needed
        unlock_hc(hc);

        if (status == hpc->imp->hp_retry_dead_socket) {
           log_printf(15, "hc_recv_thread:  Dead socket so shutting down ns=%d\n", ns_getid(ns));
           finished = 1;
        } else if ((status == hpc->imp->hp_timeout) && (hop->retry_count > 0)) {
           hop->retry_count--;
           log_printf(15, "hc_recv_thread: Command timed out.  Retrying.. retry_count=%d  ns=%d\n", hop->retry_count, ns_getid(ns));
           finished = 1;
        } else {
           log_printf(15, "hc_recv_thread:  marking op as completed status=%d retry_count=%d ns=%d\n", status, hop->retry_count, ns_getid(ns));
           oplist_mark_completed(hsop->oplist, hsop->op, status);
           free(hsop);

           //**Update the number of commands processed **
           lock_hc(hc);
           hc->cmd_count++; 
           unlock_hc(hc);

           hportal_lock(hp);
           hp->cmds_processed++; 
           hportal_unlock(hp);
        }
     } else {
       lock_hc(hc);
       finished = hc->shutdown_request;
       unlock_hc(hc);
       if (finished == 0) {
          log_printf(15, "hc_recv_thread: Nothing to do so sleeping! ns=%d\n", ns_getid(ns));
          recv_wait_for_work(hc);  //** wait until we get something to do
       }  
     }

     if (time(NULL) > check_time) {  //** Time for periodic check on # threads
        log_printf(15, "hc_recv_thread: Checking if we need more connections. ns=%d\n", ns_getid(ns));
        check_hportal_connections(hp);
        check_time = time(NULL) + hpc->check_connection_interval;
     }
  }

  log_printf(15, "hc_recv_thread: Exited loop! ns=%d\n", ns_getid(ns));
  log_printf(5, "hc_recv_thread: Total commands processed: %d (ns=%d, host=%s:%d)\n", 
       hc->cmd_count, ns_getid(ns), hp->host, hp->port);

  //** Make sure and trigger the send if their was a problem **
  lock_hc(hc);
//  if (finished != hpc->imp->hp_retry_dead_socket) { 
      hpc->imp->host_close_connection(ns);   //** there was an error so kill things
      hc->curr_workload = 0;
      hc->shutdown_request = 1;
//  }
  unlock_hc(hc);

  //** This wakes up everybody on the depot:( but just my end thread will exit
  hportal_lock(hc->hp); hportal_signal(hc->hp); hportal_unlock(hc->hp);
  //** this just wakes my other half up.
  lock_hc(hc); hc_send_signal(hc); unlock_hc(hc);

  //** Wait for send thread to complete **  
  apr_thread_join(&value, hc->send_thread);

  status = 0;  //** This is used to decide ifthe connection was killed

  //** Push any existing commands to be retried back on the stack **
  if (hc->net_connect_status != 0) {  //** The connection failed
     hportal_lock(hp);
     cmds_processed = start_cmds_processed - hp->cmds_processed; 
     if (cmds_processed == 0) {  //** Nothing was processed
        if (hp->n_conn == 0) {  //** I'm the last thread to try and fail to connect so fail all the tasks
           _hp_fail_tasks(hp, hpc->imp->hp_cant_connect);
        } else if (hp->failed_conn_attempts > hp->abort_conn_attempts) { //** Can't connect so fail
           log_printf(1, "hc_recv_thread: ns=%d failing all commands failed_conn_attempts=%d\n", ns_getid(ns), hp->failed_conn_attempts);
           _hp_fail_tasks(hp, hpc->imp->hp_cant_connect);
        }       
     }
     hportal_unlock(hp);
  } else {
     log_printf(15, "hc_recv_thread: ns=%d stack_size=%d\n", ns_getid(ns), stack_size(hc->pending_stack));

     if (hc->curr_op != NULL) {  //** This is from the sending thread
        log_printf(15, "hc_recv_thread: ns=%d Pushing sending thread task on stack\n", ns_getid(ns));
        submit_hportal(hp, hc->curr_op->oplist, hc->curr_op->op, 1);
        free(hc->curr_op);
        status = 1;
     }
     if (hsop != NULL) {  //** This is my command 
        log_printf(15, "hc_recv_thread: ns=%d Pushing current recving task on stack\n", ns_getid(ns));
        hop = hpc->imp->get_hp_op(hsop->op);
        hop->retry_count--;  //** decr in case this command is a problem
        submit_hportal(hp, hsop->oplist, hsop->op, 1);
        free(hsop);
        status = 1;
     }

     //** and everything else on the pending_stack
     while ((hsop = (Hportal_stack_op_t *)pop(hc->pending_stack)) != NULL) {
        submit_hportal(hp, hsop->oplist, hsop->op, 1);
        free(hsop);
        status = 1;
     }
  }

  //** Now remove myself from the hportal
  hportal_lock(hp);
  hp->n_conn--;
  move_to_ptr(hp->conn_list, hc->my_pos);
  delete_current(hp->conn_list, 1, 0);
  push(hp->closed_que, (void *)hc); //** place myself on the closed que for reaping

  if (status == 1) {  //** My connection was lost so update tuning params
     hp->stable_conn = hp->n_conn;
     if (hp->stable_conn <= 0) hp->stable_conn = 1;
     hp->pause_until = time(NULL) + hpc->wait_stable_time;
  }
  hportal_unlock(hp);

  check_hportal_connections(hp);

  log_printf(15, "hc_recv_thread: Exiting routine! ns=%d\n", ns_getid(ns));

  apr_thread_exit(th, 0);
  
  return(NULL);
}


//*************************************************************
// create_host_connection - Creeats a new depot connection/thread
//*************************************************************

int create_host_connection(Host_portal_t *hp)
{
  Host_connection_t *hc;
  apr_pool_t *pool;

  modify_hpc_thread_count(hp->context, 1);

  apr_pool_create(&pool, NULL);

  hc = new_host_connection(pool);
  hc->hp = hp;
  hc->last_used = time(NULL);
  

  apr_thread_create(&(hc->send_thread), NULL, hc_send_thread, (void *)hc, hc->mpool);
  apr_thread_create(&(hc->recv_thread), NULL, hc_recv_thread, (void *)hc, hc->mpool);

  return(0);
}


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
#include <assert.h>
#include <stdio.h>
#include <apr_thread_proc.h>
#include "dns_cache.h"
#include "host_portal.h"
#include "fmttypes.h"
#include "network.h"
#include "log.h"
#include "string_token.h"

//***************************************************************************
//  hportal_wait - Waits up to the specified time for the condition
//***************************************************************************

void hportal_wait(Host_portal_t *hp, int dt)  
{
   apr_interval_time_t t;

   if (dt < 0) return;   //** If negative time has run out so return

   set_net_timeout(&t, dt, 0); 
   apr_thread_cond_timedwait(hp->cond, hp->lock, t);
}


//***************************************************************************
// get_hpc_thread_count - Returns the current # of running threads
//***************************************************************************

int get_hpc_thread_count(Hportal_context_t *hpc)
{
  int n;

  apr_thread_mutex_lock(hpc->lock);
  n = hpc->running_threads;
  apr_thread_mutex_unlock(hpc->lock);

  return(n);
}

//***************************************************************************
// modify_hpc_thread_count - Modifies the total thread count
//***************************************************************************

void modify_hpc_thread_count(Hportal_context_t *hpc, int n)
{
  apr_thread_mutex_lock(hpc->lock);
  hpc->running_threads = hpc->running_threads + n;
  apr_thread_mutex_unlock(hpc->lock);

}

//************************************************************************
//  create_hportal
//************************************************************************

Host_portal_t *create_hportal(Hportal_context_t *hpc, void *connect_context, char *hostport, int min_conn, int max_conn)
{
  Host_portal_t *hp;

log_printf(15, "create_hportal: hpc=%p\n", hpc);
  assert((hp = (Host_portal_t *)malloc(sizeof(Host_portal_t))) != NULL);
  assert(apr_pool_create(&(hp->mpool), NULL) == APR_SUCCESS);
  
  char host[sizeof(hp->host)];
  int port;
  char *hp2 = strdup(hostport);
  char *bstate;
  int fin;

  host[0] = '\0'; port = 0;

  strncpy(host, string_token(hp2, ":", &bstate, &fin), sizeof(host)-1); host[sizeof(host)-1] = '\0';
  port = atoi(bstate);
  free(hp2);
  log_printf(15, "create_hportal: hostport: %s host=%s port=%d\n", hostport, host, port);

  strncpy(hp->host, host, sizeof(hp->host)-1);  hp->host[sizeof(hp->host)-1] = '\0';

  //** Check if we can resolve the host's IP address
  char in_addr[6];
  if (lookup_host(host, in_addr, NULL) != 0) {
     log_printf(1, "create_hportal: Can\'t resolve host address: %s:%d\n", host, port);
     hp->invalid_host = 1;
  } else {
     hp->invalid_host = 0;
  }    

  hp->port = port;
  snprintf(hp->skey, sizeof(hp->skey), "%s", hostport);
  hp->connect_context = hpc->imp->dup_connect_context(connect_context);

  hp->context = hpc;
  hp->min_conn = min_conn;
  hp->max_conn = max_conn;
  hp->workload = 0;
  hp->cmds_processed = 0;
  hp->n_conn = 0;  
  hp->conn_list = new_stack();
  hp->closed_que = new_stack();
  hp->que = new_stack();
  hp->sync_list = new_stack();
  hp->pause_until = 0;
  hp->stable_conn = hpc->max_threads;
  hp->failed_conn_attempts = 0;
  hp->successful_conn_attempts = 0;
  hp->abort_conn_attempts = hpc->abort_conn_attempts;

  apr_thread_mutex_create(&(hp->lock), APR_THREAD_MUTEX_DEFAULT, hp->mpool);
  apr_thread_cond_create(&(hp->cond), hp->mpool);

  return(hp);
}

//************************************************************************
// _reap_hportal - Frees the closed depot connections
//************************************************************************

void _reap_hportal(Host_portal_t *hp)
{
   Host_connection_t *hc;
   apr_status_t value;

   while ((hc = (Host_connection_t *)pop(hp->closed_que)) != NULL) {
     apr_thread_join(&value, hc->recv_thread);
     destroy_host_connection(hc);
   }
}

//************************************************************************
// destroy_hportal - Destroys a Host_portal data struct
//************************************************************************

void destroy_hportal(Host_portal_t *hp)
{
  _reap_hportal(hp);

  free_stack(hp->conn_list, 1);
  free_stack(hp->que, 1);
  free_stack(hp->closed_que, 1);
  free_stack(hp->sync_list, 1);
  
  hp->context->imp->destroy_connect_context(hp->connect_context);

  apr_thread_mutex_destroy(hp->lock);
  apr_thread_cond_destroy(hp->cond);

  apr_pool_destroy(hp->mpool);  
  log_printf(5, "destroy_hportal: Total commands processed: " I64T " (host:%s:%d)\n", hp->cmds_processed,
         hp->host, hp->port);
  free(hp);
}

//************************************************************************
// lookup_hportal - Looks up a depot/port in the current list
//************************************************************************

Host_portal_t *_lookup_hportal(Hportal_context_t *hpc, char *hostport)
{
  Host_portal_t *hp;
  
//log_printf(1, "_lookup_hportal: hpc=%p hpc->table=%p\n", hpc, hpc->table);
  hp = (Host_portal_t *)(apr_hash_get(hpc->table, hostport, APR_HASH_KEY_STRING));
//log_printf(1, "_lookup_hportal: hpc=%p hpc->table=%p hp=%p hostport=%s\n", hpc, hpc->table, hp, hostport);

  return(hp);
}

//************************************************************************
//  create_hportal_context - Creates a new hportal context structure for use
//************************************************************************

Hportal_context_t *create_hportal_context(Hportal_impl_t *imp)
{
  Hportal_context_t *hpc;

//log_printf(1, "create_hportal_context: start\n");

  assert((hpc = (Hportal_context_t *)malloc(sizeof(Hportal_context_t))) != NULL);
  memset(hpc, 0, sizeof(Hportal_context_t));


  assert(apr_pool_create(&(hpc->pool), NULL) == APR_SUCCESS);
  assert((hpc->table = apr_hash_make(hpc->pool)) != NULL);

//log_printf(15, "create_hportal_context: hpc=%p hpc->table=%p\n", hpc, hpc->table);

  apr_thread_mutex_create(&(hpc->lock), APR_THREAD_MUTEX_DEFAULT, hpc->pool);

  hpc->imp = imp;
  hpc->next_check = time(NULL);
  hpc->count = 0;
  set_net_timeout(&(hpc->dt), 1, 0);

  return(hpc);
}


//************************************************************************
// destroy_hportal_context - Destroys a hportal context structure
//************************************************************************

void destroy_hportal_context(Hportal_context_t *hpc)
{
  apr_hash_index_t *hi; 
  Host_portal_t *hp;
  void *val;

  for (hi=apr_hash_first(hpc->pool, hpc->table); hi != NULL; hi = apr_hash_next(hi)) {
     apr_hash_this(hi, NULL, NULL, &val); hp = (Host_portal_t *)val;  
     apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, NULL);
     destroy_hportal(hp);
  }

  apr_thread_mutex_destroy(hpc->lock);  

  apr_hash_clear(hpc->table);
  apr_pool_destroy(hpc->pool);
  
  free(hpc);

  return;
}

//*************************************************************************
// new_hportal_op - Creates a new hportal operation
//*************************************************************************

Hportal_stack_op_t *new_hportal_op(oplist_t *oplist, void *op)
{
  Hportal_stack_op_t *hpo;

  assert((hpo = (Hportal_stack_op_t *)malloc(sizeof(Hportal_stack_op_t))) != NULL);
  
  hpo->oplist = oplist;
  hpo->op = op;

  return(hpo);
}

//*************************************************************************
// destroy_hportal_op - Destroys a hportal operation
//*************************************************************************

void destroy_hportal_op(Hportal_stack_op_t *hpo)
{
   free(hpo);
}

//************************************************************************
// shutdown_sync - shuts down the sync hportals
//************************************************************************

void shutdown_sync(Host_portal_t *hp)
{
  Host_portal_t *shp;
  Host_connection_t *hc;

  if (stack_size(hp->sync_list) == 0) return;

  move_to_top(hp->sync_list);
  while ((shp = (Host_portal_t *)pop(hp->sync_list)) != NULL) {
     hportal_lock(shp);
     _reap_hportal(shp);  //** Clean up any closed connections

     if ((shp->n_conn == 0) && (stack_size(shp->que) == 0)) { //** if not used so remove it
        delete_current(hp->sync_list, 0, 0);  //**Already closed 
     } else {     //** Force it to close
        free_stack(shp->que, 1);  //** Empty the que so we don't respawn connections
        shp->que = new_stack();

        move_to_top(shp->conn_list);
        hc = (Host_connection_t *)get_ele_data(shp->conn_list);

        hportal_unlock(shp);
        apr_thread_mutex_unlock(hp->context->lock);

        close_hc(hc);

        apr_thread_mutex_lock(hp->context->lock);
        hportal_lock(shp);
     }

     hportal_unlock(shp);
     destroy_hportal(shp);

//     move_to_top(hp->sync_list);
  }
}

//*************************************************************************
// shutdown_hportal - Shuts down the IBP sys system
//*************************************************************************

void shutdown_hportal(Hportal_context_t *hpc)
{
  Host_portal_t *hp;
  Host_connection_t *hc;
  apr_hash_index_t *hi;
  void *val;

  apr_thread_mutex_lock(hpc->lock);

  for (hi=apr_hash_first(hpc->pool, hpc->table); hi != NULL; hi = apr_hash_next(hi)) {
     apr_hash_this(hi, NULL, NULL, &val); hp = (Host_portal_t *)val;  
     apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, NULL);  //** This removes the key

     hportal_lock(hp);
     _reap_hportal(hp);  //** clean up any closed connections

     shutdown_sync(hp);  //** Shutdown any sync connections

     move_to_top(hp->conn_list);
     while ((hc = (Host_connection_t *)get_ele_data(hp->conn_list)) != NULL) {
        free_stack(hp->que, 1);  //** Empty the que so we don't respawn connections
        hp->que = new_stack();
        hportal_unlock(hp);
        apr_thread_mutex_unlock(hpc->lock);

        close_hc(hc);

        apr_thread_mutex_lock(hpc->lock);
        hportal_lock(hp);

        move_to_top(hp->conn_list);
     }     

     hportal_unlock(hp);

     destroy_hportal(hp);
  }

  apr_thread_mutex_unlock(hpc->lock);

  return;  
}

//************************************************************************
// compact_hportal_sync - Compacts the sync hportals if needed
//************************************************************************

void compact_hportal_sync(Host_portal_t *hp)
{
  Host_portal_t *shp;

  if (stack_size(hp->sync_list) == 0) return;

  move_to_top(hp->sync_list);
  while ((shp = (Host_portal_t *)get_ele_data(hp->sync_list)) != NULL) {

     hportal_lock(shp);
     _reap_hportal(shp);  //** Clean up any closed connections

     if ((shp->n_conn == 0) && (stack_size(shp->que) == 0)) { //** if not used so remove it
        delete_current(hp->sync_list, 0, 0);
        hportal_unlock(shp);
        destroy_hportal(shp);
     } else {
       hportal_unlock(shp);      
       move_down(hp->sync_list);
     }
  }


}

//************************************************************************
// compact_hportals - Removes any hportals that are no longer used
//************************************************************************

void compact_hportals(Hportal_context_t *hpc)
{
  apr_hash_index_t *hi;
  Host_portal_t *hp;
  void *val;

  apr_thread_mutex_lock(hpc->lock);

  for (hi=apr_hash_first(hpc->pool, hpc->table); hi != NULL; hi = apr_hash_next(hi)) {
     apr_hash_this(hi, NULL, NULL, &val); hp = (Host_portal_t *)val;  

     hportal_lock(hp);

     _reap_hportal(hp);  //** Clean up any closed connections

     compact_hportal_sync(hp);

     if ((hp->n_conn == 0) && (stack_size(hp->que) == 0) && (stack_size(hp->sync_list) == 0)) { //** if not used so remove it
       hportal_unlock(hp);
       apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, NULL);  //** This removes the key
       destroy_hportal(hp);
     } else {
       hportal_unlock(hp);
     }
  }

  apr_thread_mutex_unlock(hpc->lock);
}

//*************************************************************************
//  _add_hportal_op - Adds a task to a hportal que
//        NOTE:  No locking is performed
//*************************************************************************

void _add_hportal_op(Host_portal_t *hp, oplist_t *oplist, void *op, int addtotop)
{
  Hportal_stack_op_t *hsop = new_hportal_op(oplist, op);
  Hportal_op_t *hop = hp->context->imp->get_hp_op(op);

  hp->workload = hp->workload + hop->workload;

  if (addtotop == 1) {
    push(hp->que, (void *)hsop);
  } else {
    move_to_bottom(hp->que);
    insert_below(hp->que, (void *)hsop);
  };

  hportal_signal(hp);  //** Send a signal for any tasks listening
}

//*************************************************************************
//  _get_hportal_op - Gets the next task for the depot.
//      NOTE:  No locking is done!
//*************************************************************************

Hportal_stack_op_t *_get_hportal_op(Host_portal_t *hp)
{
  Hportal_stack_op_t *hsop = (Hportal_stack_op_t *)pop(hp->que);

  if (hsop != NULL) {
     Hportal_op_t *hop = hp->context->imp->get_hp_op(hsop->op);
     hp->workload = hp->workload - hop->workload;
  }
  return(hsop);
}

//*************************************************************************
// find_hc_to_close - Finds a connection to be close
//*************************************************************************

Host_connection_t *find_hc_to_close(Hportal_context_t *hpc)
{
  apr_hash_index_t *hi;
  Host_portal_t *hp, *shp;
  Host_connection_t *hc, *best_hc, *best_sync;
  void *val;
  int best_workload;
  int oldest_sync_time;

  hc = NULL;
  best_hc = NULL;
  best_workload = 100*hpc->max_workload;
  oldest_sync_time = time(NULL) + 1;
  best_sync = NULL;

  apr_thread_mutex_lock(hpc->lock);

  for (hi=apr_hash_first(hpc->pool, hpc->table); hi != NULL; hi = apr_hash_next(hi)) {
     apr_hash_this(hi, NULL, NULL, &val); hp = (Host_portal_t *)val;  
     apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, NULL);  //** This removes the key

     hportal_lock(hp);

     //** Scan the async connections
     move_to_top(hp->conn_list);
     while ((hc = (Host_connection_t *)get_ele_data(hp->conn_list)) != NULL) {
        lock_hc(hc);
        if (hc->curr_workload < best_workload) {
           best_workload = hc->curr_workload;
           best_hc = hc;
        }    
        move_down(hp->conn_list);
        unlock_hc(hc);
     }     

     //** Scan the sync connections
     move_to_top(hp->sync_list);
     while ((shp = (Host_portal_t *)get_ele_data(hp->sync_list)) != NULL)  {
        hportal_lock(shp);
        if (stack_size(shp->conn_list) > 0) {
           move_to_top(shp->conn_list);
           hc = (Host_connection_t *)get_ele_data(shp->conn_list);
           lock_hc(hc);
           if (oldest_sync_time > hc->last_used) {
              best_sync = hc;
              oldest_sync_time = hc->last_used;              
           }
           unlock_hc(hc);
        }
        hportal_unlock(shp);
        move_down(hp->sync_list);
     }

     hportal_unlock(hp);
  }

  apr_thread_mutex_unlock(hpc->lock);

  hc = best_hc;
  if (best_sync != NULL) {
     if (best_workload > 0) hc = best_sync;
  }
     
  return(hc);  
}


//*************************************************************************
// spawn_new_connection - Creates a new hportal thread/connection
//*************************************************************************

int spawn_new_connection(Host_portal_t *hp)
{
  int n;

  n = get_hpc_thread_count(hp->context);
  if (n > hp->context->max_connections) {
       Host_connection_t *hc = find_hc_to_close(hp->context);
       close_hc(hc);
  }

  return(create_host_connection(hp));
}

//*************************************************************************
// _hp_fail_tasks - Fails all the tasks for a depot.  
//       Only used when a depot is dead
//       NOTE:  No locking is done!
//*************************************************************************

void _hp_fail_tasks(Host_portal_t *hp, int err_code)
{
  Hportal_stack_op_t *hsop;

  hp->workload = 0;  
  while ((hsop = (Hportal_stack_op_t *)pop(hp->que)) != NULL) {
      oplist_mark_completed(hsop->oplist, hsop->op, err_code);
  }
}

//*************************************************************************
// check_hportal_connections - checks if the hportal has the appropriate
//     number of connections and if not spawns them
//*************************************************************************

void check_hportal_connections(Host_portal_t *hp)
{
   int i, total, err;
   int n_newconn = 0;

   hportal_lock(hp);

   //** Now figure out how many new connections are needed, if any
   if (stack_size(hp->que) == 0) {
      n_newconn = 0;
   } else if (hp->n_conn < hp->min_conn) {
       n_newconn = hp->min_conn - hp->n_conn;
   } else {
       n_newconn = hp->workload / hp->context->max_workload;

       if ((hp->n_conn+n_newconn) > hp->max_conn) {
          n_newconn = hp->max_conn - hp->n_conn;
      }
   }

   i = n_newconn;

   total = n_newconn + hp->n_conn;
   if (total > hp->stable_conn) {
      if (time(NULL) > hp->pause_until) {
         hp->stable_conn++;
         if (hp->stable_conn > hp->max_conn) {
            hp->stable_conn = hp->max_conn;
         } else if (hp->stable_conn == 0) {
            hp->stable_conn = 1;
         }
         n_newconn = 1;
         hp->pause_until = time(NULL) + hp->context->wait_stable_time;
      } else {
        n_newconn = hp->stable_conn - hp->n_conn;
      }
   }

   //** Do a check for invalid or down host
   if (hp->invalid_host == 1) {
      if ((hp->n_conn == 0) && (stack_size(hp->que) > 0)) n_newconn = 1;   //** If no connections create one to sink the command
   }

   log_printf(15, "check_hportal_connections: host=%s n_conn=%d workload=" I64T " start_new_conn=%d new_conn=%d stable=%d\n", 
          hp->skey, hp->n_conn, hp->workload, i, n_newconn, hp->stable_conn);

   //** Update the total # of connections after the operation
   //** n_conn is used instead of conn_list to prevent false positives on a dead depot
   hp->n_conn = hp->n_conn + n_newconn;  
                                         
   hportal_unlock(hp);

   //** Spawn the new connections if needed **
   for (i=0; i<n_newconn; i++) {
       err = spawn_new_connection(hp);
//       if (err != 0) return(err);
   }
}

//*************************************************************************
// submit_hportal_sync - Returns an empty hportal for a dedicated
//    sync IBP command *and* submits the command for execution
//*************************************************************************

Host_portal_t *submit_hportal_sync(Hportal_context_t *hpc, oplist_t *oplist, void *op)
{
   Host_portal_t *hp, *shp;
   Host_connection_t *hc;
   Hportal_op_t *hop = hpc->imp->get_hp_op(op);

   apr_thread_mutex_lock(hpc->lock);

   //** Check if we should do a garbage run **
   if (hpc->next_check < time(NULL)) { 
       hpc->next_check = time(NULL) + hpc->compact_interval;

       apr_thread_mutex_unlock(hpc->lock);  
       compact_hportals(hpc);
       apr_thread_mutex_lock(hpc->lock);
   }

   //** Find it in the list or make a new one
   hp = _lookup_hportal(hpc, hop->hostport);
   if (hp == NULL) {
      log_printf(15, "submit_hportal_sync: New host: %s\n", hop->hostport);
      hp = create_hportal(hpc, hop->connect_context, hop->hostport, hpc->min_threads, hpc->max_threads);
      if (hp == NULL) {
          log_printf(15, "submit_hportal_sync: create_hportal failed!\n");
          apr_thread_mutex_unlock(hpc->lock);
          return(NULL);
      }
      apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, (const void *)hp);      
   }

   apr_thread_mutex_unlock(hpc->lock);

   log_printf(15, "submit_hportal_sync: start opid=%d\n", oplist->id);

   //** Scan the sync list for a free connection
   hportal_lock(hp);
   move_to_top(hp->sync_list);
   while ((shp = (Host_portal_t *)get_ele_data(hp->sync_list)) != NULL)  {
      if (hportal_trylock(shp) == 0) {
         log_printf(15, "submit_hportal_sync: opid=%d shp->wl=" I64T " stack_size=%d\n", oplist->id, shp->workload, stack_size(shp->que));

         if (stack_size(shp->que) == 0) {
            if (stack_size(shp->conn_list) > 0) {
               move_to_top(shp->conn_list);
               hc = (Host_connection_t *)get_ele_data(shp->conn_list);
               if (trylock_hc(hc) == 0) {
                  if ((stack_size(hc->pending_stack) == 0) && (hc->curr_workload == 0)) {
                     log_printf(15, "submit_hportal_sync(A): before submit ns=%d opid=%d wl=%d\n",ns_getid(hc->ns), oplist->id, hc->curr_workload);
                     unlock_hc(hc);
                     hportal_unlock(shp);
                     submit_hportal(shp, oplist, op, 1);
                     log_printf(15, "submit_hportal_sync(A): after submit ns=%d opid=%d\n",ns_getid(hc->ns), oplist->id);
                     hportal_unlock(hp);
                     return(shp);
                  }
                  unlock_hc(hc);
               }
            } else {
              hportal_unlock(shp);
              log_printf(15, "submit_hportal_sync(B): opid=%d\n", oplist->id);
              submit_hportal(shp, oplist, op, 1);
              hportal_unlock(hp);
              return(shp);
            }
         }

         hportal_unlock(shp);
      }

      move_down(hp->sync_list);  //** Move to the next hp in the list
   }

   //** If I made it here I have to add a new hportal
   shp = create_hportal(hpc, hop->connect_context, hop->hostport, 1, 1);
   if (shp == NULL) {
      log_printf(15, "submit_hportal_sync: create_hportal failed!\n");
      hportal_unlock(hp);
      return(NULL);
   }
   push(hp->sync_list, (void *)shp);
   submit_hportal(shp, oplist, op, 1);
   
   hportal_unlock(hp);

   return(shp);
}

//*************************************************************************
// submit_hportal - places the op in the hportal's que and also
//     spawns any new connections if needed
//*************************************************************************

int submit_hportal(Host_portal_t *hp, oplist_t *oplist, void *op, int addtotop)
{
   hportal_lock(hp);
   _add_hportal_op(hp, oplist, op, addtotop);  //** Add the task
   hportal_unlock(hp);

   //** Now figure out how many new connections are needed, if any
   check_hportal_connections(hp);

   return(0);
}

//*************************************************************************
// submit_op - submit an IBP task for execution
//*************************************************************************

int submit_hp_op(Hportal_context_t *hpc, oplist_t *oplist, void *op)
{
   Hportal_op_t *hop = hpc->imp->get_hp_op(op);

   apr_thread_mutex_lock(hpc->lock);

   //** Check if we should do a garbage run **
   if (hpc->next_check < time(NULL)) { 
       hpc->next_check = time(NULL) + hpc->compact_interval;

       apr_thread_mutex_unlock(hpc->lock);  
       log_printf(15, "submit_hp_op: Calling compact_hportals\n");
       compact_hportals(hpc);
       apr_thread_mutex_lock(hpc->lock);
   }

//log_printf(1, "submit_hp_op: hpc=%p hpc->table=%p\n",hpc, hpc->table);
   Host_portal_t *hp = _lookup_hportal(hpc, hop->hostport);
   if (hp == NULL) {
      log_printf(15, "submit_op: New host: %s\n", hop->hostport);
      hp = create_hportal(hpc, hop->connect_context, hop->hostport, hpc->min_threads, hpc->max_threads);
      if (hp == NULL) {
          log_printf(15, "submit_op: create_hportal failed!\n");
          return(1);
      }
      log_printf(15, "submit_op: New host.. hp->skey=%s\n", hp->skey);
      apr_hash_set(hpc->table, hp->skey, APR_HASH_KEY_STRING, (const void *)hp);      
Host_portal_t *hp2 = _lookup_hportal(hpc, hop->hostport);
log_printf(15, "submit_op: after lookup hp2=%p\n", hp2);
   }

   apr_thread_mutex_unlock(hpc->lock);

   return(submit_hportal(hp, oplist, op, 0));
}


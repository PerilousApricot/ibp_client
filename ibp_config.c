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
#include <apr_pools.h>
#include <apr_thread_proc.h>
#include "iniparse.h"
#include "dns_cache.h"
#include "host_portal.h"
#include "ibp.h"
#include "ibp_misc.h"
#include "network.h"
#include "net_sock.h"
#include "net_phoebus.h"
#include "net_1_ssl.h"
#include "net_2_ssl.h"
#include "log.h"
#include "phoebus.h"

extern apr_thread_once_t *_err_once;

apr_pool_t *_ibp_mpool = NULL;
Hportal_context_t *_hpc_config;
ibp_config_t global_ibp_config;
ibp_config_t *_ibp_config;

oplist_base_op_t *_get_ibp_base_op(void *);
Hportal_op_t *_get_ibp_hp_op(void *);
void *_ibp_dup_connect_context(void *connect_context);
void _ibp_destroy_connect_context(void *connect_context);
int _ibp_connect(NetStream_t *ns, void *connect_context, char *host, int port, Net_timeout_t timeout);

Hportal_impl_t _ibp_imp = { IBP_OK, ERR_RETRY_DEADSOCKET, IBP_E_CLIENT_TIMEOUT, IBP_E_GENERIC, IBP_E_CONNECTION,
    IBP_E_INVALID_HOST, IBP_E_CANT_CONNECT, 
    _get_ibp_base_op,
    _get_ibp_hp_op,
    _ibp_dup_connect_context,
    _ibp_destroy_connect_context,
    _ibp_connect,
    close_netstream };


//********************************************************************
// _get_ibp_base_op - Returns the oplist base structure
//********************************************************************

oplist_base_op_t *_get_ibp_base_op(void *gop)
{
  ibp_op_t *op = (ibp_op_t *)gop;

  return(&(op->bop));
}

//********************************************************************
// _get_ibp_base_op - Returns the oplist base structure
//********************************************************************

Hportal_op_t *_get_ibp_hp_op(void *gop)
{
  ibp_op_t *op = (ibp_op_t *)gop;

  return(&(op->hop));
}

//********************************************************************
// _ibp_dup_connect_context - Copies an IBP connect_context structure
//********************************************************************

void *_ibp_dup_connect_context(void *connect_context)
{
  ibp_connect_context_t *cc = (ibp_connect_context_t *)connect_context;
  ibp_connect_context_t *ccdup;

  if (cc == NULL) return(NULL);

  ccdup = (ibp_connect_context_t *)malloc(sizeof(ibp_connect_context_t));
  assert(ccdup != NULL);

  *ccdup = *cc;

  return((void *)ccdup);
}

//********************************************************************
// _ibp_destroy_connect_context - Frees/Destroys a IBP connect_context structure
//********************************************************************

void _ibp_destroy_connect_context(void *connect_context)
{
  if (connect_context == NULL) return;

  free(connect_context);

  return;
}


//**********************************************************
// _ibp_connect - Makes an IBP connection to a remote host
//     If connect_context == NULL then a standard socket based 
//     connection is made.
//**********************************************************

int _ibp_connect(NetStream_t *ns, void *connect_context, char *host, int port, Net_timeout_t timeout)
{
  ibp_connect_context_t *cc = (ibp_connect_context_t *)connect_context;

  if (cc != NULL) {
     switch(cc->type) {
       case NS_TYPE_SOCK:
          ns_config_sock(ns, _ibp_config->tcpsize);
          break;
       case NS_TYPE_PHOEBUS:
          ns_config_phoebus(ns, cc->data, _ibp_config->tcpsize);
          break;
       case NS_TYPE_1_SSL:
          ns_config_1_ssl(ns, -1, _ibp_config->tcpsize);
          break;
       case NS_TYPE_2_SSL:
//****          ns_config_2_ssl(ns, -1);
          break;
       default:
          log_printf(0, "_ibp__connect: Invalid type=%d Exiting!\n", cc->type);
          return(1);
      }
  } else {
     ns_config_sock(ns, _ibp_config->tcpsize);
  }

  return(net_connect(ns, host, port, timeout));
}


//**********************************************************
// set/unset routines for options
//**********************************************************

void ibp_set_abort_attempts(int n) { _ibp_config->abort_conn_attempts = n;};
int  ibp_get_abort_attempts() { return(_ibp_config->abort_conn_attempts); };
void ibp_set_tcpsize(int n) { _ibp_config->tcpsize = n;};
int  ibp_get_tcpsize() { return(_ibp_config->tcpsize); };
void ibp_set_min_depot_threads(int n) { _ibp_config->min_threads = n; _hpc_config->min_threads = n;};
int  ibp_get_min_depot_threads() { return(_ibp_config->min_threads); };
void ibp_set_max_depot_threads(int n) { _ibp_config->max_threads = n; _hpc_config->max_threads = n;};
int  ibp_get_max_depot_threads() { return(_ibp_config->max_threads); };
void ibp_set_max_connections(int n) { _ibp_config->max_connections = n; _hpc_config->max_connections = n;};
int  ibp_get_max_connections() { return(_ibp_config->max_connections); };
void ibp_set_command_weight(int n) { _ibp_config->new_command = n; };
int  ibp_get_command_weight() { return(_ibp_config->new_command); };
void ibp_set_max_thread_workload(int n) { _ibp_config->max_workload = n; _hpc_config->max_workload = n;};
int  ibp_get_max_thread_workload() { return(_ibp_config->max_workload); };
void ibp_set_wait_stable_time(int n) { _ibp_config->wait_stable_time = n; _hpc_config->wait_stable_time = n;};
int  ibp_get_wait_stable_time() { return(_ibp_config->wait_stable_time); };
void ibp_set_check_interval(int n) { _ibp_config->check_connection_interval = n; _hpc_config->check_connection_interval = n;};
int  ibp_get_check_interval() { return(_ibp_config->check_connection_interval); };
void ibp_set_max_retry(int n) { _ibp_config->max_retry = n; _hpc_config->max_retry = n;};
int  ibp_get_max_retry() { return(_ibp_config->max_retry); };

//**********************************************************
// set_ibp_config - Sets the ibp config options
//**********************************************************

void set_ibp_config(ibp_config_t *cfg)
{
  if (_ibp_config != cfg) *_ibp_config = *cfg;

  _hpc_config->min_idle = cfg->min_idle;
  _hpc_config->min_threads = cfg->min_threads;
  _hpc_config->max_threads = cfg->max_threads;
  _hpc_config->max_connections = cfg->max_connections;
  _hpc_config->max_workload = cfg->max_workload;
  _hpc_config->wait_stable_time = cfg->wait_stable_time;
  _hpc_config->abort_conn_attempts = cfg->abort_conn_attempts;
  _hpc_config->check_connection_interval = cfg->check_connection_interval;
  _hpc_config->max_retry = cfg->max_retry;
}

//**********************************************************
// cc_load - Stores a CC from the given keyfile
//**********************************************************

void cc_load(inip_file_t *kf, char *name, ibp_connect_context_t *cc)
{
  char *type = inip_get_string(kf, "ibp_connect", name, NULL);

  if (type == NULL) return;
  
  if (strcmp(type, "socket") == 0) {
     cc->type = NS_TYPE_SOCK;
  } else if (strcmp(type, "phoebus") == 0) {
     cc->type = NS_TYPE_PHOEBUS;
  } else if (strcmp(type, "ssl1") == 0) {
     cc->type = NS_TYPE_1_SSL;
  } else if (strcmp(type, "ssl2") == 0) {
     cc->type = NS_TYPE_2_SSL;
  } else {
     log_printf(0, "cc_load: Invalid CC type! command: %s type: %s\n", name, type);
  }
}

//**********************************************************
// ibp_cc_table_load - Loads the default connect_context for commands
//**********************************************************

void ibp_cc_load(inip_file_t *kf, ibp_config_t *cfg)
{
  int i;
  ibp_connect_context_t cc;

  //** Set everything to the default **
  cc.type = NS_TYPE_SOCK;
  cc_load(kf, "default", &cc);
  for (i=0; i<=IBP_MAX_NUM_CMDS; i++) cfg->cc[i] = cc;
  
  //** Now load the individual commands if they exist
  cc_load(kf, "ibp_allocate", &(cfg->cc[IBP_ALLOCATE]));
  cc_load(kf, "ibp_store", &(cfg->cc[IBP_STORE]));
  cc_load(kf, "ibp_status", &(cfg->cc[IBP_STATUS]));
  cc_load(kf, "ibp_send", &(cfg->cc[IBP_SEND]));
  cc_load(kf, "ibp_load", &(cfg->cc[IBP_LOAD]));
  cc_load(kf, "ibp_manage", &(cfg->cc[IBP_MANAGE]));
  cc_load(kf, "ibp_write", &(cfg->cc[IBP_WRITE]));
  cc_load(kf, "ibp_alias_allocate", &(cfg->cc[IBP_ALIAS_ALLOCATE]));
  cc_load(kf, "ibp_alias_manage", &(cfg->cc[IBP_ALIAS_MANAGE]));
  cc_load(kf, "ibp_rename", &(cfg->cc[IBP_RENAME]));
  cc_load(kf, "ibp_phoebus_send", &(cfg->cc[IBP_PHOEBUS_SEND]));
}


//**********************************************************
// ibp_load_config - Loads the ibp client config
//**********************************************************

int ibp_load_config(char *fname)
{
  inip_file_t *keyfile;

  //* Load the config file
  keyfile = inip_read(fname);
  if (keyfile == NULL) {
    log_printf(0, "ibp_load_config:  Error parsing config file! file=%s\n", fname);
    return(-1);
  }

  _ibp_config->abort_conn_attempts = inip_get_integer(keyfile, "ibp_async", "abort_attempts", _ibp_config->abort_conn_attempts);
  _ibp_config->tcpsize = inip_get_integer(keyfile, "ibp_async", "tcpsize", _ibp_config->tcpsize);
  _ibp_config->min_threads = inip_get_integer(keyfile, "ibp_async", "min_depot_threads", _ibp_config->min_threads);
  _ibp_config->max_threads = inip_get_integer(keyfile, "ibp_async", "max_depot_threads", _ibp_config->max_threads);
  _ibp_config->max_connections = inip_get_integer(keyfile, "ibp_async", "max_connections", _ibp_config->max_connections);
  _ibp_config->new_command = inip_get_integer(keyfile, "ibp_async", "command_weight", _ibp_config->new_command);
  _ibp_config->max_workload = inip_get_integer(keyfile, "ibp_async", "max_thread_workload", _ibp_config->max_workload);
  _ibp_config->wait_stable_time = inip_get_integer(keyfile, "ibp_async", "wait_stable_time", _ibp_config->wait_stable_time);
  _ibp_config->check_connection_interval = inip_get_integer(keyfile, "ibp_async", "check_interval", _ibp_config->check_connection_interval);
  _ibp_config->max_retry = inip_get_integer(keyfile, "ibp_async", "max_retry", _ibp_config->max_retry);

  ibp_cc_load(keyfile, _ibp_config);

  phoebus_load_config(keyfile);
  
  inip_destroy(keyfile);   //Free the keyfile context

  set_ibp_config(_ibp_config);

  return(0);
}

//**********************************************************
// default_ibp_config - Sets the default ibp config options
//**********************************************************

void default_ibp_config()
{
  int i;

  _ibp_config->tcpsize = 0;
  _ibp_config->min_idle = 30;
  _ibp_config->min_threads = 1;
  _ibp_config->max_threads = 4;
  _ibp_config->max_connections = 128;
  _ibp_config->new_command = 10*1024;
  _ibp_config->max_workload = 10*1024*1024;
  _ibp_config->wait_stable_time = 15;
  _ibp_config->abort_conn_attempts = 4;
  _ibp_config->check_connection_interval = 2;
  _ibp_config->max_retry = 2;

  for (i=0; i<=IBP_MAX_NUM_CMDS; i++) {
     _ibp_config->cc[i].type = NS_TYPE_SOCK;
  }

  phoebus_init();

  set_ibp_config(_ibp_config);
}


//**********************************************************
//  ibp_init - Initialize the IBP routines for use
//**********************************************************

void ibp_init()
{
  _ibp_config = &global_ibp_config;

  assert(apr_initialize() == APR_SUCCESS);

  dns_cache_init(100);

  ibp_configure_signals();

  _hpc_config = create_hportal_context(&_ibp_imp);
  default_ibp_config();  

  apr_pool_create(&(_ibp_mpool), NULL);
  apr_thread_once_init(&_err_once, _ibp_mpool);

  init_oplist_system();
}

//**********************************************************
//  ibp_finalize - Shuts down the IBP subsystem
//**********************************************************

void ibp_finalize()
{
  shutdown_hportal(_hpc_config);
  destroy_hportal_context(_hpc_config);

  finalize_dns_cache();

  destroy_oplist_system();

  phoebus_destroy();

  apr_pool_destroy(_ibp_mpool);

  apr_terminate();
}



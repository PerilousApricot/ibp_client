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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "ibp.h"
#include "fmttypes.h"
#include "network.h"
#include "log.h"
#include "ibp_misc.h"
#include "dns_cache.h"

Net_timeout_t global_dt = {1, 0};
int write_block(NetStream_t *ns, time_t end_time, char *buffer, int size);

//*************************************************************
// set_hostport - Sets the hostport string
//   This needs to be changed for each type of NS connection
//*************************************************************

void set_hostport(char *hostport, int max_size, char *host, int port, ibp_connect_context_t *cc)
{
  char in_addr[6];
  char ip[64];
  int type;

  type = (cc == NULL) ? NS_TYPE_SOCK : cc->type;

  if (lookup_host(host, in_addr) != 0) {
     log_printf(1, "set_hostport:  Failed to lookup host: %s\n", host);
     hostport[max_size-1] = '\0';
     snprintf(hostport, max_size-1, "%s:%d:%d:0", host, port, type);   
     return;
  }

  inet_ntop(AF_INET, (void *)in_addr, ip, 63);
  ip[63] = '\0';

  hostport[max_size-1] = '\0';
  if (type == NS_TYPE_PHOEBUS) {
     snprintf(hostport, max_size-1, "%s:%d:%d:%u", ip, port, type, phoebus_get_key((phoebus_t *)cc->data));
  } else {
     snprintf(hostport, max_size-1, "%s:%d:%d:0", ip, port, type);
  }

  log_printf(15, "set_hostport: host=%s hostport=%s\n", host, hostport);
}

//*************************************************************
// send_command - Sends a text string.  USed for sending IBP commands 
//*************************************************************

int send_command(NetStream_t *ns, char *command)
{
  Net_timeout_t dt;
  set_net_timeout(&dt, 5, 0);

  log_printf(15, "send_command: ns=%d command=%s\n", ns_getid(ns), command);

  int len = strlen(command);
  time_t t = time(NULL) + 5;  //** Should be fixed with an actual time!
  int n = write_block(ns, t, command, len);
//  int n = write_netstream(ns, command, len, dt);
//  if (n !=  len) {
  if (n !=  IBP_OK) {
     log_printf(10, "send_command: Error=%d! ns=%d command=!%s!\n", n, ns_getid(ns), command); 
     return(ERR_RETRY_DEADSOCKET);
  }

  return(IBP_OK);  
}

//*************************************************************
// readline_with_timeout - Reads a line of text with a 
//    command timeout
//*************************************************************

int readline_with_timeout(NetStream_t *ns, char *buffer, int size, time_t end_time)
{
  int nbytes, n;
  int err;

  nbytes = 0;
  err = 0;
  while ((err == 0) && (time(NULL) <= end_time)){
     n = readline_netstream_raw(ns, &(buffer[nbytes]), size, global_dt, &err);
     nbytes = nbytes + n;
     log_printf(15, "readline_with_timeout: nbytes=%d err=%d time=" TT " end_time=" TT " ns=%d\n", nbytes, err, time(NULL), end_time, ns_getid(ns));
  }

  if (err > 0) {
     err = IBP_OK;
  } else {
//     close_netstream(ns);    //** Either the connection is dead or there is a problem
     if (err == 0) {
        log_printf(15, "readline_with_timeout: Client timeout time=" TT " end_time=" TT "ns=%d\n", time(NULL), end_time, ns_getid(ns));
        err = IBP_E_CLIENT_TIMEOUT;
     } else {
        log_printf(15, "readline_with_timeout: connection error=%d ns=%d\n", err, ns_getid(ns));
//        err = IBP_E_CONNECTION;
        err = ERR_RETRY_DEADSOCKET;
     }
  } 

  return(err);
}

//*************************************************************
// ibp_op_status - Returns the status of the operation
//*************************************************************

int ibp_op_status(ibp_op_t *op)
{
  return(bop_get_status(&(op->bop)));
}

//*************************************************************
// ibp_op_id - Returns the operations id
//*************************************************************

int ibp_op_id(ibp_op_t *op)
{
  return(bop_get_id(&(op->bop)));
}

//*************************************************************
//  finalize_ibp_op - Frees an I/O operation.  Does not free
//        op structure itself!
//*************************************************************

void finalize_ibp_op(ibp_op_t *iop)
{
  if (iop->hop.destroy_command != NULL) iop->hop.destroy_command(iop);

  free(iop->hop.hostport);
}

//*************************************************************
//  free_ibp_op - Frees an I/O operation
//*************************************************************

void free_ibp_op(ibp_op_t *iop)
{
   finalize_ibp_op(iop);
   free(iop);
}

//*************************************************************
// new_ibp_op - Allocates space for a new op
//*************************************************************

ibp_op_t *new_ibp_op()
{
  return((ibp_op_t *)malloc(sizeof(ibp_op_t)));
}

//*************************************************************
// init_ibp_base_op - initializes  generic op variables
//*************************************************************

void init_ibp_base_op(ibp_op_t *op, char *logstr, int timeout, int workload, char *hostport, 
     int cmp_size, int primary_cmd, int sub_cmd, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  bop_init(&(op->bop), -1, IBP_E_GENERIC, an);

  bop_set_notify(&(op->bop), an);
  op->primary_cmd = primary_cmd;
  op->sub_cmd = sub_cmd;
  op->hop.timeout = timeout;
  op->hop.retry_count = _ibp_config->max_retry;
  op->hop.workload = workload;
  op->hop.hostport = hostport;
  op->hop.cmp_size = cmp_size;
  op->hop.send_command = NULL;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = NULL;
  op->hop.destroy_command = NULL;

  if (cc == NULL) {
    op->hop.connect_context = &(_ibp_config->cc[primary_cmd]);
  } else {
    op->hop.connect_context = cc;
  }

}

//*************************************************************
// new_ibp_base_op - Generates a new operation and initializes
//    generic op variables
//*************************************************************

ibp_op_t *new_ibp_base_op(char *logstr, int timeout, int workload, char *hostport, int cmp_size, 
    int primary_cmd, int sub_cmd, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) {
     log_printf(0, "new_ibp_%s_op: malloc failed!\n", logstr);
     return(NULL);
  }

  init_ibp_base_op(op, logstr, timeout, workload, hostport, cmp_size, primary_cmd, sub_cmd, an, cc);

  return(op);
}

//*************************************************************
//  default_next_block - Default routine for retreiving R/W data
//*************************************************************

int default_next_block(int pos, void *arg, int *nbytes, char **buffer)
{
   ibp_op_rw_t *cmd = (ibp_op_rw_t *)arg;

   *nbytes = cmd->size; 
   if (buffer != NULL) *buffer = cmd->buf;

   return(IBP_OK);
}

//=============================================================
//=============================================================

//=============================================================
//  Read routines
//=============================================================

//*************************************************************
// set_ibp_user_read_op - Generates a new read operation using
//     a user provided buffering scheme
//*************************************************************

void set_ibp_user_read_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size, 
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   set_ibp_rw_op(op, IBP_READ, cap, offset, size, next_block, arg, timeout, an, cc);
}


//*************************************************************
// new_ibp_read_op - Generates a new read operation
//*************************************************************

ibp_op_t *new_ibp_user_read_op(ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   ibp_op_t *op = new_ibp_rw_op(IBP_READ, cap, offset, size, next_block, arg, timeout, an, cc);

   return(op);
}

//*************************************************************
// set_ibp_read_op - Generates a new read operation
//*************************************************************

void set_ibp_read_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   set_ibp_rw_op(op, IBP_READ, cap, offset, size, default_next_block, (void *)&(op->rw_op), timeout, an, cc);
   op->rw_op.buf = buffer;
}

//*************************************************************
// new_ibp_read_op - Generates a new read operation
//*************************************************************

ibp_op_t *new_ibp_read_op(ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   ibp_op_t *op = new_ibp_rw_op(IBP_READ, cap, offset, size, default_next_block, NULL, timeout, an, cc);
   op->rw_op.buf = buffer;
   op->rw_op.arg = (void *)&(op->rw_op);

   return(op);
}

//*************************************************************

int read_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_rw_t *cmd;

  cmd = &(op->rw_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %d\n", 
     IBPv040, IBP_LOAD, cmd->key, cmd->typekey, cmd->offset, cmd->size, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "read_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int read_block(NetStream_t *ns, time_t end_time, char *buffer, int size)
{
  int nbytes, pos, nleft, err;

  pos = 0;
  nleft = size;
  err = IBP_OK;
  while ((nleft > 0) && (err == IBP_OK) && (time(NULL) <= end_time)) {
     nbytes = read_netstream(ns, &(buffer[pos]), nleft, global_dt);
     log_printf(15, "read_block: ns=%d size=%d nleft=%d nbytes=%d pos=%d time=" TT "\n", ns_getid(ns), 
         size, nleft, nbytes, pos, time(NULL)); 

     if (nbytes > 0) {
        pos = pos + nbytes;
        nleft = nleft - nbytes;
     } else if (nbytes < 0) {
        log_printf(0, "read_block: (read) ns=%d len=%d Error!  closed connection!\n", 
           ns_getid(ns), size);
        err = ERR_RETRY_DEADSOCKET;
     }
  }

  if ((nleft > 0) && (time(NULL) > end_time)) {
     log_printf(0, "read_block: (read) ns=%d len=%d Error!  client timeout!\n", 
         ns_getid(ns), size);
     err = IBP_E_CLIENT_TIMEOUT;
//     close_netstream(ns);
  }

  return(err);
}

//*************************************************************

int read_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int nbytes, status, pos, nleft, err;
  char buffer[1024];
  char *bstate, *rbuf;
  ibp_op_rw_t *cmd;

  cmd = &(op->rw_op);
  
  //** Need to read the depot status info
  log_printf(15, "read_recv: ns=%d starting command\n", ns_getid(ns));

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "read_recv: after readline err = %d  ns=%d buffer=%s\n", err, ns_getid(ns), buffer);

  status = IBP_E_GENERIC;
  status = atoi(string_token(buffer, " ", &bstate, &err));
  nbytes = atol(string_token(NULL, " ", &bstate, &err));
  if ((status != IBP_OK) || (nbytes != cmd->size)) {
     log_printf(15, "read_recv: (read) ns=%d cap=%s offset=%d len=%d err=%d Error!  status/nbytes=!%s!\n", 
          ns_getid(ns), cmd->cap, cmd->offset, cmd->size, err, buffer);
     return(status);
  }


//  return(read_block(ns, op->hop.end_time, cmd->buf, cmd->size));
//-----------

//  cmd->counter = 0;
  pos = 0;
  nleft = cmd->size;
  err = IBP_OK;
  while ((nleft > 0) && (err == IBP_OK) && (time(NULL) <= op->hop.end_time)) {
     cmd->next_block(pos, cmd->arg, &nbytes, &rbuf);

     err = read_block(ns, op->hop.end_time, rbuf, nbytes);

     log_printf(15, "read_recv: ns=%d size=%d nleft=%d nbytes=%d pos=%d time=" TT "\n", ns_getid(ns), 
         cmd->size, nleft, nbytes, pos, time(NULL)); 

     if (err == IBP_OK) {
        pos = pos + nbytes;
        nleft = nleft - nbytes;
     }
  }

  if ((nleft > 0) && (time(NULL) > op->hop.end_time)) {
     log_printf(0, "read_recv: (read) ns=%d cap=%s offset=%d len=%d Error!  client timeout!\n", 
         ns_getid(ns), cmd->cap, cmd->offset, cmd->size);
     err = IBP_E_CLIENT_TIMEOUT;
  }

  if (err == IBP_OK) {  //** Call the next block routine to process the last chunk
     cmd->next_block(pos, cmd->arg, &nbytes, NULL);
  }

  return(err);
}

//=============================================================
//  Write routines
//=============================================================

//*************************************************************
// set_ibp_user_write_op - Generates a new write operation using
//     a user supplied callback function
//*************************************************************

void set_ibp_user_write_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)

{
   set_ibp_rw_op(op, IBP_WRITE, cap, offset, size, next_block, arg, timeout, an, cc);
}

//*************************************************************
// new_ibp_user_write_op - Creates/Generates a new write operation
//     useing a user supplied callback function
//*************************************************************

ibp_op_t *new_ibp_user_write_op(ibp_cap_t *cap, int offset, int size,
       int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   ibp_op_t *op = new_ibp_rw_op(IBP_WRITE, cap, offset, size, next_block, arg, timeout, an, cc);

   return(op);
}

//*************************************************************
// set_ibp_write_op - Generates a new write operation
//*************************************************************

void set_ibp_write_op(ibp_op_t *op, ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   set_ibp_rw_op(op, IBP_WRITE, cap, offset, size, default_next_block, (void *)&(op->rw_op), timeout, an, cc);
   op->rw_op.buf = buffer;
}

//*************************************************************
// new_ibp_write_op - Creates/Generates a new write operation
//*************************************************************

ibp_op_t *new_ibp_write_op(ibp_cap_t *cap, int offset, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   ibp_op_t *op = new_ibp_rw_op(IBP_WRITE, cap, offset, size, default_next_block, NULL, timeout, an, cc);
   op->rw_op.buf = buffer;
   op->rw_op.arg = (void *)&(op->rw_op);

   return(op);
}

//*************************************************************

int write_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_rw_t *cmd;

  cmd = &(op->rw_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %d\n", 
       IBPv040, IBP_WRITE, cmd->key, cmd->typekey, cmd->offset, cmd->size, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "read_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int write_block(NetStream_t *ns, time_t end_time, char *buffer, int size)
{
  int pos, nleft, nbytes, err;

  pos = 0;
  nleft = size;
  nbytes = -100;
  err = IBP_OK;
  while ((nleft > 0) && (err == IBP_OK)) {
     nbytes = write_netstream(ns, &(buffer[pos]), nleft, global_dt);
     log_printf(15, "write_block: ns=%d size=%d nleft=%d nbytes=%d pos=%d time=" TT "\n", ns_getid(ns), size, nleft, 
             nbytes, pos, time(NULL));

     if (time(NULL) > end_time) {
        log_printf(15, "write_send: ns=%d Command timed out! to=" TT " ct=" TT " \n", ns_getid(ns), end_time, time(NULL));
        err = IBP_E_CLIENT_TIMEOUT;
     }

     if (nbytes < 0) {
        err = ERR_RETRY_DEADSOCKET;   //** Error with write
     } else if (nbytes > 0) {   //** Normal write
        pos = pos + nbytes;
        nleft = size - pos;
        err = IBP_OK;
     }
  }

  log_printf(15, "write_block: END ns=%d size=%d nleft=%d nbytes=%d pos=%d\n", ns_getid(ns), size, nleft, nbytes, pos);

  return(err);
}

//*************************************************************

int write_send(void *gop, NetStream_t *ns)
{
  ibp_op_t *iop = (ibp_op_t *)gop;
  int pos, nleft, nbytes, err, block_error;
  ibp_op_rw_t *cmd = &(iop->rw_op);
  char *buffer;

//  return(write_block(ns, iop->hop.end_time, cmd->buf, cmd->size));

  pos = 0;
  nleft = cmd->size;
  nbytes = -100;
  err = IBP_OK;
  block_error = 0;
  while ((nleft > 0) && (err == IBP_OK)) {
     cmd->next_block(pos, cmd->arg, &nbytes, &buffer);
     if (nbytes > nleft) {
        log_printf(0, "write_send: ns=%d next_block returned too much data!  nbytes=%d max=%d\n", ns_getid(ns), nbytes, nleft);
        nbytes = nleft;
        block_error = 1;
     }

     log_printf(15, "write_send: ns=%d size=%d nleft=%d nbytes=%d pos=%d time=" TT "\n", ns_getid(ns), cmd->size, nleft, 
             nbytes, pos, time(NULL));
     err = write_block(ns, iop->hop.end_time, buffer, nbytes);
     pos = pos + nbytes;
     nleft = cmd->size - pos;
  }

  log_printf(15, "write_send: END ns=%d size=%d nleft=%d nbytes=%d pos=%d\n", ns_getid(ns), cmd->size, nleft, nbytes, pos);

  if (block_error == 1) err = IBP_E_INTERNAL;
  return(err);
}

//*************************************************************

int write_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err, status, nbytes;
  ibp_op_rw_t *cmd;
  char *bstate;

  log_printf(15, "write_recv: Start!!! ns=%d\n", ns_getid(ns));

  cmd = &(op->rw_op);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  status = -1;
  status = atoi(buffer);
  err = status;
  if (status != IBP_OK) {
    log_printf(15, "write_recv: ns=%d cap=%s offset=%d len=%d Error!  status=%s\n", 
       ns_getid(ns), cmd->cap, cmd->offset, cmd->size, buffer);
  } else {
    err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
    if (err == IBP_OK) {
      log_printf(15, "write_recv: ns=%d cap=%s offset=%d len=%d status/nbytes=%s\n", 
             ns_getid(ns), cmd->cap, cmd->offset, cmd->size, buffer);
       status = -1; nbytes = -1;
       status = atoi(string_token(buffer, " ", &bstate, &err));
       nbytes = atol(string_token(NULL, " ", &bstate, &err));
//       sscanf(buffer, "%d %d\n", &status, &nbytes);
       if ((nbytes != cmd->size) || (status != IBP_OK)) {
          log_printf(15, "write_recv: ns=%d cap=%s offset=%d len=%d Error!  status/nbytes=%s\n", 
             ns_getid(ns), cmd->cap, cmd->offset, cmd->size, buffer);
       }

       err = status;
    } else {
       log_printf(15, "write_recv: ns=%d cap=%s offset=%d len=%d Error with readline!  buffer=%s\n", 
          ns_getid(ns), cmd->cap, cmd->offset, cmd->size, buffer);
        return(err);
    }
  }

  return(err);
}

//=============================================================
//  IBP append routines
//=============================================================

//*************************************************************

int append_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_rw_t *cmd;

  cmd = &(op->rw_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d\n", 
       IBPv040, IBP_STORE, cmd->key, cmd->typekey, cmd->size, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "append_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}


//*************************************************************
// new_ibp_write_op - Creates/Generates a new write operation
//*************************************************************

ibp_op_t *new_ibp_append_op(ibp_cap_t *cap, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   ibp_op_t *op = new_ibp_rw_op(IBP_STORE, cap, 0, size, default_next_block, NULL, timeout, an, cc);
   if (op == NULL) return(NULL);
   op->hop.send_command = append_command;
   op->rw_op.buf = buffer;
   op->rw_op.arg = (void *)&(op->rw_op);
   return(op);
}

//*************************************************************
// set_ibp_append_op - Generates a new write operation
//*************************************************************

void set_ibp_append_op(ibp_op_t *op, ibp_cap_t *cap, int size, char *buffer, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
   //** Dirty way to fill in the fields
   set_ibp_rw_op(op, IBP_STORE, cap, 0, size, default_next_block, (void *)&(op->rw_op), timeout, an, cc); 

   op->rw_op.buf = buffer;
   op->hop.send_command = append_command;
   op->hop.send_phase = write_send;
   op->hop.recv_phase = write_recv;
}

//=============================================================
//=============================================================

//*************************************************************
// set_ibp_rw_op - Generates a new IO operation
//*************************************************************

void set_ibp_rw_op(ibp_op_t *op, int rw_type, ibp_cap_t *cap, int offset, int size, 
     int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_rw_t *cmd;

  init_ibp_base_op(op, "rw", timeout, _ibp_config->new_command + size, NULL, size, rw_type, IBP_NOP, an, cc);
  
  cmd = &(op->rw_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[rw_type]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->cap = cap;
  cmd->size = size;
  cmd->offset = offset;
  cmd->next_block = next_block;
  cmd->arg = arg;

  if (rw_type == IBP_WRITE) { 
     op->hop.send_command = write_command;
     op->hop.send_phase = write_send;
     op->hop.recv_phase = write_recv;
  } else {
     op->hop.send_command = read_command;
     op->hop.send_phase = NULL;
     op->hop.recv_phase = read_recv;
  }

}

//*************************************************************
// new_ibp_rw_op - Creates/Generates a new IO operation
//*************************************************************

ibp_op_t *new_ibp_rw_op(int rw_type, ibp_cap_t *cap, int offset, int size,
     int (*next_block)(int, void *, int *, char **), void *arg, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_rw_op(op, rw_type, cap, offset, size, next_block, arg, timeout, an, cc);

  return(op);
}

//=============================================================
//  Allocate routines
//=============================================================

//*************************************************************

int allocate_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  time_t atime;
  int err;
  ibp_op_alloc_t *cmd = &(op->alloc_op);

  atime = cmd->attr->duration - time(NULL);
  snprintf(buffer, sizeof(buffer), "%d %d %d %d %d " TT " %d %d\n", 
       IBPv040, IBP_ALLOCATE, cmd->depot->rid, cmd->attr->reliability, cmd->attr->type, 
       atime, cmd->size, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "allocate_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int allocate_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int status, fin;
  char buffer[1025];
  char rcap[1025], wcap[1025], mcap[1025];
  char *bstate;
  int err;
  ibp_op_alloc_t *cmd = &(op->alloc_op);

  //** Need to read the depot status info
  log_printf(15, "allocate_recv: ns=%d Start\n", ns_getid(ns));


  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "allocate_recv: after readline ns=%d buffer=%s\n", ns_getid(ns), buffer);

  sscanf(string_token(buffer, " ", &bstate, &fin), "%d", &status);
  if (status != IBP_OK) {
     log_printf(0, "alloc_recv: ns=%d Error! status=%d buffer=%s\n", ns_getid(ns), status, buffer);
     return(status);
  }        

  rcap[0] = '\0';
  wcap[0] = '\0';
  mcap[0] = '\0';
  strncpy(rcap, string_token(NULL, " ", &bstate, &fin), sizeof(rcap)-1); rcap[sizeof(rcap)-1] = '\0';
  strncpy(wcap, string_token(NULL, " ", &bstate, &fin), sizeof(rcap)-1); wcap[sizeof(wcap)-1] = '\0';
  strncpy(mcap, string_token(NULL, " ", &bstate, &fin), sizeof(rcap)-1); mcap[sizeof(mcap)-1] = '\0';
  
  if ((strlen(rcap) == 0) || (strlen(wcap) == 0) || (strlen(mcap) == 0)) {
     log_printf(0, "alloc_recv: ns=%d Error reading caps!  buffer=%s\n", ns_getid(ns), buffer);
     if (sscanf(buffer, "%d", &status) != 1) {
        log_printf(0, "alloc_recv: ns=%d Can't read status!\n", ns_getid(ns));
        return(IBP_E_GENERIC);
     } else {
        return(status);
     }
  }        

  cmd->caps->readCap = strdup(rcap);
  cmd->caps->writeCap = strdup(wcap);
  cmd->caps->manageCap = strdup(mcap);

  log_printf(15, "alloc_recv: ns=%d rcap=%s wcap=%s mcap=%s\n", ns_getid(ns), 
       cmd->caps->readCap, cmd->caps->writeCap, cmd->caps->manageCap);

  return(IBP_OK);
}

//*************************************************************
//  set_ibp_alloc_op - generates a new IBP_ALLOC operation
//*************************************************************

void set_ibp_alloc_op(ibp_op_t *op, ibp_capset_t *caps, int size, ibp_depot_t *depot, 
       ibp_attributes_t *attr, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  ibp_op_alloc_t *cmd;

log_printf(15, "set_ibp_alloc_op: start. _hpc_config=%p\n", _hpc_config);

  if (cc==NULL) cc = &(_ibp_config->cc[IBP_ALLOCATE]);
  set_hostport(hoststr, sizeof(hoststr), depot->host, depot->port, cc);

log_printf(15, "set_ibp_alloc_op: before init_ibp_base_op\n");

  init_ibp_base_op(op, "alloc", timeout, 10*_ibp_config->new_command, strdup(hoststr), 1, IBP_ALLOCATE, IBP_NOP, an, cc);
log_printf(15, "set_ibp_alloc_op: after init_ibp_base_op\n");

  cmd = &(op->alloc_op);
  cmd->caps = caps;
  cmd->depot = depot;
  cmd->attr = attr;
  cmd->size = size;

  op->hop.send_command = allocate_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = allocate_recv;
}

//*************************************************************
//  new_ibp_alloc_op - Creates a new IBP_ALLOC operation
//*************************************************************

ibp_op_t *new_ibp_alloc_op(ibp_capset_t *caps, int size, ibp_depot_t *depot, ibp_attributes_t *attr, 
       int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  
  set_ibp_alloc_op(op, caps, size, depot, attr, timeout, an, cc);

  return(op);
}

//=============================================================
//  Rename routines
//=============================================================

//*************************************************************

int rename_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_alloc_t *cmd = &(op->alloc_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d\n", 
       IBPv040, IBP_RENAME, cmd->key, cmd->typekey, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "rename_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************
//  set_ibp_rename_op - generates a new IBP_RENAME operation
//*************************************************************

void set_ibp_rename_op(ibp_op_t *op, ibp_capset_t *caps, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  char host[256];
  ibp_op_alloc_t *cmd;
  int port;

log_printf(15, "set_ibp_rename_op: start. _hpc_config=%p\n", _hpc_config);

  init_ibp_base_op(op, "rename", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_RENAME, IBP_NOP, an, cc);

  cmd = &(op->alloc_op);

  parse_cap(mcap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_RENAME]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->caps = caps;

  op->hop.send_command = rename_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = allocate_recv;
}

//*************************************************************
//  new_ibp_rename_op - Creates a new IBP_RENAME operation
//*************************************************************

ibp_op_t *new_ibp_rename_op(ibp_capset_t *caps, ibp_cap_t *mcap,  
       int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  
  set_ibp_rename_op(op, caps, mcap, timeout, an, cc);

  return(op);
}

//=============================================================
//  Proxy Allocate routines
//=============================================================

//*************************************************************

int proxy_allocate_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_alloc_t *cmd = &(op->alloc_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %d %d\n", 
       IBPv040, IBP_PROXY_ALLOCATE, cmd->key, cmd->typekey, cmd->offset, cmd->size, cmd->duration, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "proxy_allocate_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************
//  set_ibp_proxy_alloc_op - generates a new IBP_PROXY_ALLOC operation
//*************************************************************

void set_ibp_proxy_alloc_op(ibp_op_t *op, ibp_capset_t *caps, ibp_cap_t *mcap, int offset, int size, 
   int duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  char host[256];
  ibp_op_alloc_t *cmd;
  int port;

  log_printf(15, "set_ibp_proxy_alloc_op: start. _hpc_config=%p\n", _hpc_config);

  init_ibp_base_op(op, "rename", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_PROXY_ALLOCATE, IBP_NOP, an, cc);

  cmd = &(op->alloc_op);

  parse_cap(mcap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_PROXY_ALLOCATE]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->offset = offset;
  cmd->size = size;
  if (duration == 0) {
     cmd->duration = 0;
  } else {
    cmd->duration = duration - time(NULL);
  }


  cmd->caps = caps;

  op->hop.send_command = proxy_allocate_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = allocate_recv;
}

//*************************************************************
//  new_ibp_proxy_alloc_op - Creates a new IBP_PROXY_ALLOC operation
//*************************************************************

ibp_op_t *new_ibp_proxy_alloc_op(ibp_capset_t *caps, ibp_cap_t *mcap, int offset, int size, 
   int duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  
  set_ibp_proxy_alloc_op(op, caps, mcap, offset, size, duration, timeout, an, cc);

  return(op);
}


//=============================================================
//  modify_count routines
//=============================================================

//*************************************************************

int modify_count_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_probe_t *cmd;

  cmd = &(op->probe_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %d\n", 
       IBPv040, cmd->cmd, cmd->key, cmd->typekey, cmd->mode, cmd->captype, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "modify_count_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int proxy_modify_count_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_probe_t *cmd;

  cmd = &(op->probe_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %s %s %d\n", 
       IBPv040, cmd->cmd, cmd->key, cmd->typekey, cmd->mode, cmd->captype, cmd->mkey, cmd->mtypekey, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "modify_count_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int status_get_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int status;
  char buffer[1025];
  int err;

  //** Need to read the depot status info
  log_printf(15, "status_get_recv: ns=%d Start", ns->id);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "status_get_recv: after readline ns=%d buffer=%s\n", ns_getid(ns), buffer);

  if (sscanf(buffer, "%d", &status) != 1) {
     log_printf(10, "status_get_recv: ns=%d Error reading status!  buffer=%s\n", ns_getid(ns), buffer);
     return(IBP_E_GENERIC);
  }        

  return(status);
}

//*************************************************************
//  set_ibp_generic_modify_count_op - Generates an operation to modify 
//     an allocations reference count
//*************************************************************

void set_ibp_generic_modify_count_op(int command, ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_probe_t *cmd;

  if ((command != IBP_MANAGE) && (command != IBP_PROXY_MANAGE)) {
     log_printf(0, "set_ibp_generic_modify_count_op: Invalid command! should be IBP_MANAGE or IBP_PROXY_MANAGE.  Got %d\n", command);
     return;
  }  
  if ((mode != IBP_INCR) && (mode != IBP_DECR)) {
     log_printf(0, "new_ibp_modify_count_op: Invalid mode! should be IBP_INCR or IBP_DECR\n");
     return;
  }
  if ((captype != IBP_WRITECAP) && (captype != IBP_READCAP)) {
     log_printf(0, "new_ibp_modify_count_op: Invalid captype! should be IBP_READCAP or IBP_WRITECAP\n");
     return;
  }

  init_ibp_base_op(op, "modify_count", timeout, 10*_ibp_config->new_command, NULL, 1, command, mode, an, cc);

  cmd = &(op->probe_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[command]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  if (command == IBP_PROXY_MANAGE) parse_cap(mcap, host, &port, cmd->mkey, cmd->mtypekey);

  cmd->cmd = command;
  cmd->cap = cap;
  cmd->mode = mode;
  cmd->captype = captype;

  op->hop.send_command = modify_count_command;
  if (command == IBP_PROXY_MANAGE) op->hop.send_command = proxy_modify_count_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = status_get_recv;
}

//*************************************************************
//  *_ibp_modify_count_op - Generates an operation to modify 
//     an allocations reference count
//*************************************************************

void set_ibp_modify_count_op(ibp_op_t *op, ibp_cap_t *cap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  set_ibp_generic_modify_count_op(IBP_MANAGE, op, cap, NULL, mode, captype, timeout, an, cc);
}

//***************************

ibp_op_t *new_ibp_modify_count_op(ibp_cap_t *cap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_generic_modify_count_op(IBP_MANAGE, op, cap, NULL, mode, captype, timeout, an, cc);

  return(op);
}

//*************************************************************
//  *_ibp_proxy_modify_count_op - Generates an operation to modify 
//     a PROXY allocations reference count
//*************************************************************

void set_ibp_proxy_modify_count_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  set_ibp_generic_modify_count_op(IBP_PROXY_MANAGE, op, cap, mcap, mode, captype, timeout, an, cc);
}

//***************************

ibp_op_t *new_ibp_proxy_modify_count_op(ibp_cap_t *cap, ibp_cap_t *mcap, int mode, int captype, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_generic_modify_count_op(IBP_PROXY_MANAGE, op, cap, mcap, mode, captype, timeout, an, cc);

  return(op);
}

//=============================================================
// Modify allocation routines
//=============================================================

//*************************************************************

int modify_alloc_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  time_t atime;
  ibp_op_modify_alloc_t *cmd;

  cmd = &(op->mod_alloc_op);

  atime = cmd->duration - time(NULL);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d " ST " " TT " %d %d\n", 
       IBPv040, IBP_MANAGE, cmd->key, cmd->typekey, IBP_CHNG, IBP_MANAGECAP, cmd->size, atime, 
       cmd->reliability, op->hop.timeout);

//  log_printf(0, "modify_alloc_command: buffer=!%s!\n", buffer);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "modify_count_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************
// set_ibp_modify_alloc_op - Modifes the size, duration, and 
//   reliability of an existing allocation.
//*************************************************************

void set_ibp_modify_alloc_op(ibp_op_t *op, ibp_cap_t *cap, size_t size, time_t duration, int reliability, 
     int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_modify_alloc_t *cmd;
  
  init_ibp_base_op(op, "modify_alloc", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_MANAGE, IBP_CHNG, an, cc);

  cmd = &(op->mod_alloc_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_MANAGE]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->cap = cap;
  cmd->size = size;
  cmd->duration = duration;
  cmd->reliability = reliability;

  op->hop.send_command = modify_alloc_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = status_get_recv;
  
}

//*************************************************************

ibp_op_t *new_ibp_modify_alloc_op(ibp_cap_t *cap, size_t size, time_t duration, int reliability, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_modify_alloc_op(op, cap, size, duration, reliability, timeout, an, cc);

  return(op);
}

//=============================================================
// Proxy Modify allocation routines
//=============================================================

//*************************************************************

int proxy_modify_alloc_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  time_t atime;
  ibp_op_modify_alloc_t *cmd;

  cmd = &(op->mod_alloc_op);

  atime = cmd->duration - time(NULL);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d " ST " " ST " " TT " %s %s %d\n", 
       IBPv040, IBP_PROXY_MANAGE, cmd->key, cmd->typekey, IBP_CHNG, cmd->offset,  cmd->size, atime, 
       cmd->mkey, cmd->mtypekey, op->hop.timeout);

//  log_printf(0, "proxy_modify_alloc_command: buffer=!%s!\n", buffer);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "proxy_modify_count_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************
// set_ibp_proxy_modify_alloc_op - Modifes the size, duration, and 
//   reliability of an existing allocation.
//*************************************************************

void set_ibp_proxy_modify_alloc_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, size_t offset, size_t size, time_t duration, 
     int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_modify_alloc_t *cmd;
  
  init_ibp_base_op(op, "proxy_modify_alloc", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_PROXY_MANAGE, IBP_CHNG, an, cc);

  cmd = &(op->mod_alloc_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_PROXY_MANAGE]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  parse_cap(mcap, host, &port, cmd->mkey, cmd->mtypekey);

  cmd->cap = cap;
  cmd->offset = offset;
  cmd->size = size;
  cmd->duration = duration;

  op->hop.send_command = proxy_modify_alloc_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = status_get_recv;
  
}

//*************************************************************

ibp_op_t *new_ibp_proxy_modify_alloc_op(ibp_cap_t *cap, ibp_cap_t *mcap, size_t offset, size_t size, time_t duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_proxy_modify_alloc_op(op, cap, mcap, offset, size, duration, timeout, an, cc);

  return(op);
}

//=============================================================
//  Remove routines
//=============================================================

//*************************************************************
//  set_ibp_remove_op - Generates a remove allocation operation
//*************************************************************

void set_ibp_remove_op(ibp_op_t *op, ibp_cap_t *cap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  set_ibp_modify_count_op(op, cap, IBP_DECR, IBP_READCAP, timeout, an, cc);
}

//*************************************************************
//  new_ibp_remove_op - Generates/Creates a remove allocation operation
//*************************************************************

ibp_op_t *new_ibp_remove_op(ibp_cap_t *cap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  return(new_ibp_modify_count_op(cap, IBP_DECR, IBP_READCAP, timeout, an, cc));
}

//*************************************************************
//  set_ibp_proxy_remove_op - Generates a remove proxy allocation operation
//*************************************************************

void set_ibp_proxy_remove_op(ibp_op_t *op, ibp_cap_t *cap, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  set_ibp_proxy_modify_count_op(op, cap, mcap, IBP_DECR, IBP_READCAP, timeout, an, cc);
}

//*************************************************************
//  new_ibp_proxy_remove_op - Generates/Creates a remove proxy allocation operation
//*************************************************************

ibp_op_t *new_ibp_proxy_remove_op(ibp_cap_t *cap, ibp_cap_t *mcap, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  return(new_ibp_proxy_modify_count_op(cap, mcap, IBP_DECR, IBP_READCAP, timeout, an, cc));
}

//=============================================================
//  IBP_PROBE routines for IBP_MANAGE
//=============================================================

//*************************************************************

int probe_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_probe_t *cmd;

  cmd = &(op->probe_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d 0 0 0 %d \n", 
       IBPv040, IBP_MANAGE, cmd->key, cmd->typekey, IBP_PROBE, IBP_MANAGECAP, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "probe_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int probe_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int status;
  char buffer[1025];
  int err;
  char *bstate;
  ibp_capstatus_t *p;

  //** Need to read the depot status info
  log_printf(15, "probe_recv: ns=%d Start", ns->id);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "probe_recv: after readline ns=%d buffer=%s\n", ns_getid(ns), buffer);

  status = atoi(string_token(buffer, " ", &bstate, &err));
  if ((status == IBP_OK) && (err == 0)) {
     p = op->probe_op.probe;
     p->readRefCount = atoi(string_token(NULL, " ", &bstate, &err));
     p->writeRefCount = atoi(string_token(NULL, " ", &bstate, &err));
     p->currentSize = atol(string_token(NULL, " ", &bstate, &err));
     p->maxSize = atol(string_token(NULL, " ", &bstate, &err));
     p->attrib.duration = atol(string_token(NULL, " ", &bstate, &err)) + time(NULL);
     p->attrib.reliability = atoi(string_token(NULL, " ", &bstate, &err));
     p->attrib.type = atoi(string_token(NULL, " ", &bstate, &err));
  }

  return(status);
}

//*************************************************************
//  set_ibp_probe_op - Generates a new IBP_PROBE command to get
//     information about an existing allocation
//*************************************************************

void set_ibp_probe_op(ibp_op_t *op, ibp_cap_t *cap, ibp_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_probe_t *cmd;
  
  init_ibp_base_op(op, "probe", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_MANAGE, IBP_PROBE, an, cc);

  cmd = &(op->probe_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_MANAGE]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->cap = cap;
  cmd->probe = probe;

  op->hop.send_command = probe_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = probe_recv;
}

//*************************************************************
//  new_ibp_probe_op - Creats and generates  a new IBP_PROBE 
//     command to get information about an existing allocation
//*************************************************************

ibp_op_t *new_ibp_probe_op(ibp_cap_t *cap, ibp_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_probe_op(op, cap, probe, timeout, an, cc);

  return(op);
}

//=============================================================
//  IBP_PROBE routines of IBP_PROXY_MANAGE
//=============================================================

//*************************************************************

int proxy_probe_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_probe_t *cmd;

  cmd = &(op->probe_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %d %d %d \n", 
       IBPv040, IBP_PROXY_MANAGE, cmd->key, cmd->typekey, IBP_PROBE, IBP_MANAGECAP, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "proxy_probe_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int proxy_probe_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int status;
  char buffer[1025];
  int err;
  char *bstate;
  ibp_proxy_capstatus_t *p;

  //** Need to read the depot status info
  log_printf(15, "proxy_probe_recv: ns=%d Start", ns->id);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "proxy_probe_recv: after readline ns=%d buffer=%s\n", ns_getid(ns), buffer);

  status = atoi(string_token(buffer, " ", &bstate, &err));
  if ((status == IBP_OK) && (err == 0)) {
     p = op->probe_op.proxy_probe;
     p->read_refcount = atoi(string_token(NULL, " ", &bstate, &err));
     p->write_refcount = atoi(string_token(NULL, " ", &bstate, &err));
     p->offset = atol(string_token(NULL, " ", &bstate, &err));
     p->size = atol(string_token(NULL, " ", &bstate, &err));
     p->duration = atol(string_token(NULL, " ", &bstate, &err)) + time(NULL);
  }

  return(status);
}

//*************************************************************
//  set_ibp_proxy_probe_op - Generates a new IBP_PROBE command to get
//     information about an existing PROXY allocation
//*************************************************************

void set_ibp_proxy_probe_op(ibp_op_t *op, ibp_cap_t *cap, ibp_proxy_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_probe_t *cmd;
  
  init_ibp_base_op(op, "proxy_probe", timeout, 10*_ibp_config->new_command, NULL, 1, IBP_PROXY_MANAGE, IBP_PROBE, an, cc);

  cmd = &(op->probe_op);

  parse_cap(cap, host, &port, cmd->key, cmd->typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_PROXY_MANAGE]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->cap = cap;
  cmd->proxy_probe = probe;

  op->hop.send_command = proxy_probe_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = proxy_probe_recv;
}

//*************************************************************
//  new_ibp_proxy_probe_op - Creats and generates  a new IBP_PROBE 
//     command to get information about an existing PROXY allocation
//*************************************************************

ibp_op_t *new_ibp_proxy_probe_op(ibp_cap_t *cap, ibp_proxy_capstatus_t *probe, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();

  set_ibp_proxy_probe_op(op, cap, probe, timeout, an, cc);

  return(op);
}


//=============================================================
// IBP_copyappend routines
//    These routines allow you to copy an allocation between
//    depots.  The offset is only specified for the src cap.
//    The data is *appended* to the dest cap.
//=============================================================

//*************************************************************

int copyappend_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_copy_t *cmd;

  cmd = &(op->copy_op);

  snprintf(buffer, sizeof(buffer), "%d %d %s %s %s %s %d %d %d %d %d\n", 
       IBPv040, cmd->ibp_command, cmd->path, cmd->src_key, cmd->destcap, cmd->src_typekey, cmd->src_offset, cmd->len, 
       op->hop.timeout, cmd->dest_timeout, cmd->dest_client_timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "copyappend_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int copy_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  int status;
  char buffer[1025];
  int err, nbytes;
  char *bstate;
  ibp_op_copy_t *cmd;

  cmd = &(op->copy_op);

  //** Need to read the depot status info
  log_printf(15, "copy_recv: ns=%d Start", ns->id);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "copy_recv: after readline ns=%d buffer=%s\n", ns_getid(ns), buffer);

  status = atoi(string_token(buffer, " ", &bstate, &err));
  nbytes = atol(string_token(NULL, " ", &bstate, &err));
  if ((status != IBP_OK) || (nbytes != cmd->len)) {
     log_printf(0, "copy_recv: (read) ns=%d srccap=%s destcap=%s offset=%d len=%d err=%d Error!  status/nbytes=!%s!\n", 
          ns_getid(ns), cmd->srccap, cmd->destcap, cmd->src_offset, cmd->len, err, buffer);
  }

  return(status);
}


//*************************************************************
// set_ibp_copyappend_op - Generates a new depot copy operation
//*************************************************************

void set_ibp_copyappend_op(ibp_op_t *op, int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap, int src_offset, int size, 
        int src_timeout, int  dest_timeout, int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  int port;
  char host[256];
  ibp_op_copy_t *cmd;

  init_ibp_base_op(op, "copyappend", src_timeout, _ibp_config->new_command + size, NULL, size, IBP_SEND, IBP_NOP, an, cc);
  
  cmd = &(op->copy_op);
  
  parse_cap(srccap, host, &port, cmd->src_key, cmd->src_typekey);
  if (cc==NULL) cc = &(_ibp_config->cc[IBP_SEND]);
  set_hostport(hoststr, sizeof(hoststr), host, port, cc);
  op->hop.hostport = strdup(hoststr);

  cmd->ibp_command = IBP_SEND;
  if (ns_type == NS_TYPE_PHOEBUS) { 
     cmd->ibp_command = IBP_PHOEBUS_SEND;
     cmd->path = path;
     if (cmd->path == NULL) cmd->path = "auto";  //** If NULL default to auto
  } else {    //** All other ns types don't use the path
     cmd->path = "\0";
  }

  cmd->srccap = srccap;
  cmd->destcap = destcap;
  cmd->len = size;
  cmd->src_offset = src_offset;
  cmd->dest_timeout = dest_timeout;
  cmd->dest_client_timeout = dest_client_timeout;

  op->hop.send_command = copyappend_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = copy_recv;
}

//*************************************************************

ibp_op_t *new_ibp_copyappend_op(int ns_type, char *path, ibp_cap_t *srccap, ibp_cap_t *destcap, int src_offset, int size, 
        int src_timeout, int  dest_timeout, int dest_client_timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_copyappend_op(op, ns_type, path, srccap, destcap, src_offset, size, src_timeout, dest_timeout, dest_client_timeout, an, cc);

  return(op);
}

//=============================================================
//  routines to handle modifying a depot's resources
//=============================================================

int depot_modify_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_depot_modify_t *cmd;

  cmd = &(op->depot_modify_op);

  snprintf(buffer, sizeof(buffer), "%d %d %d %d %s %d\n " ST " " ST " " TT "\n", 
       IBPv040, IBP_STATUS, cmd->depot->rid, IBP_ST_CHANGE, cmd->password, op->hop.timeout,
       cmd->max_hard, cmd->max_soft, cmd->max_duration);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "modify_depot_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************
//  set_ibp_depot_modify - Modify the settings of a depot/RID
//*************************************************************

void set_ibp_depot_modify_op(ibp_op_t *op, ibp_depot_t *depot, char *password, size_t hard, size_t soft, 
      time_t duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_depot_modify_t *cmd = &(op->depot_modify_op);

  init_ibp_base_op(op, "depot_modify", timeout, _ibp_config->new_command, NULL, 
         _ibp_config->new_command, IBP_STATUS, IBP_ST_CHANGE, an, cc);
  
  cmd->depot = depot;
  cmd->password = password;
  cmd->max_hard = hard;
  cmd->max_soft = soft;
  cmd->max_duration = duration;

  op->hop.send_command = depot_modify_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = status_get_recv;
}

//*************************************************************

ibp_op_t *new_ibp_depot_modify_op(ibp_depot_t *depot, char *password, size_t hard, size_t soft, 
      time_t duration, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_depot_modify_op(op, depot, password, hard, soft, duration, timeout, an, cc);

  return(op);
}

//=============================================================
//  routines to handle querying a depot's resource
//=============================================================

int depot_inq_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_depot_inq_t *cmd;

  cmd = &(op->depot_inq_op);

  snprintf(buffer, sizeof(buffer), "%d %d %d %d %s %d\n", 
       IBPv040, IBP_STATUS, cmd->depot->rid, IBP_ST_INQ, cmd->password, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "depot_inq_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int process_inq(char *buffer, ibp_depotinfo_t *di)
{
  char *bstate, *bstate2, *p, *key, *d;
  int err;

  memset(di, 0, sizeof(ibp_depotinfo_t));
 
  p = string_token(buffer, " ", &bstate, &err);
  while (err == 0) {
     key = string_token(p, ":", &bstate2, &err);
     d = string_token(NULL, ":", &bstate2, &err);
     
     if (strcmp(key, ST_VERSION) == 0) {
        di->majorVersion = atof(d);
        di->minorVersion = atof(string_token(NULL, ":", &bstate2, &err));
     } else if (strcmp(key, ST_DATAMOVERTYPE) == 0) {
        //*** I just skip this.  IS it used??? ***
     } else if (strcmp(key, ST_RESOURCEID) == 0) {
        di->rid = atol(d);
     } else if (strcmp(key, ST_RESOURCETYPE) == 0) {
        di->type = atol(d);
     } else if (strcmp(key, ST_CONFIG_TOTAL_SZ) == 0) {
        di->TotalConfigured = atoll(d);
     } else if (strcmp(key, ST_SERVED_TOTAL_SZ) == 0) {
        di->TotalServed = atoll(d);     
     } else if (strcmp(key, ST_USED_TOTAL_SZ) == 0) {
        di->TotalUsed = atoll(d);     
     } else if (strcmp(key, ST_USED_HARD_SZ) == 0) {
        di->HardUsed = atoll(d);
     } else if (strcmp(key, ST_SERVED_HARD_SZ) == 0) {
        di->HardServed = atoll(d);
     } else if (strcmp(key, ST_CONFIG_HARD_SZ) == 0) {
        di->HardConfigured = atoll(d);
     } else if (strcmp(key, ST_ALLOC_TOTAL_SZ) == 0) {
        di->SoftAllocable = atoll(d);  //** I have no idea what field this maps to....
     } else if (strcmp(key, ST_ALLOC_HARD_SZ) == 0) {
        di->HardAllocable = atoll(d);
     } else if (strcmp(key, ST_DURATION) == 0) {
        di->Duration = atoll(d);
     } else if (strcmp(key, "RE") == 0) {
       err = 1;
     } else {
       log_printf(1, "process_inq:  Unknown tag:%s key=%s data=%s\n", p, key, d);
     }

     p = string_token(NULL, " ", &bstate, &err);     
  }

  return(IBP_OK);
}

//*************************************************************

int depot_inq_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err, nbytes, status;
  char *bstate;
  ibp_op_depot_inq_t *cmd;

  cmd = &(op->depot_inq_op);

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

  log_printf(15, "depot_inq_recv: after readline ns=%d buffer=%s err=%d\n", ns_getid(ns), buffer, err);

  status = atoi(string_token(buffer, " ", &bstate, &err));
  if ((status == IBP_OK) && (err == 0)) {
     nbytes = atoi(string_token(NULL, " ", &bstate, &err));
//log_printf(15, "depot_inq_recv: nbytes= ns=%d err=%d\n", nbytes, ns_getid(ns), err);

     if (nbytes <= 0) { return(IBP_E_GENERIC); }
     if (sizeof(buffer) < nbytes) { return(IBP_E_GENERIC); }

     //** Read the next line.  I ignore the size....
     err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
//  log_printf(15, "depot_inq_recv: after 2nd readline ns=%d buffer=%s err=%d\n", ns_getid(ns), buffer, err);
     if (err != IBP_OK) return(err);
     
     err = process_inq(buffer, cmd->di);
  }

  return(err);
}

//*************************************************************
//  set_ibp_depot_inq - Inquires about a depots resource
//*************************************************************

void set_ibp_depot_inq_op(ibp_op_t *op, ibp_depot_t *depot, char *password, ibp_depotinfo_t *di, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  ibp_op_depot_inq_t *cmd = &(op->depot_inq_op);

  if (cc==NULL) cc = &(_ibp_config->cc[IBP_STATUS]);
  set_hostport(hoststr, sizeof(hoststr), depot->host, depot->port, cc);

  init_ibp_base_op(op, "depot_inq", timeout, _ibp_config->new_command, strdup(hoststr), 
         _ibp_config->new_command, IBP_STATUS, IBP_ST_INQ, an, cc);
  
  cmd->depot = depot;
  cmd->password = password;
  cmd->di = di;

  op->hop.send_command = depot_inq_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = depot_inq_recv;
}

//*************************************************************

ibp_op_t *new_ibp_depot_inq_op(ibp_depot_t *depot, char *password, ibp_depotinfo_t *di, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_depot_inq_op(op, depot, password, di, timeout, an, cc);

  return(op);
}

//=============================================================
// Get depot "version" routines
//=============================================================

int depot_version_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;
  ibp_op_version_t *cmd;

  cmd = &(op->ver_op);

  snprintf(buffer, sizeof(buffer), "%d %d %d %d\n",IBPv040, IBP_STATUS, IBP_ST_VERSION, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "depot_version_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int depot_version_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024], *bstate; 
  int err, pos, nmax, fin;
  ibp_op_version_t *cmd;

  cmd = &(op->ver_op);

  err = IBP_OK;
  pos = 0;
  cmd->buffer[0] = '\0';

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err > 0) err = atoi(string_token(buffer, " ", &bstate, &fin));

  if (err == IBP_OK) err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  while (err > 0)  {
     if (strcmp("END", buffer) == 0) {  //** Got the end so exit
        return(IBP_OK);
     }
  
     //** Copy what we can **
     nmax = cmd->buffer_size - pos - 2;
     strncat(cmd->buffer, buffer, nmax);
     strcat(cmd->buffer, "\n");
     if (strlen(buffer) + pos > cmd->buffer_size) {  //** Exit if we are out of space
        return(IBP_E_WOULD_EXCEED_LIMIT);
     }     
     
     err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  }

  return(err);
}


//*************************************************************

void set_ibp_version_op(ibp_op_t *op, ibp_depot_t *depot, char *buffer, int buffer_size, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  char hoststr[1024];
  ibp_op_version_t *cmd = &(op->ver_op);

  if (cc==NULL) cc = &(_ibp_config->cc[IBP_STATUS]);
  set_hostport(hoststr, sizeof(hoststr), depot->host, depot->port, cc);

  init_ibp_base_op(op, "depot_version", timeout, _ibp_config->new_command, strdup(hoststr), 
         _ibp_config->new_command, IBP_STATUS, IBP_ST_VERSION, an, cc);
  
  cmd->depot = depot;
  cmd->buffer = buffer;
  cmd->buffer_size = buffer_size;

  op->hop.send_command = depot_version_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = depot_version_recv;
}

//*************************************************************

ibp_op_t *new_ibp_version_op(ibp_depot_t *depot, char *buffer, int buffer_size, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_version_op(op, depot, buffer, buffer_size, timeout, an, cc);

  return(op);
}

//=============================================================
// routines for getting the list or resources from a depot
//=============================================================

//*************************************************************

int query_res_command(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err;

  snprintf(buffer, sizeof(buffer), "%d %d %d %d\n",IBPv040, IBP_STATUS, IBP_ST_RES, op->hop.timeout);

  err = send_command(ns, buffer);
  if (err != IBP_OK) {
     log_printf(10, "query_res_command: Error with send_command()! ns=%d\n", ns_getid(ns));
  }

  return(err);
}

//*************************************************************

int query_res_recv(void *gop, NetStream_t *ns)
{
  ibp_op_t *op = (ibp_op_t *)gop;
  char buffer[1024]; 
  int err, fin, n, i;
  char *p, *bstate;
  ibp_op_rid_inq_t *cmd = &(op->rid_op);

  err = IBP_OK;

  err = readline_with_timeout(ns, buffer, sizeof(buffer), op->hop.end_time);
  if (err != IBP_OK) return(err);

//  log_printf(0, "query_res_recv: ns=%d buffer=!%s!\n", ns_getid(ns), buffer);

  //** check to make sure the depot supports the command
  err = atoi(string_token(buffer, " ", &bstate, &fin));
  if (err != IBP_OK) return(err);

  //** Ok now we just need to process the line **
  Stack_t *list = new_stack();
  p = string_token(NULL, " ", &bstate, &fin);
  while (fin == 0) {
//    log_printf(0, "query_res_recv: ns=%d rid=%s\n", ns_getid(ns), p);
    push(list, p);
    p = string_token(NULL, " ", &bstate, &fin);
  }

  n = stack_size(list);
  ridlist_init(cmd->rlist, n);
  move_to_bottom(list);
  for (i=0; i<n; i++) {
     p = get_ele_data(list);
     cmd->rlist->rl[i] = ibp_str2rid(p);
     move_up(list);
  }

  free_stack(list, 0);

  return(err);
}

//*************************************************************

void set_ibp_query_resources_op(ibp_op_t *op, ibp_depot_t *depot, ibp_ridlist_t *rlist, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
{
  char hoststr[1024];
  ibp_op_rid_inq_t *cmd = &(op->rid_op);

  if (cc==NULL) cc = &(_ibp_config->cc[IBP_STATUS]);
  set_hostport(hoststr, sizeof(hoststr), depot->host, depot->port, cc);

  init_ibp_base_op(op, "query_resources", timeout, _ibp_config->new_command, strdup(hoststr), 
         _ibp_config->new_command, IBP_STATUS, IBP_ST_RES, an, cc);
  
  cmd->depot = depot;
  cmd->rlist = rlist;

  op->hop.send_command = query_res_command;
  op->hop.send_phase = NULL;
  op->hop.recv_phase = query_res_recv;
}

}

//*************************************************************

ibp_op_t *new_ibp_query_resources_op(ibp_depot_t *depot, ibp_ridlist_t *rlist, int timeout, oplist_app_notify_t *an, ibp_connect_context_t *cc)
{
  ibp_op_t *op = new_ibp_op();
  if (op == NULL) return(NULL);

  set_ibp_query_resources_op(op, depot, rlist, timeout, an, cc);

  return(op);
}

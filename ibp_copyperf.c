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

//*****************************************************
// ibp_copyperf - Benchmarks IBP depot-to-depot copies, 
//*****************************************************

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <glib.h>
#include "fmttypes.h"
#include "network.h"
#include "log.h"
#include "ibp.h"
#include "iovec_sync.h"

int a_duration=900;    //** Default allocation duration

IBP_DptInfo depotinfo;
struct ibp_depot *src_depot_list;
struct ibp_depot *dest_depot_list;
int src_n_depots;
int dest_n_depots;
int ibp_timeout;
int sync_transfer;
int nthreads;
int ns_mode;
//*************************************************************************
//  io_start - Simple wrapper for sync/async to start execution
//*************************************************************************

void io_start(oplist_t *oplist)
{
  if (sync_transfer == 0) oplist_start_execution(oplist);
}

//*************************************************************************
//  io_waitall - Simple wrapper for sync/async waitall
//*************************************************************************

int io_waitall(oplist_t *oplist)
{
  int err;

log_printf(15, "io_waitall: sync_transfer=%d\n", sync_transfer);
  if (sync_transfer == 1) {
    err = ibp_sync_execute(oplist, nthreads);     
  } else {
    err = oplist_waitall(oplist);
  }

  return(err);
}

//*************************************************************************
//  create_proxy_allocs - Creates a group of proxy allocations in parallel
//   The proxy allocations are based on the input allocations and round-robined 
//   among them
//*************************************************************************

ibp_capset_t *create_proxy_allocs(int nallocs, ibp_capset_t *base_caps, int n_base)
{
  int i, err;
  oplist_t *iolist;
  ibp_op_t *op;
  ibp_capset_t *bcap;

  ibp_capset_t *caps = (ibp_capset_t *)malloc(sizeof(ibp_capset_t)*nallocs);

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     bcap = &(base_caps[i % n_base]);
     op = new_ibp_proxy_alloc_op(&(caps[i]), get_ibp_cap(bcap, IBP_MANAGECAP), 0, 0, 0, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }

  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("create_proxy_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  return(caps);
}

//*************************************************************************
// proxy_remove_allocs - Remove a list of *PROXY* allocations
//*************************************************************************

void proxy_remove_allocs(ibp_capset_t *caps_list, ibp_capset_t *mcaps_list, int nallocs, int mallocs)
{
  int i, j, err;
  oplist_t *iolist;
  ibp_op_t *op;

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     j = i % mallocs;
     op = new_ibp_proxy_remove_op(get_ibp_cap(&(caps_list[i]), IBP_MANAGECAP), 
              get_ibp_cap(&(mcaps_list[j]), IBP_MANAGECAP), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("proxy_remove_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  //** Lastly free all the caps and the array
  for (i=0; i<nallocs; i++) {
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_READCAP));   
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_WRITECAP));   
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_MANAGECAP));   
  }

  free(caps_list);

  return;
}

//*************************************************************************
//  create_allocs - Creates a group of allocations in parallel
//*************************************************************************

ibp_capset_t *create_allocs(int nallocs, int asize, int nthreads, ibp_depot_t *depot_list, int n_depots)
{
  int i, err;
  ibp_attributes_t attr;
  ibp_depot_t *depot;
  oplist_t *iolist;
  ibp_op_t *op;

  ibp_capset_t *caps = (ibp_capset_t *)malloc(sizeof(ibp_capset_t)*nallocs);

  set_ibp_attributes(&attr, time(NULL) + a_duration, IBP_HARD, IBP_BYTEARRAY);
  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     depot = &(depot_list[i % n_depots]);
     op = new_ibp_alloc_op(&(caps[i]), asize, depot, &attr, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("create_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  return(caps);
}

//*************************************************************************
// remove_allocs - Remove a list of allocations
//*************************************************************************

void remove_allocs(ibp_capset_t *caps_list, int nallocs, int nthreads)
{
  int i, err;
  oplist_t *iolist;
  ibp_op_t *op;

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     op = new_ibp_remove_op(get_ibp_cap(&(caps_list[i]), IBP_MANAGECAP), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("remove_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  //** Lastly free all the caps and the array
  for (i=0; i<nallocs; i++) {
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_READCAP));   
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_WRITECAP));   
     destroy_ibp_cap(get_ibp_cap(&(caps_list[i]), IBP_MANAGECAP));   
  }

  free(caps_list);

  return;
}

//*************************************************************************
// write_allocs - Upload data to allocations
//*************************************************************************

void write_allocs(ibp_capset_t *caps, int n, int asize, int nthreads)
{
  int i, err;
  oplist_t *iolist;
  ibp_op_t *op;

  char *buffer = (char *)malloc(asize);
  memset(buffer, 'W', asize);

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<n; i++) {
     op = new_ibp_write_op(get_ibp_cap(&(caps[i]), IBP_WRITECAP), 0, asize, buffer, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }

  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("write_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);
}

//*************************************************************************
// copy_allocs - Perform the depot-depot copy
//*************************************************************************

void copy_allocs(char *path, ibp_capset_t *src_caps, ibp_capset_t *dest_caps, int n_src, int n_dest, int asize, int nthreads)
{
  int i, j, err;
  oplist_t *iolist;
  ibp_op_t *op;

  char *buffer = (char *)malloc(asize);
  memset(buffer, 'W', asize);

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<n_dest; i++) {
     j = i % n_src;
     op = new_ibp_copyappend_op(ns_mode, path, get_ibp_cap(&(src_caps[j]), IBP_READCAP), get_ibp_cap(&(dest_caps[i]), IBP_WRITECAP),
                  0, asize, ibp_timeout, ibp_timeout, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }

  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("copy_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);
}

//*************************************************************************
//*************************************************************************

int main(int argc, char **argv)
{
  double r1, r2, dt;
  int i;
  char *path;
  ibp_capset_t *src_caps_list;
  ibp_capset_t *base_src_caps_list = NULL;
  ibp_capset_t *dest_caps_list;
  ibp_capset_t *base_dest_caps_list = NULL;
  int proxy_source, proxy_dest;
  rid_t rid;
  int port;
  char buffer[1024];
  GTimer *gtimer = g_timer_new();

  if (argc < 12) {
     printf("\n");
     printf("ibp_copyperf [-d|-dd] [-config ibp.cfg] [-phoebus phoebus_path] [-duration duration]\n");
     printf("          [-sync] [-proxy-source] [-proxy-dest]\n");
     printf("          src_n_depots src_depot1 src_port1 src_resource_id1 ... src_depotN src_portN src_ridN\n");
     printf("          dest_n_depots dest_depot1 dest_port1 dest_resource_id1 ... dest_depotN dest_portN dest_ridN\n");
     printf("          nthreads ibp_timeout count size\n");
     printf("\n");
     printf("-d                  - Enable *minimal* debug output\n");
     printf("-dd                 - Enable *FULL* debug output\n");
     printf("-config ibp.cfg     - Use the IBP configuration defined in file ibp.cfg.\n");
     printf("                      nthreads overrides value in cfg file unless -1.\n");
     printf("-phoebus            - Make the transfer using the phoebus protocol.  Default is to use sockets\n");
     printf("   phoebus_list     - Specify the phoebus transfer path. Specify 'auto' for depot default\n");
     printf("                      Comma separated List of phoebus hosts/ports, eg gateway1/1234,gateway2/4321\n");
     printf("-duration duration  - Allocation duration in sec.  Needs to be big enough to last the entire\n");
     printf("                      run.  The default duration is %d sec.\n", a_duration);
     printf("-sync               - Use synchronous protocol.  Default uses async.\n");
     printf("-proxy-source       - Use proxy allocations for source allocations\n"); 
     printf("-proxy-dest         - Use proxy allocations for the destination allocations\n"); 
     printf("src_n_depots        - Number of *source* depot tuplets\n");
     printf("src_depot           - Source depot hostname\n");
     printf("src_port            - Source depot IBP port\n"); 
     printf("src_resource_id     - Resource ID to use on source depot\n");
     printf("dest_n_depots       - Number of *destination* depot tuplets\n");
     printf("dest_depot          - Destination depot hostname\n");
     printf("dest_port           - Destination depot IBP port\n"); 
     printf("dest_resource_id    - Resource ID to use on destination depot\n");
     printf("nthreads            - Max Number of simultaneous threads to use.  Use -1 for defaults or value in ibp.cfg\n");
     printf("ibp_timeout         - Timeout(sec) for each IBP copmmand\n");
     printf("count               - Total Number of allocation on destination depots\n");
     printf("size                - Size of each allocation in KB on destination depot\n");
     printf("\n");

     return(-1);
  }

  ibp_init();  //** Initialize IBP

  i = 1;
  if (strcmp(argv[i], "-d") == 0) { //** Enable debugging
     set_log_level(5);
     i++;
  }
  if (strcmp(argv[i], "-dd") == 0) { //** Enable debugging
     set_log_level(20);
     i++;
  }

  if (strcmp(argv[i], "-config") == 0) { //** Read the config file
     i++;
     ibp_load_config(argv[i]);
     i++;
  }

  ns_mode = NS_TYPE_SOCK;
  path = NULL;
  if (strcmp(argv[i], "-phoebus") == 0) { //** Check if we want phoebus transfers
     ns_mode = NS_TYPE_PHOEBUS;
     i++;

     path = argv[i];
     i++;
  }

  if (strcmp(argv[i], "-duration") == 0) { //** Check if we want sync tests
     i++;
     a_duration = atoi(argv[i]);
     i++;
  }

  sync_transfer = 0;
  if (strcmp(argv[i], "-sync") == 0) { //** Check if we want sync tests
     sync_transfer = 1;
     i++;
  }

  proxy_source = 0;
  if (strcmp(argv[i], "-proxy-source") == 0) { //** Check if we want proxy allocs
     proxy_source = 1;
     i++;
  }

  proxy_dest = 0;
  if (strcmp(argv[i], "-proxy-dest") == 0) { //** Check if we want proxy allocs
     proxy_dest = 1;
     i++;
  }

  //** Read in source depot list **
  src_n_depots = atoi(argv[i]);
  i++;
  src_depot_list = (ibp_depot_t *)malloc(sizeof(ibp_depot_t)*src_n_depots);
  int j;
  for (j=0; j<src_n_depots; j++) {
      port = atoi(argv[i+1]);
      rid = ibp_str2rid(argv[i+2]);
      set_ibp_depot(&(src_depot_list[j]), argv[i], port, rid);
      i = i + 3;
  }

  //** Read in destination depot list **
  dest_n_depots = atoi(argv[i]);
  i++;
  dest_depot_list = (ibp_depot_t *)malloc(sizeof(ibp_depot_t)*dest_n_depots);
  for (j=0; j<dest_n_depots; j++) {
      port = atoi(argv[i+1]);
      rid = ibp_str2rid(argv[i+2]);
      set_ibp_depot(&(dest_depot_list[j]), argv[i], port, rid);
      i = i + 3;
  }

  //*** Get thread count ***
  nthreads = atoi(argv[i]);
  if (nthreads <= 0) { 
     nthreads = ibp_get_max_depot_threads();
  } else {
     ibp_set_max_depot_threads(nthreads);
  }
  i++;

  ibp_timeout = atoi(argv[i]); i++;

   //****** Get the different Stream counts *****
  int count = atoi(argv[i]); i++;
  int size = atoi(argv[i])*1024; i++;

  //*** Print the ibp client version ***
  printf("\n");
  printf("================== IBP Client Version =================\n");
  printf("%s\n", ibp_client_version());

  //*** Print summary of options ***
  printf("\n");
  printf("======= Base options =======\n");
  printf("Source n_depots: %d\n", src_n_depots);
  for (i=0; i<src_n_depots; i++) {
     printf("depot %d: %s:%d rid:%s\n", i, src_depot_list[i].host, src_depot_list[i].port, ibp_rid2str(src_depot_list[i].rid, buffer));
  }
  printf("\n");
  printf("Destination n_depots: %d\n", dest_n_depots);
  for (i=0; i<dest_n_depots; i++) {
     printf("depot %d: %s:%d rid:%s\n", i, dest_depot_list[i].host, dest_depot_list[i].port, ibp_rid2str(dest_depot_list[i].rid, buffer));
  }
  printf("\n");
  printf("IBP timeout: %d\n", ibp_timeout);
  printf("Max Threads: %d\n", nthreads);
  if (sync_transfer == 1) {
     printf("Transfer_mode: SYNC\n");
  } else {
     printf("Transfer_mode: ASYNC\n");
  }
  if (ns_mode == NS_TYPE_SOCK) {
     printf("Depot->Depot transfer type: NS_TYPE_SOCK\n");
  } else {
     printf("Depot->Depot transfer type: NS_TYPE_PHOEBUS\n");
     printf("Phoebus path: %s\n", path);
  }
  printf("Proxy source: %d\n", proxy_source);
  printf("Proxy destination: %d\n", proxy_dest);
  printf("\n");
  printf("======= Bulk transfer options =======\n");
  printf("Count: %d\n", count);
  printf("Size: %dkb\n", size/1024);
  printf("\n");

  r1 =  1.0 * size/1024.0/1024.0;
  r1 = count * r1;
  printf("Approximate data for sequential tests: %lfMB\n", r1);
  printf("\n");

  //**************** Perform the tests ***************************
  i = count/nthreads;
  printf("Starting Bulk test (total files: %d, approx per thread: %d", count, i);
  r1 = 1.0*count*size/1024.0/1024.0;
  r2 = r1 / nthreads;
  printf(" -- total size: %lfMB, approx per thread: %lfMB\n", r1, r2);

  printf("Creating allocations...."); fflush(stdout);
  g_timer_start(gtimer);
  src_caps_list = create_allocs(src_n_depots, size, nthreads, src_depot_list, src_n_depots);
  if (proxy_source == 1) {
     base_src_caps_list = src_caps_list;
     src_caps_list = create_proxy_allocs(src_n_depots, base_src_caps_list, src_n_depots);
  }

  dest_caps_list = create_allocs(count, size, nthreads, dest_depot_list, dest_n_depots);
  if (proxy_dest == 1) {
     base_dest_caps_list = dest_caps_list;
     dest_caps_list = create_proxy_allocs(count, base_dest_caps_list, count);
  }
  g_timer_stop(gtimer);
  dt = g_timer_elapsed(gtimer, NULL);
  r1 = 1.0*((1+proxy_dest)*count + (1+proxy_source)*src_n_depots)/dt;
  printf(" %lf creates/sec (%.2lf sec total) \n", r1, dt);


  printf("Uploading data to source depots...."); fflush(stdout);
  g_timer_start(gtimer);
  write_allocs(src_caps_list, src_n_depots, size, nthreads);
  g_timer_stop(gtimer);
  dt = g_timer_elapsed(gtimer, NULL);
  r1 = 1.0*src_n_depots*size/(dt*1024*1024);
  printf(" %lf MB/sec (%.2lf sec total) \n", r1, dt);  fflush(stdout);

  printf("Depot-depot copy:"); fflush(stdout);
  g_timer_start(gtimer);
  copy_allocs(path, src_caps_list, dest_caps_list, src_n_depots, count, size, nthreads);
  g_timer_stop(gtimer);
  dt = g_timer_elapsed(gtimer, NULL);
  r1 = 1.0*count*size/(dt*1024*1024);
  printf(" %lf MB/sec (%.2lf sec total) \n", r1, dt);  fflush(stdout);
  
  printf("Removing allocations...."); fflush(stdout);
  g_timer_start(gtimer);
  if (proxy_source == 1) {
     proxy_remove_allocs(src_caps_list, base_src_caps_list, src_n_depots, src_n_depots);
     src_caps_list = base_src_caps_list;
  }
  remove_allocs(src_caps_list, src_n_depots, nthreads);

  if (proxy_dest == 1) {
     proxy_remove_allocs(dest_caps_list, base_dest_caps_list, count, count);
     dest_caps_list = base_dest_caps_list;
  }
  remove_allocs(dest_caps_list, count, nthreads);
  g_timer_stop(gtimer);
  dt = g_timer_elapsed(gtimer, NULL);
  r1 = 1.0*((1+proxy_dest)*count + (1+proxy_source)*src_n_depots)/dt;
  printf(" %lf removes/sec (%.2lf sec total) \n", r1, dt);
  printf("\n");

  printf("Final network connection counter: %d\n", network_counter(NULL));
     
  ibp_finalize();  //** Shutdown IBP

  g_timer_destroy(gtimer);
  return(0);
}



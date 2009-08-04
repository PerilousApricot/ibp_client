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
// ibp_perf - Benchmarks IBP depot creates, removes, 
//      reads, and writes.  The read and write tests
//      use sync an async iovec style operations.
//*****************************************************

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <apr_time.h>
#include "network.h"
#include "fmttypes.h"
#include "network.h"
#include "log.h"
#include "ibp.h"
#include "iovec_sync.h"

int a_duration=900;   //** Default duration

IBP_DptInfo depotinfo;
struct ibp_depot *depot_list;
int n_depots;
int ibp_timeout;
int sync_transfer;
int nthreads;
int use_alias;
ibp_connect_context_t *cc = NULL;

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
// simple_test - simple single allocation test of the iovec routines
//*************************************************************************

void simple_test()
{
  int size = 1024*1024;
  int block = 5000;
  char buffer[size+1];
  char buffer_cmp[size+1];
  char block_buf[block+1];
  ibp_attributes_t attr;
  ibp_depot_t *depot;
  ibp_capset_t caps;
  ibp_cap_t *cap;
  int err, i, offset, bcount, remainder;
  ibp_op_t *op;
  oplist_t *iol;

  //** Make the allocation ***
  depot = &(depot_list[0]);

  //** Create the list for handling the commands
  iol = new_ibp_oplist(NULL);
  oplist_start_execution(iol);  //** and start executing the commands

  //** Create the allocation used for test
  set_ibp_attributes(&attr, time(NULL) + a_duration, IBP_HARD, IBP_BYTEARRAY);
  op = new_ibp_alloc_op(&caps, size, depot, &attr, ibp_timeout, NULL, NULL);
  add_ibp_oplist(iol, op);
  err = oplist_waitall(iol);  
  if (err != IBP_OK) { 
     printf("simple_test: ibp_allocate error! * ibp_errno=%d\n", err); 
     abort();
  }
  
  printf("simple_test: rcap=%s\n", get_ibp_cap(&caps, IBP_READCAP));
  printf("simple_test: wcap=%s\n", get_ibp_cap(&caps, IBP_WRITECAP));
  printf("simple_test: mcap=%s\n", get_ibp_cap(&caps, IBP_MANAGECAP));

  //** Init the buffers
  buffer[size] = '\0'; memset(buffer, '*', size);
  buffer_cmp[size] = '\0'; memset(buffer_cmp, '_', size);
  block_buf[block] = '\0'; memset(block_buf, '0', block);

  //-------------------------------
  //** Do the initial upload
  op = new_ibp_write_op(get_ibp_cap(&caps, IBP_WRITECAP), 0, size, buffer_cmp, ibp_timeout, NULL, NULL);
  add_ibp_oplist(iol, op);
  err = oplist_waitall(iol);
  if (err != IBP_OK) {
     printf("simple_test: Initial ibp_write error! * ibp_errno=%d\n", err); 
     abort();
  }    

  bcount = size / (2*block);
  remainder = size - bcount * (2*block);

  //** Now do the striping **
  offset = 0;      // Now store the data in chunks
  cap = get_ibp_cap(&caps, IBP_WRITECAP);
  for (i=0; i<bcount; i++) {
     op = new_ibp_write_op(cap, offset, block, block_buf, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);
   
     memset(&(buffer_cmp[offset]), '0', block);

     offset = offset + 2*block;
  }

  if (remainder>0)  {
     if (remainder > block) remainder = block;
     op = new_ibp_write_op(cap, offset, remainder, block_buf, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);

     memset(&(buffer_cmp[offset]), '0', remainder);
  }

  //** Now wait for them to complete
  err = oplist_waitall(iol);
  if (err != IBP_OK) {
     printf("simple_test: Error in stripe write! * ibp_errno=%d\n", err); 
     abort();
  }

  //-------------------------------
  bcount = size / block;
  remainder = size - bcount * block;

  //** Generate the Read list
  offset = 0;      // Now store the data in chunks
  cap = get_ibp_cap(&caps, IBP_READCAP);
  for (i=0; i<bcount; i++) {
     op = new_ibp_read_op(cap, offset, block, &(buffer[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);

     offset = offset + block;
  }

  if (remainder>0)  {
     op = new_ibp_read_op(cap, offset, remainder, &(buffer[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);
  }

  //** Now wait for them to complete
  err = oplist_waitall(iol);
  if (err != IBP_OK) {
     printf("simple_test: Error in stripe read! * ibp_errno=%d\n", err); 
     abort();
  }

  //-------------------------------
  
  //** Do the comparison **
  i = strcmp(buffer, buffer_cmp);
  if (i == 0) {
     printf("simple_test: Success!\n");
  } else {
     printf("simple_test: Failed! strcmp = %d\n", i);
  }

//  printf("simple_test: buffer_cmp=%s\n", buffer_cmp);
//  printf("simple_test:     buffer=%s\n", buffer);


  //-------------------------------


  //** Remove the allocation **
  op = new_ibp_remove_op(get_ibp_cap(&caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  add_ibp_oplist(iol, op);
  err = oplist_waitall(iol);
  if (err != IBP_OK) { 
     printf("simple_test: Error removing the allocation!  ibp_errno=%d\n", err); 
     abort(); 
  } 

  free_oplist(iol);
}

//*************************************************************************
//  create_alias_allocs - Creates a group of alias allocations in parallel
//   The alias allocations are based on the input allocations and round-robined 
//   among them
//*************************************************************************

ibp_capset_t *create_alias_allocs(int nallocs, ibp_capset_t *base_caps, int n_base)
{
  int i, err;
  oplist_t *iolist;
  ibp_op_t *op;
  ibp_capset_t *bcap;

  ibp_capset_t *caps = (ibp_capset_t *)malloc(sizeof(ibp_capset_t)*nallocs);

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     bcap = &(base_caps[i % n_base]);
     op = new_ibp_alias_alloc_op(&(caps[i]), get_ibp_cap(bcap, IBP_MANAGECAP), 0, 0, 0, ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }

  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("create_alias_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  return(caps);
}

//*************************************************************************
// alias_remove_allocs - Remove a list of *ALIAS* allocations
//*************************************************************************

void alias_remove_allocs(ibp_capset_t *caps_list, ibp_capset_t *mcaps_list, int nallocs, int mallocs)
{
  int i, j, err;
  oplist_t *iolist;
  ibp_op_t *op;

  iolist = new_ibp_oplist(NULL);

  for (i=0; i<nallocs; i++) {
     j = i % mallocs;
     op = new_ibp_alias_remove_op(get_ibp_cap(&(caps_list[i]), IBP_MANAGECAP), 
              get_ibp_cap(&(mcaps_list[j]), IBP_MANAGECAP), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("alias_remove_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
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

ibp_capset_t *create_allocs(int nallocs, int asize)
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
     abort();
  }    
  free_oplist(iolist);

  return(caps);
}

//*************************************************************************
// remove_allocs - Remove a list of allocations
//*************************************************************************

void remove_allocs(ibp_capset_t *caps_list, int nallocs)
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
     abort();
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

void write_allocs(ibp_capset_t *caps, int n, int asize, int block_size)
{
  int i, j, nblocks, rem, len, err;
  oplist_t *iolist;
  ibp_op_t *op;

  char *buffer = (char *)malloc(asize);
  memset(buffer, 'W', asize);

  iolist = new_ibp_oplist(NULL);

  nblocks = asize / block_size;
  rem = asize % block_size;
  if (rem > 0) nblocks++;

//for (j=0; j<nblocks; j++) {
  for (j=nblocks-1; j>= 0; j--) {
     for (i=0; i<n; i++) {
         if ((j==(nblocks-1)) && (rem > 0)) { len = rem; } else { len = block_size; }
         op = new_ibp_write_op(get_ibp_cap(&(caps[i]), IBP_WRITECAP), j*block_size, len, buffer, ibp_timeout, NULL, cc);
         add_ibp_oplist(iolist, op);
     }
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
// read_allocs - Downlaod data from allocations
//*************************************************************************

void read_allocs(ibp_capset_t *caps, int n, int asize, int block_size)
{
  int i, j, nblocks, rem, len, err;
  oplist_t *iolist;
  ibp_op_t *op;

  char *buffer = (char *)malloc(asize);
    
  iolist = new_ibp_oplist(NULL);

  nblocks = asize / block_size;
  rem = asize % block_size;
  if (rem > 0) nblocks++;

//  for (j=0; j<nblocks; j++) {
  for (j=nblocks-1; j>= 0; j--) {
     for (i=0; i<n; i++) {
         if ((j==(nblocks-1)) && (rem > 0)) { len = rem; } else { len = block_size; }
         op = new_ibp_read_op(get_ibp_cap(&(caps[i]), IBP_READCAP), j*block_size, len, buffer, ibp_timeout, NULL, cc);
         add_ibp_oplist(iolist, op);
     }
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("read_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);
}

//*************************************************************************
// random_allocs - Perform random R/W on allocations
//*************************************************************************

void random_allocs(ibp_capset_t *caps, int n, int asize, int block_size, double rfrac)
{
  int i, slot, err;
  int j, nblocks, rem, len;
  oplist_t *iolist;
  ibp_op_t *op;
  double rnd;

  char *buffer = (char *)malloc(asize);    
  memset(buffer, 'R', asize);

  iolist = new_ibp_oplist(NULL);

  nblocks = asize / block_size;
  rem = asize % block_size;
  if (rem > 0) nblocks++;

  for (j=0; j<nblocks; j++) {
     for (i=0; i<n; i++) {
         rnd = rand()/(RAND_MAX+1.0);     
         slot = n * rnd;

         rnd = rand()/(RAND_MAX + 1.0);

         if ((j==(nblocks-1)) && (rem > 0)) { len = rem; } else { len = block_size; }
//         op = new_ibp_read_op(get_ibp_cap(&(caps[i]), IBP_READCAP), j*block_size, len, buffer, ibp_timeout, NULL, cc);

         if (rnd < rfrac) {
            op = new_ibp_read_op(get_ibp_cap(&(caps[i]), IBP_READCAP), j*block_size, len, buffer, ibp_timeout, NULL, cc);
         } else {
            op = new_ibp_write_op(get_ibp_cap(&(caps[i]), IBP_WRITECAP), j*block_size, len, buffer, ibp_timeout, NULL, cc);
         }
         add_ibp_oplist(iolist, op);
     }
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("random_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);
}

//*************************************************************************
// small_write_allocs - Performs small write I/O on the bulk allocations
//*************************************************************************

double small_write_allocs(ibp_capset_t *caps, int n, int asize, int small_count, int min_size, int max_size)
{
  int i, io_size, offset, slot, err;
  oplist_t *iolist;
  ibp_op_t *op;
  double rnd, lmin, lmax;
  double nbytes;

  iolist = new_ibp_oplist(NULL);

  if (asize < max_size) {
     max_size = asize;
     log_printf(0, "small_write_allocs:  Adjusting max_size=%d\n", max_size);
  }

  lmin = log(min_size);  lmax = log(max_size);

  char *buffer = (char *)malloc(max_size);
  memset(buffer, 'B', max_size);

  nbytes = 0;
  for (i=0; i<small_count; i++) {
     rnd = rand()/(RAND_MAX+1.0);     
     slot = n * rnd;

     rnd = rand()/(RAND_MAX+1.0);     
     rnd = lmin + (lmax - lmin) * rnd;
     io_size = exp(rnd);
     if (io_size == 0) io_size = 1;
     nbytes = nbytes + io_size;

     rnd = rand()/(RAND_MAX+1.0);
     offset = (asize - io_size) * rnd;

//     log_printf(15, "small_write_allocs: slot=%d offset=%d size=%d\n", slot, offset, io_size);
     op = new_ibp_write_op(get_ibp_cap(&(caps[slot]), IBP_WRITECAP), offset, io_size, buffer, ibp_timeout, NULL, cc);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("small_write_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);

  return(nbytes);
}

//*************************************************************************
// small_read_allocs - Performs small read I/O on the bulk allocations
//*************************************************************************

double small_read_allocs(ibp_capset_t *caps, int n, int asize, int small_count, int min_size, int max_size)
{
  int i, io_size, offset, slot, err;
  oplist_t *iolist;
  ibp_op_t *op;
  double rnd, lmin, lmax;
  double nbytes;

  iolist = new_ibp_oplist(NULL);

  lmin = log(min_size);  lmax = log(max_size);

  if (asize < max_size) {
     max_size = asize;
     log_printf(0, "small_read_allocs:  Adjusting max_size=%d\n", max_size);
  }

  char *buffer = (char *)malloc(max_size);
  memset(buffer, 0, max_size);

  nbytes = 0;
  for (i=0; i<small_count; i++) {
     rnd = rand()/(RAND_MAX+1.0);     
     slot = n * rnd;

     rnd = rand()/(RAND_MAX+1.0);     
     rnd = lmin + (lmax - lmin) * rnd;
     io_size = exp(rnd);
     if (io_size == 0) io_size = 1;
     nbytes = nbytes + io_size;

     rnd = rand()/(RAND_MAX+1.0);
     offset = (asize - io_size) * rnd;

//     log_printf(15, "small_read_allocs: slot=%d offset=%d size=%d\n", slot, offset, io_size);
     op = new_ibp_read_op(get_ibp_cap(&(caps[slot]), IBP_READCAP), offset, io_size, buffer, ibp_timeout, NULL, cc);
     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("small_read_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);

  return(nbytes);
}

//*************************************************************************
// small_random_allocs - Performs small random I/O on the bulk allocations
//*************************************************************************

double small_random_allocs(ibp_capset_t *caps, int n, int asize, double readfrac, int small_count, int min_size, int max_size)
{
  int i, io_size, offset, slot, err;
  oplist_t *iolist;
  ibp_op_t *op;
  double rnd, lmin, lmax;
  double nbytes;

  iolist = new_ibp_oplist(NULL);

  lmin = log(min_size);  lmax = log(max_size);

  if (asize < max_size) {
     max_size = asize;
     log_printf(0, "small_random_allocs:  Adjusting max_size=%d\n", max_size);
  }

  char *buffer = (char *)malloc(max_size);
  memset(buffer, 0, max_size);

  nbytes = 0;
  for (i=0; i<small_count; i++) {
     rnd = rand()/(RAND_MAX+1.0);     
     slot = n * rnd;

     rnd = rand()/(RAND_MAX+1.0);     
     rnd = lmin + (lmax - lmin) * rnd;
     io_size = exp(rnd);
     if (io_size == 0) io_size = 1;
     nbytes = nbytes + io_size;

     rnd = rand()/(RAND_MAX+1.0);
     offset = (asize - io_size) * rnd;

//     log_printf(15, "small_random_allocs: slot=%d offset=%d size=%d\n", slot, offset, io_size);

     rnd = rand()/(RAND_MAX+1.0);     
     if (rnd < readfrac) {
        op = new_ibp_read_op(get_ibp_cap(&(caps[slot]), IBP_READCAP), offset, io_size, buffer, ibp_timeout, NULL, cc);
     } else {
        op = new_ibp_write_op(get_ibp_cap(&(caps[slot]), IBP_WRITECAP), offset, io_size, buffer, ibp_timeout, NULL, cc);
     }

     add_ibp_oplist(iolist, op);
  }
 
  io_start(iolist);
  err = io_waitall(iolist);  
  if (err != IBP_OK) {
     printf("small_random_allocs: At least 1 error occured! * ibp_errno=%d * nfailed=%d\n", err, oplist_nfailed(iolist)); 
  }    
  free_oplist(iolist);

  free(buffer);

  return(nbytes);
}

//*************************************************************************
//*************************************************************************

int main(int argc, char **argv)
{
  double r1, r2, r3;
  int i, do_simple_test, tcpsize;
  ibp_capset_t *caps_list, *base_caps;
  rid_t rid;
  int port;
  char buffer[1024];
  apr_time_t stime, dtime;
  double dt;
  char *ppath;
  phoebus_t pcc;
  char pstr[2048];

  base_caps = NULL;

  if (argc < 12) {
     printf("\n");
     printf("ibp_perf [-d|-dd] [-config ibp.cfg] [-phoebus gateway_list] [-tcpsize tcpbufsize]\n");
     printf("           [-duration duration] [-sync] [-alias]\n");
     printf("          n_depots depot1 port1 resource_id1 ... depotN portN ridN\n");
     printf("          nthreads ibp_timeout\n");
     printf("          alias_createremove_count createremove_count\n");
     printf("          readwrite_count readwrite_alloc_size rw_block_size read_mix_fraction\n");
     printf("          smallio_count small_min_size small_max_size small_read_fraction\n");
     printf("\n");
     printf("-d                  - Enable *minimal* debug output\n");
     printf("-dd                 - Enable *FULL* debug output\n");
     printf("-config ibp.cfg     - Use the IBP configuration defined in file ibp.cfg.\n");
     printf("                      nthreads overrides value in cfg file unless -1.\n");
     printf("-phoebus            - Use Phoebus protocol for data transfers.\n");
     printf("   gateway_list     - Comma separated List of phoebus hosts/ports, eg gateway1/1234,gateway2/4321\n");
     printf("-tcpsize tcpbufsize - Use this value, in KB, for the TCP send/recv buffer sizes\n");
     printf("-duration duration  - Allocation duration in sec.  Needs to be big enough to last the entire\n");
     printf("                      run.  The default duration is %d sec.\n", a_duration);
     printf("-sync               - Use synchronous protocol.  Default uses async.\n");
     printf("-alias              - Use alias allocations for all I/O operations\n");
     printf("n_depots            - Number of depot tuplets\n");
     printf("depot               - Depot hostname\n");
     printf("port                - IBP port on depot\n"); 
     printf("resource_id         - Resource ID to use on depot\n");
     printf("nthreads            - Max Number of simultaneous threads to use.  Use -1 for defaults or value in ibp.cfg\n");
     printf("ibp_timeout         - Timeout(sec) for each IBP copmmand\n");
     printf("alias_createremove_count* - Number of 0 byte files to create and remove using alias allocations\n");
     printf("createremove_count* - Number of 0 byte files to create and remove to test metadata performance\n");
     printf("readwrite_count*    - Number of files to write sequentially then read sequentially\n");
     printf("readwrite_alloc_size  - Size of each allocation in KB for sequential and random tests\n");
     printf("rw_block_size       - Size of each R/W operation in KB for sequential and random tests\n");
     printf("read_mix_fraction   - Fraction of Random I/O operations that are READS\n");
     printf("smallio_count*      - Number of random small I/O operations\n");
     printf("small_min_size      - Minimum size of each small I/O operation(kb)\n");
     printf("small_max_size      - Max size of each small I/O operation(kb)\n");
     printf("small_read_fraction - Fraction of small random I/O operations that are READS\n");
     printf("\n");
     printf("*If the variable is set to 0 then the test is skipped\n");
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

  if (strcmp(argv[i], "-phoebus") == 0) { //** Check if we want Phoebus transfers
     cc = (ibp_connect_context_t *)malloc(sizeof(ibp_connect_context_t));
     cc->type = NS_TYPE_PHOEBUS;
     i++;

     ppath = argv[i];
     phoebus_path_set(&pcc, ppath);
     cc->data = &pcc;
     i++;
  }

  if (strcmp(argv[i], "-tcpsize") == 0) { //** Check if we want sync tests
     i++;
     tcpsize = atoi(argv[i]) * 1024;
     ibp_set_tcpsize(tcpsize);
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

  use_alias = 0;
  if (strcmp(argv[i], "-alias") == 0) { //** Check if we want to use alias allocation
     use_alias = 1;
     i++;
  }

  do_simple_test = 0;
  if (strcmp(argv[i], "-simpletest") == 0) { //** Just do the simple test
     do_simple_test = 1;
     i++;
  }

  n_depots = atoi(argv[i]);
  i++;

  depot_list = (ibp_depot_t *)malloc(sizeof(ibp_depot_t)*n_depots);
  int j;
  for (j=0; j<n_depots; j++) {
      port = atoi(argv[i+1]);
      rid = ibp_str2rid(argv[i+2]);
      set_ibp_depot(&(depot_list[j]), argv[i], port, rid);
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
  int aliascreateremove_count = atoi(argv[i]); i++;
  int createremove_count = atoi(argv[i]); i++;
  int readwrite_count = atoi(argv[i]); i++;
  int readwrite_size = atoi(argv[i])*1024; i++;
  int rw_block_size = atoi(argv[i])*1024; i++;
  double read_mix_fraction = atof(argv[i]); i++;

   //****** Get the different small I/O counts *****
  int smallio_count = atoi(argv[i]); i++;
  int small_min_size = atoi(argv[i])*1024; i++;
  int small_max_size = atoi(argv[i])*1024; i++;
  double small_read_fraction = atof(argv[i]); i++;

  //*** Print the ibp client version ***
  printf("\n");
  printf("================== IBP Client Version =================\n");
  printf("%s\n", ibp_client_version());

  //*** Print summary of options ***
  printf("\n");
  printf("======= Base options =======\n");
  printf("n_depots: %d\n", n_depots);
  for (i=0; i<n_depots; i++) {
     printf("depot %d: %s:%d rid:%s\n", i, depot_list[i].host, depot_list[i].port, ibp_rid2str(depot_list[i].rid, buffer));
  }
  printf("\n");
  printf("IBP timeout: %d\n", ibp_timeout);
  printf("Max Threads: %d\n", nthreads);
 if (sync_transfer == 1) {
     printf("Transfer_mode: SYNC\n");
  } else {
     printf("Transfer_mode: ASYNC\n");
  }
  printf("Use alias: %d\n", use_alias);

  if (cc != NULL) {
     switch (cc->type) {
       case NS_TYPE_SOCK:
          printf("Connection Type: SOCKET\n"); break;
       case NS_TYPE_PHOEBUS:
          phoebus_path_to_string(pstr, sizeof(pstr), &pcc); 
          printf("Connection Type: PHOEBUS (%s)\n", pstr); break;
       case NS_TYPE_1_SSL:
          printf("Connection Type: Single SSL\n"); break;
       case NS_TYPE_2_SSL:
          printf("Connection Type: Dual SSL\n"); break;
     }
  } else {
    printf("Connection Type: SOCKET\n");
  }
  printf("TCP buffer size: %dkb (0 defaults to OS)\n", ibp_get_tcpsize()/1024);
  printf("\n");

  printf("======= Bulk transfer options =======\n");
  printf("aliascreateremove_count: %d\n", aliascreateremove_count);
  printf("createremove_count: %d\n", createremove_count);
  printf("readwrite_count: %d\n", readwrite_count);
  printf("readwrite_alloc_size: %dkb\n", readwrite_size/1024);
  printf("rw_block_size: %dkb\n", rw_block_size/1024);
  printf("read_mix_fraction: %lf\n", read_mix_fraction);
  printf("\n");
  printf("======= Small Random I/O transfer options =======\n");
  printf("smallio_count: %d\n", smallio_count);
  printf("small_min_size: %dkb\n", small_min_size/1024);
  printf("small_max_size: %dkb\n", small_max_size/1024);
  printf("small_read_fraction: %lf\n", small_read_fraction);
  printf("\n");

  r1 =  1.0 * readwrite_size/1024.0/1024.0;
  r1 = readwrite_count * r1;
  printf("Approximate I/O for sequential tests: %lfMB\n", r1);
  printf("\n");

  if (do_simple_test == 1) { 
     simple_test(nthreads);
     return(0);
  }

  //**************** Create/Remove tests ***************************
  if (aliascreateremove_count > 0) {
     i = aliascreateremove_count/nthreads;
     printf("Starting Alias create test (total files: %d, approx per thread: %d)\n",aliascreateremove_count, i);
     base_caps = create_allocs(1, 1);
     stime = apr_time_now();
     caps_list = create_alias_allocs(aliascreateremove_count, base_caps, 1);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*aliascreateremove_count/dt;
     printf("Alias create : %lf creates/sec (%.2lf sec total) \n", r1, dt);

     stime = apr_time_now();
     alias_remove_allocs(caps_list, base_caps, aliascreateremove_count, 1);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*aliascreateremove_count/dt;
     printf("Alias remove : %lf removes/sec (%.2lf sec total) \n", r1, dt);
     printf("\n");

printf("-----------------------------\n"); fflush(stdout);

     remove_allocs(base_caps, 1);

     printf("\n");
  }

  if (createremove_count > 0) {
     i = createremove_count/nthreads;
     printf("Starting Create test (total files: %d, approx per thread: %d)\n",createremove_count, i);

     stime = apr_time_now();
     caps_list = create_allocs(createremove_count, 1);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*createremove_count/dt;
     printf("Create : %lf creates/sec (%.2lf sec total) \n", r1, dt);

     stime = apr_time_now();
     remove_allocs(caps_list, createremove_count);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*createremove_count/dt;
     printf("Remove : %lf removes/sec (%.2lf sec total) \n", r1, dt);
     printf("\n");
  }

  //**************** Read/Write tests ***************************
  if (readwrite_count > 0) {
     i = readwrite_count/nthreads;
     printf("Starting Bulk tests (total files: %d, approx per thread: %d", readwrite_count, i);
     r1 = 1.0*readwrite_count*readwrite_size/1024.0/1024.0;
     r2 = r1 / nthreads;
     printf(" -- total size: %lfMB, approx per thread: %lfMB\n", r1, r2);

     printf("Creating allocations...."); fflush(stdout);
     stime = apr_time_now();
     caps_list = create_allocs(readwrite_count, readwrite_size);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*readwrite_count/dt;
     printf(" %lf creates/sec (%.2lf sec total) \n", r1, dt);

     if (use_alias) {
        base_caps = caps_list;
        printf("Creating alias allocations...."); fflush(stdout);
        stime = apr_time_now();
        caps_list = create_alias_allocs(readwrite_count, base_caps, readwrite_count);
        dtime = apr_time_now() - stime;
        dt = dtime / (1.0 * APR_USEC_PER_SEC);
        r1 = 1.0*readwrite_count/dt;
        printf(" %lf creates/sec (%.2lf sec total) \n", r1, dt);
     }

     stime = apr_time_now();
     write_allocs(caps_list, readwrite_count, readwrite_size, rw_block_size);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*readwrite_count*readwrite_size/(dt*1024*1024);
     printf("Write: %lf MB/sec (%.2lf sec total) \n", r1, dt);

     stime = apr_time_now();
     read_allocs(caps_list, readwrite_count, readwrite_size, rw_block_size);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*readwrite_count*readwrite_size/(dt*1024*1024);
     printf("Read: %lf MB/sec (%.2lf sec total) \n", r1, dt);

     stime = apr_time_now();
     random_allocs(caps_list, readwrite_count, readwrite_size, rw_block_size, read_mix_fraction);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*readwrite_count*readwrite_size/(dt*1024*1024);
     printf("Random: %lf MB/sec (%.2lf sec total) \n", r1, dt);

     //**************** Small I/O tests ***************************
     if (smallio_count > 0) {
        if (small_min_size == 0) small_min_size = 1;
        if (small_max_size == 0) small_max_size = 1;

        printf("\n");
        printf("Starting Small Random I/O tests...\n");

        stime = apr_time_now();
        r1 = small_write_allocs(caps_list, readwrite_count, readwrite_size, smallio_count, small_min_size, small_max_size);
        dtime = apr_time_now() - stime;
        dt = dtime / (1.0 * APR_USEC_PER_SEC);
        r1 = r1/(1024.0*1024.0);
        r2 = r1/dt;
        r3 = smallio_count; r3 = r3 / dt;
        printf("Small Random Write: %lf MB/sec (%.2lf sec total using %lfMB or %.2lf ops/sec) \n", r2, dt, r1, r3);

        stime = apr_time_now();
        r1 = small_read_allocs(caps_list, readwrite_count, readwrite_size, smallio_count, small_min_size, small_max_size);
        dtime = apr_time_now() - stime;
        dt = dtime / (1.0 * APR_USEC_PER_SEC);
        r1 = r1/(1024.0*1024.0);
        r2 = r1/dt;
        r3 = smallio_count; r3 = r3 / dt;
        printf("Small Random Read: %lf MB/sec (%.2lf sec total using %lfMB or %.2lf ops/sec) \n", r2, dt, r1, r3);

        stime = apr_time_now();
        r1 = small_random_allocs(caps_list, readwrite_count, readwrite_size, small_read_fraction, smallio_count, small_min_size, small_max_size);
        dtime = apr_time_now() - stime;
        dt = dtime / (1.0 * APR_USEC_PER_SEC);
        r1 = r1/(1024.0*1024.0);
        r2 = r1/dt;
        r3 = smallio_count; r3 = r3 / dt;
        printf("Small Random R/W: %lf MB/sec (%.2lf sec total using %lfMB or %.2lf ops/sec) \n", r2, dt, r1, r3);
     }

     if (use_alias) {
        printf("Removing alias allocations...."); fflush(stdout);
        stime = apr_time_now();
        alias_remove_allocs(caps_list, base_caps, readwrite_count, readwrite_count);
        dtime = apr_time_now() - stime;
        dt = dtime / (1.0 * APR_USEC_PER_SEC);
        r1 = 1.0*readwrite_count/dt;
        printf(" %lf removes/sec (%.2lf sec total) \n", r1, dt);

        caps_list = base_caps;
     }

     printf("Removing allocations...."); fflush(stdout);
     stime = apr_time_now();
     remove_allocs(caps_list, readwrite_count);
     dtime = apr_time_now() - stime;
     dt = dtime / (1.0 * APR_USEC_PER_SEC);
     r1 = 1.0*readwrite_count/dt;
     printf(" %lf removes/sec (%.2lf sec total) \n", r1, dt);
     printf("\n");

  }  

  printf("Final network connection counter: %d\n", network_counter(NULL));

  ibp_finalize();  //** Shutdown IBP

  return(0);
}



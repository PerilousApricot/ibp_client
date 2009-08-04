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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "ibp.h"
#include "log.h"
#include "ibp.h"
#include "iovec_sync.h"
#include "fmttypes.h"

#define A_DURATION 900

IBP_DptInfo depotinfo;
struct ibp_depot *src_depot_list;
struct ibp_depot *dest_depot_list;
int src_n_depots;
int dest_n_depots;
int ibp_timeout;
int sync_transfer;
int nthreads;
int failed_tests;

typedef struct {
    char *buffer;
    int size;
    int pos;
    int nbytes;
} rw_arg_t;

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
// base_async_test - simple single allocation test of the async routines
//*************************************************************************

void base_async_test(int nthreads, ibp_depot_t *depot)
{
//  int size = 115;
//  int block = 10;
  int size = 10*1024*1024;
  int block = 5000;
//  char buffer[size+1];
//  char buffer_cmp[size+1];
  char *buffer, *buffer_cmp;
  char c;
  char block_buf[block+1];
  ibp_attributes_t attr;
  ibp_capset_t caps;
  ibp_cap_t *cap;
  int err, i, offset, bcount, remainder;
  ibp_op_t *op;
  oplist_t *iol;

  buffer = (char *)malloc(size+1);
  buffer_cmp = (char *)malloc(size+1);
  assert(buffer != NULL);
  assert(buffer_cmp != NULL);
  
  printf("base_async_test:  Starting simple test\n"); fflush(stdout);

  //** Create the list for handling the commands
  iol = new_ibp_oplist(NULL);
  oplist_start_execution(iol);  //** and start executing the commands

  //** Create the allocation used for test
  set_ibp_attributes(&attr, time(NULL) + A_DURATION, IBP_HARD, IBP_BYTEARRAY);
  op = new_ibp_alloc_op(&caps, size, depot, &attr, ibp_timeout, NULL, NULL);
  add_ibp_oplist(iol, op);
  err = oplist_waitall(iol);  
  if (err != IBP_OK) { 
     printf("base_async_test: ibp_allocate error! * ibp_errno=%d\n", err); 
     abort();
  }
  
  printf("base_async_test: rcap=%s\n", get_ibp_cap(&caps, IBP_READCAP));
  printf("base_async_test: wcap=%s\n", get_ibp_cap(&caps, IBP_WRITECAP));
  printf("base_async_test: mcap=%s\n", get_ibp_cap(&caps, IBP_MANAGECAP));

  //** Init the buffers
  buffer[size] = '\0'; memset(buffer, '*', size);
  buffer_cmp[size] = '\0'; memset(buffer_cmp, '_', size);
  block_buf[block] = '\0'; memset(block_buf, '0', block);

  //-------------------------------
  //** Do the initial upload
//  op = new_ibp_write_op(get_ibp_cap(&caps, IBP_WRITECAP), 0, size, buffer_cmp, ibp_timeout, NULL, NULL);
//  add_ibp_oplist(iol, op);
//  err = oplist_waitall(iol);
//  if (err != IBP_OK) {
//     printf("base_async_test: Initial ibp_write error! * ibp_errno=%d\n", err); 
//     abort();
//  }    

  bcount = size / (2*block);
  remainder = size - bcount * (2*block);

  //** Now do the striping **
  offset = (bcount-1)*(2*block);      // Now store the data in chunks
  cap = get_ibp_cap(&caps, IBP_WRITECAP);
  for (i=0; i<bcount; i++) {
     c = 'A' + (i%27);
     memset(&(buffer_cmp[offset]), c, 2*block);

     op = new_ibp_write_op(cap, offset, 2*block, &(buffer_cmp[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);
   
     offset = offset - 2*block;
  }

  if (remainder>0)  {
     offset = bcount*2*block;
     memset(&(buffer_cmp[offset]), '@', remainder);
     op = new_ibp_write_op(cap, offset, remainder, &(buffer_cmp[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);
  }

  //** Now wait for them to complete
  err = oplist_waitall(iol);
  if (err != IBP_OK) {
     printf("base_async_test: Error in stripe write! * ibp_errno=%d\n", err); 
     abort();
  }

  //-------------------------------
  bcount = size / block;
  remainder = size % block;

  //** Generate the Read list
  offset = 0;      // Now read the data in chunks
  cap = get_ibp_cap(&caps, IBP_READCAP);
  for (i=0; i<bcount; i++) {
     op = new_ibp_read_op(cap, offset, block, &(buffer[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);

     offset = offset + block;
  }

  if (remainder>0)  {
//printf("read remainder: rem=%d offset=%d\n", remainder, offset);
     op = new_ibp_read_op(cap, offset, remainder, &(buffer[offset]), ibp_timeout, NULL, NULL);
     add_ibp_oplist(iol, op);
  }

  //** Now wait for them to complete
  err = oplist_waitall(iol);
  if (err != IBP_OK) {
     printf("base_async_test: Error in stripe read! * ibp_errno=%d\n", err); 
     abort();
  }

  //-------------------------------
  
  //** Do the comparison **
  i = strcmp(buffer, buffer_cmp);
  if (i == 0) {
     printf("base_async_test: Success!\n");
  } else {
     failed_tests++;
     printf("base_async_test: Failed! strcmp = %d\n", i);
  }

//  printf("base_async_test: buffer_cmp=%s\n", buffer_cmp);
//  printf("base_async_test:     buffer=%s\n", buffer);
//  printf("base_async_test:block_buffer=%s\n", block_buf);


  //-------------------------------


  //** Remove the allocation **
  op = new_ibp_remove_op(get_ibp_cap(&caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  add_ibp_oplist(iol, op);
  err = oplist_waitall(iol);
  if (err != IBP_OK) { 
     printf("base_async_test: Error removing the allocation!  ibp_errno=%d\n", err); 
     abort(); 
  } 

//  free_oplist(iol);
  oplist_finished_submission(iol, OPLIST_AUTO_FREE);

  free(buffer);
  free(buffer_cmp);
}

//*********************************************************************************
// my_next_block - My routine for getting data
//*********************************************************************************

int my_next_block(int pos, void *arg, int *nbytes, char **buffer)
{
  rw_arg_t *a = (rw_arg_t *)arg;
  
  if ((a->pos + a->nbytes) >= a->size) {
     *nbytes = a->size - a->pos;
  } else {
     *nbytes = a->nbytes;
  }

//printf("pos=%d nbytes=%d\n", a->pos, *nbytes);  
  if (buffer != NULL) {
     if (a->pos < a->size) {
        *buffer = &(a->buffer[a->pos]);
     } else {
        *buffer = NULL;
     }
  }

  a->pos = a->pos + *nbytes;

  return(IBP_OK);
}

//*********************************************************************************
//  perform_user_rw_tests - Perform R/W tests using a user supplied callback
//          function for getting buffer/data
//*********************************************************************************

void perform_user_rw_tests(ibp_depot_t *depot)
{
  int bufsize = 1024*1024;
//  int bufsize = 100;
  char buffer[bufsize], rbuf[bufsize];
  ibp_op_t op;
  ibp_attributes_t attr;
  ibp_capset_t caps;
  rw_arg_t rw_arg;
  int err, i, b, nbytes, size;

  //** Fill the buffer **
  memset(rbuf, 0, bufsize);
  nbytes = 1024;
  for (i=0; i<bufsize; i=i+nbytes) {
     size = nbytes;
     if ((i+nbytes) >= bufsize) size = bufsize - i;
     b = i % 27;
     b = b + 'A';
     memset(&(buffer[i]), b, size);
  }  
  buffer[bufsize-1] = '\0';

  //*** Make the allocation used in the tests ***
  set_ibp_attributes(&attr, time(NULL) + A_DURATION, IBP_HARD, IBP_BYTEARRAY);
  set_ibp_alloc_op(&op, &caps, bufsize, depot, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_user_rw_tests:  Error creating initial allocation for tests!! error=%d\n", err);
     return;
  }

  //** Perform the upload **
  rw_arg.buffer = buffer;
  rw_arg.size = bufsize;
  rw_arg.pos = 0;
  rw_arg.nbytes = nbytes + 1;
  set_ibp_user_write_op(&op, get_ibp_cap(&caps, IBP_WRITECAP), 0, bufsize, my_next_block, (void *)&rw_arg, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) { 
     failed_tests++;
     printf("perform_user_rw_tests: Error during upload!  ibp_errno=%d\n", err); 
     return;
  } 

  //** Now download it with a different block size **
  rw_arg.buffer = rbuf;
  rw_arg.size = bufsize;
  rw_arg.pos = 0;
  rw_arg.nbytes = nbytes -1;
  set_ibp_user_read_op(&op, get_ibp_cap(&caps, IBP_READCAP), 0, bufsize, my_next_block, (void *)&rw_arg, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) { 
     failed_tests++;
     printf("perform_user_rw_tests: Error during upload!  ibp_errno=%d\n", err); 
     return;
  } 

  //** Check to see if they are the same ***
  if (strcmp(buffer, rbuf) == 0) {
     printf("perform_user_rw_tests: Success!\n");
  } else {
     failed_tests++;
     printf("perform_user_rw_tests: Failed!!!!!  buffers differ!\n");
     printf("perform_user_rw_tests: wbuf=%50s\n", buffer); 
     printf("perform_user_rw_tests: rbuf=%50s\n", rbuf); 
  }
  
  //** Remove the allocation **
  set_ibp_remove_op(&op, get_ibp_cap(&caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) { 
     printf("perform_user_rw_tests: Error removing the allocation!  ibp_errno=%d\n", err); 
     abort(); 
  } 

}

//*********************************************************************************
// perform_splitmerge_tests - Tests the ability to split/merge allocations
//*********************************************************************************

void perform_splitmerge_tests(ibp_depot_t *depot)
{
  int bufsize = 2048;
  char wbuf[bufsize+1], rbuf[bufsize+1];
  ibp_op_t op;
  ibp_attributes_t attr;
  ibp_capset_t mcaps, caps, caps2;
  ibp_capstatus_t probe;
  int nbytes, err, max_size, curr_size, dummy;
  ibp_timer_t timer;

  printf("perform_splitmerge_tests:  Starting tests!\n");

  set_ibp_timer(&timer, ibp_timeout, ibp_timeout);

  //** Initialize the buffers **
  memset(wbuf, '0', sizeof(wbuf));  wbuf[1023]='1'; wbuf[1024]='2'; wbuf[bufsize] = '\0';
  memset(rbuf, 0, sizeof(rbuf));
  nbytes = 1024;

  //*** Make the allocation used in the tests ***
  set_ibp_attributes(&attr, time(NULL) + A_DURATION, IBP_HARD, IBP_BYTEARRAY);
  set_ibp_alloc_op(&op, &mcaps, bufsize, depot, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error creating initial allocation for tests!! error=%d\n", err);
     return;
  }

  //** Fill the master with data **
  err = IBP_store(get_ibp_cap(&mcaps, IBP_WRITECAP), &timer, wbuf, bufsize);
  if (err != bufsize) {
     failed_tests++;
     printf("perform_splitmerge_tests: Error with master IBP_store! wrote=%d err=%d\n", err, IBP_errno);
  }    

  //** Split the allocation
  set_ibp_split_alloc_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), &caps, 1024, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error creating child allocation for tests!! error=%d\n", err);
     return;
  }
  
  //** Check the new size of the master
  set_ibp_probe_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), &probe, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error probing master allocation for tests!! error=%d\n", err);
     return;
  }

  get_ibp_capstatus(&probe, &dummy, &dummy, &curr_size, &max_size, &attr);
  if ((curr_size != 1024) && (max_size != 1024)) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with master allocation size!! curr_size=%d * max_size=%d should both be 1024\n", curr_size, max_size);
     return;
  }   

  //** Check the size of the child allocation
  set_ibp_probe_op(&op, get_ibp_cap(&caps, IBP_MANAGECAP), &probe, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error probing child allocation for tests!! error=%d\n", err);
     return;
  }

  get_ibp_capstatus(&probe, &dummy, &dummy, &curr_size, &max_size, &attr); 
  if ((curr_size != 0) && (max_size != 1024)) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with master allocation size!! curr_size=%d * max_size=%d should be 0 and 1024\n", curr_size, max_size);
     return;
  }   

  //** Verify the master data
  wbuf[1024] = '\0';
  err = IBP_load(get_ibp_cap(&mcaps, IBP_READCAP), &timer, rbuf, 1024, 0);
  if (err == 1024) {
     if (strcmp(rbuf, wbuf) != 0) {
        failed_tests++;
        printf("Read some data with the mastercap but it wasn't correct!\n");
        printf("Original=%s\n", wbuf);
        printf("     Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("Oops! Failed reading master cap! err=%d\n", err);
  }

  //** Load data into the child
  err = IBP_store(get_ibp_cap(&caps, IBP_WRITECAP), &timer, wbuf, 1024);
  if (err != 1024) {
     failed_tests++;
     printf("perform_splitmerge_tests: Error with child IBP_store! wrote=%d err=%d\n", err, IBP_errno);
  }    

  //** Read it back
  memset(rbuf, 0, sizeof(rbuf));
  err = IBP_load(get_ibp_cap(&caps, IBP_READCAP), &timer, rbuf, 1024, 0);
  if (err == 1024) {
     if (strcmp(rbuf, wbuf) != 0) {
        failed_tests++;
        printf("Read some data with the childcap but it wasn't correct!\n");
        printf("Original=%s\n", wbuf);
        printf("     Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("Oops! Failed reading child cap! err=%d\n", err);
  }

  //** Split the master again but htis time make it to big so it should fail
  set_ibp_split_alloc_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), &caps2, 2048, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err == IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error created child allocation when I shouldn't have! error=%d\n", err);
     return;
  }

  //** Check the size of the master to make sure it didn't change
  set_ibp_probe_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), &probe, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error probing master allocation2 for tests!! error=%d\n", err);
     return;
  }

  get_ibp_capstatus(&probe, &dummy, &dummy, &curr_size, &max_size, &attr);
  if ((curr_size != 1024) && (max_size != 1024)) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with master allocation size2!! curr_size=%d * max_size=%d should both be 1024\n", curr_size, max_size);
     return;
  }   

//GOOD!!!!!!!!!!!!!!!!!

  //** Merge the 2 allocations
  set_ibp_merge_alloc_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), get_ibp_cap(&caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with merge! error=%d\n", err);
     return;
  }

  //** Verify the child is gone
  set_ibp_probe_op(&op, get_ibp_cap(&caps, IBP_MANAGECAP), &probe, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err == IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Oops!  Child allocation is available after merge! ccap=%s\n", get_ibp_cap(&caps, IBP_MANAGECAP));
     return;
  }


  //** Verify the max/curr size of the master
  set_ibp_probe_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), &probe, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with probe of mcapafter mergs! ccap=%s err=%d\n", get_ibp_cap(&mcaps, IBP_MANAGECAP), err);
     return;
  }

//GOOD!!!!!!!!!!!!!!!!!

  get_ibp_capstatus(&probe, &dummy, &dummy, &curr_size, &max_size, &attr);
  if ((curr_size != 1024) && (max_size != 2048)) {
     failed_tests++;
     printf("perform_splitmerge_tests:  Error with master allocation size after merge!! curr_size=%d * max_size=%d should both 1024 and 2048\n", curr_size, max_size);
     return;
  }   

  //** Lastly Remove the master allocation **
  set_ibp_remove_op(&op, get_ibp_cap(&mcaps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     printf("perform_splitmerge_tests: Error removing master allocation!  ibp_errno=%d\n", err);
  }

  printf("perform_splitmerge_tests:  Passed!\n");
}

//*********************************************************************************
// perform_pushpull_tests - Tests the ability to perform push/pull copy operations
//*********************************************************************************

void perform_pushpull_tests(ibp_depot_t *depot1, ibp_depot_t *depot2)
{
  int bufsize = 2048;
  char wbuf[bufsize+1], rbuf[bufsize+1];
  ibp_op_t op;
  ibp_attributes_t attr;
  ibp_capset_t caps1, caps2;
  int nbytes, err;
  ibp_timer_t timer;
  int start_nfailed = failed_tests;

  printf("perform_pushpull_tests:  Starting tests!\n");

  set_ibp_timer(&timer, ibp_timeout, ibp_timeout);

  //** Initialize the buffers **
  memset(wbuf, '0', sizeof(wbuf));  wbuf[1023]='1'; wbuf[1024]='2'; wbuf[bufsize] = '\0';
  memset(rbuf, 0, sizeof(rbuf));
  nbytes = 1024;

  //*** Make the allocation used in the tests ***
  set_ibp_attributes(&attr, time(NULL) + A_DURATION, IBP_HARD, IBP_BYTEARRAY);
  set_ibp_alloc_op(&op, &caps1, bufsize, depot1, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error creating allocation 1 for tests!! error=%d\n", err);
     return;
  }

  set_ibp_alloc_op(&op, &caps2, bufsize, depot2, &attr, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error creating allocation 2 for tests!! error=%d\n", err);
     return;
  }

  //** Fill caps1="1" with data **
  err = IBP_store(get_ibp_cap(&caps1, IBP_WRITECAP), &timer, "1", 1);
  if (err != 1) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with master IBP_store! wrote=%d err=%d\n", err, IBP_errno);
  }    

  //** Append it to cap2="1"
  set_ibp_copy_op(&op, IBP_PUSH, NS_TYPE_SOCK, NULL, get_ibp_cap(&caps1, IBP_READCAP), 
          get_ibp_cap(&caps2, IBP_WRITECAP), 0, -1, 1, ibp_timeout, ibp_timeout, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with copy 1!! error=%d\n", err);
     return;
  }

  //** Append cap2 to cap1="11"
  set_ibp_copy_op(&op, IBP_PULL, NS_TYPE_SOCK, NULL, get_ibp_cap(&caps1, IBP_WRITECAP), 
          get_ibp_cap(&caps2, IBP_READCAP), -1, 0, 1, ibp_timeout, ibp_timeout, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with copy 2!! error=%d\n", err);
     return;
  }

  //** Append it to cap2="111"
  set_ibp_copy_op(&op, IBP_PUSH, NS_TYPE_SOCK, NULL, get_ibp_cap(&caps1, IBP_READCAP), 
          get_ibp_cap(&caps2, IBP_WRITECAP), 0, -1, 2, ibp_timeout, ibp_timeout, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with copy 3!! error=%d\n", err);
     return;
  }

  //** Change  caps1="123"
  err = IBP_write(get_ibp_cap(&caps1, IBP_WRITECAP), &timer, "23", 2, 1);
  if (err != 2) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with IBP_store 2! wrote=%d err=%d\n", err, IBP_errno);
  }    

  //** offset it to also make cap2="123"
  set_ibp_copy_op(&op, IBP_PUSH, NS_TYPE_SOCK, NULL, get_ibp_cap(&caps1, IBP_READCAP), 
          get_ibp_cap(&caps2, IBP_WRITECAP), 1, 1, 2, ibp_timeout, ibp_timeout, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Error with copy 4!! error=%d\n", err);
     return;
  }

  //** Now read them back and check them
  //** verify caps1
  memset(rbuf, 0, sizeof(rbuf));
  memcpy(wbuf, "123", 4);
  err = IBP_load(get_ibp_cap(&caps1, IBP_READCAP), &timer, rbuf, 3, 0);
  if (err == 3) {
     if (strcmp(rbuf, wbuf) != 0) {
        failed_tests++;
        printf("perform_pushpull_tests: Read some data with the cap1 but it wasn't correct!\n");
        printf("perform_pushpull_tests: Original=%s\n", wbuf);
        printf("perform_pushpull_tests:      Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Failed reading cap1! err=%d\n", err);
  }

  //** and also caps2
  memset(rbuf, 0, sizeof(rbuf));
  err = IBP_load(get_ibp_cap(&caps2, IBP_READCAP), &timer, rbuf, 3, 0);
  if (err == 3) {
     if (strcmp(rbuf, wbuf) != 0) {
        failed_tests++;
        printf("perform_pushpull_tests: Read some data with the cap2 but it wasn't correct!\n");
        printf("perform_pushpull_tests: Original=%s\n", wbuf);
        printf("perform_pushpull_tests:      Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("perform_pushpull_tests: Oops! Failed reading cap2! err=%d\n", err);
  }

  //** Lastly Remove the allocations **
  set_ibp_remove_op(&op, get_ibp_cap(&caps1, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     printf("perform_pushpull_tests: Oops! Error removing allocation 1!  ibp_errno=%d\n", err);
  }
  set_ibp_remove_op(&op, get_ibp_cap(&caps2, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     printf("perform_pushpull_tests: Oops! Error removing allocation 2!  ibp_errno=%d\n", err);
  }

  if (start_nfailed == failed_tests) {
     printf("perform_pushpull_tests: Passed!\n");
  } else {
     printf("perform_pushpull_tests: Oops! FAILED!\n");
  }
}

//*********************************************************************************
//*********************************************************************************
//*********************************************************************************

int main(int argc, char **argv)
{
  ibp_depotinfo_t *depotinfo;
  ibp_depot_t depot1, depot2;
  ibp_attributes_t attr;
  ibp_timer_t timer;
  ibp_capset_t *caps, *caps2, *caps4;
  ibp_capset_t caps3, caps5;
  ibp_capstatus_t astat;
  ibp_alias_capstatus_t alias_stat;
  int err, i, len, offset;
  int bufsize = 1024*1024;
  char wbuf[bufsize];
  char rbuf[bufsize];
  char *host1, *host2;
  int port1, port2;
  rid_t rid1, rid2;

  failed_tests = 0;

  if (argc < 6) {
     printf("ibp_test [-d loglevel] [-config ibp.cfg] host1 port1 rid1 host2 port2 rid2\n");
     printf("\n");
     printf("     2 depots are requred to test depot-depot copies.\n");
     printf("     Can use the same depot if necessary\n");
     printf("\n");
     printf("\n");
     return(-1);
  }

  ibp_init();  //** Initialize IBP


  //*** Read in the arguments ***    
  i = 1;
  if (strcmp(argv[i], "-d") == 0) {
     i++;
     set_log_level(atoi(argv[i]));
     i++;
  }

  if (strcmp(argv[i], "-config") == 0) { //** Read the config file
     i++;
     ibp_load_config(argv[i]);
     i++;
  }

  host1 = argv[i];  i++;
  port1 = atoi(argv[i]); i++;
  rid1 = ibp_str2rid(argv[i]); i++;  

  host2 = argv[i];  i++;
  port2 = atoi(argv[i]); i++;
  rid2 = ibp_str2rid(argv[i]); i++;  

  //*** Print the ibp client version ***
  printf("\n");
  printf("================== IBP Client Version =================\n");
  printf("%s\n", ibp_client_version());

  //*** Init the structures ***
  ibp_timeout = 5;
  set_ibp_depot(&depot1, host1, port1, rid1);
  set_ibp_depot(&depot2, host2, port2, rid2);
  set_ibp_attributes(&attr, time(NULL) + 60, IBP_HARD, IBP_BYTEARRAY); 
  set_ibp_timer(&timer, ibp_timeout, ibp_timeout);

//printf("Before allocate\n"); fflush(stdout);

  //*** Perform single allocation
  caps = IBP_allocate(&depot1, &timer, bufsize, &attr);

  if (caps == NULL) {
     printf("Error!!!! ibp_errno = %d\n", IBP_errno);
     return(1);
  } else {
     printf("Read: %s\n", caps->readCap);
     printf("Write: %s\n", caps->writeCap);
     printf("Manage: %s\n", caps->manageCap);
  }  

  printf("ibp_manage(IBP_PROBE):-----------------------------------\n");
  memset(&astat, 0, sizeof(astat));
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("read count = %d\n", astat.readRefCount);
     printf("write count = %d\n", astat.writeRefCount);
     printf("current size = %d\n", astat.currentSize);
     printf("max size = %lu\n", astat.maxSize);
     printf("duration = %ld\n", astat.attrib.duration - time(NULL));
     printf("reliability = %d\n", astat.attrib.reliability);
     printf("type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }


  printf("ibp_manage(IBP_DECR for write cap):-----------------------------------\n");
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_DECR, IBP_WRITECAP, &astat);
  if (err != 0) { printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno); }
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("read count = %d\n", astat.readRefCount);
     printf("write count = %d\n", astat.writeRefCount);
     printf("current size = %d\n", astat.currentSize);
     printf("max size = %lu\n", astat.maxSize);
     printf("duration = %lu\n", astat.attrib.duration - time(NULL));
     printf("reliability = %d\n", astat.attrib.reliability);
     printf("type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }


  printf("ibp_manage(IBP_CHNG - incresing size to 2MB and changing duration to 20 sec):-----------------------------------\n");
  set_ibp_attributes(&(astat.attrib), time(NULL) + 20, IBP_HARD, IBP_BYTEARRAY);
  astat.maxSize = 2*1024*1024;
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_CHNG, 0, &astat);
  if (err != 0) { printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno); }
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("read count = %d\n", astat.readRefCount);
     printf("write count = %d\n", astat.writeRefCount);
     printf("current size = %d\n", astat.currentSize);
     printf("max size = %lu\n", astat.maxSize);
     printf("duration = %lu\n", astat.attrib.duration - time(NULL));
     printf("reliability = %d\n", astat.attrib.reliability);
     printf("type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }

  //**** Basic Write tests ****
  printf("write tests..................................\n");
  for (i=0; i<bufsize; i++) wbuf[i] = '0';
  len = 3*bufsize/4;
  err = IBP_store(get_ibp_cap(caps, IBP_WRITECAP), &timer, wbuf, len);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_store1! wrote=%d err=%d\n", err, IBP_errno);
  }    

  len = bufsize - len;
  err = IBP_store(get_ibp_cap(caps, IBP_WRITECAP), &timer, wbuf, len);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_store2! wrote=%d err=%d\n", err, IBP_errno);
  }    

  for (i=0; i<bufsize; i++) wbuf[i] = '1';
  len = bufsize/2;
  offset = 10;
  err = IBP_write(get_ibp_cap(caps, IBP_WRITECAP), &timer, wbuf, len, offset);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_Write! wrote=%d err=%d\n", err, IBP_errno);
  }    


  printf("ibp_load test...............................\n");
  len = bufsize;
  offset = 0;
  err = IBP_load(get_ibp_cap(caps, IBP_READCAP), &timer, rbuf, len, offset);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_load! wrote=%d err=%d\n", err, IBP_errno);
  } else {
    rbuf[50] = '\0';
    printf("rbuf=%s\n", rbuf);
  }

  printf("ibp_copy test................................\n");
  //*** Perform single allocation
  caps2 = IBP_allocate(&depot2, &timer, bufsize, &attr);
  if (caps2 == NULL) {
     failed_tests++;
     printf("Error with allocation of dest cap!!!! ibp_errno = %d\n", IBP_errno);
     return(1);
  } else {
     printf("dest Read: %s\n", caps2->readCap);
     printf("dest Write: %s\n", caps2->writeCap);
     printf("dest Manage: %s\n", caps2->manageCap);
  }  
  err = IBP_copy(get_ibp_cap(caps, IBP_READCAP), get_ibp_cap(caps2, IBP_WRITECAP), &timer, &timer, 1024, 0);
  if (err != 1024) { failed_tests++; printf("ibp_copy size = %d * ibp_errno=%d\n", err, IBP_errno); }

  printf("ibp_phoebus_copy test................................\n");
  //*** Perform single allocation
  caps4 = IBP_allocate(&depot2, &timer, bufsize, &attr);
  if (caps4 == NULL) {
     printf("Error with allocation of dest cap!!!! ibp_errno = %d\n", IBP_errno);
     failed_tests++;
     return(1);
  } else {
     printf("dest Read: %s\n", caps4->readCap);
     printf("dest Write: %s\n", caps4->writeCap);
     printf("dest Manage: %s\n", caps4->manageCap);
  }  
  err = IBP_phoebus_copy(NULL, get_ibp_cap(caps, IBP_READCAP), get_ibp_cap(caps4, IBP_WRITECAP), &timer, &timer, 1024, 0);
  if (err != 1024) { failed_tests++; printf("ibp_copy size = %d * ibp_errno=%d\n", err, IBP_errno); }

  //** Remove the cap
  err = IBP_manage(get_ibp_cap(caps4, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { 
     failed_tests++;
     printf("Error deleting phoebus dest cap error = %d * ibp_errno=%d\n", err, IBP_errno); 
  }
  destroy_ibp_capset(caps4); caps4 = NULL;

  printf("ibp_manage(IBP_DECR):-Removing allocations----------------------------------\n");
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { failed_tests++; printf("ibp_manage(decr) for caps1 error = %d * ibp_errno=%d\n", err, IBP_errno); }
  err = IBP_manage(get_ibp_cap(caps2, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { failed_tests++; printf("ibp_manage(decr) for caps2 error = %d * ibp_errno=%d\n", err, IBP_errno); }

  printf("ibp_status: IBP_ST_INQ--------------------------------------------------------\n");
  depotinfo = IBP_status(&depot1, IBP_ST_INQ, &timer, "ibp", 10,11,12);
  if (depotinfo != NULL) {
     printf("rid=%d * duration=%ld\n", depotinfo->rid, depotinfo->Duration);
     printf("hc=" LL " hs=" LL " ha=" LL "\n",depotinfo->HardConfigured, depotinfo->HardServed, depotinfo->HardAllocable);
     printf("tc=" LL " ts=" LL " tu=" LL "\n", depotinfo->TotalConfigured, depotinfo->TotalServed, depotinfo->TotalUsed);
  } else {
     failed_tests++;
     printf("ibp_status error=%d\n", IBP_errno);
  }

  //** Perform some basic async R/W alloc/remove tests
  base_async_test(2, &depot1);

  //** Now do a few of the extra tests for async only
  ibp_op_t op;

  //*** Print the depot version ***
  set_ibp_version_op(&op, &depot1, rbuf, sizeof(rbuf), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err == IBP_OK) {
     printf("Printing depot version information......................................\n");
     printf("%s\n", rbuf);
  } else {
     failed_tests++;
     printf("Error getting ibp_version. err=%d\n", err);
  }
  
  //*** Query the depot resources ***
  ibp_ridlist_t rlist;
  set_ibp_query_resources_op(&op, &depot1, &rlist, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err == IBP_OK) {
     printf("Number of resources: %d\n", ridlist_get_size(&rlist));
     for (i=0; i<ridlist_get_size(&rlist); i++) {
        printf("  %d: %s\n", i, ibp_rid2str(ridlist_get_element(&rlist, i), rbuf));
     }
  } else {
     failed_tests++;
     printf("Error querying depot resource list. err=%d\n", err);
  }  

  perform_user_rw_tests(&depot1);  //** Perform the "user" version of the R/W functions

  //-----------------------------------------------------------------------------------------------------
  //** check ibp_rename ****

  printf("Testing IBP_RENAME...............................................\n");
  caps = IBP_allocate(&depot1, &timer, bufsize, &attr);
  if (caps == NULL) {
     failed_tests++;
     printf("Error!!!! ibp_errno = %d\n", IBP_errno);
     return(1);
  } else {
     printf("Original Cap..............\n");
     printf("Read: %s\n", caps->readCap);
     printf("Write: %s\n", caps->writeCap);
     printf("Manage: %s\n", caps->manageCap);
  }  

  //** Upload the data
  char *data = "This is a test....";
  len = strlen(data)+1;
  err = IBP_store(get_ibp_cap(caps, IBP_WRITECAP), &timer, data, len);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_store1! wrote=%d err=%d\n", err, IBP_errno);
  }    

  //** Rename the allocation
  set_ibp_rename_op(&op, caps2, get_ibp_cap(caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error with ibp_rename. err=%d\n", err);
  } else {
     printf("Renamed Cap..............\n");
     printf("Read: %s\n", caps2->readCap);
     printf("Write: %s\n", caps2->writeCap);
     printf("Manage: %s\n", caps2->manageCap);
  }  
  
  //** Try reading the original which should fail
  rbuf[0] = '\0';
  err = IBP_load(get_ibp_cap(caps, IBP_READCAP), &timer, rbuf, len, 0);
  if (err != len) {
     printf("Can't read the original cap after the rename which is good!  Got err err=%d\n", err);
  } else {
     failed_tests++;
    printf("Oops!  The read of the original cap succeeded! rbuf=%s\n", rbuf);
  }

  //** Try reading with the new cap
  rbuf[0] = '\0';
  err = IBP_load(get_ibp_cap(caps2, IBP_READCAP), &timer, rbuf, len, 0);
  if (err == len) {
     if (strcmp(rbuf, data) == 0) {
        printf("Read using the new cap the original data!\n");
     } else {
        failed_tests++;
        printf("Read some data with the new cap but it wasn't correct!\n");
        printf("Original=%s\n", data);
        printf("     Got=%s\n", wbuf);
     }
  } else {
     failed_tests++;
     printf("Oops! Failed reading with new cap! err=%d\n", err);
  }

  //** Remove the cap
  err = IBP_manage(get_ibp_cap(caps2, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { 
     failed_tests++;
     printf("Error deleting new cap after rename caps2 error = %d * ibp_errno=%d\n", err, IBP_errno); 
  }
  printf("Completed ibp_rename test...........................\n");

  //-----------------------------------------------------------------------------------------------------
  //** check ibp_alias_allocate/manage ****

//**** GOOD
  printf("Testing IBP_alias_ALLOCATE/MANAGE...............................................\n");
  caps = IBP_allocate(&depot1, &timer, bufsize, &attr);
  if (caps == NULL) {
     failed_tests++;
     printf("Error!!!! ibp_errno = %d\n", IBP_errno);
     return(1);
  } else {
     printf("Original Cap..............\n");
     printf("Read: %s\n", caps->readCap);
     printf("Write: %s\n", caps->writeCap);
     printf("Manage: %s\n", caps->manageCap);
  }  

  set_ibp_alias_alloc_op(&op, caps2, get_ibp_cap(caps, IBP_MANAGECAP), 0, 0, 0, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error with ibp_alias_alloc. err=%d\n", err);
  } else {
     printf("Alias Cap..............\n");
     printf("Read: %s\n", caps2->readCap);
     printf("Write: %s\n", caps2->writeCap);
     printf("Manage: %s\n", caps2->manageCap);
  }  


  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("Actual cap info\n");
     printf(" read count = %d\n", astat.readRefCount);
     printf(" write count = %d\n", astat.writeRefCount);
     printf(" current size = %d\n", astat.currentSize);
     printf(" max size = %lu\n", astat.maxSize);
     printf(" duration = %lu\n", astat.attrib.duration - time(NULL));
     printf(" reliability = %d\n", astat.attrib.reliability);
     printf(" type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }

  err = IBP_manage(get_ibp_cap(caps2, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("Using alias to get actual cap info\n");
     printf(" read count = %d\n", astat.readRefCount);
     printf(" write count = %d\n", astat.writeRefCount);
     printf(" current size = %d\n", astat.currentSize);
     printf(" max size = %lu\n", astat.maxSize);
     printf(" duration = %lu\n", astat.attrib.duration - time(NULL));
     printf(" reliability = %d\n", astat.attrib.reliability);
     printf(" type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }

  set_ibp_alias_probe_op(&op, get_ibp_cap(caps2, IBP_MANAGECAP), &alias_stat, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error with ibp_alias_probe. err=%d\n", err);
  } else {
     printf("Alias stat..............\n");
     printf(" read count = %d\n", alias_stat.read_refcount);
     printf(" write count = %d\n", alias_stat.write_refcount);
     printf(" offset = " ST "\n", alias_stat.offset);
     printf(" size = " ST "\n", alias_stat.size);
     printf(" duration = %lu\n", alias_stat.duration - time(NULL));
  }

  set_ibp_alias_alloc_op(&op, &caps3, get_ibp_cap(caps, IBP_MANAGECAP), 10, 40, 0, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error with ibp_alias_alloc_op. err=%d\n", err);
  } else {
     printf("Alias Cap with range 10-50.............\n");
     printf("Read: %s\n", caps3.readCap);
     printf("Write: %s\n", caps3.writeCap);
     printf("Manage: %s\n", caps3.manageCap);
  }  

  err = IBP_manage(get_ibp_cap(&caps3, IBP_MANAGECAP), &timer, IBP_PROBE, 0, &astat);
  if (err == 0) {
     printf("Using limited alias to get actual cap info\n");
     printf(" read count = %d\n", astat.readRefCount);
     printf(" write count = %d\n", astat.writeRefCount);
     printf(" current size = %d\n", astat.currentSize);
     printf(" max size = %lu *** This should be 40\n", astat.maxSize);
     printf(" duration = %lu\n", astat.attrib.duration - time(NULL));
     printf(" reliability = %d\n", astat.attrib.reliability);
     printf(" type = %d\n", astat.attrib.type);
  } else {
     failed_tests++;
     printf("ibp_manage error = %d * ibp_errno=%d\n", err, IBP_errno);
  }

  printf("*append* using the full alias..................................\n");
  for (i=0; i<bufsize; i++) wbuf[i] = '0';
  err = IBP_store(get_ibp_cap(caps2, IBP_WRITECAP), &timer, wbuf, bufsize);
  if (err != bufsize) {
     failed_tests++;
     printf("Error with IBP_store! wrote=%d err=%d\n", err, IBP_errno);
  }    

  printf("write using the limited alias..................................\n");  
  data = "This is a test.";
  len = strlen(data)+1;
  err = IBP_write(get_ibp_cap(&caps3, IBP_WRITECAP), &timer, data, len, 0);
  if (err != len) {
     failed_tests++;
     printf("Error with IBP_Write! wrote=%d err=%d\n", err, IBP_errno);
  }    

  memcpy(&(wbuf[10]), data, strlen(data)+1);
  len = 10 + strlen(data)+1;
  err = IBP_load(get_ibp_cap(caps2, IBP_READCAP), &timer, rbuf, len, 0);
  if (err == len) {
     if (strcmp(rbuf, wbuf) == 0) {
        printf("Read using the new full alias the original data!\n");
        printf("  read=%s\n", rbuf);       
     } else {
        failed_tests++;
        printf("Read some data with the new cap but it wasn't correct!\n");
        printf("Original=%s\n", wbuf);
        printf("     Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("Oops! Failed reading with new cap! err=%d\n", err);
  }


  //** Try to R/W beyond the end of the limited cap **
  printf("attempting to R/W beyond the end of the limited alias......\n");
  data = "This is a test.";
  len = strlen(data)+1;
  err = IBP_write(get_ibp_cap(&caps3, IBP_WRITECAP), &timer, data, len, 35);
  if (err != len) {
     printf("Correctly got an IBP_Write error! wrote=%d err=%d\n", err, IBP_errno);
  } else {
     failed_tests++;
     printf("Oops! Was able to write beyond the end of the limited cap with new cap!\n");
  }

  err = IBP_load(get_ibp_cap(&caps3, IBP_READCAP), &timer, rbuf, len, 35);
  if (err != len) {
     printf("Correctly got an IBP_read error! wrote=%d err=%d\n", err, IBP_errno);
  } else {
     failed_tests++;
     printf("Oops! Was able to read beyond the end of the limited cap with new cap!\n");
  }

  //** Perform a alias->alias copy.  The src alias is restricted
  printf("Testing restricted alias->full alias depot-depot copy\n");
  caps4 = IBP_allocate(&depot1, &timer, bufsize, &attr);
  if (caps == NULL) {
     failed_tests++;
     printf("alias-alias allocate Error!!!! ibp_errno = %d\n", IBP_errno);
     return(1);
  } else {
     printf("Depot-Depot copy OriginalDestiniation Cap..............\n");
     printf("Read: %s\n", caps4->readCap);
     printf("Write: %s\n", caps4->writeCap);
     printf("Manage: %s\n", caps4->manageCap);
  }  

  set_ibp_alias_alloc_op(&op, &caps5, get_ibp_cap(caps4, IBP_MANAGECAP), 0, 0, 0, ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error with ibp_alias_alloc_op. err=%d\n", err);
  } else {
     printf("Destination Alias Cap with full range.............\n");
     printf("Read: %s\n", caps5.readCap);
     printf("Write: %s\n", caps5.writeCap);
     printf("Manage: %s\n", caps5.manageCap);
  }  

//BAD!!!!!!!!!!!
  //** Perform the copy
  data = "This is a test.";
  len = strlen(data)+1;
  err = IBP_copy(get_ibp_cap(&caps3, IBP_READCAP), get_ibp_cap(&caps5, IBP_WRITECAP), &timer, &timer, len, 0);
  if (err != len) { printf("ibp_copy size = %d * ibp_errno=%d\n", err, IBP_errno); }

  //** Load it back and verify **
  err = IBP_load(get_ibp_cap(&caps5, IBP_READCAP), &timer, rbuf, len, 0);
  if (err == len) {
     if (strcmp(rbuf, data) == 0) {
        printf("Read using the new full alias the original data!\n");
        printf("  read=%s\n", rbuf);       
     } else {
        failed_tests++;
        printf("Read some data with the new cap but it wasn't correct!\n");
        printf("Original=%s\n", data);
        printf("     Got=%s\n", rbuf);
     }
  } else {
     failed_tests++;
     printf("Oops! Failed reading with new cap! err=%d\n", err);
  }

  //** Remove the cap5 (full alias)
  set_ibp_alias_remove_op(&op, get_ibp_cap(&caps5, IBP_MANAGECAP), get_ibp_cap(caps4, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error dest deleting alias cap error = %d\n", err); 
  }

  //** Remove the dest cap
  err = IBP_manage(get_ibp_cap(caps4, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { 
     failed_tests++;
     printf("Error deleting dest caps error = %d * ibp_errno=%d\n", err, IBP_errno); 
  }

  printf("completed alias depot->depot copy test\n");

  //** Try to remove the cap2 (full alias) with a bad cap
  set_ibp_alias_remove_op(&op, get_ibp_cap(caps2, IBP_MANAGECAP), get_ibp_cap(&caps3, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     printf("Correctly detected error deleting alias cap with an invalid master cap error = %d\n", err); 
  } else {
     failed_tests++;
     printf("Oops! Was able to delete the alias with an invalid master cap!!!!!!!!\n");
  }

  //** Remove the cap2 (full alias)
  set_ibp_alias_remove_op(&op, get_ibp_cap(caps2, IBP_MANAGECAP), get_ibp_cap(caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error deleting alias cap error = %d\n", err); 
  }

  printf("Try to read the deleted full falias.  This should generate an error\n");
  err = IBP_load(get_ibp_cap(caps2, IBP_READCAP), &timer, rbuf, len, 35);
  if (err != len) {
     printf("Correctly got an IBP_read error! wrote=%d err=%d\n", err, IBP_errno);
  } else {
     failed_tests++;
     printf("Oops! Was able to write beyond the end of the limited cap with new cap!\n");
  }

  //** Remove the limited alias (cap3)
  set_ibp_alias_remove_op(&op, get_ibp_cap(&caps3, IBP_MANAGECAP), get_ibp_cap(caps, IBP_MANAGECAP), ibp_timeout, NULL, NULL);
  err = ibp_sync_command(&op);
  if (err != IBP_OK) {
     failed_tests++;
     printf("Error deleting the limited alias cap  error = %d\n", err); 
  }

  //** Remove the original cap
  err = IBP_manage(get_ibp_cap(caps, IBP_MANAGECAP), &timer, IBP_DECR, IBP_READCAP, &astat);
  if (err != 0) { 
     failed_tests++;
     printf("Error deleting original caps error = %d * ibp_errno=%d\n", err, IBP_errno); 
  }

//GOOD!!!!!!!!!!!!!!!!!!!!

  printf("finished testing IBP_alias_ALLOCATE/MANAGE...............................................\n");

  perform_splitmerge_tests(&depot1);
  perform_pushpull_tests(&depot1, &depot2);

  printf("\n\n");
  printf("Final network connection counter: %d\n", network_counter(NULL));
  printf("Tests that failed: %d\n", failed_tests);

  ibp_finalize();

  return(0);
}


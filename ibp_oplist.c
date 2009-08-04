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
#include "log.h"
#include "oplist.h"
#include "ibp.h"
#include "host_portal.h"

oplist_base_op_t *_ibp_get_base_op(void *op);
void _ibp_op_finalize(void *op);
void _ibp_op_free(void *op);
void _ibp_submit_op(oplist_t *oplist, void *op);
void sort_oplist(oplist_t *iolist);

static oplist_implementation_t _ibp_imp = {IBP_OK, IBP_E_GENERIC, NULL, 
        _ibp_get_base_op,
        _ibp_op_finalize,
        _ibp_op_free,
        sort_oplist, 
        NULL,
        _ibp_submit_op };


//*************************************************************

oplist_base_op_t *_ibp_get_base_op(void *op)
{
  return(&(((ibp_op_t *)op)->bop));
}

//*************************************************************

void _ibp_op_finalize(void *op)
{
  finalize_ibp_op((ibp_op_t *)op);
}

//*************************************************************

void _ibp_op_free(void *op)
{
   free_ibp_op((ibp_op_t *)op);
}

//*************************************************************

void _ibp_submit_op(oplist_t *oplist, void *op)
{
 log_printf(15, "_ibp_submit_op: hpc=%p hpc->tablle=%p\n", _hpc_config, _hpc_config->table);

  submit_hp_op(_hpc_config, oplist, op);
}

//*************************************************************
// init_ibp_oplist - Initializes a task list container
//*************************************************************

void init_ibp_oplist(oplist_t *iol, oplist_app_notify_t *an)
{  
  init_oplist(iol, &_ibp_imp, an);
}

//*************************************************************
// new_ibp_oplist - Generates a new IBP op task list
//*************************************************************

oplist_t *new_ibp_oplist(oplist_app_notify_t *an)
{
  return(new_oplist(&_ibp_imp, an));
}

//*************************************************************
// add_ibp_oplist - Adds an operation to the iolist
//*************************************************************

int add_ibp_oplist(oplist_t *iolist, ibp_op_t *iop)
{
  return(add_oplist(iolist, (void *)iop));
}

//*************************************************************
// ibp_get_failed_op - returns a failed op from the provided oplist
//      or NULL if none exist.
//*************************************************************

ibp_op_t *ibp_get_failed_op(oplist_t *oplist)
{
  return((ibp_op_t *)pop(oplist->failed));
}

//*************************************************************
// compare_ops - Compares the ops for sorting
//*************************************************************

int compare_ops(const void *arg1, const void *arg2)
{
  int cmp;
  ibp_op_t *op1, *op2, **i1;

  i1 = (ibp_op_t **)arg1; op1 = *i1;
  i1 = (ibp_op_t **)arg2; op2 = *i1;

  cmp = strcmp(op1->hop.hostport, op2->hop.hostport);
  if (cmp == 0) {  //** Same depot so compare size
     if (op1->hop.cmp_size > op2->hop.cmp_size) {
        cmp = 1;
     } else if (op1->hop.cmp_size < op2->hop.cmp_size) {
        cmp = -1;
     }     
  }

  return(cmp);
}

//*************************************************************
// sort_oplist - Sorts the IO list to group same depot ops
//   together in descending amount of work.
//*************************************************************

void sort_oplist(oplist_t *iolist)
{
  int i, n;

  ibp_op_t **array = (ibp_op_t **)malloc(sizeof(ibp_op_t *)*stack_size(iolist->list));
  if (array == NULL) return;

  //**Create the linear array used for qsort
  n = stack_size(iolist->list);
  for (i=0; i<n; i++) {
    array[i] = (ibp_op_t *)pop(iolist->list);
//    log_printf(15, "sort_oplist: initial i=%d cap=%s len=%d\n", i, array[i]->hop.cmpstr, array[i]->hop.cmp_size);   
  }

  //** Now sort the linear array **
  qsort((void *)array, n, sizeof(ibp_op_t *), compare_ops);

  //** Now place it back on the list **
  for (i=0; i<n; i++) {
    push(iolist->list, (void *)array[i]);
    log_printf(15, "sort_io_list: i=%d hostdepot=%s size=%d\n", i, array[i]->hop.hostport, array[i]->hop.cmp_size);
  }  

  free(array);
}


//*************************************************************
// ibp_waitany - Waits until any task in the list completes
//   and returns a completed task
//*************************************************************

ibp_op_t *ibp_waitany(oplist_t *oplist)
{
  return((ibp_op_t *)oplist_waitany(oplist));
}

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


#ifndef _IBP_TYPES_H_
#define _IBP_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {    //*** Holds the data for the different network connection types
   int type;           //** Type of connection as defined in network.h
   void *data;         //** Generic container for context data
} ibp_connect_context_t;

typedef struct ibp_attributes ibp_attributes_t;
typedef struct ibp_depot ibp_depot_t;
typedef struct ibp_dptinfo ibp_depotinfo_t;
typedef struct ibp_timer ibp_timer_t;
typedef struct ibp_capstatus ibp_capstatus_t;
typedef char ibp_cap_t;
typedef struct ibp_set_of_caps ibp_capset_t;

typedef struct {  //** RID list structure
   int n;
   rid_t *rl;
} ibp_ridlist_t;

typedef struct {  //** Alias cap status
  int read_refcount;
  int write_refcount;
  size_t offset;
  size_t size;
  long int duration;
} ibp_alias_capstatus_t;

//*** ibp_types.c **
ibp_depot_t *new_ibp_depot();
void destroy_ibp_depot(ibp_depot_t *d);
void set_ibp_depot(ibp_depot_t *d, char *host, int port, rid_t rid);
ibp_attributes_t *new_ibp_attributes();
void destroy_ibp_attributes(ibp_attributes_t *attr);
void set_ibp_attributes(ibp_attributes_t *attr, time_t duration, int reliability, int type);
void get_ibp_attributes(ibp_attributes_t *attr, time_t *duration, int *reliability, int *type);
ibp_timer_t *new_ibp_timer();
void destroy_ibp_timer(ibp_timer_t *t);
void set_ibp_timer(ibp_timer_t *t, int client_timeout, int server_timeout);
void get_ibp_timer(ibp_timer_t *t, int *client_timeout, int *server_timeout);
void destroy_ibp_cap(ibp_cap_t *cap);
ibp_cap_t *dup_ibp_cap(ibp_cap_t *src);
ibp_capset_t *new_ibp_capset();
void destroy_ibp_capset(ibp_capset_t *caps);
void copy_ibp_capset(ibp_capset_t *src, ibp_capset_t *dest);
ibp_cap_t *get_ibp_cap(ibp_capset_t *caps, int ctype);
ibp_capstatus_t *new_ibp_capstatus();
void destroy_ibp_capstatus(ibp_capstatus_t *cs);
void copy_ibp_capstatus(ibp_capstatus_t *src, ibp_capstatus_t *dest);
void get_ibp_capstatus(ibp_capstatus_t *cs, int *readcount, int *writecount,
    int *current_size, int *max_size, ibp_attributes_t *attrib);
ibp_alias_capstatus_t *new_ibp_alias_capstatus();
void destroy_ibp_alias_capstatus(ibp_alias_capstatus_t *cs);
void copy_ibp_alias_capstatus(ibp_alias_capstatus_t *src, ibp_alias_capstatus_t *dest);
void get_ibp_alias_capstatus(ibp_alias_capstatus_t *cs, int *readcount, int *writecount,
    int *offset, int *size, int *duration);
void ridlist_init(ibp_ridlist_t *rlist, int size);
void ridlist_destroy(ibp_ridlist_t *rlist);
int ridlist_get_size(ibp_ridlist_t *rlist);
rid_t ridlist_get_element(ibp_ridlist_t *rlist, int index);
char *ibp_rid2str(rid_t rid, char *buffer);
rid_t ibp_str2rid(char *rid_str);
void ibp_empty_rid(rid_t *rid);

#ifdef __cplusplus
}
#endif

#endif

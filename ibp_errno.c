/*
 *           IBP Client version 1.0:  Internet BackPlane Protocol
 *               University of Tennessee, Knoxville TN.
 *          Authors: Y. Zheng A. Bassi, W. Elwasif, J. Plank, M. Beck
 *                   (C) 1999 All Rights Reserved
 *
 *                              NOTICE
 *
 * Permission to use, copy, modify, and distribute this software and
 * its documentation for any purpose and without fee is hereby granted
 * provided that the above copyright notice appear in all copies and
 * that both the copyright notice and this permission notice appear in
 * supporting documentation.
 *
 * Neither the Institution (University of Tennessee) nor the Authors 
 * make any representations about the suitability of this software for 
 * any purpose. This software is provided ``as is'' without express or
 * implied warranty.
 *
 */

// *** Modified by Alan Tackett on 7/7/2008 for sync/async compatiblity

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

pthread_once_t  errno_once = PTHREAD_ONCE_INIT;
static pthread_key_t errno_key;

void _set_errno( int err ) { errno = err; }
int _get_errno () { return(errno) ; }

void _errno_destructor( void *ptr)	{ free(ptr);}
void _errno_once(void) { pthread_key_create(&errno_key,_errno_destructor);}
int *_IBP_errno() 
{
  int *output;

  pthread_once(&errno_once,_errno_once);
  output = (int *)pthread_getspecific(errno_key);
  if (output == NULL ){
     output = (int*)calloc(1,sizeof(int));
     pthread_setspecific(errno_key,output);
  }

  return(output);
}


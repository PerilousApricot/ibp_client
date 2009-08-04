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

//*********************************************************************
//*********************************************************************

#include <sys/types.h>
//#include <sys/socket.h>
//#include <netinet/in.h>
//#include <netinet/tcp.h>
//#include <arpa/inet.h>
//#include <sys/uio.h>
//#include <netdb.h>
//#include <unistd.h>
#include <apr_network_io.h>
#include <apr_poll.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include "network.h"
#include "debug.h"
#include "log.h"
#include "dns_cache.h"
#include "fmttypes.h"
#include "net_sock.h"
//#include "net_fd.h"

//*********************************************************************
// sock_set_peer - Gets the remote sockets hostname 
//*********************************************************************

void sock_set_peer(net_sock_t *nsock, char *address, int add_size)
{
   network_sock_t *sock = (network_sock_t *)nsock;   
   apr_sockaddr_t *sa;

   address[0] = '\0';
   if (sock == NULL) return;

   if (apr_socket_addr_get(&sa, APR_REMOTE, sock->fd) != APR_SUCCESS) return;
   apr_sockaddr_ip_getbuf(address, add_size, sa);

   return;
}

//*********************************************************************
//  sock_status - Returns 1 if the socket is connected and 0 otherwise
//*********************************************************************

int sock_status(net_sock_t *nsock)
{
  network_sock_t *sock = (network_sock_t *)nsock;   
  if (sock == NULL) return(0);

  return(1);
}

//*********************************************************************
//  sock_close - Base socket close call
//*********************************************************************

int sock_close(net_sock_t *nsock)
{
  network_sock_t *sock = (network_sock_t *)nsock;   

  if (sock == NULL) return(0);

//log_printf(15, "sock_close: closing fd=%d\n", sock->fd); 

  apr_socket_close(sock->fd);
  if (sock->pollset != NULL) apr_pollset_destroy(sock->pollset);
  apr_pool_destroy(sock->mpool);

  free(sock);

  return(0);
}

//*********************************************************************
//  sock_write
//*********************************************************************

long int sock_write(net_sock_t *nsock, const void *buf, size_t count, Net_timeout_t tm)
{
  int err;
  apr_size_t nbytes;
  network_sock_t *sock = (network_sock_t *)nsock;   

//if (sock == NULL) log_printf(15, "sock_write: sock == NULL\n");

  if (sock == NULL) return(-1);   //** If closed return
  
  apr_socket_timeout_set(sock->fd, tm);
  nbytes = count;  
  err = apr_socket_send(sock->fd, buf, &nbytes);
  if ((err != APR_SUCCESS) && (err != APR_TIMEUP)) nbytes = -1;
//log_printf(15, "sock_write: count=" ST " nbytes=%ld\n", count, nbytes);
  return(nbytes);
}

//*********************************************************************
//  sock_read
//*********************************************************************

long int sock_read(net_sock_t *nsock, void *buf, size_t count, Net_timeout_t tm)
{
  int err;
  apr_size_t nbytes;
  network_sock_t *sock = (network_sock_t *)nsock;   

  if (sock == NULL) return(-1);   //** If closed return

  apr_socket_timeout_set(sock->fd, tm);
  nbytes = count;  
  err = apr_socket_recv(sock->fd, buf, &nbytes);
  if ((err != APR_SUCCESS) && (err != APR_TIMEUP)) nbytes = -1;

//log_printf(15, "sock_read: count=" ST " nbytes=%ld err=%d\n", count, nbytes, err);
  return(nbytes);
}

//*********************************************************************
// sock_connect - Creates a connection to a remote host
//*********************************************************************

int sock_connect(net_sock_t *nsock, const char *hostname, int port, Net_timeout_t timeout)
{  
   int err;
   network_sock_t *sock = (network_sock_t *)nsock;   

   if (sock == NULL) return(-1);   //** If NULL exit

   if (sock->fd != NULL) apr_socket_close(sock->fd);
  
   sock->sa = NULL;
//log_printf(0, " sock_connect: hostname=%s:%d\n", hostname, port);
//   err = apr_sockaddr_info_get(&(sock->sa), hostname, APR_INET, port, APR_IPV4_ADDR_OK, sock->mpool);
   err = apr_sockaddr_info_get(&(sock->sa), hostname, APR_INET, port, 0, sock->mpool);
//log_printf(0, "sock_connect: apr_sockaddr_info_get: err=%d\n", err);
//if (sock->sa == NULL) log_printf(0, "sock_connect: apr_sockaddr_info_get: sock->sa == NULL\n");

   if (err != APR_SUCCESS) return(err);
   
   err = apr_socket_create(&(sock->fd), APR_INET, SOCK_STREAM, APR_PROTO_TCP, sock->mpool);
//log_printf(0, "sock_connect: apr_sockcreate: err=%d\n", err);
   if (err != APR_SUCCESS) return(err);

   
//   apr_socket_opt_set(sock->fd, APR_SO_NONBLCK, 1);
   apr_socket_timeout_set(sock->fd, timeout);
   if (sock->tcpsize > 0) {
      apr_socket_opt_set(sock->fd, APR_SO_SNDBUF, sock->tcpsize);
      apr_socket_opt_set(sock->fd, APR_SO_RCVBUF, sock->tcpsize);
   }

   return(apr_socket_connect(sock->fd, sock->sa));
}

//*********************************************************************
// sock_connection_request - Waits for a connection request or times out 
//     If a request is made then 1 is returned otherwise 0 for timeout.
//     -1 signifies an error.
//*********************************************************************

int sock_connection_request(net_sock_t *nsock, int timeout)
{
  apr_int32_t n;
  const apr_pollfd_t *ret_fd;

  network_sock_t *sock = (network_sock_t *)nsock;   

  if (sock == NULL) return(-1);

  int err = apr_pollset_poll(sock->pollset, timeout*1000000, &n, &ret_fd);
  if (err == APR_SUCCESS) {
     return(1);
  } else {
     return(0);
  }
  return(-1);
}

//*********************************************************************
//  sock_accept - Accepts a socket request
//*********************************************************************

net_sock_t *sock_accept(net_sock_t *nsock)
{
  int err;
  network_sock_t *psock = (network_sock_t *)nsock;   

  network_sock_t *sock = (network_sock_t *)malloc(sizeof(network_sock_t));
  assert(sock != NULL);

  memset(sock, 0, sizeof(network_sock_t));

  err = apr_socket_accept(&(sock->fd), psock->fd, sock->mpool);
  if (err != APR_SUCCESS) {
     free(sock);
     sock = NULL;
  }

  return(sock);    
}

//*********************************************************************
//  sock_bind - Binds a socket to the requested port
//*********************************************************************

int sock_bind(net_sock_t *nsock, char *address, int port)
{
  int err;
  network_sock_t *sock = (network_sock_t *)nsock;   

  if (sock == NULL) return(1);

   err = apr_sockaddr_info_get(&(sock->sa), address, APR_INET, port, APR_IPV4_ADDR_OK, sock->mpool);
   if (err != APR_SUCCESS) return(err);
   
   err = apr_socket_create(&(sock->fd), APR_INET, SOCK_STREAM, APR_PROTO_TCP, sock->mpool);
   if (err != APR_SUCCESS) return(err);

   apr_socket_opt_set(sock->fd, APR_SO_NONBLOCK, 1);
   if (sock->tcpsize > 0) {
      apr_socket_opt_set(sock->fd, APR_SO_SNDBUF, sock->tcpsize);
      apr_socket_opt_set(sock->fd, APR_SO_RCVBUF, sock->tcpsize);
   }

  err = apr_socket_bind(sock->fd, sock->sa);

  return(err);
}

//*********************************************************************
//  sock_listen
//*********************************************************************

int sock_listen(net_sock_t *nsock, int max_pending)
{
  int err;
  network_sock_t *sock = (network_sock_t *)nsock;   

  if (sock == NULL) return(1);

  err = apr_socket_listen(sock->fd, max_pending);
  if (err != APR_SUCCESS) return(err);

  //** Create the polling info
  apr_pollset_create(&(sock->pollset), 1, sock->mpool, 0);
//  sock->pfd = { sock->mpool, APR_POLL_SOCKET, APR_POLLIN, 0, { NULL }, NULL };
  sock->pfd.p = sock->mpool;
  sock->pfd.desc_type = APR_POLL_SOCKET;
  sock->pfd.reqevents = APR_POLLIN;
  sock->pfd.rtnevents = 0;
  sock->pfd.desc.s = sock->fd;
  sock->pfd.client_data = NULL;

  apr_pollset_add(sock->pollset, &(sock->pfd));

  return(0);
}


//*********************************************************************
// ns_config_sock - Configure the connection to use standard sockets 
//*********************************************************************

void ns_config_sock(NetStream_t *ns, int tcpsize)
{
  log_printf(10, "ns_config_sock: ns=%d, \n", ns->id);

  _ns_init(ns, 0);

  ns->sock_type = NS_TYPE_SOCK;
  network_sock_t *sock = (network_sock_t *)malloc(sizeof(network_sock_t));
  assert(sock != NULL);
  memset(sock, 0, sizeof(network_sock_t));
  ns->sock = (net_sock_t *)sock;
  if (apr_pool_create(&(sock->mpool), NULL) != APR_SUCCESS) {
     return; 
  }
  
  sock->tcpsize = tcpsize;
  ns->connect = sock_connect;
  ns->sock_status = sock_status;
  ns->set_peer = sock_set_peer;
  ns->close = sock_close;
  ns->read = sock_read;
  ns->write = sock_write;
  ns->accept = sock_accept;
  ns->bind = sock_bind;
  ns->listen = sock_listen;
  ns->connection_request = sock_connection_request;
}


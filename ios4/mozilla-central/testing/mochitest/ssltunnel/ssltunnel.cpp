/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla test code
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ted Mielczarek <ted.mielczarek@gmail.com>
 *   Honza Bambas <honzab@firemni.cz>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * WARNING: DO NOT USE THIS CODE IN PRODUCTION SYSTEMS.  It is highly likely to
 *          be plagued with the usual problems endemic to C (buffer overflows
 *          and the like).  We don't especially care here (but would accept
 *          patches!) because this is only intended for use in our test
 *          harnesses in controlled situations where input is guaranteed not to
 *          be malicious.
 */

#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include "prinit.h"
#include "prerror.h"
#include "prio.h"
#include "prnetdb.h"
#include "prtpool.h"
#include "prtypes.h"
#include "nss.h"
#include "pk11func.h"
#include "key.h"
#include "keyt.h"
#include "ssl.h"
#include "plhash.h"

using std::string;
using std::vector;

#define IS_DELIM(m, c)          ((m)[(c) >> 3] & (1 << ((c) & 7)))
#define SET_DELIM(m, c)         ((m)[(c) >> 3] |= (1 << ((c) & 7)))
#define DELIM_TABLE_SIZE        32

// Copied from nsCRT
char* strtok2(char* string, const char* delims, char* *newStr)
{
  PR_ASSERT(string);
  
  char delimTable[DELIM_TABLE_SIZE];
  PRUint32 i;
  char* result;
  char* str = string;
  
  for (i = 0; i < DELIM_TABLE_SIZE; i++)
    delimTable[i] = '\0';
  
  for (i = 0; delims[i]; i++) {
    SET_DELIM(delimTable, static_cast<PRUint8>(delims[i]));
  }
  
  // skip to beginning
  while (*str && IS_DELIM(delimTable, static_cast<PRUint8>(*str))) {
    str++;
  }
  result = str;
  
  // fix up the end of the token
  while (*str) {
    if (IS_DELIM(delimTable, static_cast<PRUint8>(*str))) {
      *str++ = '\0';
      break;
    }
    str++;
  }
  *newStr = str;
  
  return str == result ? NULL : result;
}



enum client_auth_option {
  caNone = 0,
  caRequire = 1,
  caRequest = 2
};

// Structs for passing data into jobs on the thread pool
typedef struct {
  PRInt32 listen_port;
  string cert_nickname;
  PLHashTable* host_cert_table;
  PLHashTable* host_clientauth_table;
} server_info_t;

typedef struct {
  PRFileDesc* client_sock;
  PRNetAddr client_addr;
  server_info_t* server_info;
  // the original host in the Host: header for this connection is
  // stored here, for proxied connections
  string original_host;
  // true if no SSL should be used for this connection
  bool http_proxy_only;
  // true if this connection is for a WebSocket
  bool iswebsocket;
} connection_info_t;

typedef struct {
  string fullHost;
  bool matched;
} server_match_t;

const PRInt32 BUF_SIZE = 16384;
const PRInt32 BUF_MARGIN = 1024;
const PRInt32 BUF_TOTAL = BUF_SIZE + BUF_MARGIN;

struct relayBuffer
{
  char *buffer, *bufferhead, *buffertail, *bufferend;

  relayBuffer()
  {
    // Leave 1024 bytes more for request line manipulations
    bufferhead = buffertail = buffer = new char[BUF_TOTAL];
    bufferend = buffer + BUF_SIZE;
  }

  ~relayBuffer()
  {
    delete [] buffer;
  }

  void compact() {
    if (buffertail == bufferhead)
      buffertail = bufferhead = buffer;
  }

  bool empty() { return bufferhead == buffertail; }
  size_t free() { return bufferend - buffertail; }
  size_t margin() { return free() + BUF_MARGIN; }
  size_t present() { return buffertail - bufferhead; }
};

// A couple of stack classes for managing NSS/NSPR resources
class AutoCert {
public:
  AutoCert(CERTCertificate* cert) { cert_ = cert; }
  ~AutoCert() { if (cert_) CERT_DestroyCertificate(cert_); }
  operator CERTCertificate*() { return cert_; }
private:
  CERTCertificate* cert_;
};

class AutoKey {
public:
  AutoKey(SECKEYPrivateKey* key) { key_ = key; }
  ~AutoKey() { if (key_)   SECKEY_DestroyPrivateKey(key_); }
  operator SECKEYPrivateKey*() { return key_; }
private:
  SECKEYPrivateKey* key_;
};

class AutoFD {
public:
  AutoFD(PRFileDesc* fd) { fd_ = fd; }
  ~AutoFD() {
    if (fd_) {
      PR_Shutdown(fd_, PR_SHUTDOWN_BOTH);
      PR_Close(fd_);
    }
  }
  operator PRFileDesc*() { return fd_; }
  PRFileDesc* reset(PRFileDesc* newfd) {
    PRFileDesc* oldfd = fd_;
    fd_ = newfd;
    return oldfd;
  }
private:
  PRFileDesc* fd_;
};

// These are suggestions. If the number of ports to proxy on * 2
// is greater than either of these, then we'll use that value instead.
const PRUint32 INITIAL_THREADS = 1;
const PRUint32 MAX_THREADS = 5;
const PRUint32 DEFAULT_STACKSIZE = (512 * 1024);

// global data
string nssconfigdir;
vector<server_info_t> servers;
PRNetAddr remote_addr;
PRNetAddr websocket_server;
PRThreadPool* threads = NULL;
PRLock* shutdown_lock = NULL;
PRCondVar* shutdown_condvar = NULL;
// Not really used, unless something fails to start
bool shutdown_server = false;
bool do_http_proxy = false;
bool any_host_spec_config = false;

PR_CALLBACK PRIntn ClientAuthValueComparator(const void *v1, const void *v2)
{
  int a = *static_cast<const client_auth_option*>(v1) -
          *static_cast<const client_auth_option*>(v2);
  if (a == 0)
    return 0;
  if (a > 0)
    return 1;
  else // (a < 0)
    return -1;
}

static PRIntn match_hostname(PLHashEntry *he, PRIntn index, void* arg)
{
  server_match_t *match = (server_match_t*)arg;
  if (match->fullHost.find((char*)he->key) != string::npos)
    match->matched = true;
  return HT_ENUMERATE_NEXT;
}

/*
 * Signal the main thread that the application should shut down.
 */
void SignalShutdown()
{
  PR_Lock(shutdown_lock);
  PR_NotifyCondVar(shutdown_condvar);
  PR_Unlock(shutdown_lock);
}

bool ReadConnectRequest(server_info_t* server_info, 
    relayBuffer& buffer, PRInt32* result, string& certificate,
    client_auth_option* clientauth, string& host)
{
  if (buffer.present() < 4) {
    printf(" !! only %d bytes present in the buffer", (int)buffer.present());
    return false;
  }
  if (strncmp(buffer.buffertail-4, "\r\n\r\n", 4)) {
    printf(" !! request is not tailed with CRLFCRLF but with %x %x %x %x", 
           *(buffer.buffertail-4),
           *(buffer.buffertail-3),
           *(buffer.buffertail-2),
           *(buffer.buffertail-1));
    return false;
  }

  printf(" parsing initial connect request, dump:\n%.*s\n", (int)buffer.present(), buffer.bufferhead);

  *result = 400;

  char* token;
  char* _caret;
  token = strtok2(buffer.bufferhead, " ", &_caret);
  if (!token) {
    printf(" no space found");
    return true;
  }
  if (strcmp(token, "CONNECT")) {
    printf(" not CONNECT request but %s", token);
    return true;
  }

  token = strtok2(_caret, " ", &_caret);
  void* c = PL_HashTableLookup(server_info->host_cert_table, token);
  if (c)
    certificate = static_cast<char*>(c);

  host = "https://";
  host += token;

  c = PL_HashTableLookup(server_info->host_clientauth_table, token);
  if (c)
    *clientauth = *static_cast<client_auth_option*>(c);
  else
    *clientauth = caNone;

  token = strtok2(_caret, "/", &_caret);
  if (strcmp(token, "HTTP")) {  
    printf(" not tailed with HTTP but with %s", token);
    return true;
  }

  *result = 200;
  return true;
}

bool ConfigureSSLServerSocket(PRFileDesc* socket, server_info_t* si, string &certificate, client_auth_option clientAuth)
{
  const char* certnick = certificate.empty() ?
      si->cert_nickname.c_str() : certificate.c_str();

  AutoCert cert(PK11_FindCertFromNickname(
      certnick, NULL));
  if (!cert) {
    fprintf(stderr, "Failed to find cert %s\n", certnick);
    return false;
  }

  AutoKey privKey(PK11_FindKeyByAnyCert(cert, NULL));
  if (!privKey) {
    fprintf(stderr, "Failed to find private key\n");
    return false;
  }

  PRFileDesc* ssl_socket = SSL_ImportFD(NULL, socket);
  if (!ssl_socket) {
    fprintf(stderr, "Error importing SSL socket\n");
    return false;
  }

  SSLKEAType certKEA = NSS_FindCertKEAType(cert);
  if (SSL_ConfigSecureServer(ssl_socket, cert, privKey, certKEA)
      != SECSuccess) {
    fprintf(stderr, "Error configuring SSL server socket\n");
    return false;
  }

  SSL_OptionSet(ssl_socket, SSL_SECURITY, PR_TRUE);
  SSL_OptionSet(ssl_socket, SSL_HANDSHAKE_AS_CLIENT, PR_FALSE);
  SSL_OptionSet(ssl_socket, SSL_HANDSHAKE_AS_SERVER, PR_TRUE);

  if (clientAuth != caNone)
  {
    SSL_OptionSet(ssl_socket, SSL_REQUEST_CERTIFICATE, PR_TRUE);
    SSL_OptionSet(ssl_socket, SSL_REQUIRE_CERTIFICATE, clientAuth == caRequire);
  }

  SSL_ResetHandshake(ssl_socket, PR_TRUE);

  return true;
}

/**
 * This function examines the buffer for a S5ec-WebSocket-Location: field, 
 * and if it's present, it replaces the hostname in that field with the
 * value in the server's original_host field.  This function works
 * in the reverse direction as AdjustWebSocketHost(), replacing the real
 * hostname of a response with the potentially fake hostname that is expected
 * by the browser (e.g., mochi.test).
 *
 * @return true if the header was adjusted successfully, or not found, false
 * if the header is present but the url is not, which should indicate
 * that more data needs to be read from the socket
 */
bool AdjustWebSocketLocation(relayBuffer& buffer, connection_info_t *ci)
{
  assert(buffer.margin());
  buffer.buffertail[1] = '\0';

  char* wsloc = strstr(buffer.bufferhead, "Sec-WebSocket-Location:");
  if (!wsloc)
    return true;
  // advance pointer to the start of the hostname
  wsloc = strstr(wsloc, "ws://");
  if (!wsloc)
    return false;
  wsloc += 5;
  // find the end of the hostname
  char* wslocend = strchr(wsloc + 1, '/');
  if (!wslocend)
    return false;
  char *crlf = strstr(wsloc, "\r\n");
  if (!crlf)
    return false;
  if (ci->original_host.empty())
    return true;

  int diff = ci->original_host.length() - (wslocend-wsloc);
  if (diff > 0)
    assert(size_t(diff) <= buffer.margin());
  memmove(wslocend + diff, wslocend, buffer.buffertail - wsloc - diff);
  buffer.buffertail += diff;

  memcpy(wsloc, ci->original_host.c_str(), ci->original_host.length());
  return true;
}

/**
 * This function examines the buffer for a Host: field, and if it's present,
 * it replaces the hostname in that field with the hostname in the server's
 * remote_addr field.  This is needed because proxy requests may be coming
 * from mochitest with fake hosts, like mochi.test, and these need to be
 * replaced with the host that the destination server is actually running
 * on.
 */
bool AdjustWebSocketHost(relayBuffer& buffer, connection_info_t *ci)
{
  const char HEADER_UPGRADE[] = "Upgrade:";
  const char HEADER_HOST[] = "Host:";

  PRNetAddr inet_addr = (websocket_server.inet.port ? websocket_server :
    remote_addr);

  assert(buffer.margin());

  // Cannot use strnchr so add a null char at the end. There is always some
  // space left because we preserve a margin.
  buffer.buffertail[1] = '\0';

  // Verify this is a WebSocket header.
  char* h1 = strstr(buffer.bufferhead, HEADER_UPGRADE);
  if (!h1)
    return false;
  h1 += strlen(HEADER_UPGRADE);
  h1 += strspn(h1, " \t");
  char* h2 = strstr(h1, "WebSocket\r\n");
  if (!h2)
    return false;

  char* host = strstr(buffer.bufferhead, HEADER_HOST);
  if (!host)
    return false;
  // advance pointer to beginning of hostname
  host += strlen(HEADER_HOST);
  host += strspn(host, " \t");

  char* endhost = strstr(host, "\r\n");
  if (!endhost)
    return false;

  // Save the original host, so we can use it later on responses from the
  // server.
  ci->original_host.assign(host, endhost-host);

  char newhost[40];
  PR_NetAddrToString(&inet_addr, newhost, sizeof(newhost));
  assert(strlen(newhost) < sizeof(newhost) - 7);
  sprintf(newhost, "%s:%d", newhost, PR_ntohs(inet_addr.inet.port));

  int diff = strlen(newhost) - (endhost-host);
  if (diff > 0)
    assert(size_t(diff) <= buffer.margin());
  memmove(endhost + diff, endhost, buffer.buffertail - host - diff);
  buffer.buffertail += diff;

  memcpy(host, newhost, strlen(newhost));
  return true;
}

/**
 * This function prefixes Request-URI path with a full scheme-host-port
 * string.
 */
bool AdjustRequestURI(relayBuffer& buffer, string *host)
{
  assert(buffer.margin());

  // Cannot use strnchr so add a null char at the end. There is always some space left
  // because we preserve a margin.
  buffer.buffertail[1] = '\0';
  printf(" incoming request to adjust:\n%s\n", buffer.bufferhead);

  char *token, *path;
  path = strchr(buffer.bufferhead, ' ') + 1;
  if (!path)
    return false;

  // If the path doesn't start with a slash don't change it, it is probably '*' or a full
  // path already. Return true, we are done with this request adjustment.
  if (*path != '/')
    return true;

  token = strchr(path, ' ') + 1;
  if (!token)
    return false;

  if (strncmp(token, "HTTP/", 5))
    return false;

  size_t hostlength = host->length();
  assert(hostlength <= buffer.margin());

  memmove(path + hostlength, path, buffer.buffertail - path);
  memcpy(path, host->c_str(), hostlength);
  buffer.buffertail += hostlength;

  return true;
}

bool ConnectSocket(PRFileDesc *fd, const PRNetAddr *addr, PRIntervalTime timeout)
{
  PRStatus stat = PR_Connect(fd, addr, timeout);
  if (stat != PR_SUCCESS)
    return false;

  PRSocketOptionData option;
  option.option = PR_SockOpt_Nonblocking;
  option.value.non_blocking = PR_TRUE;
  PR_SetSocketOption(fd, &option);

  return true;
}

/*
 * Handle an incoming client connection. The server thread has already
 * accepted the connection, so we just need to connect to the remote
 * port and then proxy data back and forth.
 * The data parameter is a connection_info_t*, and must be deleted
 * by this function.
 */
void HandleConnection(void* data)
{
  connection_info_t* ci = static_cast<connection_info_t*>(data);
  PRIntervalTime connect_timeout = PR_SecondsToInterval(30);

  AutoFD other_sock(PR_NewTCPSocket());
  bool client_done = false;
  bool client_error = false;
  bool connect_accepted = !do_http_proxy;
  bool ssl_updated = !do_http_proxy;
  bool expect_request_start = do_http_proxy;
  string certificateToUse;
  client_auth_option clientAuth;
  string fullHost;

  printf("SSLTUNNEL(%p): incoming connection csock(0)=%p, ssock(1)=%p\n",
         static_cast<void*>(data),
         static_cast<void*>(ci->client_sock),
         static_cast<void*>(other_sock));
  if (other_sock) 
  {
    PRInt32 numberOfSockets = 1;

    relayBuffer buffers[2];

    if (!do_http_proxy)
    {
      if (!ci->http_proxy_only && 
          !ConfigureSSLServerSocket(ci->client_sock, ci->server_info, certificateToUse, caNone))
        client_error = true;
      else if (!ConnectSocket(other_sock, &remote_addr, connect_timeout))
        client_error = true;
      else
        numberOfSockets = 2;
    }

    PRPollDesc sockets[2] = 
    { 
      {ci->client_sock, PR_POLL_READ, 0},
      {other_sock, PR_POLL_READ, 0}
    };
    PRBool socketErrorState[2] = {PR_FALSE, PR_FALSE};

    while (!((client_error||client_done) && buffers[0].empty() && buffers[1].empty()))
    {
      sockets[0].in_flags |= PR_POLL_EXCEPT;
      sockets[1].in_flags |= PR_POLL_EXCEPT;
      printf("SSLTUNNEL(%p): polling flags csock(0)=%c%c, ssock(1)=%c%c\n",
             static_cast<void*>(data),
             sockets[0].in_flags & PR_POLL_READ  ? 'R' : '-',
             sockets[0].in_flags & PR_POLL_WRITE ? 'W' : '-',
             sockets[1].in_flags & PR_POLL_READ  ? 'R' : '-',
             sockets[1].in_flags & PR_POLL_WRITE ? 'W' : '-');
      PRInt32 pollStatus = PR_Poll(sockets, numberOfSockets, PR_MillisecondsToInterval(1000));
      if (pollStatus < 0)
      {
        printf("SSLTUNNEL(%p): pollStatus=%d, exiting\n",
               static_cast<void*>(data), pollStatus);
        client_error = true;
        break;
      }

      if (pollStatus == 0)
      {
        // timeout
        printf("SSLTUNNEL(%p): poll timeout, looping\n",
               static_cast<void*>(data));
        continue;
      }

      for (PRInt32 s = 0; s < numberOfSockets; ++s)
      {
        PRInt32 s2 = s == 1 ? 0 : 1;
        PRInt16 out_flags = sockets[s].out_flags;
        PRInt16 &in_flags = sockets[s].in_flags;
        PRInt16 &in_flags2 = sockets[s2].in_flags;
        sockets[s].out_flags = 0;

        printf("SSLTUNNEL(%p): %csock(%d)=%p out_flags=%d",
               static_cast<void*>(data),
               s == 0 ? 'c' : 's',
               s,
               static_cast<void*>(sockets[s].fd),
               out_flags);
        if (out_flags & (PR_POLL_EXCEPT | PR_POLL_ERR | PR_POLL_HUP))
        {
          printf(" :exception\n");
          client_error = true;
          socketErrorState[s] = PR_TRUE;
          // We got a fatal error state on the socket. Clear the output buffer
          // for this socket to break the main loop, we will never more be able
          // to send those data anyway.
          buffers[s2].bufferhead = buffers[s2].buffertail = buffers[s2].buffer;
          continue;
        } // PR_POLL_EXCEPT, PR_POLL_ERR, PR_POLL_HUP handling

        if (out_flags & PR_POLL_READ && !buffers[s].free())
        {
           printf(" no place in read buffer but got read flag, dropping it now!");
           in_flags &= ~PR_POLL_READ;
        }

        if (out_flags & PR_POLL_READ && buffers[s].free())
        {
          printf(" :reading");
          PRInt32 bytesRead = PR_Recv(sockets[s].fd, buffers[s].buffertail, 
              buffers[s].free(), 0, PR_INTERVAL_NO_TIMEOUT);

          if (bytesRead == 0)
          {
            printf(" socket gracefully closed");
            client_done = true;
            in_flags &= ~PR_POLL_READ;
          }
          else if (bytesRead < 0)
          {
            if (PR_GetError() != PR_WOULD_BLOCK_ERROR)
            {
              printf(" error=%d", PR_GetError());
              // We are in error state, indicate that the connection was 
              // not closed gracefully
              client_error = true;
              socketErrorState[s] = PR_TRUE;
              // Wipe out our send buffer, we cannot send it anyway.
              buffers[s2].bufferhead = buffers[s2].buffertail = buffers[s2].buffer;
            }
            else
              printf(" would block");
          }
          else
          {
            // If the other socket is in error state (unable to send/receive)
            // throw this data away and continue loop
            if (socketErrorState[s2])
            {
              printf(" have read but other socket is in error state\n");
              continue;
            }

            buffers[s].buffertail += bytesRead;
            printf(", read %d bytes", bytesRead);

            // We have to accept and handle the initial CONNECT request here
            PRInt32 response;
            if (!connect_accepted && ReadConnectRequest(ci->server_info, buffers[s],
                &response, certificateToUse, &clientAuth, fullHost))
            {
              // Mark this as a proxy-only connection (no SSL) if the CONNECT
              // request didn't come for port 443 or from any of the server's
              // cert or clientauth hostnames.
              if (fullHost.find(":443") == string::npos)
              {
                server_match_t match;
                match.fullHost = fullHost;
                match.matched = false;
                PL_HashTableEnumerateEntries(ci->server_info->host_cert_table, 
                                             match_hostname, 
                                             &match);
                PL_HashTableEnumerateEntries(ci->server_info->host_clientauth_table, 
                                             match_hostname, 
                                             &match);
                ci->http_proxy_only = !match.matched;
              }
              else
              {
                ci->http_proxy_only = false;
              }

              // Clean the request as it would be read
              buffers[s].bufferhead = buffers[s].buffertail = buffers[s].buffer;
              in_flags |= PR_POLL_WRITE;
              connect_accepted = true;

              // Store response to the oposite buffer
              if (response != 200)
              {
                printf(" could not read the connect request, closing connection with %d", response);
                client_done = true;
                sprintf(buffers[s2].buffer, "HTTP/1.1 %d ERROR\r\nConnection: close\r\n\r\n", response);
                buffers[s2].buffertail = buffers[s2].buffer + strlen(buffers[s2].buffer);
                break;
              }

              strcpy(buffers[s2].buffer, "HTTP/1.1 200 Connected\r\nConnection: keep-alive\r\n\r\n");
              buffers[s2].buffertail = buffers[s2].buffer + strlen(buffers[s2].buffer);

              printf(" accepted CONNECT request, connected to the server, sending OK to the client\n");
              // Send the response to the client socket
              break;
            } // end of CONNECT handling

            if (!buffers[s].free())
            {
              // Do not poll for read when the buffer is full
              printf(" no place in our read buffer, stop reading");
              in_flags &= ~PR_POLL_READ;
            }

            if (ssl_updated)
            {
              if (s == 0 && expect_request_start) 
              {
                if (!strstr(buffers[s].bufferhead, "\r\n\r\n"))
                {
                  // We haven't received the complete header yet, so wait.
                  continue;
                }
                else
                {
                  ci->iswebsocket = AdjustWebSocketHost(buffers[s], ci);
                  expect_request_start = !(ci->iswebsocket || 
                                           AdjustRequestURI(buffers[s], &fullHost));
                  PRNetAddr* addr = &remote_addr;
                  if (ci->iswebsocket && websocket_server.inet.port)
                    addr = &websocket_server;
                  if (!ConnectSocket(other_sock, addr, connect_timeout))
                  {
                    printf(" could not open connection to the real server\n");
                    client_error = true;
                    break;
                  }
                  printf("\n connected to remote server\n");
                  numberOfSockets = 2;
                }
              }
              else if (s == 1 && ci->iswebsocket)
              {
                if (!AdjustWebSocketLocation(buffers[s], ci))
                  continue;
              }

              in_flags2 |= PR_POLL_WRITE;
              printf(" telling the other socket to write");
            }
            else
              printf(" we have something for the other socket to write, but ssl has not been administered on it");
          }
        } // PR_POLL_READ handling

        if (out_flags & PR_POLL_WRITE)
        {
          printf(" :writing");
          PRInt32 bytesWrite = PR_Send(sockets[s].fd, buffers[s2].bufferhead, 
              buffers[s2].present(), 0, PR_INTERVAL_NO_TIMEOUT);

          if (bytesWrite < 0)
          {
            if (PR_GetError() != PR_WOULD_BLOCK_ERROR) {
              printf(" error=%d", PR_GetError());
              client_error = true;
              socketErrorState[s] = PR_TRUE;
              // We got a fatal error while writting the buffer. Clear it to break
              // the main loop, we will never more be able to send it.
              buffers[s2].bufferhead = buffers[s2].buffertail = buffers[s2].buffer;
            }
            else
              printf(" would block");
          }
          else
          {
            printf(", written %d bytes", bytesWrite);
            buffers[s2].buffertail[1] = '\0';
            printf(" dump:\n%.*s\n", bytesWrite, buffers[s2].bufferhead);
            
            buffers[s2].bufferhead += bytesWrite;
            if (buffers[s2].present())
            {
              printf(" still have to write %d bytes", (int)buffers[s2].present());
              in_flags |= PR_POLL_WRITE;
            }              
            else
            {
              if (!ssl_updated)
              {
                printf(" proxy response sent to the client");
                // Proxy response has just been writen, update to ssl
                ssl_updated = true;
                if (!ci->http_proxy_only && 
                    !ConfigureSSLServerSocket(ci->client_sock, ci->server_info, certificateToUse, clientAuth))
                {
                  printf(" but failed to config server socket\n");
                  client_error = true;
                  break;
                }

                printf(" client socket updated to SSL");
              } // sslUpdate

              printf(" dropping our write flag and setting other socket read flag");
              in_flags &= ~PR_POLL_WRITE;
              in_flags2 |= PR_POLL_READ;
              buffers[s2].compact();
            }
          }
        } // PR_POLL_WRITE handling
        printf("\n"); // end the log
      } // for...
    } // while, poll
  }
  else
    client_error = true;

  printf("SSLTUNNEL(%p): exiting root function for csock=%p, ssock=%p\n",
         static_cast<void*>(data),
         static_cast<void*>(ci->client_sock),
         static_cast<void*>(other_sock));
  if (!client_error)
    PR_Shutdown(ci->client_sock, PR_SHUTDOWN_SEND);
  PR_Close(ci->client_sock);

  delete ci;
}

/*
 * Start listening for SSL connections on a specified port, handing
 * them off to client threads after accepting the connection.
 * The data parameter is a server_info_t*, owned by the calling
 * function.
 */
void StartServer(void* data)
{
  server_info_t* si = static_cast<server_info_t*>(data);

  //TODO: select ciphers?
  AutoFD listen_socket(PR_NewTCPSocket());
  if (!listen_socket) {
    fprintf(stderr, "failed to create socket\n");
    SignalShutdown();
    return;
  }

  // In case the socket is still open in the TIME_WAIT state from a previous
  // instance of ssltunnel we ask to reuse the port.
  PRSocketOptionData socket_option;
  socket_option.option = PR_SockOpt_Reuseaddr;
  socket_option.value.reuse_addr = PR_TRUE;
  PR_SetSocketOption(listen_socket, &socket_option);

  PRNetAddr server_addr;
  PR_InitializeNetAddr(PR_IpAddrAny, si->listen_port, &server_addr);
  if (PR_Bind(listen_socket, &server_addr) != PR_SUCCESS) {
    fprintf(stderr, "failed to bind socket\n");
    SignalShutdown();
    return;
  }

  if (PR_Listen(listen_socket, 1) != PR_SUCCESS) {
    fprintf(stderr, "failed to listen on socket\n");
    SignalShutdown();
    return;
  }

  printf("Server listening on port %d with cert %s\n", si->listen_port,
         si->cert_nickname.c_str());

  while (!shutdown_server) {
    connection_info_t* ci = new connection_info_t();
    ci->server_info = si;
    // block waiting for connections
    ci->client_sock = PR_Accept(listen_socket, &ci->client_addr,
                                PR_INTERVAL_NO_TIMEOUT);
    
    PRSocketOptionData option;
    option.option = PR_SockOpt_Nonblocking;
    option.value.non_blocking = PR_TRUE;
    PR_SetSocketOption(ci->client_sock, &option);

    if (ci->client_sock)
      // Not actually using this PRJob*...
      //PRJob* job =
      PR_QueueJob(threads, HandleConnection, ci, PR_TRUE);
    else
      delete ci;
  }
}

// bogus password func, just don't use passwords. :-P
char* password_func(PK11SlotInfo* slot, PRBool retry, void* arg)
{
  if (retry)
    return NULL;

  return PL_strdup("");
}

server_info_t* findServerInfo(int portnumber)
{
  for (vector<server_info_t>::iterator it = servers.begin();
       it != servers.end(); it++) 
  {
    if (it->listen_port == portnumber)
      return &(*it);
  }

  return NULL;
}

int processConfigLine(char* configLine)
{
  if (*configLine == 0 || *configLine == '#')
    return 0;

  char* _caret;
  char* keyword = strtok2(configLine, ":", &_caret);

  // Configure usage of http/ssl tunneling proxy behavior
  if (!strcmp(keyword, "httpproxy"))
  {
    char* value = strtok2(_caret, ":", &_caret);
    if (!strcmp(value, "1"))
      do_http_proxy = true;

    return 0;
  }

  if (!strcmp(keyword, "websocketserver"))
  {
    char* ipstring = strtok2(_caret, ":", &_caret);
    if (PR_StringToNetAddr(ipstring, &websocket_server) != PR_SUCCESS) {
      fprintf(stderr, "Invalid IP address in proxy config: %s\n", ipstring);
      return 1;
    }
    char* remoteport = strtok2(_caret, ":", &_caret);
    int port = atoi(remoteport);
    if (port <= 0) {
      fprintf(stderr, "Invalid remote port in proxy config: %s\n", remoteport);
      return 1;
    }
    websocket_server.inet.port = PR_htons(port);
    return 0;
  }

  // Configure the forward address of the target server
  if (!strcmp(keyword, "forward"))
  {
    char* ipstring = strtok2(_caret, ":", &_caret);
    if (PR_StringToNetAddr(ipstring, &remote_addr) != PR_SUCCESS) {
      fprintf(stderr, "Invalid remote IP address: %s\n", ipstring);
      return 1;
    }
    char* serverportstring = strtok2(_caret, ":", &_caret);
    int port = atoi(serverportstring);
    if (port <= 0) {
      fprintf(stderr, "Invalid remote port: %s\n", serverportstring);
      return 1;
    }
    remote_addr.inet.port = PR_htons(port);

    return 0;
  }

  // Configure all listen sockets and port+certificate bindings
  if (!strcmp(keyword, "listen"))
  {
    char* hostname = strtok2(_caret, ":", &_caret);
    char* hostportstring = NULL;
    if (strcmp(hostname, "*"))
    {
      any_host_spec_config = true;
      hostportstring = strtok2(_caret, ":", &_caret);
    }

    char* serverportstring = strtok2(_caret, ":", &_caret);
    char* certnick = strtok2(_caret, ":", &_caret);

    int port = atoi(serverportstring);
    if (port <= 0) {
      fprintf(stderr, "Invalid port specified: %s\n", serverportstring);
      return 1;
    }

    if (server_info_t* existingServer = findServerInfo(port))
    {
      char *certnick_copy = new char[strlen(certnick)+1];
      char *hostname_copy = new char[strlen(hostname)+strlen(hostportstring)+2];

      strcpy(hostname_copy, hostname);
      strcat(hostname_copy, ":");
      strcat(hostname_copy, hostportstring);
      strcpy(certnick_copy, certnick);

      PLHashEntry* entry = PL_HashTableAdd(existingServer->host_cert_table, hostname_copy, certnick_copy);
      if (!entry) {
        fprintf(stderr, "Out of memory");
        return 1;
      }
    }
    else
    {
      server_info_t server;
      server.cert_nickname = certnick;
      server.listen_port = port;
      server.host_cert_table = PL_NewHashTable(0, PL_HashString, PL_CompareStrings, PL_CompareStrings, NULL, NULL);
      if (!server.host_cert_table)
      {
        fprintf(stderr, "Internal, could not create hash table\n");
        return 1;
      }
      server.host_clientauth_table = PL_NewHashTable(0, PL_HashString, PL_CompareStrings, ClientAuthValueComparator, NULL, NULL);
      if (!server.host_clientauth_table)
      {
        fprintf(stderr, "Internal, could not create hash table\n");
        return 1;
      }
      servers.push_back(server);
    }

    return 0;
  }
  
  if (!strcmp(keyword, "clientauth"))
  {
    char* hostname = strtok2(_caret, ":", &_caret);
    char* hostportstring = strtok2(_caret, ":", &_caret);
    char* serverportstring = strtok2(_caret, ":", &_caret);

    int port = atoi(serverportstring);
    if (port <= 0) {
      fprintf(stderr, "Invalid port specified: %s\n", serverportstring);
      return 1;
    }

    if (server_info_t* existingServer = findServerInfo(port))
    {
      char* authoptionstring = strtok2(_caret, ":", &_caret);
      client_auth_option* authoption = new client_auth_option;
      if (!authoption) {
        fprintf(stderr, "Out of memory");
        return 1;
      }

      if (!strcmp(authoptionstring, "require"))
        *authoption = caRequire;
      else if (!strcmp(authoptionstring, "request"))
        *authoption = caRequest;
      else if (!strcmp(authoptionstring, "none"))
        *authoption = caNone;
      else
      {
        fprintf(stderr, "Incorrect client auth option modifier for host '%s'", hostname);
        return 1;
      }

      any_host_spec_config = true;

      char *hostname_copy = new char[strlen(hostname)+strlen(hostportstring)+2];
      if (!hostname_copy) {
        fprintf(stderr, "Out of memory");
        return 1;
      }

      strcpy(hostname_copy, hostname);
      strcat(hostname_copy, ":");
      strcat(hostname_copy, hostportstring);

      PLHashEntry* entry = PL_HashTableAdd(existingServer->host_clientauth_table, hostname_copy, authoption);
      if (!entry) {
        fprintf(stderr, "Out of memory");
        return 1;
      }
    }
    else
    {
      fprintf(stderr, "Server on port %d for client authentication option is not defined, use 'listen' option first", port);
      return 1;
    }

    return 0;
  }

  // Configure the NSS certificate database directory
  if (!strcmp(keyword, "certdbdir"))
  {
    nssconfigdir = strtok2(_caret, "\n", &_caret);
    return 0;
  }

  printf("Error: keyword \"%s\" unexpected\n", keyword);
  return 1;
}

int parseConfigFile(const char* filePath)
{
  FILE* f = fopen(filePath, "r");
  if (!f)
    return 1;

  char buffer[1024], *b = buffer;
  while (!feof(f))
  {
    char c;
    fscanf(f, "%c", &c);
    switch (c)
    {
    case '\n':
      *b++ = 0;
      if (processConfigLine(buffer))
        return 1;
      b = buffer;
    case '\r':
      continue;
    default:
      *b++ = c;
    }
  }

  fclose(f);

  // Check mandatory items
  if (nssconfigdir.empty())
  {
    printf("Error: missing path to NSS certification database\n,use certdbdir:<path> in the config file\n");
    return 1;
  }

  if (any_host_spec_config && !do_http_proxy)
  {
    printf("Warning: any host-specific configurations are ignored, add httpproxy:1 to allow them\n");
  }

  return 0;
}

PRIntn freeHostCertHashItems(PLHashEntry *he, PRIntn i, void *arg)
{
  delete [] (char*)he->key;
  delete [] (char*)he->value;
  return HT_ENUMERATE_REMOVE;
}

PRIntn freeClientAuthHashItems(PLHashEntry *he, PRIntn i, void *arg)
{
  delete [] (char*)he->key;
  delete (client_auth_option*)he->value;
  return HT_ENUMERATE_REMOVE;
}

int main(int argc, char** argv)
{
  const char* configFilePath;
  if (argc == 1)
    configFilePath = "ssltunnel.cfg";
  else
    configFilePath = argv[1];

  memset(&websocket_server, 0, sizeof(PRNetAddr));

  if (parseConfigFile(configFilePath)) {
    fprintf(stderr, "Error: config file \"%s\" missing or formating incorrect\n"
      "Specify path to the config file as parameter to ssltunnel or \n"
      "create ssltunnel.cfg in the working directory.\n\n"
      "Example format of the config file:\n\n"
      "       # Enable http/ssl tunneling proxy-like behavior.\n"
      "       # If not specified ssltunnel simply does direct forward.\n"
      "       httpproxy:1\n\n"
      "       # Specify path to the certification database used.\n"
      "       certdbdir:/path/to/certdb\n\n"
      "       # Forward/proxy all requests in raw to 127.0.0.1:8888.\n"
      "       forward:127.0.0.1:8888\n\n"
      "       # Accept connections on port 4443 or 5678 resp. and authenticate\n"
      "       # to any host ('*') using the 'server cert' or 'server cert 2' resp.\n"
      "       listen:*:4443:server cert\n"
      "       listen:*:5678:server cert 2\n\n"
      "       # Accept connections on port 4443 and authenticate using\n"
      "       # 'a different cert' when target host is 'my.host.name:443'.\n"
      "       # This only works in httpproxy mode and has higher priority\n"
      "       # than the previous option.\n"
      "       listen:my.host.name:443:4443:a different cert\n\n"
      "       # To make a specific host require or just request a client certificate\n"
      "       # to authenticate use the following options. This can only be used\n"
      "       # in httpproxy mode and only after the 'listen' option has been\n"
      "       # specified. You also have to specify the tunnel listen port.\n"
      "       clientauth:requesting-client-cert.host.com:443:4443:request\n"
      "       clientauth:requiring-client-cert.host.com:443:4443:require\n"
      "       # Proxy WebSocket traffic to the server at 127.0.0.1:9999,\n"
      "       # instead of the server specified in the 'forward' option.\n"
      "       websocketserver:127.0.0.1:9999\n",
      configFilePath);
    return 1;
  }

  // create a thread pool to handle connections
  threads = PR_CreateThreadPool(PR_MAX(INITIAL_THREADS, servers.size()*2),
                                PR_MAX(MAX_THREADS, servers.size()*2),
                                DEFAULT_STACKSIZE);
  if (!threads) {
    fprintf(stderr, "Failed to create thread pool\n");
    return 1;
  }

  shutdown_lock = PR_NewLock();
  if (!shutdown_lock) {
    fprintf(stderr, "Failed to create lock\n");
    PR_ShutdownThreadPool(threads);
    return 1;
  }
  shutdown_condvar = PR_NewCondVar(shutdown_lock);
  if (!shutdown_condvar) {
    fprintf(stderr, "Failed to create condvar\n");
    PR_ShutdownThreadPool(threads);
    PR_DestroyLock(shutdown_lock);
    return 1;
  }

  PK11_SetPasswordFunc(password_func);

  // Initialize NSS
  if (NSS_Init(nssconfigdir.c_str()) != SECSuccess) {
    PRInt32 errorlen = PR_GetErrorTextLength();
    char* err = new char[errorlen+1];
    PR_GetErrorText(err);
    fprintf(stderr, "Failed to init NSS: %s", err);
    delete[] err;
    PR_ShutdownThreadPool(threads);
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    return 1;
  }

  if (NSS_SetDomesticPolicy() != SECSuccess) {
    fprintf(stderr, "NSS_SetDomesticPolicy failed\n");
    PR_ShutdownThreadPool(threads);
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    NSS_Shutdown();
    return 1;
  }

  // these values should make NSS use the defaults
  if (SSL_ConfigServerSessionIDCache(0, 0, 0, NULL) != SECSuccess) {
    fprintf(stderr, "SSL_ConfigServerSessionIDCache failed\n");
    PR_ShutdownThreadPool(threads);
    PR_DestroyCondVar(shutdown_condvar);
    PR_DestroyLock(shutdown_lock);
    NSS_Shutdown();
    return 1;
  }

  for (vector<server_info_t>::iterator it = servers.begin();
       it != servers.end(); it++) {
    // Not actually using this PRJob*...
    // PRJob* server_job =
    PR_QueueJob(threads, StartServer, &(*it), PR_TRUE);
  }
  // now wait for someone to tell us to quit
  PR_Lock(shutdown_lock);
  PR_WaitCondVar(shutdown_condvar, PR_INTERVAL_NO_TIMEOUT);
  PR_Unlock(shutdown_lock);
  shutdown_server = true;
  printf("Shutting down...\n");
  // cleanup
  PR_ShutdownThreadPool(threads);
  PR_JoinThreadPool(threads);
  PR_DestroyCondVar(shutdown_condvar);
  PR_DestroyLock(shutdown_lock);
  if (NSS_Shutdown() == SECFailure) {
    fprintf(stderr, "Leaked NSS objects!\n");
  }
  
  for (vector<server_info_t>::iterator it = servers.begin();
       it != servers.end(); it++) 
  {
    PL_HashTableEnumerateEntries(it->host_cert_table, freeHostCertHashItems, NULL);
    PL_HashTableEnumerateEntries(it->host_clientauth_table, freeClientAuthHashItems, NULL);
    PL_HashTableDestroy(it->host_cert_table);
    PL_HashTableDestroy(it->host_clientauth_table);
  }

  PR_Cleanup();
  return 0;
}

/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) 2012 - 2017, Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.haxx.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ***************************************************************************/

/* <DESC>
 * demonstrate the use of multi socket interface with boost::asio
 * </DESC>
 */
/*
 * This program is in c++ and uses boost::asio instead of libevent/libev.
 * Requires boost::asio, boost::bind and boost::system
 *
 * This is an adaptation of libcurl's "hiperfifo.c" and "evhiperfifo.c"
 * sample programs. This example implements a subset of the functionality from
 * hiperfifo.c, for full functionality refer hiperfifo.c or evhiperfifo.c
 *
 * Written by Lijo Antony based on hiperfifo.c by Jeff Pohlmeyer
 *
 * When running, the program creates an easy handle for a URL and
 * uses the curl_multi API to fetch it.
 *
 * Note:
 *  For the sake of simplicity, URL is hard coded to "www.google.com"
 *
 * This is purely a demo app, all retrieved data is simply discarded by the
 * write callback.
 */


#include <curl/curl.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <logs/logs.hpp>
#include <async/async.h>
#include <errors/errors.h>

#define MSG_OUT stdout /* Send info to stdout, change to stderr if you want */

std::map<curl_socket_t, boost::asio::ip::tcp::socket *> socket_map;
async::Timer timer; 
std::shared_ptr<async::Thread> main_thread;

/* Global information, common to all connections */
typedef struct _GlobalInfo
{
  CURLM *multi;
  int still_running;
} GlobalInfo;

/* Information associated with a specific easy handle */
typedef struct _ConnInfo
{
  CURL *easy;
  char *url;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE];
} ConnInfo;

static void timer_cb(GlobalInfo *g);

/* Update the event timer after curl_multi library calls */
static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g)
{
  LOG_INFO("multi_timer_cb-------------------------------------------------------------------");
  fprintf(MSG_OUT, "\nmulti_timer_cb: timeout_ms %ld, %p\n", timeout_ms, main_thread.get());

  if(timeout_ms > 0) {
    /* update timer */
    timer.CreateOnceTimerTask(boost::bind(&timer_cb, g), boost::posix_time::millisec(timeout_ms), main_thread);
  }
  else if(timeout_ms == 0) {
    /* call timeout function immediately */
    timer_cb(g);
  }

  return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
  if(CURLM_OK != code) {
    const char *s;
    switch(code) {
    case CURLM_CALL_MULTI_PERFORM:
      s = "CURLM_CALL_MULTI_PERFORM";
      break;
    case CURLM_BAD_HANDLE:
      s = "CURLM_BAD_HANDLE";
      break;
    case CURLM_BAD_EASY_HANDLE:
      s = "CURLM_BAD_EASY_HANDLE";
      break;
    case CURLM_OUT_OF_MEMORY:
      s = "CURLM_OUT_OF_MEMORY";
      break;
    case CURLM_INTERNAL_ERROR:
      s = "CURLM_INTERNAL_ERROR";
      break;
    case CURLM_UNKNOWN_OPTION:
      s = "CURLM_UNKNOWN_OPTION";
      break;
    case CURLM_LAST:
      s = "CURLM_LAST";
      break;
    default:
      s = "CURLM_unknown";
      break;
    case CURLM_BAD_SOCKET:
      s = "CURLM_BAD_SOCKET";
      fprintf(MSG_OUT, "\nERROR: %s returns %s", where, s);
      /* ignore this error */
      return;
    }

    fprintf(MSG_OUT, "\nERROR: %s returns %s", where, s);

    exit(code);
  }
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
  char *eff_url;
  CURLMsg *msg;
  int msgs_left;
  ConnInfo *conn;
  CURL *easy;
  CURLcode res;

  fprintf(MSG_OUT, "\nREMAINING: %d", g->still_running);

  while((msg = curl_multi_info_read(g->multi, &msgs_left))) {
    if(msg->msg == CURLMSG_DONE) {
      easy = msg->easy_handle;
      res = msg->data.result;
      curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
      curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      fprintf(MSG_OUT, "\nDONE: %s => (%d) %s", eff_url, res, conn->error);
      curl_multi_remove_handle(g->multi, easy);
      free(conn->url);
      curl_easy_cleanup(easy);
      free(conn);
    }
  }
}

/* Called by asio when there is an action on a socket */
static void event_cb(GlobalInfo *g, curl_socket_t s,
                     int action, const boost::system::error_code & error,
                     int *fdp)
{
  fprintf(MSG_OUT, "\nevent_cb: action=%d", action);

  if(socket_map.find(s) == socket_map.end()) {
    fprintf(MSG_OUT, "\nevent_cb: socket already closed");
    return;
  }

  /* make sure the event matches what are wanted */
  if(*fdp == action || *fdp == CURL_POLL_INOUT) {
    CURLMcode rc;
    if(error)
      action = CURL_CSELECT_ERR;
    rc = curl_multi_socket_action(g->multi, s, action, &g->still_running);

    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(g);

    if(g->still_running <= 0) {
    }

    /* keep on watching.
     * the socket may have been closed and/or fdp may have been changed
     * in curl_multi_socket_action(), so check them both */
    if(!error && socket_map.find(s) != socket_map.end() &&
       (*fdp == action || *fdp == CURL_POLL_INOUT)) {
      boost::asio::ip::tcp::socket *tcp_socket = socket_map.find(s)->second;

      if(action == CURL_POLL_IN) {
        tcp_socket->async_read_some(boost::asio::null_buffers(),
                                    boost::bind(&event_cb, g, s,
                                                action, _1, fdp));
      }
      if(action == CURL_POLL_OUT) {
        tcp_socket->async_write_some(boost::asio::null_buffers(),
                                     boost::bind(&event_cb, g, s,
                                                 action, _1, fdp));
      }
    }
  }
}

/* Called by asio when our timeout expires */
static void timer_cb(GlobalInfo *g)
{
    LOG_INFO("timer_cb---------------------------------------------------------------");
    fprintf(MSG_OUT, "\ntimer_cb: ");

    CURLMcode rc;
    rc = curl_multi_socket_action(g->multi, CURL_SOCKET_TIMEOUT, 0,
                                    &g->still_running);

    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(g);
}

/* Clean up any data */
static void remsock(int *f, GlobalInfo *g)
{
  fprintf(MSG_OUT, "\nremsock: ");

  if(f) {
    free(f);
  }
}

static void setsock(int *fdp, curl_socket_t s, CURL *e, int act, int oldact,
                    GlobalInfo *g)
{
  fprintf(MSG_OUT, "\nsetsock: socket=%d, act=%d, fdp=%p", s, act, fdp);

  std::map<curl_socket_t, boost::asio::ip::tcp::socket *>::iterator it =
    socket_map.find(s);

  if(it == socket_map.end()) {
    fprintf(MSG_OUT, "\nsocket %d is a c-ares socket, ignoring", s);
    return;
  }

  boost::asio::ip::tcp::socket * tcp_socket = it->second;

  *fdp = act;

  if(act == CURL_POLL_IN) {
    fprintf(MSG_OUT, "\nwatching for socket to become readable");
    if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_read_some(boost::asio::null_buffers(),
                                  boost::bind(&event_cb, g, s,
                                              CURL_POLL_IN, _1, fdp));
    }
  }
  else if(act == CURL_POLL_OUT) {
    fprintf(MSG_OUT, "\nwatching for socket to become writable");
    if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_write_some(boost::asio::null_buffers(),
                                   boost::bind(&event_cb, g, s,
                                               CURL_POLL_OUT, _1, fdp));
    }
  }
  else if(act == CURL_POLL_INOUT) {
    fprintf(MSG_OUT, "\nwatching for socket to become readable & writable");
    if(oldact != CURL_POLL_IN && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_read_some(boost::asio::null_buffers(),
                                  boost::bind(&event_cb, g, s,
                                              CURL_POLL_IN, _1, fdp));
    }
    if(oldact != CURL_POLL_OUT && oldact != CURL_POLL_INOUT) {
      tcp_socket->async_write_some(boost::asio::null_buffers(),
                                   boost::bind(&event_cb, g, s,
                                               CURL_POLL_OUT, _1, fdp));
    }
  }
}

static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
  /* fdp is used to store current action */
  int *fdp = (int *) calloc(sizeof(int), 1);

  setsock(fdp, s, easy, action, 0, g);
  curl_multi_assign(g->multi, s, fdp);
}

/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
  fprintf(MSG_OUT, "\nsock_cb: socket=%d, what=%d, sockp=%p", s, what, sockp);

  GlobalInfo *g = (GlobalInfo*) cbp;
  int *actionp = (int *) sockp;
  const char *whatstr[] = { "none", "IN", "OUT", "INOUT", "REMOVE"};

  fprintf(MSG_OUT,
          "\nsocket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);

  if(what == CURL_POLL_REMOVE) {
    fprintf(MSG_OUT, "\n");
    remsock(actionp, g);
  }
  else {
    if(!actionp) {
      fprintf(MSG_OUT, "\nAdding data: %s", whatstr[what]);
      addsock(s, e, what, g);
    }
    else {
      fprintf(MSG_OUT,
              "\nChanging action from %s to %s",
              whatstr[*actionp], whatstr[what]);
      setsock(actionp, s, e, what, *actionp, g);
    }
  }

  return 0;
}

/* CURLOPT_WRITEFUNCTION */
static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
  size_t written = size * nmemb;
  char *pBuffer = (char *)malloc(written + 1);

  strncpy(pBuffer, (const char *)ptr, written);
  pBuffer[written] = '\0';

  fprintf(MSG_OUT, "%s", pBuffer);

  free(pBuffer);

  return written;
}

/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb(void *p, double dltotal, double dlnow, double ult,
                   double uln)
{
  ConnInfo *conn = (ConnInfo *)p;

  (void)ult;
  (void)uln;

  fprintf(MSG_OUT, "\nProgress: %s (%g/%g)", conn->url, dlnow, dltotal);
  fprintf(MSG_OUT, "\nProgress: %s (%g)", conn->url, ult);

  return 0;
}

/* CURLOPT_OPENSOCKETFUNCTION */
static curl_socket_t opensocket(void *clientp, curlsocktype purpose,
                                struct curl_sockaddr *address)
{
  LOG_INFO("opensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocketopensocket");
  fprintf(MSG_OUT, "\nopensocket :");

  curl_socket_t sockfd = CURL_SOCKET_BAD;

  /* restrict to IPv4 */
  if(purpose == CURLSOCKTYPE_IPCXN && address->family == AF_INET) {
    /* create a tcp socket object */
    boost::asio::ip::tcp::socket *tcp_socket =
      new boost::asio::ip::tcp::socket(main_thread->io_service());

    /* open it and get the native handle*/
    boost::system::error_code ec;
    tcp_socket->open(boost::asio::ip::tcp::v4(), ec);

    if(ec) {
      /* An error occurred */
      std::cout << std::endl << "Couldn't open socket [" << ec << "][" <<
        ec.message() << "]";
      fprintf(MSG_OUT, "\nERROR: Returning CURL_SOCKET_BAD to signal error");
    }
    else {
      sockfd = tcp_socket->native_handle();
      fprintf(MSG_OUT, "\nOpened socket %d", sockfd);

      /* save it for monitoring */
      socket_map.insert(std::pair<curl_socket_t,
                        boost::asio::ip::tcp::socket *>(sockfd, tcp_socket));
    }
  }

  return sockfd;
}

/* CURLOPT_CLOSESOCKETFUNCTION */
static int close_socket(void *clientp, curl_socket_t item)
{
  fprintf(MSG_OUT, "\nclose_socket : %d", item);

  std::map<curl_socket_t, boost::asio::ip::tcp::socket *>::iterator it =
    socket_map.find(item);

  if(it != socket_map.end()) {
    delete it->second;
    socket_map.erase(it);
  }

  return 0;
}

/* Create a new easy handle, and add it to the global curl_multi */
static void new_conn(char *url, GlobalInfo *g)
{
  ConnInfo *conn;
  CURLMcode rc;

  conn = (ConnInfo *) calloc(1, sizeof(ConnInfo));

  conn->easy = curl_easy_init();
  if(!conn->easy) {
    fprintf(MSG_OUT, "\ncurl_easy_init() failed, exiting!");
    exit(2);
  }

  conn->global = g;
  conn->url = strdup(url);
  curl_easy_setopt(conn->easy, CURLOPT_URL, conn->url);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, &conn);
  curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
  curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);
  curl_easy_setopt(conn->easy, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSFUNCTION, prog_cb);
  curl_easy_setopt(conn->easy, CURLOPT_PROGRESSDATA, conn);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_TIME, 3L);
  curl_easy_setopt(conn->easy, CURLOPT_LOW_SPEED_LIMIT, 10L);

  /* call this function to get a socket */
  curl_easy_setopt(conn->easy, CURLOPT_OPENSOCKETFUNCTION, opensocket);

  /* call this function to close a socket */
  curl_easy_setopt(conn->easy, CURLOPT_CLOSESOCKETFUNCTION, close_socket);

  fprintf(MSG_OUT,
          "\nAdding easy %p to multi %p (%s)", conn->easy, g->multi, url);
  rc = curl_multi_add_handle(g->multi, conn->easy);
  mcode_or_die("new_conn: curl_multi_add_handle", rc);

  /* note that the add_handle() will set a time-out to trigger very soon so
     that the necessary socket_action() call will be called by this app */
}

bool is_init = false;

void init(GlobalInfo* g) {
  memset(g, 0, sizeof(GlobalInfo));
  g->multi = curl_multi_init();
  curl_multi_setopt(g->multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  curl_multi_setopt(g->multi, CURLMOPT_SOCKETDATA, g);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
  curl_multi_setopt(g->multi, CURLMOPT_TIMERDATA, g);

  //new_conn((char *)"www.baidu.com", g);  /* add a URL */
  is_init = true;
}

int main(int argc, char **argv)
{
    INIT_ERRORS_SIGNAL();

    main_thread.reset(new async::Thread(1));
    printf("main_thread:%p\n", main_thread.get());

  GlobalInfo g;

  (void)argc;
  (void)argv;

  main_thread->PostTask(boost::bind(&init, &g));

  while (true) {
      if (is_init) {
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          main_thread->PostTask(boost::bind(&new_conn, (char *)"www.baidu.com", &g));
          sleep(1);
      }
  }


  //new_conn((char *)"www.baidu.com", &g);  /* add a URL */

  curl_multi_cleanup(g.multi);

  fprintf(MSG_OUT, "\ndone.\n");

  main_thread->Join();

  return 0;
}

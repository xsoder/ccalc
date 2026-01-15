/* network_helpers.c
   Build:
     gcc -shared -fPIC -o libnetwrap.so network.c
*/
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Create a TCP server socket bound to INADDR_ANY:port.
   Returns listening fd on success, -1 on error. */
int nw_tcp_server(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 16) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* Create a UNIX domain server socket bound to path.
   Returns listening fd on success, -1 on error. */
int nw_unix_server(const char* path) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

  unlink(path);

  if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  if (listen(fd, 16) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* Accept a connection on server_fd. Returns client fd or -1 on error. */
int nw_accept(int server_fd) {
  int cfd = accept(server_fd, NULL, NULL);
  if (cfd < 0) return -1;
  return cfd;
}

/* Close a file descriptor. Returns 0 on success, -1 on error. */
int nw_close(int fd) {
  if (close(fd) == 0) return 0;
  return -1;
}

/* Send a nul-terminated string to fd. Returns bytes sent or -1. */
int nw_send(int fd, const char* msg) {
  if (!msg) return -1;
  size_t len = strlen(msg);
  ssize_t s = send(fd, msg, len, 0);
  if (s < 0) return -1;
  return (int)s;
}

/* Receive up to 4096 bytes and return a malloc'd C string.
   Returns NULL on EOF/error. Caller (ccalc) will get it as string. */
char* nw_recv_str(int fd) {
  char buf[4096];
  ssize_t r = recv(fd, buf, sizeof(buf) - 1, 0);
  if (r <= 0) {
    return NULL;
  }
  /* strip trailing CR/LF */
  while (r > 0 && (buf[r-1] == '\n' || buf[r-1] == '\r')) {
    r--;
  }
  buf[r] = '\0';
  char* out = malloc(r + 1);
  if (!out) return NULL;
  memcpy(out, buf, r + 1);
  return out;
}

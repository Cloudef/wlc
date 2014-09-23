#define _POSIX_C_SOURCE 200809L
#include "wlc.h"
#include "wlc_internal.h"
#include "visibility.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>

static bool init = false;
static int client_sock;
static int open_fds[32];

struct msg_request_fd_open {
   char path[32];
   int flags;
};

enum msg_type {
   TYPE_CHECK,
   TYPE_FD_OPEN,
   TYPE_FD_CLOSE
};

struct msg_request {
   enum msg_type type;
   union {
      struct msg_request_fd_open fd_open;
   };
};

struct msg_response {
   enum msg_type type;
};

static void
__attribute__((noreturn,format(printf,1,2)))
die(const char *format, ...)
{
   va_list vargs;
   va_start(vargs, format);
   vfprintf(stderr, format, vargs);
   va_end(vargs);
   fflush(stderr);
   exit(EXIT_FAILURE);
}

static ssize_t
write_fd(const int sock, const int fd, const void *buffer, const ssize_t buffer_size)
{
   char control[CMSG_SPACE(sizeof(int))];
   memset(control, 0, sizeof(control));
   struct msghdr message = {
      .msg_name = NULL,
      .msg_namelen = 0,
      .msg_iov = &(struct iovec){
         .iov_base = (void*)buffer,
         .iov_len = buffer_size
      },
      .msg_iovlen = 1,
   };

   message.msg_control = control;
   message.msg_controllen = sizeof(control);
   struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
   cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
   cmsg->cmsg_level = SOL_SOCKET;
   cmsg->cmsg_type = SCM_RIGHTS;
   memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
   return sendmsg(sock, &message, 0);
}

static ssize_t
recv_fd(const int sock, int *out_fd, void *out_buffer, const ssize_t buffer_size)
{
   assert(out_fd && out_buffer);

   char control[CMSG_SPACE(sizeof(int))];
   struct msghdr message = {
      .msg_name = NULL,
      .msg_namelen = 0,
      .msg_iov = &(struct iovec){
         .iov_base = out_buffer,
         .iov_len = buffer_size
      },
      .msg_iovlen = 1,
      .msg_control = &control,
      .msg_controllen = sizeof(control)
   };
   struct cmsghdr *cmsg;

   ssize_t read;
   if ((read = recvmsg(sock, &message, 0)) < 0)
      return read;

   if (!(cmsg = CMSG_FIRSTHDR(&message)) || cmsg->cmsg_len != CMSG_LEN(sizeof(int)) || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
      return read;

   memcpy(out_fd, CMSG_DATA(cmsg), sizeof(int));
   return read;
}

static int
fd_open(const char *path, const int flags)
{
   assert(path);

   int *pfd = NULL;
   for (unsigned int i = 0; i < sizeof(open_fds) / sizeof(int); ++i) {
      if (open_fds[i] < 0) {
         pfd = &open_fds[i];
         break;
      }
   }

   if (pfd == NULL) {
      fprintf(stderr, "-!- Maximum number of fds opened\n");
      return -1;
   }

   *pfd = open(path, flags);
   return *pfd;
}

static void
fd_close(const int fd)
{
   if (fd < 0)
      return;

   int *pfd = NULL;
   for (unsigned int i = 0; i < sizeof(open_fds) / sizeof(int); ++i) {
      if (open_fds[i] == fd) {
         pfd = &open_fds[i];
         break;
      }
   }

   if (pfd == NULL)
      return;

   close(fd);
   *pfd = -1;
}

static void
write_or_die(const int sock, const int fd, const void *buffer, const ssize_t size)
{
   if (write_fd(sock, (fd >= 0 ? fd : 0), buffer, size) != size)
      die("-!- Failed to write %zu bytes to socket\n", size);
}

static void
handle_request(const int sock, int fd, const struct msg_request *request)
{
   struct msg_response response;
   memset(&response, 0, sizeof(response));
   response.type = request->type;

   switch (request->type) {
      case TYPE_CHECK:
         write_or_die(sock, fd, &response, sizeof(response));
         break;
      case TYPE_FD_OPEN:
         /* we will only open raw input for compositor, nothing else. */
         if (!memcmp(request->fd_open.path, "/dev/input/", strlen("/dev/input/"))) {
            fd = fd_open(request->fd_open.path, request->fd_open.flags);
            if (fd < 0)
               fprintf(stderr, "-!- error opening (%m): %s\n", request->fd_open.path);
         } else {
            fd = -1;
         }
         write_or_die(sock, fd, &response, sizeof(response));
         break;
      case TYPE_FD_CLOSE:
         /* we will only close file descriptors opened by us. */
         fd_close(fd);
         break;
   }
}

static void
communicate(const int sock, const pid_t parent)
{
   memset(open_fds, -1, sizeof(open_fds));

   do {
      int fd = -1;
      struct msg_request request;
      while (recv_fd(sock, &fd, &request, sizeof(request)) == sizeof(request))
         handle_request(sock, fd, &request);
   } while (kill(parent, 0) == 0);
}

static bool
read_response(const int sock, int *out_fd, struct msg_response *response, const enum msg_type expected_type)
{
   if (out_fd)
      *out_fd = -1;

   memset(response, 0, sizeof(struct msg_response));

   fd_set set;
   FD_ZERO(&set);
   FD_SET(sock, &set);

   struct timeval timeout;
   memset(&timeout, 0, sizeof(timeout));
   timeout.tv_sec = 1;

   if (select(sock + 1, &set, NULL, NULL, &timeout) != 1)
      return false;

   int fd = -1;
   ssize_t ret = 0;
   do {
      ret = recv_fd(sock, &fd, response, sizeof(struct msg_response));
   } while (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));

   if (out_fd)
      *out_fd = fd;

   return (ret == sizeof(struct msg_response) && response->type == expected_type);
}

static bool
check_socket(const int sock)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   write_or_die(sock, -1, &request, sizeof(request));
   struct msg_response response;
   return read_response(sock, NULL, &response, TYPE_CHECK);
}

int
wlc_fd_open(const char *path, const int flags)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_FD_OPEN;
   strncpy(request.fd_open.path, path, sizeof(request.fd_open.path));
   request.fd_open.flags = flags;
   write_or_die(client_sock, -1, &request, sizeof(request));

   int fd = -1;
   struct msg_response response;
   if (!read_response(client_sock, &fd, &response, TYPE_FD_OPEN))
      return -1;

   return fd;
}

void
wlc_fd_close(const int fd)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_FD_CLOSE;
   write_or_die(client_sock, fd, &request, sizeof(request));
}

bool
wlc_has_init(void)
{
   return init;
}

WLC_API bool
wlc_init(void)
{
   if (getuid() != geteuid() || getgid() != getegid()) {
      fprintf(stdout, "-!- Doing work on SUID side and dropping permissions\n");
   } else if (getuid() == 0) {
      die("-!- Do not run wlc compositor as root\n");
      return false;
   } else {
      fprintf(stdout, "-!- Binary is not marked as SUID, raw input won't work.\n");
   }

   int sock[2];
   if (socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, sock) != 0)
      die("-!- Failed to create fd passing unix domain socket pair: %m\n");

   if (fcntl(sock[0], F_SETFD, FD_CLOEXEC | O_NONBLOCK) != 0)
      die("Could not set CLOEXEC and NONBLOCK on socket: %m\n");

   if (fcntl(sock[1], F_SETFL, fcntl(sock[1], F_GETFL) & ~O_NONBLOCK) != 0)
      die("Could not reset NONBLOCK on socket: %m\n");

   pid_t child;
   if ((child = fork()) == 0) {
      close(sock[0]);
      communicate(sock[1], getppid());
      _exit(EXIT_SUCCESS);
   } else if (child < 0) {
      die("-!- Fork failed\n");
   } else {
      close(sock[1]);

      fprintf(stdout, "-!- Work done, dropping permissions and checking communication\n");

      if (setuid(getuid()) != 0 || setgid(getgid()) != 0)
         die("-!- Could not drop permissions: %m");

      if (kill(child, 0) != 0)
         die("-!- Child process died\n");

      if (!check_socket(sock[0]))
         die("-!- Communication between parent and child process seems to be broken\n");

      client_sock = sock[0];
   }

   return (init = true);
}

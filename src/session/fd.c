#include <dlfcn.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/major.h>
#include "internal.h"
#include "macros.h"
#include "fd.h"
#include "tty.h"
#include "logind.h"

#ifndef EVIOCREVOKE
#  define EVIOCREVOKE _IOW('E', 0x91, int)
#endif

#define DRM_MAJOR 226

static struct {
   struct {
      void *handle;
      int (*drmSetMaster)(int);
      int (*drmDropMaster)(int);
   } api;
} drm;

struct msg_request_fd_open {
   char path[32];
   int flags;
   enum wlc_fd_type type;
};

struct msg_request_fd_close {
   dev_t st_dev;
   ino_t st_ino;
};

struct msg_request_activate_vt {
   int vt;
};

enum msg_type {
   TYPE_CHECK,
   TYPE_FD_OPEN,
   TYPE_FD_CLOSE,
   TYPE_ACTIVATE,
   TYPE_DEACTIVATE,
   TYPE_ACTIVATE_VT,
};

struct msg_request {
   enum msg_type type;
   union {
      struct msg_request_fd_open fd_open;
      struct msg_request_fd_close fd_close;
      struct msg_request_activate_vt vt_activate;
   };
};

struct msg_response {
   enum msg_type type;
   union {
      bool activate;
      bool deactivate;
   };
};

static struct {
   struct wlc_fd {
      dev_t st_dev;
      ino_t st_ino;
      int fd;
      enum wlc_fd_type type;
   } fds[32];
   int socket;
   pid_t child;
   bool has_logind;
} wlc;

static bool
drm_load(void)
{
   const char *lib = "libdrm.so", *func = NULL;

   if (!(drm.api.handle = dlopen(lib, RTLD_LAZY))) {
      wlc_log(WLC_LOG_WARN, "%s", dlerror());
      return false;
   }

#define load(x) (drm.api.x = dlsym(drm.api.handle, (func = #x)))

   if (!load(drmSetMaster))
      goto function_pointer_exception;
   if (!load(drmDropMaster))
      goto function_pointer_exception;

#undef load

   return true;

function_pointer_exception:
   wlc_log(WLC_LOG_WARN, "Could not load function '%s' from '%s'", func, lib);
   return false;
}

static ssize_t
write_fd(int sock, int fd, const void *buffer, ssize_t buffer_size)
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
      .msg_control = control,
      .msg_controllen = sizeof(control),
   };

   struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
   if (fd >= 0) {
      cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
      cmsg->cmsg_level = SOL_SOCKET;
      cmsg->cmsg_type = SCM_RIGHTS;
      memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
   } else {
      cmsg->cmsg_len = CMSG_LEN(0);
   }

   return sendmsg(sock, &message, 0);
}

static ssize_t
recv_fd(int sock, int *out_fd, void *out_buffer, ssize_t buffer_size)
{
   assert(out_fd && out_buffer);
   *out_fd = -1;

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

   errno = 0;
   ssize_t read;
   if ((read = recvmsg(sock, &message, 0)) < 0)
      return read;

   if (!(cmsg = CMSG_FIRSTHDR(&message)))
      return read;

   if (cmsg->cmsg_len == CMSG_LEN(sizeof(int))) {
      if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
         return read;

      memcpy(out_fd, CMSG_DATA(cmsg), sizeof(int));
   } else if (cmsg->cmsg_len == CMSG_LEN(0)) {
      *out_fd = -1;
   }

   return read;
}

static int
fd_open(const char *path, int flags, enum wlc_fd_type type)
{
   assert(path);

   struct wlc_fd *pfd = NULL;
   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].fd < 0) {
         pfd = &wlc.fds[i];
         break;
      }
   }

   if (!pfd) {
      wlc_log(WLC_LOG_ERROR, "Maximum number of fds opened");
      return -1;
   }

   /* we will only open allowed paths */
#define FILTER(x, m) { x, (sizeof(x) > 32 ? 32 : sizeof(x)) - 1, m }
   static struct {
      const char *base;
      const size_t size;
      uint32_t major;
   } allow[WLC_FD_LAST] = {
      FILTER("/dev/input/", INPUT_MAJOR), // WLC_FD_INPUT
      FILTER("/dev/dri/card", DRM_MAJOR), // WLC_FD_DRM
   };
#undef FILTER

   if (type > WLC_FD_LAST || memcmp(path, allow[type].base, allow[type].size)) {
      wlc_log(WLC_LOG_WARN, "Denying open from: %s", path);
      return -1;
   }

   struct stat st;
   if (stat(path, &st) < 0 || major(st.st_rdev) != allow[type].major)
      return -1;

   int fd;
   if ((fd = open(path, flags | O_CLOEXEC)) < 0)
      return fd;

   pfd->fd = fd;
   pfd->type = type;
   pfd->st_dev = st.st_dev;
   pfd->st_ino = st.st_ino;

   if (pfd->type == WLC_FD_DRM && drm.api.drmSetMaster)
      drm.api.drmSetMaster(pfd->fd);

   if (pfd->fd < 0)
      wlc_log(WLC_LOG_WARN, "Error opening (%m): %s", path);

   return pfd->fd;
}

static void
fd_close(dev_t st_dev, ino_t st_ino)
{
   struct wlc_fd *pfd = NULL;
   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].st_dev == st_dev && wlc.fds[i].st_ino == st_ino) {
         pfd = &wlc.fds[i];
         break;
      }
   }

   if (!pfd) {
      wlc_log(WLC_LOG_WARN, "Tried to close fd that we did not open: (%zu, %zu)", st_dev, st_ino);
      return;
   }

   if (pfd->type == WLC_FD_DRM && drm.api.drmDropMaster)
      drm.api.drmDropMaster(pfd->fd);

   close(pfd->fd);
   pfd->fd = -1;
}

static bool
activate(void)
{
   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].fd < 0)
         continue;

      switch (wlc.fds[i].type) {
         case WLC_FD_DRM:
            if (!drm.api.drmSetMaster || drm.api.drmSetMaster(wlc.fds[i].fd)) {
               wlc_log(WLC_LOG_WARN, "Could not set master for drm fd (%d)", wlc.fds[i].fd);
               return false;
            }
            break;

         case WLC_FD_INPUT:
         case WLC_FD_LAST:
            break;
      }
   }

   return wlc_tty_activate();
}

static bool
deactivate(void)
{
   // try drop drm fds first before we kill input
   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].fd < 0 || wlc.fds[i].type != WLC_FD_DRM)
         continue;

      if (!drm.api.drmDropMaster || drm.api.drmDropMaster(wlc.fds[i].fd)) {
         wlc_log(WLC_LOG_WARN, "Could not drop master for drm fd (%d)", wlc.fds[i].fd);
         return false;
      }
   }

   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].fd < 0)
         continue;

      switch (wlc.fds[i].type) {
         case WLC_FD_INPUT:
            if (ioctl(wlc.fds[i].fd, EVIOCREVOKE, 0) == -1) {
               wlc_log(WLC_LOG_WARN, "Kernel does not support EVIOCREVOKE, can not revoke input devices");
               return false;
            }
            close(wlc.fds[i].fd);
            wlc.fds[i].fd = -1;
            break;

         case WLC_FD_DRM:
         case WLC_FD_LAST:
            break;
      }
   }

   return wlc_tty_deactivate();
}

static void
handle_request(int sock, int fd, const struct msg_request *request)
{
   struct msg_response response;
   memset(&response, 0, sizeof(response));
   response.type = request->type;

   switch (request->type) {
      case TYPE_CHECK:
         write_fd(sock, fd, &response, sizeof(response));
         break;
      case TYPE_FD_OPEN:
         fd = fd_open(request->fd_open.path, request->fd_open.flags, request->fd_open.type);
         write_fd(sock, fd, &response, sizeof(response));
         break;
      case TYPE_FD_CLOSE:
         /* we will only close file descriptors opened by us. */
         fd_close(request->fd_close.st_dev, request->fd_close.st_ino);
         break;
      case TYPE_ACTIVATE:
         response.activate = activate();
         write_fd(sock, fd, &response, sizeof(response));
         break;
      case TYPE_DEACTIVATE:
         response.deactivate = deactivate();
         write_fd(sock, fd, &response, sizeof(response));
         break;
      case TYPE_ACTIVATE_VT:
         response.activate = wlc_tty_activate_vt(request->vt_activate.vt);
         write_fd(sock, fd, &response, sizeof(response));
   }
}

static void
communicate(int sock, pid_t parent)
{
   memset(wlc.fds, -1, sizeof(wlc.fds));

   do {
      int fd = -1;
      struct msg_request request;
      while (recv_fd(sock, &fd, &request, sizeof(request)) == sizeof(request))
         handle_request(sock, fd, &request);
   } while (kill(parent, 0) == 0);

   // Close all open fds
   for (uint32_t i = 0; i < LENGTH(wlc.fds); ++i) {
      if (wlc.fds[i].fd < 0)
         continue;

      if (wlc.fds[i].type == WLC_FD_DRM && drm.api.drmDropMaster)
         drm.api.drmDropMaster(wlc.fds[i].fd);

      close(wlc.fds[i].fd);
   }

   wlc_log(WLC_LOG_INFO, "Parent exit (%u)", parent);
   wlc_cleanup();
}

static bool
read_response(int sock, int *out_fd, struct msg_response *response, enum msg_type expected_type)
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

static void
write_or_die(int sock, int fd, const void *buffer, ssize_t size)
{
   ssize_t wrt;
   if ((wrt = write_fd(sock, fd, buffer, size)) != size)
      die("Failed to write %zi bytes to socket (wrote %zi)", size, wrt);
}

static bool
check_socket(int sock)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   write_or_die(sock, -1, &request, sizeof(request));
   struct msg_response response;
   return read_response(sock, NULL, &response, TYPE_CHECK);
}

static void
signal_handler(int signal)
{
   if (wlc.has_logind && (signal == SIGTERM || signal == SIGINT))
      _exit(EXIT_SUCCESS);

   // For non-logind no-op all catched signals
   // we might need to restore tty
}

int
wlc_fd_open(const char *path, int flags, enum wlc_fd_type type)
{
#ifdef HAS_LOGIND
   if (wlc.has_logind)
      return wlc_logind_open(path, flags);
#endif

   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_FD_OPEN;
   strncpy(request.fd_open.path, path, sizeof(request.fd_open.path));
   request.fd_open.flags = flags;
   request.fd_open.type = type;
   write_or_die(wlc.socket, -1, &request, sizeof(request));

   int fd = -1;
   struct msg_response response;
   if (!read_response(wlc.socket, &fd, &response, TYPE_FD_OPEN))
      return -1;

   return fd;
}

void
wlc_fd_close(int fd)
{
#ifdef HAS_LOGIND
   if (wlc.has_logind) {
      wlc_logind_close(fd);
      goto close;
   }
#endif

   struct stat st;
   if (fstat(fd, &st) == 0) {
      struct msg_request request;
      memset(&request, 0, sizeof(request));
      request.type = TYPE_FD_CLOSE;
      request.fd_close.st_dev = st.st_dev;
      request.fd_close.st_ino = st.st_ino;
      write_or_die(wlc.socket, -1, &request, sizeof(request));
   }

close:
   close(fd);
}

bool
wlc_fd_activate(void)
{
#ifdef HAS_LOGIND
   if (wlc.has_logind)
      return wlc_tty_activate();
#endif

   struct msg_response response;
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_ACTIVATE;
   write_or_die(wlc.socket, -1, &request, sizeof(request));
   return read_response(wlc.socket, NULL, &response, TYPE_ACTIVATE) && response.activate;
}

bool
wlc_fd_deactivate(void)
{
#ifdef HAS_LOGIND
   if (wlc.has_logind)
      return wlc_tty_deactivate();
#endif

   struct msg_response response;
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_DEACTIVATE;
   write_or_die(wlc.socket, -1, &request, sizeof(request));
   return read_response(wlc.socket, NULL, &response, TYPE_DEACTIVATE) && response.deactivate;
}

bool
wlc_fd_activate_vt(int vt)
{
#ifdef HAS_LOGIND
   if (wlc.has_logind)
      return wlc_tty_activate_vt(vt);
#endif

   struct msg_response response;
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_ACTIVATE_VT;
   request.vt_activate.vt = vt;
   write_or_die(wlc.socket, -1, &request, sizeof(request));
   return read_response(wlc.socket, NULL, &response, TYPE_ACTIVATE_VT) && response.activate;
}

void
wlc_fd_terminate(void)
{
   if (wlc.child >= 0) {
      if (wlc.has_logind) {
         // XXX: We need fd passer to cleanup sessionless tty
         kill(wlc.child, SIGTERM);
      }

      wlc.child = 0;

      if (drm.api.handle)
         dlclose(drm.api.handle);
   }

   memset(&drm, 0, sizeof(drm));
   memset(&wlc, 0, sizeof(wlc));
}

void
wlc_fd_init(int argc, char *argv[], bool has_logind)
{
   wlc.has_logind = has_logind;

   int sock[2];
   if (socketpair(AF_LOCAL, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sock) != 0)
      die("Failed to create fd passing unix domain socket pair: %m");

   if ((wlc.child = fork()) == 0) {
      close(sock[0]);

      if (clearenv() != 0)
         die("Failed to clear environment");

      struct sigaction action = {
         .sa_handler = signal_handler
      };

      sigaction(SIGUSR1, &action, NULL);
      sigaction(SIGUSR2, &action, NULL);
      sigaction(SIGINT, &action, NULL);
      sigaction(SIGTERM, &action, NULL);

      for (int i = 0; i < argc; ++i)
         strncpy(argv[i], (i == 0 ? "wlc" : ""), strlen(argv[i]));

      drm_load();
      communicate(sock[1], getppid());
      _exit(EXIT_SUCCESS);
   } else if (wlc.child < 0) {
      die("Fork failed");
   } else {
      close(sock[1]);

      wlc_log(WLC_LOG_INFO, "Work done, dropping permissions and checking communication");

      if (setuid(getuid()) != 0 || setgid(getgid()) != 0)
         die("Could not drop permissions: %m");

      if (kill(wlc.child, 0) != 0)
         die("Child process died");

      if (!check_socket(sock[0]))
         die("Communication between parent and child process seems to be broken");

      wlc.socket = sock[0];
   }
}

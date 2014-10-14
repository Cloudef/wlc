#define _DEFAULT_SOURCE
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
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#if defined(__linux__)
#  include <linux/kd.h>
#  include <linux/major.h>
#  include <linux/vt.h>
#  include <linux/version.h>
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#     include <sys/prctl.h> /* for yama */
#     define HAS_YAMA_PRCTL 1
#  endif
#endif

#if defined(__linux__) && defined(__GNUC__)
#  define _GNU_SOURCE
#  include <fenv.h>
int feenableexcept(int excepts);
#endif

#if (defined(__APPLE__) && (defined(__i386__) || defined(__x86_64__)))
#  define OSX_SSE_FPE
#  include <xmmintrin.h>
#endif

static struct {
   int socket;
   int fds[32];
   int tty;
   bool init;
} wlc;

static struct {
    bool altered;
    int vt;
    long kb_mode;
    long console_mode;
} original_vt_state;

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
   for (unsigned int i = 0; i < sizeof(wlc.fds) / sizeof(int); ++i) {
      if (wlc.fds[i] < 0) {
         pfd = &wlc.fds[i];
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
   for (unsigned int i = 0; i < sizeof(wlc.fds) / sizeof(int); ++i) {
      if (wlc.fds[i] == fd) {
         pfd = &wlc.fds[i];
         break;
      }
   }

   if (pfd == NULL)
      return;

   close(fd);
   *pfd = -1;
}

static void
handle_request(const int sock, int fd, const struct msg_request *request)
{
   struct msg_response response;
   memset(&response, 0, sizeof(response));
   response.type = request->type;

   switch (request->type) {
      case TYPE_CHECK:
         write_fd(sock, (fd >= 0 ? fd : 0), &response, sizeof(response));
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
         write_fd(sock, (fd >= 0 ? fd : 0), &response, sizeof(response));
         break;
      case TYPE_FD_CLOSE:
         /* we will only close file descriptors opened by us. */
         fd_close(fd);
         break;
   }
}

static void
cleanup(void)
{
   if (wlc.tty >= 0) {
      fprintf(stderr, "-!- Restoring tty %d\n", wlc.tty);
      struct vt_mode mode = { .mode = VT_AUTO };
      ioctl(wlc.tty, VT_SETMODE, &mode);
      ioctl(wlc.tty, KDSETMODE, KD_GRAPHICS);
      ioctl(wlc.tty, KDSETMODE, original_vt_state.console_mode);
      ioctl(wlc.tty, KDSKBMODE, original_vt_state.kb_mode);
      close(wlc.tty);
   }

   fprintf(stderr, "-!- Cleanup wlc\n");
}

static void
communicate(const int sock, const pid_t parent)
{
   memset(wlc.fds, -1, sizeof(wlc.fds));

   do {
      int fd = -1;
      struct msg_request request;
      while (recv_fd(sock, &fd, &request, sizeof(request)) == sizeof(request))
         handle_request(sock, fd, &request);
   } while (kill(parent, 0) == 0);

   fprintf(stderr, "-!- Parent exit (%u)\n", parent);
   cleanup();
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

static void
write_or_die(const int sock, const int fd, const void *buffer, const ssize_t size)
{
   if (write_fd(sock, (fd >= 0 ? fd : 0), buffer, size) != size)
      die("-!- Failed to write %zu bytes to socket\n", size);
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

static int find_vt(const char *vt_string)
{
   int vt;

   if (vt_string) {
      char *end;
      vt = strtoul(vt_string, &end, 10);
      if (*end == '\0')
         return vt;
   }

   int tty0_fd;
   if ((tty0_fd = open("/dev/tty0", O_RDWR)) < 0)
      die("Could not open /dev/tty0 to find unused VT");

   if (ioctl(tty0_fd, VT_OPENQRY, &vt) != 0)
      die("Could not find unused VT");

   close(tty0_fd);
   fprintf(stderr, "Running on VT %d\n", vt);
   return vt;
}

static int open_tty(int vt)
{
   char tty_name[64];
   snprintf(tty_name, sizeof tty_name, "/dev/tty%d", vt);

   /* check if we are running on the desired VT */
   char *current_tty_name = ttyname(STDIN_FILENO);
   if (!strcmp(tty_name, current_tty_name))
      return STDIN_FILENO;

   int fd;
   if ((fd = open(tty_name, O_RDWR | O_NOCTTY)) < 0)
      die("Could not open %s", tty_name);

   return fd;
}

static bool
setup_tty(const char *xdg_vtnr)
{
   struct stat st;
   int vt;
   struct vt_stat state;
   struct vt_mode mode = {
      .mode = VT_PROCESS,
      .relsig = SIGUSR1,
      .acqsig = SIGUSR2
   };

   int fd;
   if ((fd = open_tty(find_vt(xdg_vtnr)) < 0))
      return false;

   if (fstat(fd, &st) == -1)
      die("Could not stat TTY fd");

   vt = minor(st.st_rdev);

   if (major(st.st_rdev) != TTY_MAJOR || vt == 0)
      die("Not a valid VT");

   if (ioctl(fd, VT_GETSTATE, &state) == -1)
      die("Could not get the current VT state");

   original_vt_state.vt = state.v_active;

   if (ioctl(fd, KDGKBMODE, &original_vt_state.kb_mode))
      die("Could not get keyboard mode");

   if (ioctl(fd, KDGETMODE, &original_vt_state.console_mode))
      die("Could not get console mode");

   if (ioctl(fd, KDSKBMODE, K_OFF) == -1)
      die("Could not set keyboard mode to K_OFF");

   if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == -1) {
      die("Could not set console mode to KD_GRAPHICS");
      goto error0;
   }

   if (ioctl(fd, VT_SETMODE, &mode) == -1) {
      die("Could not set VT mode");
      goto error1;
   }

   if (ioctl(fd, VT_ACTIVATE, vt) == -1) {
      die("Could not activate VT");
      goto error2;
   }

   if (ioctl(fd, VT_WAITACTIVE, vt) == -1) {
      die("Could not wait for VT to become active");
      goto error2;
   }

   original_vt_state.altered = true;
   wlc.tty = fd;
   return true;

error2:
    mode = (struct vt_mode) { .mode = VT_AUTO };
    ioctl(fd, VT_SETMODE, &mode);
error1:
    ioctl(fd, KDSETMODE, original_vt_state.console_mode);
error0:
    ioctl(fd, KDSKBMODE, original_vt_state.kb_mode);
    return false;
}

static void
backtrace(int signal)
{
   (void)signal;

   /* GDB */
#if defined(__linux__) || defined(__APPLE__)
   char buf[1024];
   pid_t dying_pid = getpid();
   pid_t child_pid = fork();

#if HAS_YAMA_PRCTL
   /* tell yama that we allow our child_pid to trace our process */
   if (child_pid > 0) prctl(PR_SET_PTRACER, child_pid);
#endif

   if (child_pid < 0) {
      fprintf(stderr, "-!- Fork failed for gdb backtrace");
   } else if (child_pid == 0) {
      /* sed -n '/bar/h;/bar/!H;$!b;x;p' (another way, if problems) */
      fprintf(stdout, "\n---- gdb ----");
      snprintf(buf, sizeof(buf)-1, "gdb -p %d -batch -ex bt 2>/dev/null | sed -n '/<signal handler/{n;x;b};H;${x;p}'", dying_pid);
      const char* argv[] = { "sh", "-c", buf, NULL };
      execve("/bin/sh", (char**)argv, NULL);
      fprintf(stderr, "-!- Failed to launch gdb for backtrace");
      _exit(EXIT_FAILURE);
   } else {
      waitpid(child_pid, NULL, 0);
   }
#endif

   /* SIGABRT || SIGSEGV */
   exit(EXIT_FAILURE);
}

static void
fpehandler(int signal)
{
   (void)signal;
   fprintf(stderr, "\nSIGFPE signal received");
   abort();
}

static void
fpesetup(struct sigaction *action)
{
#if defined(__linux__) || defined(_WIN32) || defined(OSX_SSE_FPE)
   action->sa_handler = fpehandler;
   sigaction(SIGFPE, action, NULL);
#  if defined(__linux__) && defined(__GNUC__)
   feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW);
#  endif /* defined(__linux__) && defined(__GNUC__) */
#  if defined(OSX_SSE_FPE)
   return; /* causes issues */
   /* OSX uses SSE for floating point by default, so here
    * use SSE instructions to throw floating point exceptions */
   _MM_SET_EXCEPTION_MASK(_MM_MASK_MASK & ~(_MM_MASK_OVERFLOW | _MM_MASK_INVALID | _MM_MASK_DIV_ZERO));
#  endif /* OSX_SSE_FPE */
#  if defined(_WIN32) && defined(_MSC_VER)
   _controlfp_s(NULL, 0, _MCW_EM); /* enables all fp exceptions */
   _controlfp_s(NULL, _EM_DENORMAL | _EM_UNDERFLOW | _EM_INEXACT, _MCW_EM); /* hide the ones we don't care about */
#  endif /* _WIN32 && _MSC_VER */
#endif
}

int
wlc_fd_open(const char *path, const int flags)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_FD_OPEN;
   strncpy(request.fd_open.path, path, sizeof(request.fd_open.path));
   request.fd_open.flags = flags;
   write_or_die(wlc.socket, -1, &request, sizeof(request));

   int fd = -1;
   struct msg_response response;
   if (!read_response(wlc.socket, &fd, &response, TYPE_FD_OPEN))
      return -1;

   return fd;
}

void
wlc_fd_close(const int fd)
{
   struct msg_request request;
   memset(&request, 0, sizeof(request));
   request.type = TYPE_FD_CLOSE;
   write_or_die(wlc.socket, fd, &request, sizeof(request));
}

bool
wlc_has_init(void)
{
   return wlc.init;
}

WLC_API bool
wlc_init(const int argc, char *argv[])
{
   (void)argc;

   if (wlc_has_init())
      return true;

   /* env variables that need to be stored before clear */
   struct {
      char *env, *value;
   } stored_env[] = {
      { "DISPLAY", NULL },
      { "XAUTHORITY", NULL },
      { "HOME", NULL },
      { "USER", NULL },
      { "LOGNAME", NULL },
      { "LANG", NULL },
      { "PATH", NULL },
      { "USER", NULL },
      { "SHELL", NULL }, /* weston-terminal relies on this */
      { "TERMINAL", NULL }, /* expose temporarily to loliwm */
      { "XDG_RUNTIME_DIR", NULL },
      { "XDG_CONFIG_HOME", NULL },
      { "XDG_CONFIG_DIRS", NULL },
      { "XDG_DATA_HOME", NULL },
      { "XDG_DATA_DIRS", NULL },
      { "XDG_CACHE_HOME", NULL },
      { "XDG_SEAT", NULL },
      { "XDG_VTNR", NULL },
      { "XKB_DEFAULT_RULES", NULL },
      { "XKB_DEFAULT_LAYOUT", NULL },
      { "XKB_DEFAULT_VARIANT", NULL },
      { "XKB_DEFAULT_OPTIONS", NULL },
      { NULL, NULL }
   };

   for (int i = 0; stored_env[i].env; ++i)
      stored_env[i].value = getenv(stored_env[i].env);

   const char *xdg_vtnr = getenv("XDG_VTNR");
   const char *display = getenv("DISPLAY");

   if (clearenv() != 0)
      die("-!- Failed to clear environment\n");

   if (getuid() != geteuid() || getgid() != getegid()) {
      fprintf(stdout, "-!- Doing work on SUID/SGID side and dropping permissions\n");
   } else if (getuid() == 0) {
      die("-!- Do not run wlc compositor as root\n");
      return false;
   } else if (!display && access("/dev/input/event0", R_OK | W_OK) != 0) {
      fprintf(stdout, "-!- Not running from X11 and no access to /dev/input/event0\n");
      return false;
   }

#ifndef NDEBUG
   struct sigaction action;
   memset(&action, 0, sizeof(action));

   action.sa_handler = backtrace;
   sigaction(SIGABRT, &action, NULL);
   sigaction(SIGSEGV, &action, NULL);

   fpesetup(&action);
#endif

   wlc.tty = -1;
   if (!display && !setup_tty(xdg_vtnr))
      die("-!- Failed to setup TTY");

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
      strncpy(argv[0], "wlc", strlen(argv[0]));
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

      wlc.socket = sock[0];
   }

   for (int i = 0; stored_env[i].env; ++i) {
      if (stored_env[i].value) {
         setenv(stored_env[i].env, stored_env[i].value, 0);
         printf("%s: %s\n", stored_env[i].env, stored_env[i].value);
      }
   }

   return (wlc.init = true);
}

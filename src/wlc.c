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
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>

static bool init = false;
static int client_sock;
static int open_fds[32];
static int tty_fd = -1;

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

static void
cleanup(void)
{
   struct vt_mode mode = { .mode = VT_AUTO };
   ioctl(tty_fd, VT_SETMODE, &mode);
   ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
   ioctl(tty_fd, KDSETMODE, original_vt_state.console_mode);
   ioctl(tty_fd, KDSKBMODE, original_vt_state.kb_mode);
   close(tty_fd);
}

static void
handle_usr1(int signal)
{
   (void)signal;
   ioctl(tty_fd, VT_RELDISP, 1);
   cleanup();
}

static void
handle_usr2(int signal)
{
   (void)signal;
   ioctl(tty_fd, VT_RELDISP, VT_ACKACQ);
   cleanup();
}

static void
setup_tty(const int fd)
{
   struct stat st;
   int vt;
   struct vt_stat state;
   struct vt_mode mode = {
      .mode = VT_PROCESS,
      .relsig = SIGUSR1,
      .acqsig = SIGUSR2
   };

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

#if 0
    if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == -1)
    {
        perror("Could not set console mode to KD_GRAPHICS");
        goto error0;
    }
#endif

    if (ioctl(fd, VT_SETMODE, &mode) == -1)
    {
        perror("Could not set VT mode");
        goto error1;
    }

#if 0
    if (ioctl(fd, VT_ACTIVATE, vt) == -1)
    {
        perror("Could not activate VT");
        goto error2;
    }

    if (ioctl(fd, VT_WAITACTIVE, vt) == -1)
    {
        perror("Could not wait for VT to become active");
        goto error2;
    }
#endif

    original_vt_state.altered = true;
    tty_fd = fd;
    return;

error2:
    mode = (struct vt_mode) { .mode = VT_AUTO };
    ioctl(fd, VT_SETMODE, &mode);
error1:
    ioctl(fd, KDSETMODE, original_vt_state.console_mode);
error0:
    ioctl(fd, KDSKBMODE, original_vt_state.kb_mode);
    exit(EXIT_FAILURE);
}

WLC_API bool
wlc_init(void)
{
   /* env variables that need to be stored before clear */
   struct {
      char *env, *value;
   } stored_env[] = {
      { "WAYLAND_DISPLAY", NULL },
      { "DISPLAY", NULL },
      { "XAUTHORITY", NULL },
      { "LANG", NULL },
      { "USER", NULL },
      { "XDG_RUNTIME_DIR", NULL },
      { "XKB_DEFAULT_RULES", NULL },
      { "XKB_DEFAULT_LAYOUT", NULL },
      { "XKB_DEFAULT_VARIANT", NULL },
      { "XKB_DEFAULT_OPTIONS", NULL },
      { NULL, NULL }
   };

   for (int i = 0; stored_env[i].env; ++i)
      stored_env[i].value = getenv(stored_env[i].env);

   const char *xdg_vtnr = getenv("XDG_VTNR");

   if (clearenv() != 0)
      die("-!- Failed to clear environment\n");

   if (getuid() != geteuid() || getgid() != getegid()) {
      fprintf(stdout, "-!- Doing work on SUID/SGID side and dropping permissions\n");
   } else if (getuid() == 0) {
      die("-!- Do not run wlc compositor as root\n");
      return false;
   } else if (!stored_env[1].value && access("/dev/input/event0", R_OK | W_OK) != 0) {
      fprintf(stdout, "-!- Not running from X11 and no access to /dev/input/event0\n");
      return false;
   }

   int sock[2];
   if (socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, sock) != 0)
      die("-!- Failed to create fd passing unix domain socket pair: %m\n");

   if (fcntl(sock[0], F_SETFD, FD_CLOEXEC | O_NONBLOCK) != 0)
      die("Could not set CLOEXEC and NONBLOCK on socket: %m\n");

   if (fcntl(sock[1], F_SETFL, fcntl(sock[1], F_GETFL) & ~O_NONBLOCK) != 0)
      die("Could not reset NONBLOCK on socket: %m\n");

   struct sigaction action;
   memset(&action, 0, sizeof(action));

   action.sa_handler = &handle_usr1;
   if (sigaction(SIGUSR1, &action, NULL) != 0)
      die("Failed to register signal handler for SIGUSR1");

#if 0
   if (sigaction(SIGINT, &action, NULL) != 0)
      die("Failed to register signal handler for SIGUSR1");
   if (sigaction(SIGTERM, &action, NULL) != 0)
      die("Failed to register signal handler for SIGUSR1");
#endif

   action.sa_handler = &handle_usr2;
   if (sigaction(SIGUSR2, &action, NULL) != 0)
      die("Failed to register signal handler for SIGUSR2");

   atexit(cleanup);

   if (!stored_env[1].value)
      setup_tty(open_tty(find_vt(xdg_vtnr)));

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

   for (int i = 0; stored_env[i].env; ++i) {
      if (stored_env[i].value) {
         setenv(stored_env[i].env, stored_env[i].value, 0);
         printf("%s: %s\n", stored_env[i].env, stored_env[i].value);
      }
   }

   return (init = true);
}

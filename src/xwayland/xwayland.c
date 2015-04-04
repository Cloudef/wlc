/* This is mostly based on swc's xwayland.c which is based on weston's xwayland/launcher.c */

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include "internal.h"
#include "macros.h"
#include "xwayland.h"
#include "xwm.h"
#include "compositor/compositor.h"

static const char *lock_fmt = "/tmp/.X%d-lock";
static const char *socket_dir = "/tmp/.X11-unix";
static const char *socket_fmt = "/tmp/.X11-unix/X%d";

static struct {
   time_t start_time;
   struct sigaction old_sigusr1;
   char display_name[16];
   struct wl_client *client;
   int display;
   int wl[2], wm[2], socks[2];
   pid_t pid;
} xserver;

static int
open_socket(struct sockaddr_un *addr, size_t path_size)
{
   int fd;
   socklen_t size = offsetof(struct sockaddr_un, sun_path) + path_size + 1;

   if ((fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
      goto socket_fail;

   /* Unlink the socket location in case it was being used by a process which left around a stale lockfile. */
   unlink(addr->sun_path);

   if (bind(fd, (struct sockaddr*)addr, size) < 0)
      goto bind_fail;

   if (listen(fd, 1) < 0)
      goto listen_fail;

   return fd;

socket_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create socket: %s", addr->sun_path);
   goto fail;
bind_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind socket: %s", addr->sun_path);
   goto fail;
listen_fail:
   wlc_log(WLC_LOG_WARN, "Failed to listen to socket");
   if (addr->sun_path[0])
      unlink(addr->sun_path);
   goto fail;
fail:
   if (fd >= 0)
      close(fd);
   return -1;
}

static bool
open_display(int socks[2])
{
   int lock_fd, dpy = -1;
   char lock_name[64];

retry:
   dpy += 1;
   for (lock_fd = -1; dpy <= 32 && lock_fd < 0; ++dpy) {
      snprintf(lock_name, sizeof(lock_name), lock_fmt, dpy);
      if ((lock_fd = open(lock_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444)) >= 0)
         break;

      if ((lock_fd = open(lock_name, O_RDONLY | O_CLOEXEC)) < 0)
         continue;

      char pid[12];
      memset(pid, 0, sizeof(pid));
      ssize_t bytes = read(lock_fd, pid, sizeof(pid) - 1);
      close(lock_fd);
      lock_fd = -1;

      if (bytes != sizeof(pid) -1)
         continue;

      pid_t owner;
      if (!chck_cstr_to_i32(pid, &owner))
         continue;

      /**
       * Check if the pid for existing lock file is not alive by
       * sending kill signal and checking that errno == ESRCH (process not found, in most cases)
       */
      errno = 0;
      if (kill(owner, 0) != 0 && errno == ESRCH) {
         unlink(lock_name);
         snprintf(lock_name, sizeof(lock_name), socket_fmt, dpy);
         unlink(lock_name);

         /* try open again, as the X server for this lock is not running,
          * if we fail here, give up and try next display */
         snprintf(lock_name, sizeof(lock_name), lock_fmt, dpy);
         if ((lock_fd = open(lock_name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444)) >= 0)
            break;
      }
   }

   if (dpy > 32)
      goto no_open_display;

   char pid[12];
   snprintf(pid, sizeof(pid), "%10d", getpid());
   if (write(lock_fd, pid, sizeof(pid) - 1) != sizeof(pid) -1) {
      unlink(lock_name);
      close(lock_fd);
      goto retry;
   }

   close(lock_fd);

   struct sockaddr_un addr = { .sun_family = AF_LOCAL };
   addr.sun_path[0] = '\0';
   size_t path_size = snprintf(addr.sun_path + 1, sizeof(addr.sun_path) - 1, socket_fmt, dpy);
   if ((socks[0] = open_socket(&addr, path_size)) < 0) {
      unlink(lock_name);
      unlink(addr.sun_path + 1);
      goto retry;
   }

   mkdir(socket_dir, 0777);
   path_size = snprintf(addr.sun_path, sizeof(addr.sun_path), socket_fmt, dpy);
   if ((socks[1] = open_socket(&addr, path_size)) < 0) {
      close(socks[0]);
      unlink(lock_name);
      unlink(addr.sun_path);
      goto retry;
   }

   snprintf(xserver.display_name, sizeof(xserver.display_name), ":%d", (xserver.display = dpy));
   return true;

no_open_display:
   wlc_log(WLC_LOG_WARN, "No open display in first 32");
   goto fail;
fail:
   if (lock_fd > 0) {
      unlink(lock_name);
      close(lock_fd);
   }
   return false;
}

static void
close_display(void)
{
   char path[64];
   snprintf(path, sizeof(path), socket_fmt, xserver.display);
   unlink(path);
   snprintf(path, sizeof(path), lock_fmt, xserver.display);
   unlink(path);
   unsetenv("DISPLAY");
}

static void
sigusr_handler(int signal_number)
{
   assert(signal_number == SIGUSR1);
   wlc_log(WLC_LOG_INFO, "Xwayland started (DISPLAY %s)", xserver.display_name);
   sigaction(signal_number, &xserver.old_sigusr1, NULL);
   setenv("DISPLAY", xserver.display_name, true);
   wl_signal_emit(&wlc_system_signals()->xwayland, &(bool){true});
}

struct wl_client*
wlc_xwayland_get_client(void)
{
   return xserver.client;
}

int
wlc_xwayland_get_fd(void)
{
   return xserver.wm[0];
}

static void
destroy_event(struct wl_listener *listener, void *data)
{
   (void)listener, (void)data;
   time_t diff = time(NULL) - xserver.start_time;
   xserver.client = NULL;
   wlc_xwayland_terminate();

   // Will not start if delay less or equal to 5 seconds
   if (diff > 5) {
      wlc_log(WLC_LOG_INFO, "Xwayland crashed, restarting");
      wlc_xwayland_init();
   }
}

static struct wl_listener destroy_listener = {
   .notify = destroy_event,
};

void
wlc_xwayland_terminate(void)
{
   wl_signal_emit(&wlc_system_signals()->xwayland, &(bool){false});

   if (xserver.client) {
      wlc_log(WLC_LOG_INFO, "Closing Xwayland");
      wl_list_remove(&destroy_listener.link);
      wl_client_destroy(xserver.client);
   }

   const int fds[] = { xserver.socks[0], xserver.socks[1], xserver.wl[0], xserver.wl[1], xserver.wm[0], xserver.wm[1] };
   for (uint32_t i = 0; i < LENGTH(fds); ++i) {
      if (fds[i] >= 0)
         close(fds[i]);
   }

   if (xserver.socks[0] >= 0 || xserver.socks[1] >= 0)
      close_display();

   memset(&xserver, 0, sizeof(xserver));
}

bool
wlc_xwayland_init(void)
{
   memset(xserver.socks, -1, sizeof(xserver.socks));
   memset(xserver.wl, -1, sizeof(xserver.wl));
   memset(xserver.wm, -1, sizeof(xserver.wm));

   if (!open_display(xserver.socks))
      goto display_open_fail;

   /* Open a socket for the Wayland connection from Xwayland. */
   if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xserver.wl) != 0)
      goto socketpair_fail;

   /* Open a socket for the X connection to Xwayland. */
   if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, xserver.wm) != 0)
      goto socketpair_fail;

   const int fds[] = { xserver.wl[1], xserver.wm[1], xserver.socks[0], xserver.socks[1] };
   if ((xserver.pid = fork()) == 0) {
      char strings[LENGTH(fds)][16];

      /* Unset the FD_CLOEXEC flag on the FDs that will get passed to Xwayland. */
      for (uint32_t i = 0; i < LENGTH(fds); ++i) {
         if (fcntl(fds[i], F_SETFD, 0) != 0) {
            wlc_log(WLC_LOG_WARN, "fcntl() failed: %m");
            _exit(EXIT_FAILURE);
         }

         if (snprintf(strings[i], sizeof(strings[i]), "%d", fds[i]) >= (ssize_t)sizeof(strings[i])) {
            wlc_log(WLC_LOG_WARN, "FD is too large");
            _exit(EXIT_FAILURE);
         }
      }

      /* Ignore the USR1 signal so that Xwayland will send a USR1 signal
       * to the parent process (us) after it finishes initializing. See
       * Xserver(1) for more details. */
      struct sigaction action = { .sa_handler = SIG_IGN };
      if (sigaction(SIGUSR1, &action, NULL) != 0) {
         wlc_log(WLC_LOG_WARN, "Failed to set SIGUSR1 handler to SIG_IGN: %m");
         _exit(EXIT_FAILURE);
      }

      const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
      if (!xdg_runtime) {
         wlc_log(WLC_LOG_WARN, "No XDG_RUNTIME_DIR set");
         _exit(EXIT_FAILURE);
      }

      if (clearenv() != 0) {
         wlc_log(WLC_LOG_WARN, "Failed to clear environment");
         _exit(EXIT_FAILURE);
      }

      setenv("XDG_RUNTIME_DIR", xdg_runtime, true);
      setenv("WAYLAND_SOCKET", strings[0], true);

      if (wlc_get_log_file() != stdout)
         dup2(fileno(wlc_get_log_file()), STDOUT_FILENO);

      if (wlc_get_log_file() != stderr)
         dup2(fileno(wlc_get_log_file()), STDERR_FILENO);

      wlc_log(WLC_LOG_INFO, "Xwayland %s -rootless -terminate -listen %s -listen %s -wm %s",
            xserver.display_name, strings[2], strings[3], strings[1]);

      execlp("Xwayland", "Xwayland",
            xserver.display_name,
            "-rootless",
            "-terminate",
            "-listen", strings[2],
            "-listen", strings[3],
            "-wm", strings[1],
            NULL);
      _exit(EXIT_FAILURE);
   } else if (xserver.pid < 0) {
      goto fork_fail;
   }

   /* Close fds that went to child */
   for (uint32_t i = 0; i < LENGTH(fds); ++i)
      close(fds[i]);
   xserver.wm[1] = xserver.wl[1] = -1;
   memset(xserver.socks, -1, sizeof(xserver.socks));

   if (!(xserver.client = wl_client_create(wlc_display(), xserver.wl[0])))
      goto client_create_fail;

   xserver.start_time = time(NULL);
   wl_client_add_destroy_listener(xserver.client, &destroy_listener);

   struct sigaction action;
   memset(&action, 0, sizeof(action));
   action.sa_handler = sigusr_handler;
   sigaction(SIGUSR1, &action,  &xserver.old_sigusr1);
   return true;

display_open_fail:
   wlc_log(WLC_LOG_WARN, "Failed to open xwayland display");
   goto fail;
socketpair_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create socketpair for wayland and xwayland");
   goto fail;
client_create_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create wayland client");
   goto fail;
fork_fail:
   wlc_log(WLC_LOG_WARN, "Fork failed");
fail:
   wlc_xwayland_terminate();
   return false;
}

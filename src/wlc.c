#include "internal.h"
#include "visibility.h"

#include "session/tty.h"
#include "session/fd.h"
#include "session/udev.h"

#include "xwayland/xwayland.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>

#if defined(__linux__)
#  include <linux/version.h>
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
#     include <sys/prctl.h> /* for yama */
#     define HAS_YAMA_PRCTL 1
#  endif
#endif

#if defined(__linux__) && defined(__GNUC__)
#  include <fenv.h>
int feenableexcept(int excepts);
#endif

#if (defined(__APPLE__) && (defined(__i386__) || defined(__x86_64__)))
#  define OSX_SSE_FPE
#  include <xmmintrin.h>
#endif

static struct {
   struct wlc_interface interface;
   struct wlc_system_signals signals;
   struct wl_display *display;
   struct wl_event_source *terminate_timer;
   FILE *log_file;
   int cached_tm_mday;
   bool active;
   bool init;
} wlc;

#ifndef NDEBUG

static void
backtrace(int signal)
{
   (void)signal;

   if (clearenv() != 0)
      exit(EXIT_FAILURE);

   /* GDB */
#if defined(__linux__) || defined(__APPLE__)
   pid_t child_pid = fork();

#if HAS_YAMA_PRCTL
   /* tell yama that we allow our child_pid to trace our process */
   if (child_pid > 0) {
      if (!prctl(PR_GET_DUMPABLE)) {
         wlc_log(WLC_LOG_WARN, "Compositor binary is suid/sgid, most likely since you are running from TTY.");
         wlc_log(WLC_LOG_WARN, "Kernel ptracing security policy does not allow attaching to suid/sgid processes.");
         wlc_log(WLC_LOG_WARN, "If you don't get backtrace below, try `setcap cap_sys_ptrace=eip gdb` temporarily.");
      }
      prctl(PR_SET_DUMPABLE, 1);
      prctl(PR_SET_PTRACER, child_pid);
   }
#endif

   if (child_pid < 0) {
      wlc_log(WLC_LOG_WARN, "Fork failed for gdb backtrace");
   } else if (child_pid == 0) {
      /*
       * NOTE: gdb-7.8 does not seem to work with this,
       *       either downgrade to 7.7 or use gdb from master.
       */

      /* sed -n '/bar/h;/bar/!H;$!b;x;p' (another way, if problems) */
      char buf[255];
      const int fd = fileno(wlc_get_log_file());
      snprintf(buf, sizeof(buf) - 1, "gdb -p %d -n -batch -ex bt 2>/dev/null | sed -n '/<signal handler/{n;x;b};H;${x;p}' 1>&%d", getppid(), fd);
      execl("/bin/sh", "/bin/sh", "-c", buf, NULL);
      wlc_log(WLC_LOG_ERROR, "Failed to launch gdb for backtrace");
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
   wlc_log(WLC_LOG_INFO, "SIGFPE signal received");
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

#endif /* NDEBUG */

static inline void
wlc_log_timestamp(FILE *out)
{
   struct timeval tv;
   struct tm *brokendown_time;
   gettimeofday(&tv, NULL);

   if (!(brokendown_time = localtime(&tv.tv_sec))) {
      fprintf(out, "[(NULL)localtime] ");
      return;
   }

   char string[128];
   if (brokendown_time->tm_mday != wlc.cached_tm_mday) {
      strftime(string, sizeof(string), "%Y-%m-%d %Z", brokendown_time);
      fprintf(out, "Date: %s\n", string);
      wlc.cached_tm_mday = brokendown_time->tm_mday;
   }

   strftime(string, sizeof(string), "%H:%M:%S", brokendown_time);
   fprintf(out, "[%s.%03li] ", string, tv.tv_usec / 1000);
}

static inline void
wl_cb_log(const char *fmt, va_list args)
{
   FILE *out = wlc_get_log_file();

   if (out != stderr && out != stdout)
      wlc_log_timestamp(out);

   fprintf(out, "libwayland: ");
   vfprintf(out, fmt, args);
   fflush(out);
}

void
wlc_dlog(enum wlc_debug dbg, const char *fmt, ...)
{
   static struct {
      const char *name;
      bool active;
      bool checked;
   } channels[WLC_DBG_LAST] = {
      { "render", false, false },
      { "focus", false, false },
      { "xwm", false, false },
   };

   if (!channels[dbg].checked) {
      const char *name = channels[dbg].name;
      const char *s = getenv("WLC_DEBUG");
      for (size_t len = strlen(name); s && *s && strncmp(s, name, len); s += strcspn(s, ",") + 1);
      channels[dbg].checked = true;
      if (!(channels[dbg].active = (s && *s != 0)))
         return;
   } else if (!channels[dbg].active) {
      return;
   }

   va_list argp;
   va_start(argp, fmt);
   wlc_vlog(WLC_LOG_INFO, fmt, argp);
   va_end(argp);
}

uint32_t
wlc_get_time(struct timespec *out_ts)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   if (out_ts) memcpy(out_ts, &ts, sizeof(ts));
   return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void
wlc_set_active(bool active)
{
   if (active == wlc.active)
      return;

   wlc.active = active;
   wl_signal_emit(&wlc.signals.activated, &wlc.active);
   wlc_log(WLC_LOG_INFO, (wlc.active ? "become active" : "deactive"));
}

bool
wlc_get_active(void)
{
   return wlc.active;
}

const struct wlc_interface*
wlc_interface(void)
{
   return &wlc.interface;
}

struct wlc_system_signals*
wlc_system_signals(void)
{
   return &wlc.signals;
}

struct wl_event_loop*
wlc_event_loop(void)
{
   return wl_display_get_event_loop(wlc.display);
}

struct wl_display*
wlc_display(void)
{
   return wlc.display;
}

void
wlc_cleanup(void)
{
   if (wlc.terminate_timer)
      wl_event_source_remove(wlc.terminate_timer);

   if (wlc.display) {
      // fd process never allocates display
      wlc_xwayland_terminate();
      wlc_input_terminate();
      wlc_udev_terminate();
      wlc_fd_terminate();
   }

   // however if main process crashed, fd process does
   // know enough about tty to reset it.
   wlc_tty_terminate();

   if (wlc.display)
      wl_display_terminate(wlc.display);

   wlc.display = NULL;
}

static int
cb_terminate_timer(void *data)
{
   (void)data;
   wlc_cleanup();
   return 0;
}

WLC_API void
wlc_vlog(const enum wlc_log_type type, const char *fmt, va_list args)
{
   FILE *out = wlc_get_log_file();

   if (out == stderr || out == stdout) {
      fprintf(out, "wlc: ");
   } else {
      wlc_log_timestamp(out);
   }

   switch (type) {
      case WLC_LOG_WARN:
         fprintf(out, "(WARN) ");
         break;
      case WLC_LOG_ERROR:
         fprintf(out, "(ERROR) ");
         break;
      default:
         break;
   }

   vfprintf(out, fmt, args);
   fprintf(out, "\n");
   fflush(out);
}

WLC_API void
wlc_log(const enum wlc_log_type type, const char *fmt, ...)
{
   va_list argp;
   va_start(argp, fmt);
   wlc_vlog(type, fmt, argp);
   va_end(argp);
}

WLC_API FILE*
wlc_get_log_file(void)
{
   return (wlc.log_file ? wlc.log_file : stderr);
}

WLC_API void
wlc_set_log_file(FILE *out)
{
   if (wlc.log_file && wlc.log_file != stdout && wlc.log_file != stderr)
      fclose(wlc.log_file);

   wlc.log_file = out;
}

WLC_API void
wlc_run(void)
{
   wl_display_run(wlc.display);
}

WLC_API void
wlc_terminate(void)
{
   if (wlc.terminate_timer)
      return;

   wlc.terminate_timer = wl_event_loop_add_timer(wlc_event_loop(), cb_terminate_timer, NULL);
   wl_signal_emit(&wlc.signals.terminated, NULL);
   wl_event_source_timer_update(wlc.terminate_timer, 100);
   wlc_log(WLC_LOG_INFO, "Terminating...");
}

WLC_API bool
wlc_init(const struct wlc_interface *interface, const int argc, char *argv[])
{
   if (wlc.display)
      return true;

   memset(&wlc, 0, sizeof(wlc));

   wl_log_set_handler_server(wl_cb_log);

   for (int i = 1; i < argc; ++i) {
      if (!strcmp(argv[i], "--log")) {
         if (i + 1 >= argc)
            die("--log takes an argument (filename)");
         wlc_set_log_file(fopen(argv[++i], "w"));
      }
   }

   unsetenv("TERM");
   const char *display = getenv("DISPLAY");

   if (getuid() != geteuid() || getgid() != getegid()) {
      wlc_log(WLC_LOG_INFO, "Doing work on SUID/SGID side and dropping permissions");
   } else if (getuid() == 0) {
      die("Do not run wlc compositor as root");
   } else if (!display && access("/dev/input/event0", R_OK | W_OK) != 0) {
      die("Not running from X11 and no access to /dev/input/event0");
   }

#ifndef NDEBUG
   {
      struct sigaction action;
      memset(&action, 0, sizeof(action));
      action.sa_handler = backtrace;
      sigaction(SIGABRT, &action, NULL);
      sigaction(SIGSEGV, &action, NULL);

      // XXX: Some weird sigfpes seems to come when running
      // wlc compositor inside wlc compositor (X11 backend).
      // Seems to be caused by resolution changes and mouse clicks.
      // Gather more information about this later and see what's going on.
      if (!getenv("WAYLAND_DISPLAY"))
         fpesetup(&action);
   }
#endif

   if (!display)
      wlc_tty_init();

   // -- we open tty before dropping permissions
   //    so the fd process can also handle cleanup in case of crash

   wlc_fd_init(argc, argv);

   // -- permissions are now dropped

   wl_signal_init(&wlc.signals.terminated);
   wl_signal_init(&wlc.signals.activated);
   wl_signal_init(&wlc.signals.surface);
   wl_signal_init(&wlc.signals.input);
   wl_signal_init(&wlc.signals.output);
   wl_signal_init(&wlc.signals.xwayland);

   if (!(wlc.display = wl_display_create()))
      die("Failed to create wayland display");

   const char *socket_name;
   if (!(socket_name = wl_display_add_socket_auto(wlc.display)))
      die("Failed to add socket to wayland display");

   setenv("WAYLAND_DISPLAY", socket_name, true);

   if (wl_display_init_shm(wlc.display) != 0)
      die("Failed to init shm");

   if (!wlc_udev_init())
      return false;

   const char *libinput = getenv("WLC_LIBINPUT");
   if (!display || (libinput && !strcmp(libinput, "1"))) {
      if (!wlc_input_init())
         return false;
   }

   const char *xwayland = getenv("WLC_XWAYLAND");
   if (!xwayland || strcmp(xwayland, "0")) {
      if (!(wlc_xwayland_init()))
         return false;
   }

   memcpy(&wlc.interface, interface, sizeof(wlc.interface));
   wlc_set_active(true);
   return (wlc.init = true);
}

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <chck/string/string.h>
#include "internal.h"
#include "visibility.h"
#include "compositor/compositor.h"
#include "session/tty.h"
#include "session/fd.h"
#include "session/udev.h"
#include "session/logind.h"
#include "xwayland/xwayland.h"
#include "resources/resources.h"

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
   struct wlc_compositor compositor;
   struct wlc_interface interface;
   struct wlc_system_signals signals;
   struct wl_display *display;
   FILE *log_file;
   int cached_tm_mday;
   bool active, set_ready_on_run;
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
      { "handle", false, false },
      { "render", false, false },
      { "render-loop", false, false },
      { "focus", false, false },
      { "xwm", false, false },
      { "keyboard", false, false },
      { "commit", false, false },
   };

   if (!channels[dbg].checked) {
      const char *name = channels[dbg].name;
      const char *s = getenv("WLC_DEBUG");
      for (size_t len = strlen(name); s && *s && !chck_cstrneq(s, name, len); s += strcspn(s, ",") + 1);
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
   struct wlc_activate_event ev = { .active = active, .vt = 0 };
   wl_signal_emit(&wlc.signals.activate, &ev);
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

static void
compositor_event(struct wl_listener *listener, void *data)
{
   (void)listener, (void)data;
   // this event is currently only used for knowing when compositor died
   wl_display_terminate(wlc.display);
}

static struct wl_listener compositor_listener = {
   .notify = compositor_event,
};

void
wlc_cleanup(void)
{
   wlc_log(WLC_LOG_INFO, "Cleanup wlc");

   if (wlc.display) {
      // fd process never allocates display
      wlc_compositor_release(&wlc.compositor);
      wl_list_remove(&compositor_listener.link);
      wlc_xwayland_terminate();
      wlc_input_terminate();
      wlc_udev_terminate();
      wlc_fd_terminate();
   }

   // however if main process crashed, fd process does
   // know enough about tty to reset it.
   wlc_tty_terminate();

   if (wlc.display)
      wl_display_destroy(wlc.display);

   memset(&wlc, 0, sizeof(wlc));
}

WLC_API void
wlc_vlog(enum wlc_log_type type, const char *fmt, va_list args)
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

      default:break;
   }

   vfprintf(out, fmt, args);
   fprintf(out, "\n");
   fflush(out);
}

WLC_API void
wlc_log(enum wlc_log_type type, const char *fmt, ...)
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
wlc_exec(const char *bin, char *const args[])
{
   assert(bin);

   if (chck_cstr_is_empty(bin))
      return;

   pid_t p;
   if ((p = fork()) == 0) {
      setsid();
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "w", stderr);
      execvp(bin, args);
      _exit(EXIT_FAILURE);
   } else if (p < 0) {
      wlc_log(WLC_LOG_ERROR, "Failed to fork for '%s'", bin);
   }
}

WLC_API void
wlc_run(void)
{
   if (!wlc.display)
      return;

   // Called when no xwayland is requested
   if (wlc.set_ready_on_run) {
      WLC_INTERFACE_EMIT(compositor.ready);
      wlc.set_ready_on_run = false;
   }

   wl_display_run(wlc.display);
   wlc_cleanup();
}

WLC_API void
wlc_terminate(void)
{
   if (!wlc.display)
      return;

   wlc_log(WLC_LOG_INFO, "Terminating wlc...");
   wl_signal_emit(&wlc.signals.terminate, NULL);
}

WLC_API bool
wlc_init(const struct wlc_interface *interface, int argc, char *argv[])
{
   assert(interface);

   if (!interface)
      die("no wlc_interface was given");

   if (wlc.display)
      return true;

   memset(&wlc, 0, sizeof(wlc));

   wl_log_set_handler_server(wl_cb_log);

   for (int i = 1; i < argc; ++i) {
      if (chck_cstreq(argv[i], "--log")) {
         if (i + 1 >= argc)
            die("--log takes an argument (filename)");
         wlc_set_log_file(fopen(argv[++i], "a"));
      }
   }

   unsetenv("TERM");
   const char *x11display = getenv("DISPLAY");
   bool privilidged = false;
   const bool has_logind  = wlc_logind_available();

   if (getuid() != geteuid() || getgid() != getegid()) {
      wlc_log(WLC_LOG_INFO, "Doing work on SUID/SGID side and dropping permissions");
      privilidged = true;
   } else if (getuid() == 0) {
      die("Do not run wlc compositor as root");
   } else if (!x11display && !has_logind && access("/dev/input/event0", R_OK | W_OK) != 0) {
      die("Not running from X11 and no access to /dev/input/event0 or logind available");
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

   int vt = 0;

#ifdef HAS_LOGIND
   // Init logind if we are not running as SUID.
   // We need event loop for logind to work, and thus we won't allow it on SUID process.
   if (!privilidged && !x11display && has_logind) {
      if (!(wlc.display = wl_display_create()))
         die("Failed to create wayland display");
      if (!(vt = wlc_logind_init("seat0")))
         die("Failed to init logind");
   }
#else
   (void)privilidged;
#endif

   if (!x11display)
      wlc_tty_init(vt);

   // -- we open tty before dropping permissions
   //    so the fd process can also handle cleanup in case of crash
   //    if logind initialized correctly, fd process does nothing but handle crash.

   {
      struct wl_display *display = wlc.display;
      wlc.display = NULL;
      wlc_fd_init(argc, argv, (vt != 0));
      wlc.display = display;
   }


   // -- permissions are now dropped

   wl_signal_init(&wlc.signals.terminate);
   wl_signal_init(&wlc.signals.activate);
   wl_signal_init(&wlc.signals.compositor);
   wl_signal_init(&wlc.signals.focus);
   wl_signal_init(&wlc.signals.surface);
   wl_signal_init(&wlc.signals.input);
   wl_signal_init(&wlc.signals.output);
   wl_signal_init(&wlc.signals.render);
   wl_signal_init(&wlc.signals.xwayland);
   wl_signal_add(&wlc.signals.compositor, &compositor_listener);

   if (!wlc_resources_init())
      die("Failed to init resource manager");

   if (!wlc.display && !(wlc.display = wl_display_create()))
      die("Failed to create wayland display");

   const char *socket_name;
   if (!(socket_name = wl_display_add_socket_auto(wlc.display)))
      die("Failed to add socket to wayland display");

   if (socket_name) // shut up static analyze
      setenv("WAYLAND_DISPLAY", socket_name, true);

   if (wl_display_init_shm(wlc.display) != 0)
      die("Failed to init shm");

   if (!wlc_udev_init())
      die("Failed to init udev");

   const char *libinput = getenv("WLC_LIBINPUT");
   if (!x11display || (libinput && !chck_cstreq(libinput, "0"))) {
      if (!wlc_input_init())
         die("Failed to init input");
   }

   memcpy(&wlc.interface, interface, sizeof(wlc.interface));

   if (!wlc_compositor(&wlc.compositor))
      die("Failed to init compositor");

   const char *xwayland = getenv("WLC_XWAYLAND");
   if (!xwayland || !chck_cstreq(xwayland, "0")) {
      if (!(wlc_xwayland_init()))
         die("Failed to init xwayland");
   } else {
      wlc.set_ready_on_run = true;
   }

   wlc_set_active(true);
   return true;
}

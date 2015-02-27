#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <chck/string/string.h>
#include "internal.h"
#include "tty.h"
#include "fd.h"

#if defined(__linux__)
#  include <linux/kd.h>
#  include <linux/major.h>
#  include <linux/vt.h>
#endif

static struct {
   struct {
      long kb_mode, console_mode;
      int vt;
      bool altered;
   } old_state;
   int tty;
} wlc = {
   .old_state = {0},
   .tty = -1,
};

static int
find_vt(const char *vt_string)
{
   if (vt_string) {
      char *end;
      int vt = strtoul(vt_string, &end, 10);
      if (*end == '\0')
         return vt;
   }

   int tty0_fd;
   if ((tty0_fd = open("/dev/tty0", O_RDWR)) < 0)
      die("Could not open /dev/tty0 to find unused VT");

   int vt;
   if (ioctl(tty0_fd, VT_OPENQRY, &vt) != 0)
      die("Could not find unused VT");

   close(tty0_fd);
   return vt;
}

static int
open_tty(int vt)
{
   char tty_name[64];
   snprintf(tty_name, sizeof tty_name, "/dev/tty%d", vt);

   /* check if we are running on the desired VT */
   if (ttyname(STDIN_FILENO) && chck_cstreq(tty_name, ttyname(STDIN_FILENO)))
      return STDIN_FILENO;

   int fd;
   if ((fd = open(tty_name, O_RDWR | O_NOCTTY)) < 0)
      die("Could not open %s", tty_name);

   wlc_log(WLC_LOG_INFO, "Running on VT %d", vt);
   return fd;
}

static int
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
      die("Could not open TTY");

   if (fstat(fd, &st) == -1)
      die("Could not stat TTY fd");

   vt = minor(st.st_rdev);

   if (major(st.st_rdev) != TTY_MAJOR || vt == 0)
      die("Not a valid VT");

   if (ioctl(fd, VT_GETSTATE, &state) == -1)
      die("Could not get the current VT state");

   wlc.old_state.vt = state.v_active;

   if (ioctl(fd, KDGKBMODE, &wlc.old_state.kb_mode))
      die("Could not get keyboard mode");

   if (ioctl(fd, KDGETMODE, &wlc.old_state.console_mode))
      die("Could not get console mode");

   if (ioctl(fd, KDSKBMODE, K_OFF) == -1)
      die("Could not set keyboard mode to K_OFF");

   if (ioctl(fd, KDSETMODE, KD_GRAPHICS) == -1)
      die("Could not set console mode to KD_GRAPHICS");

   if (ioctl(fd, VT_SETMODE, &mode) == -1)
      die("Could not set VT mode");

   if (ioctl(fd, VT_ACTIVATE, vt) == -1)
      die("Could not activate VT");

   if (ioctl(fd, VT_WAITACTIVE, vt) == -1)
      die("Could not wait for VT to become active");

   wlc.old_state.altered = true;
   return fd;
}

static void
sigusr_handler(int signal)
{
   switch (signal) {
      case SIGUSR1:
         wlc_log(WLC_LOG_INFO, "SIGUSR1");

         if (!wlc_fd_deactivate())
            return;

         wlc_set_active(false);
         ioctl(wlc.tty, VT_RELDISP, 1);
         break;
      case SIGUSR2:
         wlc_log(WLC_LOG_INFO, "SIGUSR2");

         if (!wlc_fd_activate())
            return;

         ioctl(wlc.tty, VT_RELDISP, VT_ACKACQ);
         wlc_set_active(true);
         break;
   }
}

bool
wlc_tty_activate_vt(int vt)
{
   if (wlc.tty < 0)
      return false;

   wlc_log(WLC_LOG_INFO, "Activate VT: %d", vt);
   return (ioctl(wlc.tty, VT_ACTIVATE, vt) != -1);
}

void
wlc_tty_terminate(void)
{
   if (wlc.tty >= 0) {
      wlc_log(WLC_LOG_INFO, "Restoring tty %d", wlc.tty);
      struct vt_mode mode = { .mode = VT_AUTO };
      ioctl(wlc.tty, VT_SETMODE, &mode);
      ioctl(wlc.tty, KDSETMODE, KD_GRAPHICS);
      ioctl(wlc.tty, KDSETMODE, wlc.old_state.console_mode);
      ioctl(wlc.tty, KDSKBMODE, wlc.old_state.kb_mode);
      ioctl(wlc.tty, VT_ACTIVATE, wlc.old_state.vt);
      close(wlc.tty);
   }

   memset(&wlc, 0, sizeof(wlc));
   wlc.tty = -1;

   wlc_log(WLC_LOG_INFO, "Cleanup wlc");
}

void
wlc_tty_init(void)
{
   if (wlc.tty >= 0)
      return;

   wlc.tty = setup_tty(getenv("XDG_VTNR"));

   struct sigaction action;
   memset(&action, 0, sizeof(action));
   action.sa_handler = sigusr_handler;
   sigaction(SIGUSR1, &action, NULL);
   sigaction(SIGUSR2, &action, NULL);
}

#define _XOPEN_SOURCE 500
#include "keymap.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <xkbcommon/xkbcommon.h>
#include <wayland-server.h>

static char*
csprintf(const char *fmt, ...)
{
   assert(fmt);

   va_list args;
   va_start(args, fmt);
   size_t len = vsnprintf(NULL, 0, fmt, args) + 1;
   va_end(args);

   char *buffer;
   if (!(buffer = calloc(1, len)))
      return NULL;

   va_start(args, fmt);
   vsnprintf(buffer, len, fmt, args);
   va_end(args);
   return buffer;
}

static int
set_cloexec_or_close(int fd)
{
   if (fd == -1)
      return -1;

   long flags = fcntl(fd, F_GETFD);
   if (flags == -1)
      goto err;

   if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
      goto err;

   return fd;

err:
   close(fd);
   return -1;
}

static int
create_tmpfile_cloexec(char *tmpname)
{
   int fd;

#ifdef HAVE_MKOSTEMP
   if ((fd = mkostemp(tmpname, O_CLOEXEC)) >= 0)
      unlink(tmpname);
#else
   if ((fd = mkstemp(tmpname)) >= 0) {
      fd = set_cloexec_or_close(fd);
      unlink(tmpname);
   }
#endif

   return fd;
}

static int
os_create_anonymous_file(off_t size)
{
   static const char template[] = "/loliwm-shared-XXXXXX";
   int fd;
   int ret;

   const char *path;
   if (!(path = getenv("XDG_RUNTIME_DIR")) || strlen(path) <= 0) {
      errno = ENOENT;
      return -1;
   }

   char *name;
   int ts = (path[strlen(path) - 1] == '/');
   if (!(name = csprintf("%s%s%s", path, (ts ? "" : "/"), template)))
      return -1;

   fd = create_tmpfile_cloexec(name);
   free(name);

   if (fd < 0)
      return -1;

#ifdef HAVE_POSIX_FALLOCATE
   if ((ret = posix_fallocate(fd, 0, size)) != 0) {
      close(fd);
      errno = ret;
      return -1;
   }
#else
   if ((ret = ftruncate(fd, size)) < 0) {
      close(fd);
      return -1;
   }
#endif

   return fd;
}

void
wlc_keymap_free(struct wlc_keymap *keymap)
{
   assert(keymap);

   if (keymap->keymap)
      xkb_map_unref(keymap->keymap);

   if (keymap->area)
      munmap(keymap->area, keymap->size);

   if (keymap->fd >= 0)
      close(keymap->fd);

   free(keymap);
}

struct wlc_keymap*
wlc_keymap_new(void)
{
   struct wlc_keymap *keymap;
   if (!(keymap = calloc(1, sizeof(struct wlc_keymap))))
      goto fail;

   struct xkb_rule_names names;
   memset(&names, 0, sizeof(names));
   names.rules = "evdev";
   names.model = "pc105";
   names.layout = "fi";

   struct xkb_context *context;
   if (!(context = xkb_context_new(0)))
      goto context_fail;

   if (!(keymap->keymap = xkb_map_new_from_names(context, &names, 0)))
      goto keymap_fail;

   char *keymap_str;
   if (!(keymap_str = xkb_map_get_as_string(keymap->keymap)))
      goto string_fail;

   keymap->size = strlen(keymap_str) + 1;
   if ((keymap->fd = os_create_anonymous_file(keymap->size)) < 0)
      goto file_fail;

   if (!(keymap->area = mmap(NULL, keymap->size, PROT_READ | PROT_WRITE, MAP_SHARED, keymap->fd, 0)))
      goto mmap_fail;

   keymap->format = WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
   strcpy(keymap->area, keymap_str);
   free(keymap_str);
   return keymap;

context_fail:
   fprintf(stderr, "-!- Failed to create xkb context\n");
   goto fail;
keymap_fail:
   fprintf(stderr, "-!- Failed to get xkb keymap\n");
   goto fail;
string_fail:
   fprintf(stderr, "-!- Failed to get keymap as string\n");
   goto fail;
file_fail:
   fprintf(stderr, "-!- Failed to create file for keymap\n");
   goto fail;
mmap_fail:
   fprintf(stderr, "-!- Failed to mmap keymap\n");
fail:
   if (keymap)
      wlc_keymap_free(keymap);
   return NULL;
}

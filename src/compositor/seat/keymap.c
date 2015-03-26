#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <wlc/wlc.h>
#include <wayland-server.h>
#include <chck/string/string.h>
#include "keymap.h"

const char* WLC_MOD_NAMES[WLC_MOD_LAST] = {
   XKB_MOD_NAME_SHIFT,
   XKB_MOD_NAME_CAPS,
   XKB_MOD_NAME_CTRL,
   XKB_MOD_NAME_ALT,
   "Mod2",
   "Mod3",
   XKB_MOD_NAME_LOGO,
   "Mod5",
};

const char* WLC_LED_NAMES[WLC_LED_LAST] = {
   XKB_LED_NAME_NUM,
   XKB_LED_NAME_CAPS,
   XKB_LED_NAME_SCROLL
};

static int
set_cloexec_or_close(int fd)
{
   if (fd == -1)
      return -1;

   long flags;
   if ((flags = fcntl(fd, F_GETFD)) == -1)
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

   const char *path = getenv("XDG_RUNTIME_DIR");
   if (chck_cstr_is_empty(path))
      return -1;

   struct chck_string name = {0};
   if (!chck_string_set_format(&name, "%s%s%s", path, (chck_cstr_ends_with(path, "/") ? "" : "/"), template))
      return -1;

   int fd = create_tmpfile_cloexec(name.data);
   chck_string_release(&name);

   if (fd < 0)
      return -1;

   int ret;
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

uint32_t
wlc_keymap_get_mod_mask(struct wlc_keymap *keymap, uint32_t in)
{
   assert(keymap);

   const enum wlc_modifier_bit mod_bits[WLC_MOD_LAST] = {
      WLC_BIT_MOD_SHIFT,
      WLC_BIT_MOD_CAPS,
      WLC_BIT_MOD_CTRL,
      WLC_BIT_MOD_ALT,
      WLC_BIT_MOD_MOD2,
      WLC_BIT_MOD_MOD3,
      WLC_BIT_MOD_LOGO,
      WLC_BIT_MOD_MOD5,
   };

   uint32_t mods = 0;
   for (uint32_t i = 0; i < WLC_MOD_LAST; ++i) {
      if (keymap->mods[i] != XKB_MOD_INVALID && (in & (1 << keymap->mods[i])))
         mods |= mod_bits[i];
   }

   return mods;
}

uint32_t
wlc_keymap_get_led_mask(struct wlc_keymap *keymap, struct xkb_state *xkb)
{
   assert(keymap && xkb);

   const enum wlc_led_bit led_bits[WLC_LED_LAST] = {
      WLC_BIT_LED_NUM,
      WLC_BIT_LED_CAPS,
      WLC_BIT_LED_SCROLL,
   };

   uint32_t leds = 0;
   for (uint32_t i = 0; i < WLC_LED_LAST; ++i) {
      if (xkb_state_led_index_is_active(xkb, keymap->leds[i]))
         leds |= led_bits[i];
   }

   return leds;
}

void
wlc_keymap_release(struct wlc_keymap *keymap)
{
   if (!keymap)
      return;

   if (keymap->keymap)
      xkb_map_unref(keymap->keymap);

   if (keymap->area)
      munmap(keymap->area, keymap->size);

   if (keymap->fd >= 0)
      close(keymap->fd);
}

bool
wlc_keymap(struct wlc_keymap *keymap, const struct xkb_rule_names *names, enum xkb_keymap_compile_flags flags)
{
   assert(keymap);
   memset(keymap, 0, sizeof(struct wlc_keymap));

   char *keymap_str = NULL;

   struct xkb_context *context;
   if (!(context = xkb_context_new(XKB_CONTEXT_NO_FLAGS)))
      goto context_fail;

   if (!(keymap->keymap = xkb_map_new_from_names(context, names, flags)))
      goto keymap_fail;

   xkb_context_unref(context);
   context = NULL;

   if (!(keymap_str = xkb_map_get_as_string(keymap->keymap)))
      goto string_fail;

   keymap->size = strlen(keymap_str) + 1;
   if ((keymap->fd = os_create_anonymous_file(keymap->size)) < 0)
      goto file_fail;

   if (!(keymap->area = mmap(NULL, keymap->size, PROT_READ | PROT_WRITE, MAP_SHARED, keymap->fd, 0)))
      goto mmap_fail;

   for (uint32_t i = 0; i < WLC_MOD_LAST; ++i)
      keymap->mods[i] = xkb_map_mod_get_index(keymap->keymap, WLC_MOD_NAMES[i]);

   for (uint32_t i = 0; i < WLC_LED_LAST; ++i)
      keymap->leds[i] = xkb_map_led_get_index(keymap->keymap, WLC_LED_NAMES[i]);

   keymap->format = WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1;
   memcpy(keymap->area, keymap_str, keymap->size - 1);
   free(keymap_str);
   return keymap;

context_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create xkb context");
   goto fail;
keymap_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get xkb keymap");
   goto fail;
string_fail:
   wlc_log(WLC_LOG_WARN, "Failed to get keymap as string");
   goto fail;
file_fail:
   wlc_log(WLC_LOG_WARN, "Failed to create file for keymap");
   goto fail;
mmap_fail:
   wlc_log(WLC_LOG_WARN, "Failed to mmap keymap");
fail:
   free(keymap_str);
   xkb_context_unref(context);
   wlc_keymap_release(keymap);
   return NULL;
}

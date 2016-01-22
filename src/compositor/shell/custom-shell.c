#include <stdlib.h>
#include <assert.h>
#include <wlc/wlc-wayland.h>
#include "visibility.h"
#include "internal.h"
#include "custom-shell.h"
#include "compositor/compositor.h"
#include "compositor/view.h"
#include "resources/types/surface.h"

// XXX: We do not currently expose compositor to public API.
//      So we use static variable here for some public api functions.
//
//      Never use this variable anywhere else.
static struct wlc_custom_shell *_g_custom_shell;

WLC_API wlc_handle
wlc_view_from_surface(wlc_resource surface, struct wl_client *client, const struct wl_interface *interface, const void *implementation, uint32_t version, uint32_t id, void *userdata)
{
   assert(_g_custom_shell);

   struct wlc_surface *s;
   if (!(s = convert_from_wlc_resource(surface, "surface")))
      return 0;

   wlc_resource r = 0;
   if (client || interface || implementation) {
      assert(client && interface && implementation);

      if (!(r = wlc_resource_create(&_g_custom_shell->surfaces, client, interface, version, version, id)))
         return 0;

      wlc_resource_implement(r, implementation, userdata);
   }

   struct wlc_surface_event ev = { .attach = { .type = WLC_CUSTOM_SURFACE, .role = r }, .surface = s, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);
   return s->view;
}

void
wlc_custom_shell_release(struct wlc_custom_shell *custom_shell)
{
   if (!custom_shell)
      return;

   wlc_source_release(&custom_shell->surfaces);
   *custom_shell = (struct wlc_custom_shell){0};
}

bool
wlc_custom_shell(struct wlc_custom_shell *custom_shell)
{
   assert(custom_shell);
   *custom_shell = (struct wlc_custom_shell){0};

   if (!wlc_source(&custom_shell->surfaces, "custom-surface", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   _g_custom_shell = custom_shell;
   return custom_shell;

fail:
   wlc_custom_shell_release(custom_shell);
   return NULL;
}

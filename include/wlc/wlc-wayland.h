#ifndef _WLC_WAYLAND_H_
#define _WLC_WAYLAND_H_

#ifdef __cplusplus
extern "C" {
#endif 

#include <wlc/defines.h>
#include <wlc/geometry.h>
#include <wayland-server.h>

typedef uintptr_t wlc_resource;

struct wl_resource;
struct wl_display;

/** Returns Wayland display. */
struct wl_display* wlc_get_wl_display(void);

/** Returns view handle from wl_surface resource. */
WLC_NONULL wlc_handle wlc_handle_from_wl_surface_resource(struct wl_resource *resource);

/** Returns output handle from wl_output resource. */
WLC_NONULL wlc_handle wlc_handle_from_wl_output_resource(struct wl_resource *resource);

/** Returns internal wlc surface from wl_surface resource. */
WLC_NONULL wlc_resource wlc_resource_from_wl_surface_resource(struct wl_resource *resource);

/** Returns internal wlc surface from view handle */
wlc_resource wlc_view_get_surface(wlc_handle view);

/** Get surface size. */
const struct wlc_size* wlc_surface_get_size(wlc_resource surface);

/** Function for rendering surfaces inside post / pre render hooks. */
WLC_NONULL void wlc_surface_render(wlc_resource surface, const struct wlc_geometry *geometry);

#ifdef __cplusplus
}
#endif 

#endif /* _WLC_WAYLAND_H_ */
